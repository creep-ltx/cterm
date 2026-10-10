/*
 * CTerm - a VT100/xterm terminal for AmigaOS 3.2.
 *
 * This file is the Amiga side: the window (on Workbench, or on CTerm's
 * own public screen, windowed or full screen), drawing the emulator's
 * cells, keys, and the telnet connection. The emulator itself is vt.c.
 *
 * The connection: CTerm listens on a TCP port (7478) and a program on
 * the PC (pc/cterm-relay.py) calls in, because the PC's firewall drops
 * calls from the LAN. They speak telnet, so CTerm can say how big its
 * window is (NAWS) and the program on the PC follows.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/gfxmacros.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <devices/inputevent.h>
#include <devices/clipboard.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/ttengine.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>

#include <string.h>
#include <stdlib.h>

#include "vt.h"
#include "boxtable.h"
#include "console.h"
#include "ssh.h"
#include "dbg.h"

static const char version[] = "$VER: CTerm 0.3b1 (10.10.2026)";

struct Library *SocketBase;
struct Library *KeymapBase;
struct Library *GadToolsBase;
struct Library *AslBase;
struct Library *DiskfontBase;
struct Library *TTEngineBase;
struct Library *CyberGfxBase;

void __chkabort(void) {}

#define DEFAULT_PORT 7478
#define SCROLLBACK   2000
#define POSBAR       6

/* ======================================================================
 * Tabs: each a terminal of its own - its text, its Shell or connection,
 * its characters, where it is scrolled, what is marked. The names below
 * stand for the current tab's (T); only the shown tab is drawn.
 * ==================================================================== */

struct Tab {
    Vt *t_vt;
    struct Con *con;                    /* a local Shell's console */
    BOOL t_local;                        /* the Amiga Shell, not a host */
    LONG t_sock, t_lfd;                     /* a host; LISTEN's socket */
    int t_tstate, t_tcmd;                   /* telnet's parser */
    UBYTE t_sbuf[64];                     /* a subnegotiation coming in */
    int t_sblen;
    char t_dialhost[128];                 /* HOST/SSH: the place called */
    char t_dialtitle[140];                /* "CTerm: host" */
    BOOL t_sshmode;                       /* dialhost is user@host */
    BOOL t_utf8, t_pcmode;                  /* its characters: UTF-8; PC BBS */
    int t_back;                           /* lines scrolled back */
    BOOL t_sel_on, t_sel_drag;              /* the selection, in its cells */
    int t_sa_r, t_sa_c, t_sb_r, t_sb_c;
    BOOL ended;                         /* its Shell or connection is over */
    void *ssh;                          /* its ssh connection */
};

#define MAXTABS 9
#define MAXSPENT 64
static struct Con *spent[MAXSPENT];     /* consoles of closed tabs */
static int nspent;
static struct Tab tab0 = { NULL, NULL, TRUE, -1, -1 };
static struct Tab *tabs[MAXTABS] = { &tab0 };
static int ntabs = 1;
static struct Tab *T = &tab0;           /* the current tab */
static struct Tab *shown = &tab0;       /* the one in the window */

#define vt        (T->t_vt)
#define local     (T->t_local)
#define sock      (T->t_sock)
#define lfd       (T->t_lfd)
#define tstate    (T->t_tstate)
#define tcmd      (T->t_tcmd)
#define sbuf      (T->t_sbuf)
#define sblen     (T->t_sblen)
#define dialhost  (T->t_dialhost)
#define dialtitle (T->t_dialtitle)
#define sshmode   (T->t_sshmode)
#define utf8      (T->t_utf8)
#define pcmode    (T->t_pcmode)
#define back      (T->t_back)
#define sel_on    (T->t_sel_on)
#define sel_drag  (T->t_sel_drag)
#define sa_r      (T->t_sa_r)
#define sa_c      (T->t_sa_c)
#define sb_r      (T->t_sb_r)
#define sb_c      (T->t_sb_c)

static void tt_attach(void);
static void title_update(void);
static void title_update_now(void);
static ULONG shells_sigmask(void);
static void shells_handle(ULONG sigs);
static void tab_new_shell(void);
static void tab_close_current(void);
static void tab_show(int i);
static struct Tab *tab_new(void);
static BOOL connect_out(void);

/* ======================================================================
 * Colours
 * ==================================================================== */

/* Themes: the text and background, the 16 ANSI colours, and what the
 * Amiga's pens 2-7 mean in the local Shell (Ed and friends use them). */
struct Theme {
    const char *name;
    ULONG fg, bg, ansi[16], wbpens[6];
};

static const struct Theme themes[] = {
    { "Nord", 0xD8DEE9, 0x1A1D23,
      { 0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD,
        0x88C0D0, 0xE5E9F0, 0x4C566A, 0xD57780, 0xB5D09E, 0xF0D399,
        0x94B4D6, 0xC5A3C0, 0x8FBCBB, 0xFFFFFF },
      { 0xFFFFFF, 0x88C0D0, 0x4C566A, 0xB48EAD, 0xA3BE8C, 0xEBCB8B } },
    { "Black", 0xC0C0C0, 0x000000,
      { 0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA,
        0x00AAAA, 0xAAAAAA, 0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
        0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF },
      { 0xFFFFFF, 0x6688BB, 0x777777, 0xAA00AA, 0x00AA00, 0xAA5500 } },
    { "Workbench", 0x000000, 0xAAAAAA,
      { 0x000000, 0x990000, 0x006600, 0x885500, 0x002288, 0x772277,
        0x006666, 0xDDDDDD, 0x444444, 0xCC2222, 0x228822, 0xAA7700,
        0x2244CC, 0x993399, 0x118888, 0xFFFFFF },
      { 0xFFFFFF, 0x6688BB, 0x777777, 0x772277, 0x006600, 0x885500 } },
    { "Amber", 0xFFB000, 0x1A1000,
      { 0x3A2800, 0xFF7A00, 0xD09000, 0xFFC840, 0xB07800, 0xE08800,
        0xFFD070, 0xFFB000, 0x6A4800, 0xFF9A40, 0xE8A830, 0xFFE090,
        0xC89040, 0xF0A040, 0xFFE0A0, 0xFFF0D0 },
      { 0xFFE0A0, 0xD09000, 0x6A4800, 0xFF7A00, 0xE8A830, 0xFFC840 } },
    { "Green", 0x33FF66, 0x001A08,
      { 0x0A3A18, 0x22AA44, 0x33FF66, 0x88FF88, 0x22CC55, 0x44DD77,
        0x66FF99, 0x33FF66, 0x115522, 0x55CC66, 0x77FF99, 0xBBFFBB,
        0x44DD77, 0x66EE88, 0x99FFBB, 0xDDFFDD },
      { 0xBBFFBB, 0x22CC55, 0x115522, 0x44DD77, 0x77FF99, 0x88FF88 } },
    { "Light", 0x2E3440, 0xECEFF4,
      { 0x2E3440, 0xBF616A, 0x5E8A3A, 0xB08A30, 0x3B6EA5, 0x8F5A8A,
        0x3A8A8A, 0xD8DEE9, 0x4C566A, 0xD06070, 0x6A9A48, 0xC09A40,
        0x4C7EB5, 0x9F6A9A, 0x4A9A9A, 0xFFFFFF },
      { 0x000000, 0x3B6EA5, 0x9099A8, 0x8F5A8A, 0x5E8A3A, 0xB08A30 } },
};
#define NTHEMES (int)(sizeof themes / sizeof themes[0])

/* The PC's own 16 colours (VGA): PC BBS art is drawn for these. */
static const ULONG vga16[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA,
    0xAAAAAA, 0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF,
    0x55FFFF, 0xFFFFFF
};

static int theme;
static BOOL ownfg, ownbg;               /* the person's own text/background */
static ULONG myfg, mybg;
static ULONG deffg = 0xD8DEE9, defbg = 0x1A1D23;
static const ULONG *ansi16 = themes[0].ansi;


static void apply_theme(void)
{
    if (theme < 0 || theme >= NTHEMES)
        theme = 0;
    deffg = ownfg ? myfg : themes[theme].fg;
    defbg = ownbg ? mybg : themes[theme].bg;
    ansi16 = themes[theme].ansi;
}

static ULONG index_rgb(int i)
{
    static const UBYTE cube[6] = { 0, 95, 135, 175, 215, 255 };
    if (i < 16)
        return pcmode ? vga16[i] : ansi16[i];
    if (i < 232) {
        i -= 16;
        return ((ULONG)cube[i / 36] << 16) | ((ULONG)cube[(i / 6) % 6] << 8) |
               cube[i % 6];
    }
    i = 8 + (i - 232) * 10;
    return ((ULONG)i << 16) | ((ULONG)i << 8) | i;
}

static ULONG mix(ULONG a, ULONG b, int pct)       /* pct of b into a */
{
    ULONG out = 0;
    int sh;
    for (sh = 0; sh <= 16; sh += 8) {
        int x = (a >> sh) & 255, y = (b >> sh) & 255;
        out |= (ULONG)((x * (100 - pct) + y * pct) / 100) << sh;
    }
    return out;
}

/* ======================================================================
 * The display
 * ==================================================================== */

enum { SCR_WB, SCR_OWN };
static int scrmode = SCR_WB, owndepth = 8;
static ULONG ownmode = INVALID_ID;      /* the own screen's mode; INVALID_ID:
                                           the Workbench's, at owndepth */
static LONG ownw, ownh;                 /* its size; 0: the mode's own */
static BOOL fullscreen, scrollbar = TRUE;
static struct Screen *ownscr, *scr;
static struct DrawInfo *dri;
struct Window *win;                     /* console.c hands it to More/Ed */
static struct RastPort *rp;
static struct TextFont *font, *ownfont;
static char fontname[48];
static int fontsize;
static int cw, ch, cbase;               /* the cell */
static int linespace;                   /* extra pixels between lines */

/* TrueType, through ttengine.library: a .ttf file at a pixel size, with
 * its bold and italic files where there are any. */
static BOOL usett;
static char ttfile[160] = "SYS:Fonts/TrueType/DejaVuSansMono.ttf";
/* The PC's own font for PC BBS mode: the IBM VGA 8x16 as pixel outlines
 * (int10h.org's Ultimate Oldschool PC Font Pack, CC BY-SA 4.0), at its
 * one true size and not smoothed. */
static BOOL pcfont = TRUE;              /* the setting */
static char pcttfile[160] = "PROGDIR:Fonts/PxPlus_IBM_VGA_8x16.ttf";
static BOOL pcfont_on;                  /* it is the font now */
static BOOL ttaa = TRUE;                /* smoothing; off for pixel fonts */
static struct { BOOL usett; char ttfile[160]; int ttsize; } keepfont;
static int ttsize = 14;
static APTR ttface[4];                  /* regular, bold, italic, both */
static BOOL ttruns;                     /* whole cells per glyph: runs */
static int ox, oy, cols, rows;          /* where the grid starts, its size */
static APTR visual;
static struct Menu *menu;
static BOOL want_switch;
static int want_mode, want_depth;
static BOOL want_full, want_bar;

static LONG want_cols = 80, want_rows = 25; /* the first window, in cells */
static LONG wl = -1, wt, ww, wh;        /* the Workbench window, remembered */
static char calls[300];                 /* places called, newest first:
                                           "host[:port][!];..." (! = PC) */
static BOOL remember = TRUE;            /* FALSE: a one-off start (COLS,
                                           HOST...) - its window is not kept */
static BOOL quitting;
static BOOL owed;                       /* drawing owed: a frame timer runs */
static BOOL flood;                      /* the last draw was a scroll */

#ifdef PROFILE
/* A profiling build (make PROFILE=1): Emu68's free-running 54 MHz counter
 * (MOVEC $e1, as Wasabi's health.c reads it), ticks per part. */
#define MOVEC_D0(rc, out) \
    asm volatile(".short 0x4e7a, %c1\n\tmove.l %%d0,%0" \
                 : "=g"(out) : "i"(rc) : "d0")
#ifdef PROFILE_ECLOCK
/* make PROFILE=1 PROFILE_ECLOCK=1: the CIA E clock (709379 Hz on PAL),
 * for a stock Amiga or an emulator; TPMS: ticks per millisecond */
#include <proto/timer.h>
extern struct Device *TimerBase;
#define TPMS 709
static ULONG ticks(void)
{
    struct EClockVal e;
    if (!TimerBase)
        return 0;
    ReadEClock(&e);
    return e.ev_lo;
}
#else
#define TPMS 54000
static ULONG ticks(void)
{
    ULONG lo;
    APTR ssp = SuperState();
    MOVEC_D0(0xe1, lo);
    if (ssp)
        UserState(ssp);
    return lo;
}
#endif
static ULONG pf_vt, pf_draw, pf_pkt, pf_wait, pf_all0, pf_writes, pf_draws;
static ULONG pf_scroll, pf_text, pf_cur, pf_knob, pf_rows, pf_fill, pf_nrows, pf_nall, pf_nscroll;
static struct timeval pf_tv0;
static ULONG pf_calib;
#define PF_START(v) ULONG v = ticks()
#define PF_ADD(acc, v) (acc += ticks() - (v))
#else
#define PF_START(v)
#define PF_ADD(acc, v)
#endif
static int curx = -1, cury = -1;        /* where the cursor was drawn */

/* The pen cache: an RGB value asked of the screen once. */
#define NPEN 256
static ULONG pc_rgb[NPEN];
static LONG pc_pen[NPEN];
static int npc;

static LONG pen_for(ULONG rgb)
{
    int i, best = -1;
    LONG d, bd = 0x7FFFFFFF;
    static int mru;                     /* where the last one was found */
    if (mru < npc && pc_rgb[mru] == rgb)
        return pc_pen[mru];
    for (i = 0; i < npc; i++)
        if (pc_rgb[i] == rgb) {
            mru = i;
            return pc_pen[i];
        }
    if (npc < NPEN) {
        LONG p = ObtainBestPen(scr->ViewPort.ColorMap,
                               ((rgb >> 16) & 255) * 0x01010101,
                               ((rgb >> 8) & 255) * 0x01010101,
                               (rgb & 255) * 0x01010101,
                               OBP_Precision, PRECISION_IMAGE, TAG_END);
        if (p >= 0) {
            pc_rgb[npc] = rgb;
            pc_pen[npc++] = p;
            return p;
        }
    }
    for (i = 0; i < npc; i++) {             /* full: the nearest we have */
        int dr = ((pc_rgb[i] >> 16) & 255) - ((rgb >> 16) & 255);
        int dg = ((pc_rgb[i] >> 8) & 255) - ((rgb >> 8) & 255);
        int db = (pc_rgb[i] & 255) - (rgb & 255);
        d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best >= 0 ? pc_pen[best] : 1;
}

/* Plain (planar) screens: only the bitplanes some pen on show differs
 * from the background pen in are drawn or scrolled - on a 16-colour
 * Workbench with the default colours that is one plane of four. After a
 * full clear every plane outside the mask holds the background's bit
 * everywhere, so skipping it changes nothing (CCON's plane mask). */
static BOOL painter;                    /* rows built in memory (RTG) */
static LONG pbase = -1;                 /* the clear's pen; -1: no mask */
static UBYTE pmask;

static void rp_forget(void);

static void planes_reset(void)
{
    if (!painter && !usett && rp->BitMap) {
        pbase = pen_for(defbg);
        pmask = 0;
    } else {
        pbase = -1;
        pmask = 0xFF;
    }
    SetWriteMask(rp, pmask);
    rp_forget();                        /* every pen must pass mask_pen */
}

static void planes_full(void)
{
    pbase = -1;
    pmask = 0xFF;
    SetWriteMask(rp, 0xFF);
}

static void mask_pen(LONG p)
{
    UBYTE m;
    if (pbase < 0)
        return;
    m = pmask | (UBYTE)(p ^ pbase);
    if (m != pmask) {
        pmask = m;
        SetWriteMask(rp, pmask);
    }
}

/* What the RastPort is set to, so a run in the same pens and style as
 * the last costs no graphics calls (each one is dozens of instructions
 * on a 14 MHz Amiga). Forgotten at every draw: others may touch rp. */
static LONG c_apen = -1, c_bpen = -1;
static ULONG c_argb = ~0UL, c_brgb = ~0UL;
static int c_drmd = -1, c_style = -1;
static BOOL rgb_memo;                   /* cell_rgb's last answer holds */

static void rp_forget(void)
{
    c_apen = c_bpen = -1;
    c_argb = c_brgb = ~0UL;
    c_drmd = c_style = -1;
    rgb_memo = FALSE;
}

static void apen(ULONG rgb)
{
    LONG p;
    if (rgb == c_argb)
        return;
    c_argb = rgb;
    p = pen_for(rgb);
    mask_pen(p);
    if (p != c_apen) {
        SetAPen(rp, p);
        c_apen = p;
    }
}

static void bpen(ULONG rgb)
{
    LONG p;
    if (rgb == c_brgb)
        return;
    c_brgb = rgb;
    p = pen_for(rgb);
    mask_pen(p);
    if (p != c_bpen) {
        SetBPen(rp, p);
        c_bpen = p;
    }
}

static void drmd(int m)
{
    if (m != c_drmd) {
        SetDrMd(rp, m);
        c_drmd = m;
    }
}

static void softstyle(int st)
{
    if (st != c_style) {
        SetSoftStyle(rp, st, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        c_style = st;
    }
}

static void release_pens(void)
{
    while (npc > 0)
        ReleasePen(scr->ViewPort.ColorMap, pc_pen[--npc]);
}

/* What a cell's colours come to, as RGB. */
/* Amiga programs mean Workbench pens by colours 0-7 (Ed's text is pen 1,
 * "black"): in the local Shell they follow the window's own colours -
 * background, text, highlight, accent - not ANSI's. */
static ULONG pen_rgb(int i)
{
    if (local && i < 8)
        return i == 0 ? defbg : i == 1 ? deffg : themes[theme].wbpens[i - 2];
    return index_rgb(i);
}

static void cell_rgb_(const VtCell *c, ULONG *fg, ULONG *bg);

/* Runs next to each other mostly share colours: the last answer is kept
 * (forgotten with the RastPort state, at every draw). */
static void cell_rgb(const VtCell *c, ULONG *fg, ULONG *bg)
{
    static ULONG kf, kb, rf, rb;
    static UWORD ka;
    if (rgb_memo && c->fg == kf && c->bg == kb && c->attr == ka) {
        *fg = rf;
        *bg = rb;
        return;
    }
    cell_rgb_(c, fg, bg);
    kf = c->fg; kb = c->bg; ka = c->attr;
    rf = *fg; rb = *bg;
    rgb_memo = TRUE;
}

static void cell_rgb_(const VtCell *c, ULONG *fg, ULONG *bg)
{
    ULONG f, b, cf = c->fg, cb = c->bg;
    if (pcmode) {
        /* a PC: bold is the bright half of the 16 colours, blink the
         * bright backgrounds; the default is grey on black */
        if (cf == VT_DEFAULT)
            cf = VT_INDEX(7);
        if (cb == VT_DEFAULT)
            cb = VT_INDEX(0);
        if ((c->attr & VT_BOLD) && VT_IS_INDEX(cf) && (cf & 255) < 8)
            cf += 8;
        if ((c->attr & VT_BLINK) && VT_IS_INDEX(cb) && (cb & 255) < 8)
            cb += 8;
        f = VT_IS_INDEX(cf) ? index_rgb(cf & 255) : cf & 0xFFFFFF;
        /* black behind text is the window's own background: on a
         * Workbench of few colours "black" may come out as another pen */
        b = cb == VT_INDEX(0) ? defbg
          : VT_IS_INDEX(cb) ? index_rgb(cb & 255) : cb & 0xFFFFFF;
    } else {
        f = cf == VT_DEFAULT ? deffg : VT_IS_INDEX(cf)
          ? pen_rgb(cf & 255) : cf & 0xFFFFFF;
        b = cb == VT_DEFAULT ? defbg : VT_IS_INDEX(cb)
          ? pen_rgb(cb & 255) : cb & 0xFFFFFF;
    }
    BOOL inv = (c->attr & VT_INVERSE) != 0;
    if (vt_reverse_video(vt))
        inv = !inv;
    if (inv) {
        ULONG t = f;
        f = b;
        b = t;
    }
    if (c->attr & VT_DIM)
        f = mix(f, b, 40);
    if (c->attr & VT_HIDDEN)
        f = b;
    *fg = f;
    *bg = b;
}

/* ---- characters a bitmap font does not have -------------------------- */

/* Drawing either into the window, or into a cell picture in memory (the
 * painter's letter cache): the shapes below draw through these two. */
static ULONG *cellbuf;                  /* non-NULL: painting a cell */
static int cellw;                       /* its row length in pixels */
static ULONG cellcol;

static void RF(int x0, int y0, int x1, int y1)
{
    int x, y;
    if (!cellbuf) {
        RectFill(rp, x0, y0, x1, y1);
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cellw) x1 = cellw - 1;
    if (y1 >= ch) y1 = ch - 1;
    for (y = y0; y <= y1; y++) {
        ULONG *p = cellbuf + y * cellw + x0;
        for (x = x0; x <= x1; x++)
            *p++ = cellcol;
    }
}

static void SETCOL(ULONG rgb)
{
    if (cellbuf)
        cellcol = rgb;
    else
        apen(rgb);
}

static void hline(int x0, int x1, int y, int th)
{
    RF(x0, y, x1, y + th - 1);
}

static void vline(int x, int y0, int y1, int th)
{
    RF(x, y0, x + th - 1, y1);
}

/* Box drawing: four arms from the middle of the cell to its edges -
 * light, heavy (twice as thick) or double (two thin lines). Each arm
 * runs a little past the middle, so arms of any kind meet. */
static void draw_box(UBYTE arms, int x, int y)
{
    int t = cw >= 12 ? 2 : 1, cx = x + cw / 2, cy = y + ch / 2, g = t + 1;
    int a[4], i, k;
    a[0] = arms >> 6 & 3;               /* up */
    a[1] = arms >> 4 & 3;               /* right */
    a[2] = arms >> 2 & 3;               /* down */
    a[3] = arms & 3;                    /* left */
    for (i = 0; i < 4; i++) {
        int w = a[i], th, n, off[2], reach;
        if (!w)
            continue;
        if (w == 3) {
            n = 2; th = t; off[0] = -g; off[1] = g - t + 1; reach = g;
        } else {
            n = 1; th = w == 2 ? 2 * t : t; off[0] = -(th / 2);
            reach = th / 2;             /* thin arms meet exactly at the
                                           middle: corners do not stick out */
        }
        for (k = 0; k < n; k++) {
            int o = off[k];
            switch (i) {
            case 0: RF(cx + o, y, cx + o + th - 1, cy + reach); break;
            case 2: RF(cx + o, cy - reach, cx + o + th - 1,
                             y + ch - 1); break;
            case 1: RF(cx - reach, cy + o, x + cw - 1,
                             cy + o + th - 1); break;
            case 3: RF(x, cy + o, cx + reach, cy + o + th - 1); break;
            }
        }
    }
}

/* Block elements U+2580-259F: parts of the cell filled. */
static void draw_block(ULONG cp, int x, int y, ULONG fg, ULONG bg)
{
    int hw = cw / 2, hh = ch / 2, i;
    static const UBYTE quads[16] = {     /* 2596..259F: UL UR LL LR bits */
        2, 1, 8, 8 | 2 | 1, 8 | 1, 8 | 4 | 2, 8 | 4 | 1, 4, 4 | 2, 4 | 2 | 1
    };
    if (cp == 0x2580) { RF(x, y, x + cw - 1, y + hh - 1); return; }
    if (cp >= 0x2581 && cp <= 0x2588) {          /* lower eighths */
        i = (cp - 0x2580) * ch / 8;
        RF(x, y + ch - i, x + cw - 1, y + ch - 1);
        return;
    }
    if (cp >= 0x2589 && cp <= 0x258F) {          /* left eighths */
        i = (0x2590 - cp) * cw / 8;
        RF(x, y, x + i - 1, y + ch - 1);
        return;
    }
    if (cp == 0x2590) { RF(x + hw, y, x + cw - 1, y + ch - 1); return; }
    if (cp >= 0x2591 && cp <= 0x2593) {
        /* shades: dots, as the PC's font has them (ANSI art lives on
         * their texture): a quarter, half, three quarters */
        static UWORD pat[3][2] = { { 0x2222, 0x8888 }, { 0x5555, 0xAAAA },
                                   { 0x7777, 0xDDDD } };
        const UWORD *pp = pat[cp - 0x2591];
        if (cellbuf) {
            int px, py;
            for (py = 0; py < ch; py++) {
                ULONG *row = cellbuf + (y + py) * cellw + x;
                UWORD bits = pp[py & 1];
                for (px = 0; px < cw; px++)
                    row[px] = bits & (0x8000 >> (px & 15)) ? fg : bg;
            }
        } else {
            apen(fg);
            bpen(bg);
            drmd(JAM2);
            SetAfPt(rp, (UWORD *)pp, 1);    /* two lines, from the window */
            RectFill(rp, x, y, x + cw - 1, y + ch - 1);
            SetAfPt(rp, NULL, 0);
        }
        return;
    }
    if (cp == 0x2594) { RF(x, y, x + cw - 1, y + ch / 8); return; }
    if (cp == 0x2595) { RF(x + cw - cw / 8 - 1, y, x + cw - 1,
                                 y + ch - 1); return; }
    if (cp >= 0x2596) {
        UBYTE q = quads[cp - 0x2596];          /* 8 UL, 4 UR, 2 LL, 1 LR */
        if (q & 8) RF(x, y, x + hw - 1, y + hh - 1);
        if (q & 4) RF(x + hw, y, x + cw - 1, y + hh - 1);
        if (q & 2) RF(x, y + hh, x + hw - 1, y + ch - 1);
        if (q & 1) RF(x + hw, y + hh, x + cw - 1, y + ch - 1);
    }
}

static void disc(int cx, int cy, int r, BOOL leftonly)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r)
            dx++;
        RF(cx - dx, cy + dy, leftonly ? cx : cx + dx, cy + dy);
    }
}

/* A few shapes Claude Code and others use. TRUE if drawn. */
static BOOL draw_shape(ULONG cp, int x, int y)
{
    int cx = x + cw / 2, cy = y + ch / 2, r = (cw < ch ? cw : ch) / 3, i;
    switch (cp) {
    case 0x25CF: case 0x23FA: case 0x2B24:              /* ● ⏺ */
        disc(cx, cy, r, FALSE);
        return TRUE;
    case 0x25D0: case 0x25D1: case 0x25D2: case 0x25D3:  /* ◐ spinner */
        disc(cx, cy, r, TRUE);
        return TRUE;
    case 0x25CB: case 0x25EF:                            /* ○ */
        disc(cx, cy, r, FALSE);
        return TRUE;
    case 0x25B6: case 0x23F5: case 0x25B8: case 0x25BA:  /* ▶ ⏵ */
        for (i = 0; i <= r; i++)
            RF(cx - r / 2, cy - (r - i), cx - r / 2 + i, cy + (r - i));
        return TRUE;
    case 0x276F: case 0x203A: case 0x276D:               /* ❯ */
        for (i = 0; i <= r; i++) {
            RF(cx - r / 2 + i, cy - r + i, cx - r / 2 + i + 1,
                     cy - r + i);
            RF(cx - r / 2 + i, cy + r - i, cx - r / 2 + i + 1,
                     cy + r - i);
        }
        return TRUE;
    case 0x2026:                                         /* … */
        for (i = 0; i < 3; i++)
            RF(x + 1 + i * (cw - 2) / 3, y + cbase - 1,
                     x + 1 + i * (cw - 2) / 3, y + cbase - 1);
        return TRUE;
    case 0x2193: case 0x2191:                            /* ↓ ↑ */
        vline(cx, y + 1, y + ch - 2, 1);
        for (i = 1; i <= r; i++) {
            int yy = cp == 0x2193 ? y + ch - 2 - i : y + 1 + i;
            RF(cx - i, yy, cx + i, yy);
        }
        return TRUE;
    case 0x2192: case 0x2190:                            /* → ← */
        hline(x + 1, x + cw - 2, cy, 1);
        for (i = 1; i <= r; i++) {
            int xx = cp == 0x2192 ? x + cw - 2 - i : x + 1 + i;
            RF(xx, cy - i, xx, cy + i);
        }
        return TRUE;
    case 0x23BF:                                         /* ⎿ like └ */
        draw_box(0x50, x, y);
        return TRUE;
    case 0x25A0: case 0x25AA: case 0x25FC:               /* ■ */
        RF(cx - r, cy - r, cx + r, cy + r);
        return TRUE;
    case 0x25C6: case 0x2666:                            /* ◆ */
        for (i = -r; i <= r; i++)
            RF(cx - (r - abs(i)), cy + i, cx + (r - abs(i)), cy + i);
        return TRUE;
    }
    return FALSE;
}

/* Everything else outside Latin-1: the nearest Latin-1 character. */
static UBYTE fallback(ULONG cp)
{
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201B: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
    case 0x2015: case 0x2212: return '-';
    case 0x2022: case 0x2219: case 0x22C5: return 0xB7;
    case 0x2713: case 0x2714: case 0x2705: return 'v';
    case 0x2717: case 0x2718: case 0x274C: return 'x';
    case 0x26A0: return '!';
    case 0x2264: return '<';
    case 0x2265: return '>';
    case 0x2260: return '#';
    case 0x03C0: return 'p';
    case 0x00A0: return ' ';
    }
    if ((cp >= 0x2720 && cp <= 0x274B) || cp == 0x2731 || cp == 0x2736 ||
        cp == 0x2733 || cp == 0x22C6 || cp == 0x2605 || cp == 0x2606)
        return '*';                                      /* ✻ ✢ ✽ ✶ ✳ */
    return '?';
}

/* ---- drawing rows ------------------------------------------------------ */

/* Cells CTerm draws itself rather than with the font: table lines and
 * blocks always (they must join exactly), and with a bitmap font
 * everything outside Latin-1. */
static BOOL is_special(ULONG cp)
{
    if (cp == VT_WIDE_TAIL)
        return FALSE;
    if (cp >= 0x2500 && cp <= 0x259F)
        return TRUE;
    return !usett && cp > 0xFF;
}

/* Characters Claude Code uses that DejaVu Sans Mono lacks: a close
 * one it has (measured: Knowledge/amiga/graphics-and-performance.md). */
static ULONG tt_stand_in(ULONG cp)
{
    switch (cp) {
    case 0x23F5: return 0x25B6;         /* the status line's play marks */
    case 0x23F4: return 0x25C0;
    case 0x23BF: return 0x2514;         /* the hook under tool results */
    case 0x23FA: return 0x25CF;         /* the record dot */
    }
    return cp;
}

static APTR tt_for(UWORD attr)
{
    if (pcmode)
        attr &= ~VT_BOLD;               /* a PC's bold is a colour */
    int i = (attr & VT_BOLD ? 1 : 0) | (attr & VT_ITALIC ? 2 : 0);
    return ttface[i] ? ttface[i] : ttface[i & 1] ? ttface[i & 1] : ttface[0];
}

/* A run of TrueType text on its background; cols[i]: glyph i's cell. */
static void tt_run(const ULONG *u, const int *cols, int n, int y,
                   ULONG fg, UWORD attr)
{
    int i;
    drmd(JAM1);
    apen(fg);
    TT_SetFont(rp, tt_for(attr));
    TT_SetAttrs(rp, TT_SoftStyle, attr & VT_UNDERLINE
                ? TT_SoftStyle_Underlined : TT_SoftStyle_None, TAG_END);
    if (ttruns) {
        int from = 0;
        /* glyphs are exactly one cell: draw runs, broken only where a
         * wide character takes two */
        for (i = 1; i <= n; i++)
            if (i == n || cols[i] != cols[i - 1] + 1) {
                Move(rp, ox + cols[from] * cw, y + cbase);
                TT_Text(rp, (APTR)(u + from), i - from);
                from = i;
            }
    } else {
        for (i = 0; i < n; i++) {
            Move(rp, ox + cols[i] * cw, y + cbase);
            TT_Text(rp, (APTR)(u + i), 1);
        }
    }
    drmd(JAM2);
}

/* Plain (planar) screens: Text() draws the letters. A CPU writer into
 * the planes was measured on a stock A1200 (FS-UAE, 2026-10-10) and lost:
 * in C, 13000 instructions for a 76-letter row (~17 ms) against Text()'s
 * ~3 ms; only assembly could win, and only on many-coloured rows. */
static BOOL planar;                     /* a native screen, real planes */

/* ---- the selection: text marked with the mouse ------------------------
 * In screen cells, reading order from the anchor (a) to the end (b),
 * both included; it goes when the text under it moves (a scroll). */

static void sel_bounds(int *r0, int *c0, int *r1, int *c1)
{
    if (sa_r < sb_r || (sa_r == sb_r && sa_c <= sb_c)) {
        *r0 = sa_r; *c0 = sa_c; *r1 = sb_r; *c1 = sb_c;
    } else {
        *r0 = sb_r; *c0 = sb_c; *r1 = sa_r; *c1 = sa_c;
    }
}

/* The selected columns of row r: FALSE when none. */
static BOOL sel_span(int r, int *s0, int *s1)
{
    int r0, c0, r1, c1;
    if (!sel_on)
        return FALSE;
    sel_bounds(&r0, &c0, &r1, &c1);
    if (r < r0 || r > r1)
        return FALSE;
    *s0 = r == r0 ? c0 : 0;
    *s1 = r == r1 ? c1 : cols - 1;
    return TRUE;
}

static void draw_cells(int r, int x0, int x1)
{
    static const VtCell empty = { 0, VT_DEFAULT, VT_DEFAULT, 0 };
    static UBYTE text[512];
    int w, x, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);

    int s0 = -1, s1 = -2, xend = x1;
    BOOL insel;
    sel_span(r, &s0, &s1);
    drmd(JAM2);
    x = x0;
    while (x <= xend) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        int n = 0, start = x;
        /* a run stays on one side of the selection's edges */
        insel = x >= s0 && x <= s1;
        x1 = insel ? (s1 < xend ? s1 : xend)
                   : (x < s0 && s0 - 1 < xend ? s0 - 1 : xend);
        UWORD style = (c->attr & VT_BOLD && !pcmode ? FSF_BOLD : 0) |
                      (c->attr & VT_ITALIC ? FSF_ITALIC : 0) |
                      (c->attr & VT_UNDERLINE ? FSF_UNDERLINED : 0);

        cell_rgb(c, &fg, &bg);
        if (insel) {
            ULONG t = fg; fg = bg; bg = t;
        }
        if (usett)
            cp = tt_stand_in(cp);
        if (is_special(cp)) {
            /* one special cell: its background, then the shape */
            apen(bg);
            RectFill(rp, ox + x * cw, y, ox + x * cw + cw - 1, y + ch - 1);
            apen(fg);
            if (cp >= 0x2500 && cp <= 0x257F) {
                UBYTE a = boxarms[cp - 0x2500];
                if (a)
                    draw_box(a, ox + x * cw, y);
                else {                                  /* ╱ ╲ ╳ */
                    if (cp != 0x2572) {
                        Move(rp, ox + x * cw, y + ch - 1);
                        Draw(rp, ox + x * cw + cw - 1, y);
                    }
                    if (cp != 0x2571) {
                        Move(rp, ox + x * cw, y);
                        Draw(rp, ox + x * cw + cw - 1, y + ch - 1);
                    }
                }
            } else if (cp >= 0x2580 && cp <= 0x259F) {
                draw_block(cp, ox + x * cw, y, fg, bg);
            } else if (!draw_shape(cp, ox + x * cw, y)) {
                UBYTE ch1 = fallback(cp);
                bpen(bg);
                softstyle(style);
                Move(rp, ox + x * cw, y + cbase);
                Text(rp, (STRPTR)&ch1, 1);
            }
            x++;
            continue;
        }
        /* a run of ordinary characters that look the same - first the
         * common case in a tight loop: Latin-1 letters in exactly the
         * same colours and style as the run's first */
        if (line) {
            const VtCell *d = &line[x];
            int lim = x1 < w - 1 ? x1 : w - 1;
            ULONG cf = c->fg, cb = c->bg;
            UWORD ca = c->attr;
            if (lim - x >= (int)sizeof text)
                lim = x + (int)sizeof text - 1;
            while (x <= lim) {
                ULONG dc = d->cp;
                if (dc > 0xFF || d->fg != cf || d->bg != cb || d->attr != ca)
                    break;
                text[n++] = dc < 0x20 ? ' ' : (UBYTE)dc;
                d++;
                x++;
            }
        }
        /* then the rest, one by one */
        while (x <= x1 && n < (int)sizeof text) {
            const VtCell *d = x < w && line ? &line[x] : &empty;
            ULONG f2, b2;
            if (is_special(d->cp))
                break;
            if (d != c && (d->attr != c->attr || d->fg != c->fg ||
                           d->bg != c->bg)) {
                cell_rgb(d, &f2, &b2);
                if (f2 != fg || b2 != bg ||
                    (d->attr & (VT_BOLD | VT_ITALIC | VT_UNDERLINE)) !=
                    (c->attr & (VT_BOLD | VT_ITALIC | VT_UNDERLINE)))
                    break;
            }
            text[n++] = d->cp == VT_WIDE_TAIL ? ' ' : d->cp < 0x20 ? ' '
                      : (UBYTE)d->cp;
            x++;
        }
        if (usett) {
            static ULONG u[512];
            static int ucol[512];
            int k, m = 0;
            apen(bg);
            RectFill(rp, ox + start * cw, y, ox + x * cw - 1, y + ch - 1);
            for (k = start; k < x; k++) {
                const VtCell *d = k < w && line ? &line[k] : &empty;
                if (d->cp == VT_WIDE_TAIL || d->cp <= ' ')
                    continue;           /* spaces: the background did it */
                u[m] = tt_stand_in(d->cp);
                ucol[m++] = k;
            }
            if (m)
                tt_run(u, ucol, m, y, fg, c->attr);
        } else {
            if (linespace) {            /* Text() fills the font's rows only */
                apen(bg);
                RectFill(rp, ox + start * cw, y, ox + x * cw - 1, y + ch - 1);
            }
            apen(fg);
            bpen(bg);
            PF_START(tt);
            softstyle(style);
            Move(rp, ox + start * cw, y + cbase);
            Text(rp, (STRPTR)text, n);
            PF_ADD(pf_text, tt);
        }
        if (c->attr & VT_STRIKE)
            hline(ox + start * cw, ox + x * cw - 1, y + ch / 2, 1);
    }
    softstyle(0);
}

/* The text area's edges: the margin left of column 0 and whatever is
 * right of the last column. TrueType letters can reach a pixel into the
 * margin (a "y" at the start of a line), so a row clears its edges too. */
static int area_right(void)
{
    return win->Width - win->BorderRight - 1 - (fullscreen ? POSBAR + 2 : 0);
}

static void paint_row(int r);
static UBYTE shown_blank[1024];

/* Columns a..b of row r, brought up to date: the text up to the last
 * cell in the span that shows anything, then one fill for the rest. A
 * blank row already shown blank (after a scroll, say) is not touched. */
static void draw_span(int r, int a, int b)
{
    int w, last, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);
    if (b >= cols)
        b = cols - 1;
    if (a > b)
        return;
    last = b;
    if (!vt_reverse_video(vt)) {
        if (!line || w <= a)
            last = a - 1;
        else {
            if (last > w - 1)
                last = w - 1;
            while (last >= a) {
                const VtCell *c = &line[last];
                if ((c->cp && c->cp != ' ') || c->bg != VT_DEFAULT ||
                    (c->attr & (VT_INVERSE | VT_UNDERLINE | VT_STRIKE)))
                    break;
                last--;
            }
        }
    }
    {
        int s0, s1;                     /* marked cells are drawn, blank or not */
        if (sel_span(r, &s0, &s1) && s1 >= a && s0 <= b && last < s1)
            last = s1 < b ? s1 : b;
    }
    if (last < a && a == 0 && b == cols - 1 &&
        r < (int)sizeof shown_blank && shown_blank[r])
        return;
    if (last >= a)
        draw_cells(r, a, last);
    {
    PF_START(tf);
    apen(defbg);
    if (a == 0)
        RectFill(rp, win->BorderLeft, y, ox - 1, y + ch - 1);
    if (last < b || b == cols - 1) {
        int x1 = b == cols - 1 ? area_right() : ox + (b + 1) * cw - 1;
        if (ox + (last + 1) * cw <= x1)
            RectFill(rp, ox + (last + 1) * cw, y, x1, y + ch - 1);
    }
    PF_ADD(pf_fill, tf);
    }
    if (r < (int)sizeof shown_blank) {
        if (last >= a)
            shown_blank[r] = 0;
        else if (a == 0 && b == cols - 1)
            shown_blank[r] = 1;
    }
}

static void draw_row(int r)
{
    if (r < 0 || r >= rows)
        return;
    if (painter) {
        paint_row(r);                   /* the cursor comes with the row */
        return;
    }
    draw_span(r, 0, cols - 1);
}

static void draw_cursor(BOOL on)
{
    int x, y, vis;
    vt_cursor(vt, &x, &y, &vis);
    if (painter) {
        /* the rows carry the cursor: repaint where it was and where it is */
        if (!on)
            return;
        if (cury >= 0 && cury < rows && cury != y)
            paint_row(cury);
        if (y < rows)
            paint_row(y);
        curx = x;
        cury = y;
        return;
    }
    if (curx >= 0 && cury >= 0 && cury < rows && curx < cols) {
        draw_cells(cury, curx, curx);       /* where it was: plain again */
        curx = cury = -1;
    }
    if (!on || !vis || back > 0 || x >= cols || y >= rows)
        return;
    /* the cursor: the cell in reverse */
    {
        int w;
        const VtCell *line = vt_line(vt, y, 0, &w);
        VtCell c;
        ULONG fg, bg;
        UBYTE t;
        if (line && x < w)
            c = line[x];
        else
            memset(&c, 0, sizeof c);
        cell_rgb(&c, &fg, &bg);
        apen(fg);
        RectFill(rp, ox + x * cw, oy + y * ch, ox + x * cw + cw - 1,
                 oy + y * ch + ch - 1);
        if (usett && c.cp > ' ' && !is_special(c.cp)) {
            ULONG u = c.cp;
            int col = x;
            tt_run(&u, &col, 1, oy + y * ch, bg, c.attr);
        } else if (c.cp > ' ' && c.cp <= 0xFF) {
            t = (UBYTE)c.cp;
            drmd(JAM1);
            apen(bg);
            Move(rp, ox + x * cw, oy + y * ch + cbase);
            Text(rp, (STRPTR)&t, 1);
        }
        curx = x;
        cury = y;
    }
}

/* ======================================================================
 * The painter: rows built in fast memory, one WritePixelArray each
 *
 * On an RTG screen with 15 bits or more, graphics.library's ScrollRaster
 * reads video memory back and costs ~10 ms a line on the A1200 (Emu68,
 * VideoCore), while writing a ready row costs ~0.03 ms. So here nothing
 * is ever scrolled or read: every cell (a letter in its colours and
 * style, a table line, a block) is drawn once into a small picture - the
 * cell cache - and rows are put together from those pictures and written
 * in one go. Measured, and CCON found the same: see Knowledge/amiga/
 * graphics-and-performance.md, "RTG on PiStorm/Emu68".
 * ==================================================================== */

static ULONG *rowbuf;                   /* one text row: cols*cw x ch */
static LONG rowbuf_n;

#define CSIZE 4096                      /* cache entries (a power of 2) */
struct CEntry {
    ULONG cp, fg, bg;
    UWORD attr;
    UBYTE wide, used;
    ULONG *pix;
};
static struct CEntry *ccache;
static ULONG *cpool;
static LONG cell_longs;                 /* per entry: 2*cw x ch */

static void cache_free(void)
{
    if (ccache) { FreeVec(ccache); ccache = NULL; }
    if (cpool) { FreeVec(cpool); cpool = NULL; }
}

static BOOL cache_alloc(void)
{
    int i;
    cache_free();
    cell_longs = 2 * cw * ch;
    ccache = AllocVec(CSIZE * sizeof *ccache, MEMF_ANY | MEMF_CLEAR);
    cpool = AllocVec(CSIZE * cell_longs * 4, MEMF_ANY);
    if (!ccache || !cpool) {
        cache_free();
        return FALSE;
    }
    for (i = 0; i < CSIZE; i++)
        ccache[i].pix = cpool + i * cell_longs;
    return TRUE;
}

/* A letter of the bitmap font, from its own bits (no window needed). */
static void cell_bitmap_glyph(UBYTE c, ULONG fg, UWORD attr)
{
    struct TextFont *f = font;
    int lo = f->tf_LoChar, hi = f->tf_HiChar, y, b, w, off;
    ULONG loc;
    if (c < lo || c > hi)
        c = hi + 1 - lo + lo;           /* the font's "missing" glyph */
    loc = ((ULONG *)f->tf_CharLoc)[c - lo];
    off = loc >> 16;
    w = loc & 0xFFFF;
    if (w > cw)
        w = cw;
    for (y = 0; y < f->tf_YSize && y + cbase - f->tf_Baseline < ch; y++) {
        const UBYTE *row = (const UBYTE *)f->tf_CharData + y * f->tf_Modulo;
        ULONG *dst = cellbuf + (y + cbase - f->tf_Baseline) * cellw;
        int shift = attr & VT_ITALIC ? (f->tf_Baseline - y) / 2 : 0;
        for (b = 0; b < w; b++) {
            int bit = off + b;
            if (row[bit >> 3] & (0x80 >> (bit & 7))) {
                int x = b + shift;
                if (x >= 0 && x < cellw)
                    dst[x] = fg;
                if ((attr & VT_BOLD) && x + 1 < cellw)
                    dst[x + 1] = fg;    /* the bold smear */
            }
        }
    }
}

/* A TrueType letter: ttengine's greyscale picture, blended. */
static void cell_tt_glyph(ULONG cp, ULONG fg, ULONG bg, UWORD attr, int w)
{
    struct TT_Pixmap *pm;
    ULONG u = cp;
    int x, y, ph, pw;
    struct TextExtent te;
    int top, left;
    pm = TT_GetPixmap(tt_for(attr), (APTR)&u, 1, TT_Encoding,
                      TT_Encoding_UTF32_BE, TT_Antialias,
                      ttaa ? TT_Antialias_On : TT_Antialias_Off,
                      TAG_END);
    if (!pm)
        return;
    /* the picture is the letter's ink only: it goes where the ink sits
     * on the baseline (measured: an "n" is 8 rows, an "E" 10 - put both
     * at the cell's top and the text jitters) */
    TT_SetFont(rp, tt_for(attr));
    TT_SetAttrs(rp, TT_Encoding, TT_Encoding_UTF32_BE, TAG_END);
    TT_TextExtent(rp, (APTR)&u, 1, &te);
    top = cbase + te.te_Extent.MinY;
    left = te.te_Extent.MinX;
    if (top < 0) top = 0;
    if (left < 0) left = 0;
    ph = (int)pm->ttp_Height;
    if (top + ph > ch) ph = ch - top;
    pw = (int)pm->ttp_Width;
    if (left + pw > w) pw = w - left;
    for (y = 0; y < ph; y++) {
        const UBYTE *a = pm->ttp_Data + y * pm->ttp_Width;
        ULONG *dst = cellbuf + (top + y) * cellw + left;
        for (x = 0; x < pw; x++) {
            int k = a[x];
            if (k == 0)
                continue;
            if (k == 255)
                dst[x] = fg;
            else {
                ULONG out = 0;
                int sh;
                for (sh = 0; sh <= 16; sh += 8) {
                    int f2 = (fg >> sh) & 255, b2 = (bg >> sh) & 255;
                    out |= (ULONG)(b2 + ((f2 - b2) * k) / 255) << sh;
                }
                dst[x] = out;
            }
        }
    }
    TT_FreePixmap(pm);
}

/* Draw one cell picture into an entry. */
static void make_cell(struct CEntry *e)
{
    ULONG cp = e->cp;
    int w = e->wide ? 2 * cw : cw;
    cellbuf = e->pix;
    cellw = w;
    SETCOL(e->bg);
    RF(0, 0, w - 1, ch - 1);
    SETCOL(e->fg);
    if (cp >= 0x2500 && cp <= 0x257F && boxarms[cp - 0x2500]) {
        draw_box(boxarms[cp - 0x2500], 0, 0);
    } else if (cp >= 0x2571 && cp <= 0x2573) {
        int i;                          /* the diagonals, by hand */
        for (i = 0; i < cw; i++) {
            int y1 = (ch - 1) - i * (ch - 1) / (cw > 1 ? cw - 1 : 1);
            if (cp != 0x2572) RF(i, y1, i, y1);
            if (cp != 0x2571) RF(i, ch - 1 - y1, i, ch - 1 - y1);
        }
    } else if (cp >= 0x2580 && cp <= 0x259F) {
        draw_block(cp, 0, 0, e->fg, e->bg);
    } else if (cp > ' ' && cp != 0xA0) {
        if (usett)
            cell_tt_glyph(cp, e->fg, e->bg, e->attr, w);
        else if (cp <= 0xFF || !draw_shape(cp, 0, 0))
            cell_bitmap_glyph(cp <= 0xFF ? (UBYTE)cp : fallback(cp), e->fg,
                              e->attr);
    }
    if (e->attr & VT_UNDERLINE) {
        int uy = cbase + 1 < ch ? cbase + 1 : ch - 1;
        RF(0, uy, w - 1, uy);
    }
    if (e->attr & VT_STRIKE)
        RF(0, ch / 2, w - 1, ch / 2);
    cellbuf = NULL;
}

static struct CEntry *cell_get(ULONG cp, ULONG fg, ULONG bg, UWORD attr,
                               UBYTE wide)
{
    ULONG h = (cp * 2654435761UL) ^ (fg * 0x9E37UL) ^ (bg * 0x85EBUL) ^
              ((ULONG)attr << 7) ^ wide;
    int i, k;
    attr &= VT_BOLD | VT_ITALIC | VT_UNDERLINE | VT_STRIKE;
    for (k = 0; k < 8; k++) {
        struct CEntry *e = &ccache[(h + k) & (CSIZE - 1)];
        if (e->used && e->cp == cp && e->fg == fg && e->bg == bg &&
            e->attr == attr && e->wide == wide)
            return e;
        if (!e->used) {
            e->cp = cp; e->fg = fg; e->bg = bg; e->attr = attr;
            e->wide = wide; e->used = 1;
            make_cell(e);
            return e;
        }
    }
    i = h & (CSIZE - 1);                /* full there: replace the first */
    ccache[i].cp = cp; ccache[i].fg = fg; ccache[i].bg = bg;
    ccache[i].attr = attr; ccache[i].wide = wide; ccache[i].used = 1;
    make_cell(&ccache[i]);
    return &ccache[i];
}

/* What each screen row shows now: a blank row painted blank need not be
 * painted again (CCON's blank-scroll skip: a flood of empty lines then
 * costs nothing on screen). Cleared whenever the window is redrawn. */
/* The rows a scroll moved on screen carry what they showed; the rows it
 * brought in are blank. */
static void shift_shown(int top, int bottom, int n)
{
    int r;
    if (bottom >= (int)sizeof shown_blank)
        bottom = sizeof shown_blank - 1;
    if (n > 0)
        for (r = top; r <= bottom; r++)
            shown_blank[r] = r + n <= bottom ? shown_blank[r + n] : 1;
    else if (n < 0)
        for (r = bottom; r >= top; r--)
            shown_blank[r] = r + n >= top ? shown_blank[r + n] : 1;
}

static void forget_shown(void)
{
    memset(shown_blank, 0, sizeof shown_blank);
}

/* One text row: put together from cell pictures, written in one go. */
static void paint_row(int r)
{
    int ps0 = -1, ps1 = -2;
    static const VtCell empty = { 0, VT_DEFAULT, VT_DEFAULT, 0 };
    int w, x, cx = -1, cy = -1, vis = 0, rw = cols * cw;
    const VtCell *line = vt_line(vt, r, back, &w);
    const VtCell *prev = NULL;
    ULONG pfg = 0, pbg = 0;
    struct CEntry *pe = NULL;
    if (r < 0 || r >= rows)
        return;
    vt_cursor(vt, &cx, &cy, &vis);
    if (!vis || back > 0)
        cy = -1;
    sel_span(r, &ps0, &ps1);
    /* a blank row (all default spaces, no cursor): one fill, or nothing */
    if (r != cy && !vt_reverse_video(vt) && ps1 < 0) {
        int blank = 1;
        for (x = 0; x < w && x < cols && line; x++) {
            const VtCell *c = &line[x];
            if ((c->cp && c->cp != ' ') || c->fg != VT_DEFAULT ||
                c->bg != VT_DEFAULT || c->attr) {
                blank = 0;
                break;
            }
        }
        if (blank) {
            if (r < (int)sizeof shown_blank && shown_blank[r])
                return;
            apen(defbg);
            RectFill(rp, ox, oy + r * ch, ox + rw - 1, oy + r * ch + ch - 1);
            if (r < (int)sizeof shown_blank)
                shown_blank[r] = 1;
            return;
        }
    }
    if (r < (int)sizeof shown_blank)
        shown_blank[r] = 0;
    for (x = 0; x < cols; x++) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        UBYTE wide = 0;
        struct CEntry *e;
        int y, n;
        ULONG *src, *dst;
        if (cp == VT_WIDE_TAIL)
            continue;                   /* drawn with its left half */
        if (x + 1 < cols && x + 1 < w && line && line[x + 1].cp == VT_WIDE_TAIL)
            wide = 1;
        /* neighbours mostly share colours: work them out once a run */
        if (prev && c->fg == prev->fg && c->bg == prev->bg &&
            c->attr == prev->attr) {
            fg = pfg;
            bg = pbg;
        } else {
            cell_rgb(c, &fg, &bg);
            pfg = fg;
            pbg = bg;
            pe = NULL;
        }
        prev = c;
        if ((r == cy && x == cx) ^ (x >= ps0 && x <= ps1)) {
            /* the cursor, or a marked cell: reversed */
            ULONG t = fg; fg = bg; bg = t;
            pe = NULL;
            prev = NULL;
        }
        if (usett)
            cp = tt_stand_in(cp);
        if (cp < ' ')
            cp = ' ';
        if (pe && pe->cp == cp && !wide && !pe->wide && pe->fg == fg &&
            pe->bg == bg)
            e = pe;                     /* the same cell again (spaces) */
        else
            e = pe = cell_get(cp, fg, bg,
                              pcmode ? c->attr & ~VT_BOLD : c->attr, wide);
        n = wide ? 2 * cw : cw;
        if (x * cw + n > rw)
            n = rw - x * cw;
        src = e->pix;
        dst = rowbuf + x * cw;
        for (y = 0; y < ch; y++) {
            ULONG *s2 = src, *d2 = dst;
            int k = n;
            while (k-- > 0)             /* a few longs: no call overhead */
                *d2++ = *s2++;
            src += wide ? 2 * cw : cw;
            dst += rw;
        }
    }
    WritePixelArray(rowbuf, 0, 0, rw * 4, rp, ox, oy + r * ch, rw, ch,
                    RECTFMT_ARGB);
}

/* Use the painter on this screen? And the buffers it needs. */
static void painter_setup(void)
{
    struct BitMap *bm = scr->RastPort.BitMap;
    LONG need = (LONG)cols * cw * ch;
    painter = FALSE;
    planar = !CyberGfxBase || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX);
    if (planar || GetCyberMapAttr(bm, CYBRMATTR_DEPTH) < 15)
        return;
    if (!ccache && !cache_alloc())
        return;
    if (need > rowbuf_n) {
        if (rowbuf) FreeVec(rowbuf);
        rowbuf = AllocVec(need * 4, MEMF_ANY);
        rowbuf_n = rowbuf ? need : 0;
    }
    painter = rowbuf != NULL;
}

/* After a font or colour change the pictures are out of date. */
static void painter_forget(void)
{
    cache_free();
    if (win)
        painter_setup();
}

/* ---- the scroll bar (from AmiClaude: see intuition-border-gadgets.md) - */

static struct Gadget vgad, agup, agdn;
static struct PropInfo vpi;
static struct Image vim;
static APTR iup, idn;
static BOOL gadsok;
static int arrowheld, arrowticks;

static APTR mksysi(ULONG which, int sysz)
{
    return NewObject(NULL, (STRPTR)"sysiclass", SYSIA_DrawInfo, (ULONG)dri,
                     SYSIA_Which, which, SYSIA_Size, sysz, TAG_DONE);
}

static void mkarrow(struct Gadget *g, APTR im, int le, int te, int id)
{
    struct Image *i = (struct Image *)im;
    g->LeftEdge = le;
    g->TopEdge = te;
    g->Width = i->Width;
    g->Height = i->Height;
    g->Flags = GFLG_RELRIGHT | GFLG_RELBOTTOM | GFLG_GADGIMAGE |
               GFLG_GADGHCOMP;
    g->Activation = GACT_RIGHTBORDER | GACT_RELVERIFY | GACT_IMMEDIATE;
    g->GadgetType = GTYP_BOOLGADGET;
    g->GadgetRender = im;
    g->GadgetID = id;
}

static void add_scroller(void)
{
    int brw = win->BorderRight, bbh = win->BorderBottom, vah = 0, vaw = 0;
    int sysz = scr->Height >= 400 ? SYSISIZE_MEDRES : SYSISIZE_LOWRES;
    int n = 1;
    APTR sz = mksysi(SIZEIMAGE, sysz);
    if (sz) {
        if (((struct Image *)sz)->Height > bbh)
            bbh = ((struct Image *)sz)->Height;
        DisposeObject(sz);
    }
    iup = mksysi(UPIMAGE, sysz);
    idn = mksysi(DOWNIMAGE, sysz);
    if (iup && idn) {
        vaw = ((struct Image *)iup)->Width;
        vah = ((struct Image *)iup)->Height;
    }
    memset(&vpi, 0, sizeof vpi);
    vpi.Flags = AUTOKNOB | FREEVERT | PROPNEWLOOK | PROPBORDERLESS;
    vpi.VertBody = 0xFFFF;
    memset(&vgad, 0, sizeof vgad);
    vgad.LeftEdge = -(brw - 5);
    vgad.TopEdge = win->BorderTop + 1;
    vgad.Width = brw - 8;
    vgad.Height = -(win->BorderTop + bbh + 2 + 2 * vah);
    vgad.Flags = GFLG_RELRIGHT | GFLG_RELHEIGHT;
    vgad.Activation = GACT_RELVERIFY | GACT_IMMEDIATE | GACT_RIGHTBORDER |
                      GACT_FOLLOWMOUSE;
    vgad.GadgetType = GTYP_PROPGADGET;
    vgad.GadgetRender = (APTR)&vim;
    vgad.SpecialInfo = (APTR)&vpi;
    vgad.GadgetID = 1;
    if (vah) {
        int vx = vgad.LeftEdge + (vgad.Width - vaw) / 2;
        memset(&agup, 0, sizeof agup);
        memset(&agdn, 0, sizeof agdn);
        mkarrow(&agup, iup, vx, -(bbh + 2 * vah - 1), 2);
        mkarrow(&agdn, idn, vx, -(bbh + vah - 1), 3);
        vgad.NextGadget = &agup;
        agup.NextGadget = &agdn;
        n = 3;
    }
    AddGList(win, &vgad, -1, n, NULL);
    RefreshWindowFrame(win);
    gadsok = TRUE;
}

/* The scroll bar's knob, or full screen's place bar, says where we are. */
static void update_knob(void)
{
    LONG total = vt_history(vt) + rows, top = vt_history(vt) - back;
    if (fullscreen && win) {
        int x0 = win->Width - POSBAR, y0 = oy, h = rows * ch, a, b;
        apen(defbg);
        RectFill(rp, x0 - 1, y0, win->Width - 1, y0 + h - 1);
        if (vt_history(vt) > 0) {
            a = y0 + (LONG)h * top / total;
            b = y0 + (LONG)h * (top + rows) / total - 1;
            if (b < a + 3) b = a + 3;
            apen(mix(defbg, deffg, 15));
            RectFill(rp, x0, y0, win->Width - 3, y0 + h - 1);
            apen(mix(defbg, deffg, 45));
            RectFill(rp, x0, a, win->Width - 3, b);
        }
        return;
    }
    if (!gadsok || (vpi.Flags & KNOBHIT))
        return;
    NewModifyProp(&vgad, win, NULL, vpi.Flags, 0,
                  total > rows ? (0xFFFFUL * top) / (total - rows) : 0, 0,
                  (0xFFFFUL * rows) / total, 1);
}

/* ---- layout ------------------------------------------------------------ */

/* The tab bar: a row of names above the text, while there are tabs. */
static int tabbar_h(void)
{
    return ntabs > 1 && font ? font->tf_YSize + 4 : 0;
}

static void layout_and_resize(void)
{
    int w, h, i;
    ox = win->BorderLeft + 2;
    oy = win->BorderTop + 1 + tabbar_h();
    w = win->Width - ox - win->BorderRight - 2 -
        (fullscreen ? POSBAR + 2 : 0);
    h = win->Height - win->BorderTop - win->BorderBottom - 2 - tabbar_h();
    cols = w / cw;
    rows = h / ch;
    if (cols < 10) cols = 10;
    if (rows < 3) rows = 3;
    for (i = 0; i < ntabs; i++)         /* all tabs share the window */
        if (tabs[i]->t_vt && (cols != vt_cols(tabs[i]->t_vt) ||
                            rows != vt_rows(tabs[i]->t_vt)))
            vt_resize(tabs[i]->t_vt, cols, rows);
}

static void tab_label(struct Tab *t, char *out)
{
    if (t->t_local)
        strcpy(out, "Shell");
    else if (t->t_dialhost[0]) {
        strcpy(out, t->t_sshmode ? "ssh " : "");
        strncat(out, t->t_dialhost, 40);
    } else
        strcpy(out, "relay");
}

static void draw_tabbar(void)
{
    int h = tabbar_h(), x0 = win->BorderLeft, y0 = win->BorderTop;
    int x1 = win->Width - win->BorderRight - 1, wd = x1 - x0 + 1, i;
    if (!h)
        return;
    planes_full();
    rp_forget();
    apen(mix(defbg, deffg, 12));
    RectFill(rp, x0, y0, x1, y0 + h - 1);
    SetFont(rp, font);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    for (i = 0; i < ntabs; i++) {
        char label[64], t[72];
        /* the width shared equally: a half each, a third each... */
        int sx = x0 + i * wd / ntabs, slot = x0 + (i + 1) * wd / ntabs - sx;
        int n, fit;
        BOOL on = tabs[i] == shown;
        tab_label(tabs[i], label);
        t[0] = '1' + i;
        t[1] = ' ';
        strcpy(t + 2, label);
        apen(on ? defbg : mix(defbg, deffg, 12));
        RectFill(rp, sx + 1, y0 + 1, sx + slot - 2, y0 + h - 1);
        apen(on ? deffg : mix(defbg, deffg, 55));
        bpen(on ? defbg : mix(defbg, deffg, 12));
        SetDrMd(rp, JAM2);
        n = strlen(t);
        fit = (slot - 8) / (font->tf_XSize ? font->tf_XSize : 8);
        if (n > fit) n = fit;
        Move(rp, sx + 4, y0 + 2 + font->tf_Baseline);
        Text(rp, (STRPTR)t, n > 0 ? n : 0);
    }
    if (usett)
        tt_attach();                    /* the window's font for TrueType */
    planes_reset();
    rp_forget();
}

static void forget_shown(void);

static void redraw_all(void)
{
    int r;
    VtChanges chg;
    forget_shown();
    rp_forget();
    planes_full();
    apen(defbg);
    RectFill(rp, win->BorderLeft, win->BorderTop,
             win->Width - win->BorderRight - 1,
             win->Height - win->BorderBottom - 1);
    planes_reset();                     /* all background: no plane yet */
    draw_tabbar();
    vt_take_changes(vt, &chg);
    curx = cury = -1;
    for (r = 0; r < rows; r++)
        draw_row(r);
    draw_cursor(TRUE);
    update_knob();
}

/* Bring the window up to date with the emulator: one blit for a scroll,
 * then only the rows that changed. */
static void refresh(void)
{
    VtChanges chg;
    int r;
    if (!vt_take_changes(vt, &chg))
        return;
    rp_forget();
    if (sel_on && (chg.all || chg.scroll_n)) {
        sel_on = sel_drag = FALSE;      /* the text under it moved */
        if (!chg.all) {
            int r;
            for (r = 0; r < rows; r++)  /* repaint where it was */
                ((uint8_t *)chg.dirty)[r] = 1, ((uint16_t *)chg.x0)[r] = 0,
                ((uint16_t *)chg.x1)[r] = cols - 1;
        }
    }
    draw_cursor(FALSE);
    flood = chg.all || chg.scroll_n != 0;
#ifdef PROFILE
    if (chg.all || back > 0) pf_nall++;
    if (chg.scroll_n) pf_nscroll++;
    for (r = 0; r < rows; r++) if (chg.dirty[r]) pf_nrows++;
#endif
    if (painter) {
        /* nothing is ever scrolled on screen: the rows a scroll moved
         * are painted again (cheap), the rest only if they changed */
        int y0 = chg.scroll_n ? chg.scroll_top : rows, y1 = chg.scroll_bottom;
        for (r = 0; r < rows; r++)
            if (chg.all || back > 0 || chg.dirty[r] || (r >= y0 && r <= y1))
                paint_row(r);
    } else if (chg.all || back > 0) {
        for (r = 0; r < rows; r++)
            draw_row(r);
    } else {
        if (chg.scroll_n) {
            bpen(defbg);
            PF_START(ts);
            ScrollRaster(rp, 0, chg.scroll_n * ch, win->BorderLeft,
                         oy + chg.scroll_top * ch, area_right(),
                         oy + (chg.scroll_bottom + 1) * ch - 1);
            PF_ADD(pf_scroll, ts);
            shift_shown(chg.scroll_top, chg.scroll_bottom, chg.scroll_n);
        }
        PF_START(tr);
        for (r = 0; r < rows; r++)
            if (chg.dirty[r])
                draw_span(r, chg.x0[r], chg.x1[r]);
        PF_ADD(pf_rows, tr);
    }
    {
        PF_START(tc);
        draw_cursor(TRUE);
        PF_ADD(pf_cur, tc);
    }
    {
        PF_START(tk);
        update_knob();
        PF_ADD(pf_knob, tk);
    }
}

/* ---- for console.c ------------------------------------------------- */

/* The console's calls, for its tab (t): the text goes into that tab's
 * emulator; only the shown tab is drawn. */
void term_write(void *t, const UBYTE *b, LONG n)
{
    PF_START(t0);
    vt_write(((struct Tab *)t)->t_vt, b, n);
    PF_ADD(pf_vt, t0);
#ifdef PROFILE
    pf_writes++;
#endif
}
int term_cols(void *t) { (void)t; return cols; }
int term_rows(void *t) { (void)t; return rows; }
int term_cursor_x(void *t)
{
    int x, y, v;
    vt_cursor(((struct Tab *)t)->t_vt, &x, &y, &v);
    return x;
}
int term_cursor_y(void *t)
{
    int x, y, v;
    vt_cursor(((struct Tab *)t)->t_vt, &x, &y, &v);
    return y;
}

static LONG lastcost;                   /* us: the last draw, planar only */

static void draw_now(void)
{
    PF_START(t0);
    owed = FALSE;
    if (win) {
        if (planar && !painter) {
            /* a stock Amiga: how long a draw takes paces the next one */
            struct timeval a, b;
            term_now(&a);
            refresh();
            term_now(&b);
            lastcost = (b.tv_secs - a.tv_secs) * 1000000 +
                       (LONG)b.tv_micro - (LONG)a.tv_micro;
        } else
            refresh();
    }
    PF_ADD(pf_draw, t0);
#ifdef PROFILE
    pf_draws++;
#endif
}

/* Draw within a frame: writes that keep coming meanwhile are drawn with
 * it, so a flood costs one draw per frame, not per write. A timer request
 * says when the frame is up - no clock is read here: on the A1200 (Emu68,
 * PiStorm) reading the system time is costly, and this runs per write. */
static void draw_soon(void)
{
    if (owed)
        return;
    owed = TRUE;
    /* jump scroll: while text pours in and every draw is a whole scrolled
     * window, draw every 60 ms instead of every frame - a third of the
     * work, still smooth to the eye; back to every frame once it calms */
    {
        /* paced: where a draw costs more than the frame (a 14 MHz Amiga
         * redrawing a flooded screen), wait half as long again as it
         * took, so the text keeps coming in between (CCON's pacing) */
        LONG us = flood ? 60000 : 20000, p = lastcost + lastcost / 2;
        if (flood && p > us)
            us = p > 160000 ? 160000 : p;
        con_arm_render(us);
    }
}

void term_flush(void *t)
{
    if (t == shown) {
        struct Tab *keep = T;
        T = shown;
        draw_now();
        T = keep;
    }
}

void term_render_soon(void *t)
{
    if (t == shown)
        draw_soon();
}

static void scroll_view(int to)
{
    int max = vt_history(vt);
    if (to < 0) to = 0;
    if (to > max) to = max;
    if (to == back)
        return;
    back = to;
    {
        int r;
        draw_cursor(FALSE);
        for (r = 0; r < rows; r++)
            draw_row(r);
        draw_cursor(TRUE);
    }
    update_knob();
}

/* ======================================================================
 * The connection: telnet over TCP
 * ==================================================================== */

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define O_BINARY 0
#define O_ECHO   1
#define O_SGA    3
#define O_NAWS   31
#define O_TTYPE  24

static LONG port = DEFAULT_PORT;
static BOOL saved_utf8 = TRUE;          /* the setting; utf8 may be LATIN1's */


static void send_raw(const UBYTE *b, LONG n)
{
    LONG done = 0;
    if (sock < 0 || sshmode)            /* telnet's own bytes only */
        return;
    while (done < n) {
        LONG k = send(sock, (APTR)(b + done), n - done, 0);
        if (k <= 0)
            return;
        done += k;
    }
}

/* Data to the host: 0xFF doubled (telnet), or through ssh. */
static void send_data(const UBYTE *b, LONG n)
{
    static UBYTE buf[1024];
    LONG i, k = 0;
    if (sshmode) {
        if (sock >= 0)
            ssh_write(T->ssh, b, n);
        return;
    }
    for (i = 0; i < n; i++) {
        if (k >= (LONG)sizeof buf - 2) {
            send_raw(buf, k);
            k = 0;
        }
        buf[k++] = b[i];
        if (b[i] == IAC)
            buf[k++] = IAC;
    }
    send_raw(buf, k);
}

static void send_naws(void)
{
    if (sshmode) {                      /* the pty's size, ssh's way */
        ssh_resize(T->ssh, cols, rows);
        return;
    }
    UBYTE m[9] = { IAC, SB, O_NAWS, 0, 0, 0, 0, IAC, SE };
    m[3] = cols >> 8; m[4] = cols & 255;
    m[5] = rows >> 8; m[6] = rows & 255;
    send_raw(m, 9);
}

static void telnet_option(int cmd, int opt)
{
    UBYTE r[3] = { IAC, 0, 0 };
    r[2] = opt;
    if (cmd == DO)
        r[1] = (opt == O_NAWS || opt == O_BINARY || opt == O_TTYPE) ? WILL
                                                                   : WONT;
    else if (cmd == WILL)
        r[1] = (opt == O_ECHO || opt == O_SGA || opt == O_BINARY) ? DO : DONT;
    else
        return;                         /* DONT/WONT: nothing to say */
    send_raw(r, 3);
    if (cmd == DO && opt == O_NAWS)
        send_naws();
}

/* Bytes from the host: telnet commands out, data to the emulator. */
static void from_host(UBYTE *b, LONG n)
{
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = b[i];
        switch (tstate) {
        case 0:
            if (c == IAC) tstate = 1;
            else b[k++] = c;
            break;
        case 1:
            if (c == IAC) { b[k++] = IAC; tstate = 0; }
            else if (c >= WILL && c <= DONT) { tcmd = c; tstate = 2; }
            else if (c == SB) { tstate = 3; sblen = 0; }
            else tstate = 0;
            break;
        case 2:
            if (k) { vt_write(vt, b, k); k = 0; }
            telnet_option(tcmd, c);
            tstate = 0;
            break;
        case 3:                         /* subnegotiation */
            if (c == IAC) tstate = 4;
            else if (sblen < (int)sizeof sbuf) sbuf[sblen++] = c;
            break;
        case 4:
            if (c == SE) {
                /* "which terminal are you?": a BBS picks ANSI or not by it */
                if (sblen >= 2 && sbuf[0] == O_TTYPE && sbuf[1] == 1) {
                    static const UBYTE head[4] = { IAC, SB, O_TTYPE, 0 };
                    static const UBYTE tail[2] = { IAC, SE };
                    const char *name = pcmode ? "ANSI" : "XTERM-256COLOR";
                    send_raw(head, 4);
                    send_raw((const UBYTE *)name, strlen(name));
                    send_raw(tail, 2);
                }
                sblen = 0;
                tstate = 0;
            } else {
                if (c == IAC && sblen < (int)sizeof sbuf) sbuf[sblen++] = c;
                tstate = 3;
            }
            break;
        }
    }
    if (k)
        vt_write(vt, b, k);
}

static void numstr_cat(char *out, LONG n)
{
    char tmp[12], *p = tmp + sizeof tmp - 1;
    *p = 0;
    if (n < 0) n = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    strcat(out, p);
}

static void say(const char *s)
{
    vt_write(vt, (const uint8_t *)s, strlen(s));
}

static LONG listen_on(void)
{
    struct sockaddr_in sa;
    LONG one = 1, fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 ||
        listen(fd, 1) < 0) {
        CloseSocket(fd);
        return -1;
    }
    return fd;
}

/* HOST: call out ("name" or "name:port", telnet's 23 by default). */
static BOOL dial(void)
{
    char name[128], *colon;
    struct hostent *he;
    struct sockaddr_in sa;
    LONG one = 1, p = 23;
    strcpy(name, dialhost);
    colon = strchr(name, ':');
    if (colon) {
        *colon = 0;
        p = atol(colon + 1);
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(p);
    sa.sin_addr.s_addr = inet_addr(name);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        he = gethostbyname((STRPTR)name);
        if (!he) {
            say("\x1b[1;31m[no such host]\x1b[0m\r\n");
            return FALSE;
        }
        memcpy(&sa.sin_addr, he->h_addr, 4);
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return FALSE;
    if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) {
        CloseSocket(sock);
        sock = -1;
        say("\x1b[1;31m[the call was not answered]\x1b[0m\r\n");
        return FALSE;
    }
    setsockopt(sock, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
    tstate = 0;
    return TRUE;
}

#ifndef NO_SSH
/* A line typed in the window while ssh sets up (a password: echo off).
 * FALSE: the window was closed. */
static BOOL term_ask(const char *prompt, char *buf, int max, BOOL echo)
{
    int n = 0;
    say(prompt);
    refresh();
    for (;;) {
        struct IntuiMessage *m;
        ULONG wsig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(wsig | shells_sigmask());
        if (got & ~wsig)
            shells_handle(got);         /* the other tabs go on */
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cl = m->Class;
            if (cl == IDCMP_RAWKEY && !(m->Code & IECODE_UP_PREFIX)) {
                struct InputEvent ie;
                UBYTE k[8];
                int got, i;
                memset(&ie, 0, sizeof ie);
                ie.ie_Class = IECLASS_RAWKEY;
                ie.ie_Code = m->Code;
                ie.ie_Qualifier = m->Qualifier;
                ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
                got = MapRawKey(&ie, (STRPTR)k, sizeof k, NULL);
                ReplyMsg((struct Message *)m);
                for (i = 0; i < got; i++) {
                    if (k[i] == '\r' || k[i] == '\n') {
                        buf[n] = 0;
                        say("\r\n");
                        refresh();
                        return TRUE;
                    }
                    if (k[i] == 8 && n > 0) {
                        n--;
                        if (echo) say("\b \b");
                    } else if (k[i] >= 32 && n < max - 1) {
                        buf[n++] = k[i];
                        if (echo) {
                            char e[2] = { (char)k[i], 0 };
                            say(e);
                        }
                    }
                    refresh();
                }
                continue;
            }
            ReplyMsg((struct Message *)m);
            if (cl == IDCMP_CLOSEWINDOW) {
                buf[0] = 0;
                return FALSE;
            }
        }
    }
}

#endif

/* Call the host: telnet, or ssh (which asks what it must in the window).
 * The TCP/IP stack is opened the first time a call needs it: a CTerm
 * started as a Shell has none open (and a call must not use a NULL base). */
static BOOL connect_out(void)
{
    if (!SocketBase)
        SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        say("\x1b[1;31m[a call needs a TCP/IP stack (bsdsocket.library)]"
            "\x1b[0m\r\n");
        return FALSE;
    }
    if (!sshmode)
        return dial();
    T->ssh = ssh_open(dialhost, cols, rows, term_ask, say);
    if (!T->ssh)
        return FALSE;
    sock = ssh_socket(T->ssh);
    return TRUE;
}

static void connected(void)
{
    LONG one = 1;
    UBYTE hello[3] = { IAC, WILL, O_NAWS };
    sock = accept(lfd, NULL, NULL);
    if (sock < 0)
        return;
    CloseSocket(lfd);
    lfd = -1;
    setsockopt(sock, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
    tstate = 0;
    send_raw(hello, 3);
    send_naws();
}

static void hung_up(void)
{
    if (sshmode) {
        ssh_close(T->ssh);              /* it owns the socket */
        T->ssh = NULL;
    }
    else
        CloseSocket(sock);
    sock = -1;
    if (dialhost[0]) {
        say("\r\n\x1b[0;2m[the connection closed - Return calls again]"
            "\x1b[0m\r\n");
        return;
    }
    say("\r\n\x1b[0;2m[the connection closed - waiting for it again]"
        "\x1b[0m\r\n");
    lfd = listen_on();
}

/* ======================================================================
 * Keys
 * ==================================================================== */

static BOOL connect_out(void);

/* After a call ended: Return calls again; other keys do nothing. */
static BOOL redial_key(const char *b)
{
    if (local || !dialhost[0] || sock >= 0)
        return FALSE;
    if (b[0] == '\r') {
        say("\x1b[2m[calling ");
        say(dialhost);
        say("...]\x1b[0m\r\n");
        refresh();
        if (connect_out())
            say("\x1b[2m[connected]\x1b[0m\r\n");
        else
            say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    }
    return TRUE;
}

static void send_str(const char *s)
{
    send_data((const UBYTE *)s, strlen(s));
}

/* Latin-1 typed on the Amiga, sent as UTF-8 when the host speaks it. */
static void send_typed(const UBYTE *b, LONG n)
{
    LONG i;
    for (i = 0; i < n; i++) {
        if (utf8 && b[i] >= 0x80) {
            UBYTE u[2];
            u[0] = 0xC0 | (b[i] >> 6);
            u[1] = 0x80 | (b[i] & 63);
            send_data(u, 2);
        } else
            send_data(&b[i], 1);
    }
}

static void key(struct IntuiMessage *m)
{
    UWORD code = m->Code, q = m->Qualifier;
    BOOL shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    BOOL ctrl = (q & IEQUALIFIER_CONTROL) != 0;
    char buf[16];
    LONG n;
    static const char *const fkeys[10] = {
        "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~",
        "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~"
    };

    if (code & IECODE_UP_PREFIX)
        return;
    if ((q & IEQUALIFIER_RCOMMAND) && code >= 0x01 && code <= 0x09) {
        tab_show(code - 0x01);          /* Right Amiga + 1..9: that tab */
        return;
    }
    if (local) {
        /* the Amiga Shell: the keymap's own bytes, as CON: gives them;
         * only the scrollback keys stay CTerm's */
        if (code == 0x7A) { scroll_view(back + 3); return; }
        if (code == 0x7B) { scroll_view(back - 3); return; }
        if (shift && (code == 0x4C || code == 0x4D)) {
            scroll_view(back + (code == 0x4C ? rows - 1 : -(rows - 1)));
            return;
        }
        {
            struct InputEvent ie;
            memset(&ie, 0, sizeof ie);
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = code;
            ie.ie_Qualifier = q;
            ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
            n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
        }
        if (n > 0) {
            if (back)
                scroll_view(0);
            con_keys(T->con, (UBYTE *)buf, n);
        }
        return;
    }
    switch (code) {
    case 0x7A: scroll_view(back + 3); return;           /* the wheel */
    case 0x7B: scroll_view(back - 3); return;
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: {        /* arrows */
        static const char dirs[4] = { 'A', 'B', 'C', 'D' };
        char d = dirs[code - 0x4C];
        if (shift && (code == 0x4C || code == 0x4D)) {
            scroll_view(back + (code == 0x4C ? rows - 1 : -(rows - 1)));
            return;
        }
        if (ctrl)
            buf[0] = 0x1b, buf[1] = '[', buf[2] = '1', buf[3] = ';',
            buf[4] = '5', buf[5] = d, buf[6] = 0;
        else if (vt_app_cursor(vt))
            buf[0] = 0x1b, buf[1] = 'O', buf[2] = d, buf[3] = 0;
        else
            buf[0] = 0x1b, buf[1] = '[', buf[2] = d, buf[3] = 0;
        break;
    }
    case 0x41: buf[0] = 0x7F; buf[1] = 0; break;         /* backspace */
    case 0x46: strcpy(buf, "\x1b[3~"); break;            /* del */
    case 0x45: buf[0] = 0x1B; buf[1] = 0; break;         /* esc */
    case 0x44: case 0x43: buf[0] = '\r'; buf[1] = 0; break;
    case 0x42:                                           /* tab */
        strcpy(buf, shift ? "\x1b[Z" : "\t");
        break;
    case 0x5F: strcpy(buf, "\x1b[2~"); break;            /* help: insert */
    default:
        if (code >= 0x50 && code <= 0x59) {
            strcpy(buf, fkeys[code - 0x50]);
            break;
        }
        {
            struct InputEvent ie;
            memset(&ie, 0, sizeof ie);
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = code;
            ie.ie_Qualifier = q;
            ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
            n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
        }
        if (n <= 0)
            return;
        if (back)
            scroll_view(0);
        if (!redial_key(buf))
            send_typed((UBYTE *)buf, n);
        return;
    }
    if (back)
        scroll_view(0);
    if (!redial_key(buf))
        send_str(buf);
}

/* ======================================================================
 * Screens, windows and menus
 * ==================================================================== */

enum { M_QUIT = 1, M_FONT, M_BAR, M_FULL, M_SCR_WB, M_SCR_16, M_SCR_256,
       M_SCR_DEEP, M_UTF8, M_ABOUT, M_TTFONT, M_BIGGER, M_SMALLER, M_SPEED,
       M_PC, M_CALL, M_COPY, M_PASTE, M_SETTINGS, M_NEWTAB, M_CLOSETAB };

static struct NewMenu newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"New Shell tab", (STRPTR)"T", 0, 0, (APTR)M_NEWTAB },
    {  NM_ITEM, (STRPTR)"Call...", (STRPTR)"K", 0, 0, (APTR)M_CALL },
    {  NM_ITEM, (STRPTR)"Close tab", (STRPTR)"W", 0, 0, (APTR)M_CLOSETAB },
    {  NM_ITEM, (STRPTR)"About", 0, 0, 0, (APTR)M_ABOUT },
    {  NM_ITEM, (STRPTR)"Speed test", 0, 0, 0, (APTR)M_SPEED },
    {  NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Copy", (STRPTR)"C", 0, 0, (APTR)M_COPY },
    {  NM_ITEM, (STRPTR)"Paste", (STRPTR)"V", 0, 0, (APTR)M_PASTE },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Settings...", (STRPTR)"S", 0, 0, (APTR)M_SETTINGS },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Amiga font...", 0, 0, 0, (APTR)M_FONT },
    {  NM_ITEM, (STRPTR)"TrueType font...", 0, 0, 0,
       (APTR)M_TTFONT },
    {  NM_ITEM, (STRPTR)"Bigger text", (STRPTR)"+", 0, 0, (APTR)M_BIGGER },
    {  NM_ITEM, (STRPTR)"Smaller text", (STRPTR)"-", 0, 0, (APTR)M_SMALLER },
    {  NM_ITEM, (STRPTR)"Screen", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"Workbench window", 0, CHECKIT, ~1, (APTR)M_SCR_WB },
    {   NM_SUB, (STRPTR)"Own screen, 16 colours", 0, CHECKIT, ~2,
        (APTR)M_SCR_16 },
    {   NM_SUB, (STRPTR)"Own screen, 256 colours", 0, CHECKIT, ~4,
        (APTR)M_SCR_256 },
    {   NM_SUB, (STRPTR)"Own screen, like Workbench", 0, CHECKIT, ~8,
        (APTR)M_SCR_DEEP },
    {  NM_ITEM, (STRPTR)"Full screen", (STRPTR)"F", CHECKIT | MENUTOGGLE, 0,
       (APTR)M_FULL },
    {  NM_ITEM, (STRPTR)"Scroll bar", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_BAR },
    {  NM_ITEM, (STRPTR)"UTF-8", 0, CHECKIT | MENUTOGGLE, 0, (APTR)M_UTF8 },
    {  NM_ITEM, (STRPTR)"PC BBS (CP437)", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_PC },
    { NM_END, 0, 0, 0, 0, 0 },
};

static void call_dialog(void);
static void open_settings(void);
static void close_settings(void);
static void settings_events(void);
static struct Window *setwin;
static void sel_copy(void);
static void paste(void);
static void clip_close(void);

static void make_menus(void)
{
    struct NewMenu *n;
    for (n = newmenu; n->nm_Type != NM_END; n++) {
        ULONG id = (ULONG)n->nm_UserData;
        BOOL on;
        switch (id) {
        case M_SCR_WB: on = scrmode == SCR_WB; break;
        case M_SCR_16: on = scrmode == SCR_OWN && owndepth == 4; break;
        case M_SCR_256: on = scrmode == SCR_OWN && owndepth == 8; break;
        case M_SCR_DEEP: on = scrmode == SCR_OWN && owndepth > 8; break;
        case M_FULL:
            on = fullscreen;
            if (scrmode == SCR_OWN) n->nm_Flags &= ~NM_ITEMDISABLED;
            else n->nm_Flags |= NM_ITEMDISABLED;
            break;
        case M_BAR: on = scrollbar; break;
        case M_UTF8: on = utf8 && !pcmode; break;
        case M_PC: on = pcmode; break;
        default: continue;
        }
        if (on) n->nm_Flags |= CHECKED;
        else n->nm_Flags &= ~CHECKED;
    }
    visual = GetVisualInfo(scr, TAG_END);
    if (visual && (menu = CreateMenus(newmenu, TAG_END)) &&
        LayoutMenus(menu, visual, GTMN_NewLookMenus, TRUE, TAG_END))
        SetMenuStrip(win, menu);
}

static BOOL open_own_screen(void)
{
    struct Screen *wb = LockPubScreen(NULL);
    ULONG id = INVALID_ID;
    int depth = owndepth;
    UWORD pens[] = { (UWORD)~0 };
    if (wb) {
        id = GetVPModeID(&wb->ViewPort);
        if (depth > 8 && ownmode == INVALID_ID)
            depth = wb->RastPort.BitMap->Depth > 8
                  ? wb->RastPort.BitMap->Depth : 8;
        UnlockPubScreen(NULL, wb);
    }
    if (ownmode != INVALID_ID)
        id = ownmode;                   /* chosen in the settings */
    else if (depth <= 8) {
        ULONG best = BestModeID(BIDTAG_SourceID, id, BIDTAG_Depth, depth,
                                TAG_END);
        if (best != INVALID_ID)
            id = best;
    }
    ownscr = OpenScreenTags(NULL,
        SA_DisplayID, id, SA_Depth, depth,
        SA_Width, ownmode != INVALID_ID && ownw > 0 ? ownw : STDSCREENWIDTH,
        SA_Height, ownmode != INVALID_ID && ownh > 0 ? ownh : STDSCREENHEIGHT,
        SA_Title, (ULONG)"CTerm", SA_PubName, (ULONG)"CTERM",
        SA_Type, PUBLICSCREEN, SA_Pens, (ULONG)pens, SA_SysFont, 1,
        SA_SharePens, TRUE, SA_Interleaved, TRUE,
        SA_ShowTitle, !fullscreen, TAG_END);
    if (!ownscr)
        return FALSE;
    PubScreenStatus(ownscr, 0);
    return TRUE;
}

static void close_own_screen(void)
{
    if (ownscr && (PubScreenStatus(ownscr, PSNF_PRIVATE) & PSNF_PRIVATE)) {
        CloseScreen(ownscr);
        ownscr = NULL;
    }
}

/* The cell from the Amiga font, with the line spacing shared above and
 * below the letters. */
static void bitmap_metrics(void)
{
    cw = font->tf_XSize;
    ch = font->tf_YSize + linespace;
    cbase = font->tf_Baseline + linespace / 2;
}

static BOOL load_font(void)
{
    static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *f = NULL;
    if (fontname[0] && DiskfontBase) {
        struct TextAttr ta;
        ta.ta_Name = (STRPTR)fontname;
        ta.ta_YSize = fontsize;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        f = OpenDiskFont(&ta);
        if (f && (f->tf_Flags & FPF_PROPORTIONAL)) {
            CloseFont(f);
            f = NULL;
        }
    }
    if (!f) {
        struct TextFont *d = GfxBase->DefaultFont;
        if (d && !(d->tf_Flags & FPF_PROPORTIONAL)) {
            struct TextAttr ta;
            ta.ta_Name = (STRPTR)d->tf_Message.mn_Node.ln_Name;
            ta.ta_YSize = d->tf_YSize;
            ta.ta_Style = 0;
            ta.ta_Flags = 0;
            f = OpenFont(&ta);
        }
    }
    if (!f)
        f = OpenFont(&topaz8);
    if (!f)
        return FALSE;
    if (ownfont)
        CloseFont(ownfont);
    font = ownfont = f;
    bitmap_metrics();
    return TRUE;
}

/* ======================================================================
 * Settings: ENV:CTerm.prefs (and ENVARC:), one "key=value" per line.
 * ==================================================================== */

#define PREFS_VAR "CTerm.prefs"
static BOOL hadprefs;

static ULONG hexval(const char *q)
{
    ULONG v = 0;
    while ((*q >= '0' && *q <= '9') || ((*q | 32) >= 'a' && (*q | 32) <= 'f'))
        v = v * 16 + (*q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10), q++;
    return v;
}

static void hex_cat(char *out, ULONG v, int digits)
{
    char t[9];
    int i;
    for (i = 0; i < digits; i++) {
        int n = (v >> (4 * (digits - 1 - i))) & 15;
        t[i] = n < 10 ? '0' + n : 'A' + n - 10;
    }
    t[digits] = 0;
    strcat(out, t);
}

static BOOL envonly;                    /* Use: until a reboot, not saved */
static BOOL used_only;                  /* the settings were Used, not Saved */
static BOOL saved_pc;                   /* the setting; pcmode may be PC's */

static void load_prefs(void)
{
    static char buf[1200];
    char *p = buf;
    /* the whole file: without GVF_BINARY_VAR, GetVar stops at the first
     * line break (every setting after "truetype" was lost) */
    LONG n = GetVar(PREFS_VAR, buf, sizeof buf,
                    GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (n <= 0)
        return;
    if (n >= (LONG)sizeof buf)
        n = sizeof buf - 1;
    buf[n] = 0;
    hadprefs = TRUE;
    while (*p) {
        char *eol = strchr(p, '\n'), *val;
        if (eol) *eol = 0;
        val = strchr(p, '=');
        if (val) {
            *val++ = 0;
            if (!stricmp(p, "truetype")) usett = val[0] == '1';
            else if (!stricmp(p, "ttfile")) {
                strncpy(ttfile, val, sizeof ttfile - 1);
                ttfile[sizeof ttfile - 1] = 0;
            }
            else if (!stricmp(p, "ttsize")) ttsize = atoi(val);
            else if (!stricmp(p, "font")) {
                char *sl = strrchr(val, '/');
                if (sl) {
                    *sl = 0;
                    strncpy(fontname, val, sizeof fontname - 1);
                    fontsize = atoi(sl + 1);
                }
            }
            else if (!stricmp(p, "screen")) {
                scrmode = stricmp(val, "wb") ? SCR_OWN : SCR_WB;
                if (scrmode == SCR_OWN) owndepth = atoi(val + 3);
                if (owndepth < 1) owndepth = 8;
            }
            else if (!stricmp(p, "full")) fullscreen = val[0] == '1';
            else if (!stricmp(p, "scrollbar")) scrollbar = val[0] == '1';
            else if (!stricmp(p, "utf8")) utf8 = saved_utf8 = val[0] == '1';
            else if (!stricmp(p, "theme")) {
                int i;
                for (i = 0; i < NTHEMES; i++)
                    if (!stricmp(val, themes[i].name))
                        theme = i;
            }
            else if (!stricmp(p, "fg")) { myfg = hexval(val); ownfg = TRUE; }
            else if (!stricmp(p, "bg")) { mybg = hexval(val); ownbg = TRUE; }
            else if (!stricmp(p, "spacing")) {
                linespace = atoi(val);
                if (linespace < 0 || linespace > 8) linespace = 0;
            }
            else if (!stricmp(p, "pc")) pcmode = saved_pc = val[0] == '1';
            else if (!stricmp(p, "pcfont")) pcfont = val[0] != '0';
            else if (!stricmp(p, "mode")) {     /* "hexid,w,h" */
                const char *q = val;
                ownmode = hexval(q);
                while (*q && *q != ',') q++;
                ownw = ownh = 0;
                if (*q == ',') for (q++; *q >= '0' && *q <= '9'; q++)
                    ownw = ownw * 10 + *q - '0';
                if (*q == ',') for (q++; *q >= '0' && *q <= '9'; q++)
                    ownh = ownh * 10 + *q - '0';
            }
            else if (!stricmp(p, "calls")) {
                strncpy(calls, val, sizeof calls - 1);
                calls[sizeof calls - 1] = 0;
            }
            else if (!stricmp(p, "window")) {
                /* four numbers: left,top,width,height (libnix's strtol
                 * gave no end pointer here, so read them by hand) */
                LONG v[4] = { -1, 0, 0, 0 };
                int k = 0;
                const char *q = val;
                for (k = 0; k < 4 && *q; k++) {
                    LONG n = 0;
                    while (*q >= '0' && *q <= '9')
                        n = n * 10 + (*q++ - '0');
                    v[k] = n;
                    if (*q == ',')
                        q++;
                }
                wl = v[0]; wt = v[1]; ww = v[2]; wh = v[3];
                if (k < 4 || ww < 100 || wh < 50) wl = -1;
            }
        }
        if (!eol)
            break;
        p = eol + 1;
    }
    if (ttsize < 6 || ttsize > 72)
        ttsize = 14;
}

static void save_prefs(void)
{
    if (setwin)
        return;                         /* the settings window decides */
    static char buf[1200];
    buf[0] = 0;
    strcat(buf, "truetype=");
    strcat(buf, (pcfont_on ? keepfont.usett : usett) ? "1" : "0");
    strcat(buf, "\nttfile=");
    strcat(buf, pcfont_on ? keepfont.ttfile : ttfile);
    strcat(buf, "\nttsize=");
    numstr_cat(buf, pcfont_on ? keepfont.ttsize : ttsize);
    if (!pcfont) strcat(buf, "\npcfont=0");
    if (fontname[0]) {
        strcat(buf, "\nfont=");
        strcat(buf, fontname);
        strcat(buf, "/");
        numstr_cat(buf, fontsize);
    }
    strcat(buf, "\nscreen=");
    if (scrmode == SCR_WB)
        strcat(buf, "wb");
    else {
        strcat(buf, "own");
        numstr_cat(buf, owndepth);
    }
    strcat(buf, "\nfull="); strcat(buf, fullscreen ? "1" : "0");
    strcat(buf, "\nscrollbar="); strcat(buf, scrollbar ? "1" : "0");
    strcat(buf, "\nutf8="); strcat(buf, saved_utf8 ? "1" : "0");
    strcat(buf, "\ntheme="); strcat(buf, themes[theme].name);
    if (ownfg) { strcat(buf, "\nfg="); hex_cat(buf, myfg, 6); }
    if (ownbg) { strcat(buf, "\nbg="); hex_cat(buf, mybg, 6); }
    if (linespace) { strcat(buf, "\nspacing="); numstr_cat(buf, linespace); }
    if (saved_pc) strcat(buf, "\npc=1");
    if (ownmode != INVALID_ID) {
        strcat(buf, "\nmode="); hex_cat(buf, ownmode, 8);
        strcat(buf, ","); numstr_cat(buf, ownw);
        strcat(buf, ","); numstr_cat(buf, ownh);
    }
    if (calls[0]) {
        strcat(buf, "\ncalls=");
        strcat(buf, calls);
    }
    if (wl >= 0) {
        strcat(buf, "\nwindow=");
        numstr_cat(buf, wl); strcat(buf, ",");
        numstr_cat(buf, wt); strcat(buf, ",");
        numstr_cat(buf, ww); strcat(buf, ",");
        numstr_cat(buf, wh);
    }
    strcat(buf, "\n");
    SetVar(PREFS_VAR, buf, -1,
           GVF_GLOBAL_ONLY | (envonly ? 0 : GVF_SAVE_VAR));
}

static void close_tt(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if (ttface[i]) {
            TT_CloseFont(ttface[i]);
            ttface[i] = NULL;
        }
}

static BOOL file_exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l)
        UnLock(l);
    return l != 0;
}

/* The bold or italic file beside a .ttf: DejaVuSansMono-Bold.ttf,
 * VeraMonoBold.ttf, ...-Oblique / -Italic / -BoldOblique. */
static APTR open_variant(const char *const *suffixes)
{
    char path[180], base[160];
    int n = strlen(ttfile), i;
    if (n < 5 || stricmp(ttfile + n - 4, ".ttf"))
        return NULL;
    strncpy(base, ttfile, n - 4);
    base[n - 4] = 0;
    for (i = 0; suffixes[i]; i++) {
        strcpy(path, base);
        strcat(path, suffixes[i]);
        strcat(path, ".ttf");
        if (file_exists(path))
            return TT_OpenFont(TT_FontFile, (ULONG)path, TT_FontSize, ttsize,
                               TAG_END);
    }
    return NULL;
}

/* Open the TrueType font and measure its cell. FALSE: not possible. */
static BOOL load_tt(void)
{
    static const char *const bold[] = { "-Bold", "Bold", NULL };
    static const char *const ital[] = { "-Oblique", "-Italic", "Italic",
                                        "Oblique", NULL };
    static const char *const both[] = { "-BoldOblique", "-BoldItalic",
                                        "BoldItalic", "BoldOblique", NULL };
    struct RastPort trp;
    ULONG top = 0, bot = 0;
    int ten;
    if (!TTEngineBase)
        return FALSE;
    close_tt();
    ttface[0] = TT_OpenFont(TT_FontFile, (ULONG)ttfile, TT_FontSize, ttsize,
                            TAG_END);
    if (!ttface[0])
        return FALSE;
    ttface[1] = open_variant(bold);
    ttface[2] = open_variant(ital);
    ttface[3] = open_variant(both);
    InitRastPort(&trp);
    TT_SetFont(&trp, ttface[0]);
    TT_SetAttrs(&trp, TT_Encoding, TT_Encoding_ISO8859_1, TAG_END);
    TT_GetAttrs(&trp, TT_FontMaxTop, (ULONG)&top, TT_FontMaxBottom,
                (ULONG)&bot, TAG_END);
    cw = TT_TextLength(&trp, (APTR)"0", 1);
    ten = TT_TextLength(&trp, (APTR)"0000000000", 10);
    TT_DoneRastPort(&trp);
    if (cw <= 0 || top + bot <= 0) {
        close_tt();
        return FALSE;
    }
    ttruns = ten == 10 * cw;
    ch = top + bot + linespace;         /* the spacing shared above, below */
    cbase = top + linespace / 2;
    return TRUE;
}

/* After the window opens or the font changes: tell ttengine about it. */
static void tt_attach(void)
{
    if (usett && win)
        TT_SetAttrs(rp, TT_Window, (ULONG)win, TT_Antialias,
                    ttaa ? TT_Antialias_On : TT_Antialias_Off,
                    TT_Encoding, TT_Encoding_UTF32_BE,
                    TAG_END);
}

static BOOL open_display(void)
{
    ULONG idcmp = IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY |
                  IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE |
                  IDCMP_INTUITICKS | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW |
                  IDCMP_INACTIVEWINDOW | IDCMP_MOUSEBUTTONS;
    if (scrmode == SCR_OWN && !ownscr && !open_own_screen())
        scrmode = SCR_WB;
    if (scrmode == SCR_WB) {
        fullscreen = FALSE;
        scr = LockPubScreen(NULL);
        if (!scr)
            return FALSE;
    } else {
        scr = ownscr;
        ShowTitle(ownscr, !fullscreen);
    }
    dri = GetScreenDrawInfo(scr);
    if (!font && !load_font())          /* the bitmap font is always there */
        return FALSE;
    if (usett && !ttface[0] && !load_tt())
        usett = FALSE;
    if (!usett) {
        bitmap_metrics();
    }

    if (fullscreen) {
        win = OpenWindowTags(NULL,
            WA_Left, 0, WA_Top, 0, WA_Width, scr->Width,
            WA_Height, scr->Height, WA_CustomScreen, (ULONG)scr,
            WA_Backdrop, TRUE, WA_Borderless, TRUE,
            WA_SimpleRefresh, TRUE, WA_Activate, TRUE,
            WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_END);
    } else {
        int top0 = scr->BarHeight + 1;
        /* the window for want_cols x want_rows cells, frame included */
        int w = want_cols * cw + 4 + scr->WBorLeft + 22 + 2,
            h = want_rows * ch + 2 + scr->WBorTop + scr->Font->ta_YSize + 1 +
                scr->WBorBottom + 2;
        int l = 0, t = top0;
        if (dialhost[0]) {              /* a call: beside the Shell's window */
            l = 32;
            t = top0 + 24;
        }
        if (remember && wl >= 0 && scr != ownscr) {
            l = wl; t = wt; w = ww; h = wh;     /* as it was left */
        }
        if (w > scr->Width) w = scr->Width;
        if (h > scr->Height - top0) h = scr->Height - top0;
        if (l + w > scr->Width) l = scr->Width - w;
        if (t + h > scr->Height) t = scr->Height - h;
        if (l < 0) l = 0;
        if (t < top0) t = top0;
        win = OpenWindowTags(NULL,
            WA_Left, l, WA_Top, t, WA_Width, w, WA_Height, h,
            WA_Title, (ULONG)(dialhost[0] ? dialtitle : "CTerm"),
            scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
            WA_SizeGadget, TRUE, WA_SizeBRight, scrollbar,
            WA_SizeBBottom, !scrollbar,
            WA_SimpleRefresh, TRUE, WA_Activate, TRUE,
            WA_NewLookMenus, TRUE, WA_MinWidth, 200, WA_MinHeight, 80,
            WA_MaxWidth, -1, WA_MaxHeight, -1, WA_IDCMP,
            idcmp | IDCMP_REFRESHWINDOW, TAG_END);
    }
    if (!win)
        return FALSE;
    rp = win->RPort;
    SetFont(rp, font);
    tt_attach();
    make_menus();
    if (!fullscreen && scrollbar)
        add_scroller();
    layout_and_resize();
    painter_setup();
    redraw_all();
    return TRUE;
}

static void close_display(BOOL keep_screen)
{
    if (win && remember && !fullscreen && scr != ownscr) {
        wl = win->LeftEdge; wt = win->TopEdge;  /* kept for next time */
        ww = win->Width; wh = win->Height;
    }
    if (win) {
        if (menu) ClearMenuStrip(win);
        if (gadsok) RemoveGList(win, &vgad, -1);
        gadsok = FALSE;
        release_pens();
        if (TTEngineBase)
            TT_DoneRastPort(rp);
        CloseWindow(win);
        win = NULL;
    }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (visual) { FreeVisualInfo(visual); visual = NULL; }
    if (iup) { DisposeObject(iup); iup = NULL; }
    if (idn) { DisposeObject(idn); idn = NULL; }
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    if (scr && scr != ownscr)
        UnlockPubScreen(NULL, scr);
    scr = NULL;
    if (!keep_screen)
        close_own_screen();
}

static void switch_display(void)
{
    if (setwin)
        close_settings();               /* its screen may be going */
    BOOL keep = want_mode == SCR_OWN && scrmode == SCR_OWN &&
                want_depth == owndepth;
    want_switch = FALSE;
    close_display(keep);
    scrmode = want_mode;
    owndepth = want_depth;
    fullscreen = want_full;
    scrollbar = want_bar;
    if (!open_display()) {
        close_display(FALSE);
        scrmode = SCR_WB;
        fullscreen = FALSE;
        open_display();
    }
    if (sock >= 0)
        send_naws();
    save_prefs();
}

static void font_changed(void)
{
    SetFont(rp, font);
    tt_attach();
    layout_and_resize();
    painter_forget();
    redraw_all();
    if (sock >= 0)
        send_naws();
}

static void ask_ttfont(void)
{
    struct FileRequester *req;
    char path[160];
    if (!AslBase || !TTEngineBase) {
        say("\r\n[TrueType needs ttengine.library in LIBS:]\r\n");
        return;
    }
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: a TrueType font (fixed width)",
        ASLFR_InitialDrawer, (ULONG)"SYS:Fonts/TrueType",
        ASLFR_InitialPattern, (ULONG)"#?.ttf", ASLFR_DoPatterns, TRUE,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL)) {
        char old[160];
        strncpy(path, (char *)req->fr_Drawer, sizeof path - 1);
        path[sizeof path - 1] = 0;
        AddPart((STRPTR)path, req->fr_File, sizeof path);
        strcpy(old, ttfile);
        strcpy(ttfile, path);
        usett = TRUE;
        if (!load_tt()) {
            say("\r\n[that TrueType font would not open]\r\n");
            strcpy(ttfile, old);
            usett = load_tt();
            if (!usett)
                load_font();
        }
        font_changed();
        save_prefs();
    }
    FreeAslRequest(req);
}

static void tt_size(int d)
{
    if (!usett)
        return;
    if (ttsize + d < 6 || ttsize + d > 72)
        return;
    ttsize += d;
    if (!load_tt()) {
        ttsize -= d;
        load_tt();
    }
    font_changed();
    save_prefs();
}

/* PC BBS mode wears the PC's font (when it is there and wanted), and
 * leaving it brings the person's own back. */
static void pc_font(BOOL on)
{
    if (on == pcfont_on)
        return;
    if (on) {
        if (!pcfont || !TTEngineBase || !file_exists(pcttfile))
            return;
        keepfont.usett = usett;
        strcpy(keepfont.ttfile, ttfile);
        keepfont.ttsize = ttsize;
        usett = TRUE;
        strcpy(ttfile, pcttfile);
        ttsize = 16;
        ttaa = FALSE;
        pcfont_on = TRUE;
        if (!load_tt()) {
            pcfont_on = FALSE;
            on = FALSE;                 /* fall through: put it back */
        }
    }
    if (!on) {
        usett = keepfont.usett;
        strcpy(ttfile, keepfont.ttfile);
        ttsize = keepfont.ttsize;
        ttaa = TRUE;
        pcfont_on = FALSE;
        if (usett && !load_tt())
            usett = FALSE;
        if (!usett) {
            close_tt();
            load_font();
        }
    }
    if (win)
        font_changed();
}

/* The drawing tools themselves, in microseconds each: what a painter
 * should be built from on this machine. */
static LONG us_since(const struct timeval *a)
{
    struct timeval b;
    term_now(&b);
    return (LONG)(b.tv_secs - a->tv_secs) * 1000000 +
           ((LONG)b.tv_micro - (LONG)a->tv_micro);
}

static void bench_line(const char *what, LONG us, LONG n)
{
    char l[100];
    strcpy(l, "  ");
    strcat(l, what);
    while (strlen(l) < 34)
        strcat(l, " ");
    numstr_cat(l, n ? us / n : 0);
    strcat(l, " us each\r\n");
    say(l);
}

static void bench_primitives(void)
{
    struct timeval a;
    int i, x1 = ox + cols * cw - 1, y1 = oy + rows * ch - 1;
    static UBYTE row[256];
    ULONG *argb = NULL;
    LONG w = cols * cw, h = ch;
    for (i = 0; i < (int)sizeof row; i++)
        row[i] = 'A' + i % 26;
    say("\r\n\x1b[1m[the drawing tools, timed]\x1b[0m\r\n");
    refresh();
    planes_full();                      /* every plane: the honest cost */

    bpen(defbg);
    term_now(&a);
    for (i = 0; i < 50; i++)
        ScrollRaster(rp, 0, ch, ox, oy, x1, y1);
    bench_line("ScrollRaster, a line", us_since(&a), 50);

    term_now(&a);
    for (i = 0; i < 50; i++)
        ScrollRaster(rp, 0, ch * (rows / 4), ox, oy, x1, y1);
    bench_line("ScrollRaster, a quarter", us_since(&a), 50);

    apen(defbg);
    term_now(&a);
    for (i = 0; i < 50; i++)
        RectFill(rp, ox, oy, x1, y1);
    bench_line("RectFill, the whole area", us_since(&a), 50);

    term_now(&a);
    for (i = 0; i < 200; i++)
        RectFill(rp, ox, oy, x1, oy + ch - 1);
    bench_line("RectFill, one row", us_since(&a), 200);

    SetFont(rp, font);
    apen(deffg);
    bpen(defbg);
    SetDrMd(rp, JAM2);
    term_now(&a);
    for (i = 0; i < 200; i++) {
        Move(rp, ox, oy + font->tf_Baseline);
        Text(rp, (STRPTR)row, cols);
    }
    bench_line("Text, a row (Amiga font)", us_since(&a), 200);

    if (usett && ttface[0]) {
        static ULONG u[256];
        for (i = 0; i < 256; i++)
            u[i] = 'A' + i % 26;
        SetDrMd(rp, JAM1);
        term_now(&a);
        for (i = 0; i < 200; i++) {
            Move(rp, ox, oy + cbase);
            TT_Text(rp, (APTR)u, cols);
        }
        bench_line("TT_Text, a row (TrueType)", us_since(&a), 200);
        SetDrMd(rp, JAM2);
    }

    if (CyberGfxBase)
        argb = AllocVec(w * h * 4, MEMF_ANY);
    if (argb) {
        for (i = 0; i < w * h; i++)
            argb[i] = (i & 8) ? 0x00D8DEE9 : 0x001A1D23;
        term_now(&a);
        for (i = 0; i < 200; i++)
            WritePixelArray(argb, 0, 0, w * 4, rp, ox, oy, w, h, RECTFMT_ARGB);
        bench_line("WritePixelArray, a row (ARGB)", us_since(&a), 200);
        term_now(&a);
        for (i = 0; i < 200; i++) {
            int k;                      /* the CPU building a row */
            for (k = 0; k < w * h; k++)
                argb[k] = (k & 8) ? 0x00D8DEE9 : 0x001A1D23;
        }
        bench_line("filling a row in memory (CPU)", us_since(&a), 200);
        FreeVec(argb);
    } else
        say("  (no cybergraphics: no WritePixelArray)\r\n");
    redraw_all();
}

/* Full redraws of a busy screen, timed: how fast this font draws here. */
static void speed_test(void)
{
    struct DateStamp a, b;
    LONG ticks, i, r;
    char line[120], num[12];
    static const char sample[] =
        "\x1b[2J\x1b[H";
    say(sample);
    for (r = 0; r < rows - 1; r++) {
        int k;
        for (k = 0; k < cols - 1; k++) {
            char cc[24];
            int col = (k + r) % 8;
            if (k % 10 == 0) {
                strcpy(cc, "\x1b[3");      /* ESC [ 3, then the colour */
                cc[3] = '0' + (col ? col : 7);
                cc[4] = 'm';
                cc[5] = 0;
                say(cc);
            }
            cc[0] = "The quick brown fox jumps over the lazy dog "[(k + r) % 44];
            cc[1] = 0;
            say(cc);
        }
        say("\x1b[0m\r\n");
    }
    refresh();
    DateStamp(&a);
    for (i = 0; i < 10; i++)
        for (r = 0; r < rows; r++)
            draw_row(r);
    DateStamp(&b);
    ticks = (b.ds_Days - a.ds_Days) * 24 * 60 * 60 * 50 +
            (b.ds_Minute - a.ds_Minute) * 60 * 50 + (b.ds_Tick - a.ds_Tick);
    strcpy(line, "\r\n\x1b[1m[speed: 10 full screens of ");
    numstr_cat(line, cols); strcat(line, "x"); numstr_cat(line, rows);
    strcat(line, usett ? " in TrueType: " : " in a bitmap font: ");
    numstr_cat(line, ticks * 20);
    strcat(line, " ms, ");
    numstr_cat(line, ticks * 2);
    strcat(line, " ms each]\x1b[0m\r\n");
    (void)num;
    say(line);
    bench_primitives();
}

static void ask_font(void)
{
    struct FontRequester *req;
    if (!AslBase)
        return;
    req = AllocAslRequestTags(ASL_FontRequest,
        ASLFO_Window, (ULONG)win, ASLFO_SleepWindow, TRUE,
        ASLFO_TitleText, (ULONG)"CTerm's font (fixed width)",
        ASLFO_FixedWidthOnly, TRUE,
        ASLFO_InitialName, (ULONG)font->tf_Message.mn_Node.ln_Name,
        ASLFO_InitialSize, font->tf_YSize, TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL)) {
        strncpy(fontname, (char *)req->fo_Attr.ta_Name, sizeof fontname - 1);
        fontsize = req->fo_Attr.ta_YSize;
        if (load_font()) {
            usett = FALSE;
            bitmap_metrics();
            font_changed();
            save_prefs();
        }
    }
    FreeAslRequest(req);
}

static void ask_switch(int mode, int depth, BOOL full, BOOL bar)
{
    want_mode = mode;
    want_depth = depth;
    want_full = mode == SCR_OWN && full;
    want_bar = bar;
    want_switch = TRUE;
}

static BOOL menu_pick(ULONG id)
{
    switch (id) {
    case M_QUIT: return FALSE;
    case M_FONT: ask_font(); break;
    case M_TTFONT: ask_ttfont(); break;
    case M_BIGGER: tt_size(+2); break;
    case M_SMALLER: tt_size(-2); break;
    case M_SPEED: speed_test(); break;
    case M_CALL: call_dialog(); break;
    case M_SETTINGS: open_settings(); break;
    case M_NEWTAB: tab_new_shell(); break;
    case M_CLOSETAB: tab_close_current(); break;
    case M_COPY: sel_copy(); break;
    case M_PASTE: paste(); break;
    case M_ABOUT:
        say("\r\n\x1b[1mCTerm\x1b[0m - a VT100/xterm terminal for the "
            "Amiga.\r\n");
#ifdef PROFILE
        {   /* ms = ticks / TPMS */
            char l[200];
            ULONG all = ticks() - pf_all0;
            strcpy(l, "[profile ms: all ");
            numstr_cat(l, all / TPMS);
            strcat(l, ", waiting ");
            numstr_cat(l, pf_wait / TPMS);
            strcat(l, ", packets ");
            numstr_cat(l, pf_pkt / TPMS);
            strcat(l, " (emulator ");
            numstr_cat(l, pf_vt / TPMS);
            strcat(l, ", drawing ");
            numstr_cat(l, pf_draw / TPMS);
            strcat(l, " in ");
            numstr_cat(l, pf_draws);
            strcat(l, " draws); writes ");
            numstr_cat(l, pf_writes);
            strcat(l, "]\r\n");
            say(l);
            pf_vt = pf_draw = pf_pkt = pf_wait = pf_writes = pf_draws = 0;
            pf_all0 = ticks();
            refresh();
        }
#endif
        break;
    case M_UTF8:
        utf8 = saved_utf8 = !utf8;
        pcmode = FALSE;
        pc_font(FALSE);
        vt_set_utf8(vt, utf8);
        painter_forget();
        redraw_all();
        break;
    case M_PC:
        pcmode = !pcmode;
        if (pcmode)
            vt_set_cp437(vt, 1);
        else
            vt_set_utf8(vt, utf8);
        pc_font(pcmode);
        painter_forget();
        redraw_all();
        break;
    case M_BAR: ask_switch(scrmode, owndepth, fullscreen, !scrollbar); break;
    case M_FULL: ask_switch(scrmode, owndepth, !fullscreen, scrollbar); break;
    case M_SCR_WB: ask_switch(SCR_WB, owndepth, FALSE, scrollbar); break;
    case M_SCR_16: ask_switch(SCR_OWN, 4, fullscreen, scrollbar); break;
    case M_SCR_256: ask_switch(SCR_OWN, 8, fullscreen, scrollbar); break;
    case M_SCR_DEEP: ask_switch(SCR_OWN, 24, fullscreen, scrollbar); break;
    }
    return TRUE;
}

/* A raw input event report, for an Amiga program that asked (CSI n{). */
static void raw_report(int cls, int code, int qual)
{
    char r[64];
    UBYTE *p;
    if (!local || !(vt_raw_events(vt) & (1UL << cls)))
        return;
    r[0] = (char)0x9B;
    r[1] = 0;
    numstr_cat(r, cls); strcat(r, ";0;");
    numstr_cat(r, code); strcat(r, ";");
    numstr_cat(r, qual); strcat(r, ";0;0;0;0|");
    p = (UBYTE *)r;
    con_reply(T->con, p, strlen(r));
}

/* ---- copy and paste: clipboard.device unit 0, IFF FTXT (the console
 * family's format: Knowledge/amiga/clipboard-and-selection.md) ---------- */

static struct MsgPort *clipport;
static struct IOClipReq *clipreq;

static BOOL clip_open(void)
{
    if (clipreq)
        return TRUE;
    if (!(clipport = CreateMsgPort()))
        return FALSE;
    clipreq = (struct IOClipReq *)CreateIORequest(clipport, sizeof *clipreq);
    if (!clipreq || OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP,
                               (struct IORequest *)clipreq, 0)) {
        if (clipreq) DeleteIORequest((struct IORequest *)clipreq);
        DeleteMsgPort(clipport);
        clipreq = NULL;
        clipport = NULL;
        return FALSE;
    }
    return TRUE;
}

static void clip_close(void)
{
    if (!clipreq)
        return;
    CloseDevice((struct IORequest *)clipreq);
    DeleteIORequest((struct IORequest *)clipreq);
    DeleteMsgPort(clipport);
    clipreq = NULL;
    clipport = NULL;
}

static void put_be(UBYTE *p, ULONG v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static ULONG get_be(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

/* Redraw the rows between two (the selection grew or went). */
static void sel_redraw(int ra, int rb)
{
    int r;
    if (ra > rb) { r = ra; ra = rb; rb = r; }
    if (ra < 0) ra = 0;
    if (rb >= rows) rb = rows - 1;
    rp_forget();
    draw_cursor(FALSE);
    for (r = ra; r <= rb; r++)
        draw_row(r);
    draw_cursor(TRUE);
}

static void sel_clear(void)
{
    int r0, c0, r1, c1;
    if (!sel_on)
        return;
    sel_bounds(&r0, &c0, &r1, &c1);
    sel_on = sel_drag = FALSE;
    sel_redraw(r0, r1);
}

/* The marked text onto the clipboard: Latin-1, lines ended by LF, the
 * spaces at a line's end left off. */
static void sel_copy(void)
{
    int r0, c0, r1, c1, r, x;
    ULONG n = 0;
    UBYTE *buf;
    if (!sel_on || !clip_open())
        return;
    sel_bounds(&r0, &c0, &r1, &c1);
    buf = AllocVec(20 + (r1 - r0 + 1) * (cols + 1) + 2, MEMF_ANY);
    if (!buf)
        return;
    for (r = r0; r <= r1; r++) {
        int w, a = r == r0 ? c0 : 0, b = r == r1 ? c1 : cols - 1;
        const VtCell *line = vt_line(vt, r, back, &w);
        ULONG start = n;
        for (x = a; x <= b; x++) {
            ULONG cp = line && x < w ? line[x].cp : 0;
            if (cp == VT_WIDE_TAIL)
                continue;
            buf[20 + n++] = cp == 0 ? ' ' : cp < 256 ? (UBYTE)cp
                          : (cp >= 0x2500 && cp <= 0x257F) ? '+' : '?';
        }
        while (n > start && buf[20 + n - 1] == ' ')
            n--;
        if (r < r1)
            buf[20 + n++] = '\n';
    }
    memcpy(buf, "FORM", 4);
    put_be(buf + 4, 12 + n + (n & 1));
    memcpy(buf + 8, "FTXTCHRS", 8);
    put_be(buf + 16, n);
    if (n & 1)
        buf[20 + n] = 0;
    clipreq->io_Command = CMD_WRITE;
    clipreq->io_Data = (STRPTR)buf;
    clipreq->io_Length = 20 + n + (n & 1);
    clipreq->io_Offset = 0;
    clipreq->io_ClipID = 0;
    DoIO((struct IORequest *)clipreq);
    if (!clipreq->io_Error) {
        clipreq->io_Command = CMD_UPDATE;   /* publishes it */
        DoIO((struct IORequest *)clipreq);
    }
    FreeVec(buf);
}

/* The clipboard's text, typed in: to the Shell's line, or to the host
 * (bracketed when the program asked); a new line is a Return. */
static void paste(void)
{
    static UBYTE buf[16384], scratch[256];
    ULONG got, o;
    if (!clip_open())
        return;
    clipreq->io_Command = CMD_READ;
    clipreq->io_Data = (STRPTR)buf;
    clipreq->io_Length = sizeof buf;
    clipreq->io_Offset = 0;
    clipreq->io_ClipID = 0;
    DoIO((struct IORequest *)clipreq);
    got = clipreq->io_Error ? 0 : clipreq->io_Actual;
    do {                                /* read it dry, or it stays held */
        clipreq->io_Command = CMD_READ;
        clipreq->io_Data = (STRPTR)scratch;
        clipreq->io_Length = sizeof scratch;
        DoIO((struct IORequest *)clipreq);
    } while (!clipreq->io_Error && clipreq->io_Actual > 0);
    if (got < 20 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "FTXT", 4))
        return;
    for (o = 12; o + 8 <= got; ) {
        ULONG id = get_be(buf + o), sz = get_be(buf + o + 4), i;
        if (id == 0x43485253) {         /* CHRS */
            UBYTE *t = buf + o + 8;
            if (sz > got - o - 8)
                sz = got - o - 8;
            for (i = 0; i < sz; i++)
                if (t[i] == '\n')
                    t[i] = '\r';
            if (back)
                scroll_view(0);
            if (local)
                con_keys(T->con, t, sz);
            else {
                if (vt_bracketed_paste(vt))
                    send_str("\x1b[200~");
                send_typed(t, sz);
                if (vt_bracketed_paste(vt))
                    send_str("\x1b[201~");
            }
            return;
        }
        o += 8 + sz + (sz & 1);
    }
}

/* Mouse position to a cell, held inside the grid. */
static void mouse_cell(int mx, int my, int *r, int *c)
{
    int x = (mx - ox) / cw, y = (my - oy) / ch;
    if (mx < ox) x = 0;
    if (my < oy) y = 0;
    if (x >= cols) x = cols - 1;
    if (y >= rows) y = rows - 1;
    *r = y;
    *c = x;
}

/* ---- Project/Call...: a host to call, in a new CTerm window ----------- */

#define NCALLS 8

/* The recent calls as a list for the list view. */
static struct List calllist;
static struct Node callnode[NCALLS];
static char callname[NCALLS][64];

static int calls_split(void)
{
    const char *p = calls;
    int n = 0;
    calllist.lh_Head = (struct Node *)&calllist.lh_Tail;  /* NewList */
    calllist.lh_Tail = NULL;
    calllist.lh_TailPred = (struct Node *)&calllist.lh_Head;
    while (*p && n < NCALLS) {
        int k = 0;
        while (*p && *p != ';' && k < 63)
            callname[n][k++] = *p++;
        callname[n][k] = 0;
        while (*p && *p != ';')
            p++;
        if (*p == ';')
            p++;
        if (k) {
            callnode[n].ln_Name = callname[n];
            AddTail(&calllist, &callnode[n]);
            n++;
        }
    }
    return n;
}

/* host (with a ! when PC) to the front of the recent calls. */
static void calls_add(const char *entry)
{
    char out[sizeof calls];
    int i, n = calls_split();
    strcpy(out, entry);
    for (i = 0; i < n; i++) {
        if (!stricmp(callname[i], entry))
            continue;
        if (strlen(out) + strlen(callname[i]) + 2 >= sizeof out)
            break;
        strcat(out, ";");
        strcat(out, callname[i]);
    }
    strcpy(calls, out);
}

/* A program beside ours (NULL: ours), to start another CTerm with. */
static void own_path(char *buf, int max, const char *sibling)
{
    char name[108];
    BPTR dir = GetProgramDir();
    buf[0] = 0;
    if (!GetProgramName((STRPTR)name, sizeof name) || !name[0])
        strcpy(name, "CTerm");
    if (sibling)
        strcpy(name, sibling);
    if (dir && NameFromLock(dir, (STRPTR)buf, max)) {
        if (!AddPart((STRPTR)buf, FilePart((STRPTR)name), max))
            strcpy(buf, name);
    } else
        strncpy(buf, name, max - 1);
}

static void call_start(const char *host, BOOL pc, BOOL ssh)
{
    char cmd[400], path[256];
    BPTR nil;
#ifdef NO_SSH
    own_path(path, sizeof path, ssh ? "CTermSSH" : NULL);
#else
    own_path(path, sizeof path, NULL);
#endif
    strcpy(cmd, "Run >NIL: \"");
    strcat(cmd, path);
    strcat(cmd, ssh ? "\" SSH \"" : "\" HOST \"");
    strcat(cmd, host);
    strcat(cmd, pc ? "\" PC" : "\"");
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                   TAG_END) != 0 && nil)
        Close(nil);
}

/* A recent call's text: "ssh:user@host", or "host" with "!" for a PC BBS. */
static void call_parse(const char *entry, char *host, BOOL *pc, BOOL *ssh)
{
    *ssh = !strncmp(entry, "ssh:", 4);
    strcpy(host, *ssh ? entry + 4 : entry);
    *pc = FALSE;
    if (host[0] && host[strlen(host) - 1] == '!') {
        host[strlen(host) - 1] = 0;
        *pc = TRUE;
    }
}

/* A call in a new tab of this window. */
static void call_tab(const char *host, BOOL pc, BOOL ssh)
{
    struct Tab *t = tab_new();
    if (!t) {
        call_start(host, pc, ssh);      /* no room: a window of its own */
        return;
    }
    T = t;
    local = FALSE;
    strncpy(dialhost, host, sizeof dialhost - 1);
    strcpy(dialtitle, ssh ? "CTerm: ssh " : "CTerm: ");
    strncat(dialtitle, dialhost, sizeof dialtitle - 20);
    sshmode = ssh;
    pcmode = pc;
    if (pc)
        vt_set_cp437(vt, 1);
    tab_show(ntabs - 1);
    say("\x1b[2m[calling ");
    say(dialhost);
    say("...]\x1b[0m\r\n");
    refresh();
    if (!connect_out())
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    refresh();
}

static void call_dialog(void)
{
    enum { G_HOST, G_PC, G_SSH, G_NEWTAB, G_LIST, G_CALL, G_CANCEL };
    struct Gadget *glist = NULL, *g, *gad[7];
    struct NewGadget ng;
    struct Window *dw;
    struct TextAttr *ta = scr->Font;
    int th = ta->ta_YSize, n, W, H, x0, y, bw;
    BOOL pc = pcmode, ssh = FALSE, done = FALSE, go = FALSE;
    static BOOL newtab = TRUE;
    char host[128];
    ULONG dsig;

    n = calls_split();
    host[0] = 0;
    if (n)                              /* the last place, ready to call */
        call_parse(callname[0], host, &pc, &ssh);
    x0 = scr->WBorLeft + 8 + TextLength(&scr->RastPort, (STRPTR)"Recent", 6) + 8;
    W = x0 + 300 + scr->WBorRight + 8;
    bw = 90;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = ta;
    ng.ng_VisualInfo = visual;
    g = CreateContext(&glist);
    y = scr->WBorTop + th + 1 + 6;

    ng.ng_LeftEdge = x0; ng.ng_TopEdge = y; ng.ng_Width = 260;
    ng.ng_Height = th + 6; ng.ng_GadgetText = (STRPTR)"Host";
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = G_HOST;
    g = gad[G_HOST] = CreateGadget(STRING_KIND, g, &ng, GTST_String,
                                   (ULONG)host, GTST_MaxChars, 120, TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"PC BBS (CP437)"; ng.ng_Flags = PLACETEXT_RIGHT;
    ng.ng_GadgetID = G_PC;
    g = gad[G_PC] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, pc,
                                 GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0 + 150; ng.ng_GadgetText = (STRPTR)"ssh (user@host)";
    ng.ng_GadgetID = G_SSH;
    g = gad[G_SSH] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, ssh,
                                  GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0;
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"in a new tab of this window";
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_GadgetID = G_NEWTAB;
    g = gad[G_NEWTAB] = CreateGadget(CHECKBOX_KIND, g, &ng,
                                     GTCB_Checked, newtab, GTCB_Scaled, TRUE,
                                     TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 260; ng.ng_Height = (th + 2) * 5 + 4;
    ng.ng_GadgetText = (STRPTR)"Recent"; ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = G_LIST;
    g = gad[G_LIST] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels,
                                   (ULONG)&calllist, TAG_END);
    y += ng.ng_Height + 8;
    ng.ng_TopEdge = y; ng.ng_Width = bw; ng.ng_Height = th + 6;
    ng.ng_GadgetText = (STRPTR)"Call"; ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = G_CALL; ng.ng_LeftEdge = scr->WBorLeft + 8;
    g = gad[G_CALL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    ng.ng_LeftEdge = W - scr->WBorRight - 8 - bw;
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = G_CANCEL;
    g = gad[G_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    H = y + th + 6 + 6 + scr->WBorBottom;
    if (!g) {
        FreeGadgets(glist);
        return;
    }
    dw = OpenWindowTags(NULL,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
        WA_Width, W, WA_Height, H, WA_Title, (ULONG)"CTerm: Call",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_IDCMP, STRINGIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP | BUTTONIDCMP |
                  IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_END);
    if (!dw) {
        FreeGadgets(glist);
        return;
    }
    GT_RefreshWindow(dw, NULL);
    ActivateGadget(gad[G_HOST], dw, NULL);
    dsig = 1UL << dw->UserPort->mp_SigBit;
    while (!done) {
        struct IntuiMessage *m;
        ULONG sigs = Wait(dsig | shells_sigmask());
        if (sigs & ~dsig) {
            /* the Shells go on meanwhile */
            shells_handle(sigs);
            if (con_render_due() && owed)
                draw_now();
        }
        while ((m = GT_GetIMsg(dw->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *src = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            switch (cl) {
            case IDCMP_CLOSEWINDOW: done = TRUE; break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(dw);
                GT_EndRefresh(dw, TRUE);
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27) done = TRUE;
                break;
            case IDCMP_GADGETUP:
                switch (src->GadgetID) {
                case G_HOST:            /* Return in the host field */
                    if (code != 9) go = done = TRUE;
                    break;
                case G_PC: pc = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_SSH: ssh = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_NEWTAB:
                    newtab = (src->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_LIST:
                    if (code < n) {
                        call_parse(callname[code], host, &pc, &ssh);
                        GT_SetGadgetAttrs(gad[G_HOST], dw, NULL,
                                          GTST_String, (ULONG)host, TAG_END);
                        GT_SetGadgetAttrs(gad[G_PC], dw, NULL,
                                          GTCB_Checked, pc, TAG_END);
                        GT_SetGadgetAttrs(gad[G_SSH], dw, NULL,
                                          GTCB_Checked, ssh, TAG_END);
                    }
                    break;
                case G_CALL: go = done = TRUE; break;
                case G_CANCEL: done = TRUE; break;
                }
                break;
            }
        }
    }
    if (go) {
        STRPTR s0 = ((struct StringInfo *)gad[G_HOST]->SpecialInfo)->Buffer;
        strncpy(host, (char *)s0, sizeof host - 2);
        host[sizeof host - 2] = 0;
    }
    CloseWindow(dw);
    FreeGadgets(glist);
    if (go && host[0]) {
        char entry[136];
        strcpy(entry, ssh ? "ssh:" : "");
        strcat(entry, host);
        if (pc)
            strcat(entry, "!");
        calls_add(entry);
        save_prefs();
#ifdef NO_SSH
        if (newtab && !ssh)             /* ssh is CTermSSH's: a window */
#else
        if (newtab)
#endif
            call_tab(host, pc, ssh);
        else
            call_start(host, pc, ssh);
    }
}

/* ======================================================================
 * Settings: one window for the screen, the letters and the colours
 *
 * Letters and colours change at once, as a preview; where CTerm lives
 * (screen, mode, full screen, scroll bar) changes on Use or Save, which
 * open the window, and maybe the screen, again. Cancel puts everything
 * back. Save keeps it after a reboot, Use until then (ENV: only).
 * ==================================================================== */

enum { S_WHERE = 1, S_MODE, S_MODETXT, S_FULL, S_BAR, S_LETTERS, S_FONT,
       S_FONTTXT, S_TT, S_TTTXT, S_TTSIZE, S_SPACE, S_CHARS, S_PCFONT,
       S_THEME, S_PART,
       S_R, S_G, S_B, S_RESET, S_SAVE, S_USE, S_CANCEL, S_N };

static struct Gadget *setglist, *sg[S_N];
static char modetxt[80], fonttxt[64], tttxt[64];
static int setpart;                     /* 0 the text, 1 the background */
static int pend_mode, pend_depth;
static ULONG pend_id;
static LONG pend_w, pend_h;
static BOOL pend_full, pend_bar;

static struct {                         /* what Cancel goes back to */
    int theme, ttsize, fontsize, linespace;
    BOOL ownfg, ownbg, usett, s_utf8, pc, pcfont;
    ULONG myfg, mybg;
    char ttfile[160], fontname[48];
} snap;

static STRPTR wherelabels[] = { (STRPTR)"Workbench window",
                                (STRPTR)"Own screen", NULL };
static STRPTR letterlabels[] = { (STRPTR)"Amiga font", (STRPTR)"TrueType",
                                 NULL };
static STRPTR charlabels[] = { (STRPTR)"UTF-8", (STRPTR)"Latin-1",
                               (STRPTR)"PC BBS (CP437)", NULL };
static STRPTR partlabels[] = { (STRPTR)"Text", (STRPTR)"Background", NULL };
static STRPTR themelabels[NTHEMES + 1];

static void set_text(int id, char *t)
{
    if (setwin && sg[id])
        GT_SetGadgetAttrs(sg[id], setwin, NULL, GTTX_Text, (ULONG)t, TAG_END);
}

static void describe_mode(void)
{
    if (pend_mode == SCR_WB) {
        strcpy(modetxt, "the Workbench");
        return;
    }
    modetxt[0] = 0;
    if (pend_id != INVALID_ID) {
        struct NameInfo ni;
        if (GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof ni, DTAG_NAME,
                               pend_id) > 0) {
            strncpy(modetxt, (char *)ni.Name, 40);
            modetxt[40] = 0;
        }
        if (pend_w > 0) {
            strcat(modetxt, ", ");
            numstr_cat(modetxt, pend_w);
            strcat(modetxt, "x");
            numstr_cat(modetxt, pend_h);
        }
    } else
        strcpy(modetxt, "WB mode");
    strcat(modetxt, ", ");
    if (pend_depth > 8)
        strcat(modetxt, "millions of");
    else
        numstr_cat(modetxt, 1L << pend_depth);
    strcat(modetxt, " colours");
}

static void describe_fonts(void)
{
    const char *n = font ? font->tf_Message.mn_Node.ln_Name : "?";
    strncpy(fonttxt, n ? n : "?", 40);
    fonttxt[40] = 0;
    if (strlen(fonttxt) > 5 &&
        !stricmp(fonttxt + strlen(fonttxt) - 5, ".font"))
        fonttxt[strlen(fonttxt) - 5] = 0;
    strcat(fonttxt, " ");
    numstr_cat(fonttxt, font ? font->tf_YSize : 0);
    strncpy(tttxt, (char *)FilePart((STRPTR)ttfile), 40);
    tttxt[40] = 0;
    if (strlen(tttxt) > 4 && !stricmp(tttxt + strlen(tttxt) - 4, ".ttf"))
        tttxt[strlen(tttxt) - 4] = 0;
    if (!TTEngineBase)
        strcpy(tttxt, "(no ttengine.library)");
}

static void set_sliders(void)
{
    ULONG c = setpart ? defbg : deffg;
    if (!setwin)
        return;
    GT_SetGadgetAttrs(sg[S_R], setwin, NULL, GTSL_Level, (c >> 16) & 255,
                      TAG_END);
    GT_SetGadgetAttrs(sg[S_G], setwin, NULL, GTSL_Level, (c >> 8) & 255,
                      TAG_END);
    GT_SetGadgetAttrs(sg[S_B], setwin, NULL, GTSL_Level, c & 255, TAG_END);
}

static void colours_changed(void)
{
    apply_theme();
    painter_forget();
    redraw_all();
}

static int chars_now(void)
{
    return pcmode ? 2 : utf8 ? 0 : 1;
}

static void set_chars(int k)
{
    pcmode = k == 2;
    utf8 = k == 0;
    pc_font(pcmode);
    if (pcmode)
        vt_set_cp437(vt, 1);
    else
        vt_set_utf8(vt, utf8);
    painter_forget();
    redraw_all();
}

/* The letters again after a font, size or spacing change. */
static void letters_changed(void)
{
    if (usett && !load_tt())
        usett = FALSE;
    if (!usett)
        bitmap_metrics();
    font_changed();
    describe_fonts();
    set_text(S_FONTTXT, fonttxt);
    set_text(S_TTTXT, tttxt);
    if (setwin) {
        GT_SetGadgetAttrs(sg[S_LETTERS], setwin, NULL, GTCY_Active, usett,
                          TAG_END);
        GT_SetGadgetAttrs(sg[S_TTSIZE], setwin, NULL, GA_Disabled, !usett,
                          GTSL_Level, ttsize, TAG_END);
    }
}

static void where_changed(void)
{
    GT_SetGadgetAttrs(sg[S_WHERE], setwin, NULL, GTCY_Active,
                      pend_mode == SCR_OWN, TAG_END);
    GT_SetGadgetAttrs(sg[S_FULL], setwin, NULL, GA_Disabled,
                      pend_mode == SCR_WB, TAG_END);
    GT_SetGadgetAttrs(sg[S_MODE], setwin, NULL, GA_Disabled,
                      pend_mode == SCR_WB, TAG_END);
    describe_mode();
    set_text(S_MODETXT, modetxt);
}

static void ask_mode(void)
{
    struct ScreenModeRequester *req;
    ULONG start = pend_id != INVALID_ID ? pend_id
                : ownscr ? GetVPModeID(&ownscr->ViewPort) : INVALID_ID;
    if (!AslBase)
        return;
    req = AllocAslRequestTags(ASL_ScreenModeRequest,
        ASLSM_Window, (ULONG)setwin, ASLSM_SleepWindow, TRUE,
        ASLSM_TitleText, (ULONG)"CTerm's own screen",
        ASLSM_DoDepth, TRUE, ASLSM_MinDepth, 1, ASLSM_MaxDepth, 24,
        ASLSM_DoWidth, TRUE, ASLSM_DoHeight, TRUE,
        start != INVALID_ID ? ASLSM_InitialDisplayID : TAG_IGNORE, start,
        ASLSM_InitialDisplayDepth, pend_depth > 8 ? 24 : pend_depth,
        TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL)) {
        pend_id = req->sm_DisplayID;
        pend_w = req->sm_DisplayWidth;
        pend_h = req->sm_DisplayHeight;
        pend_depth = req->sm_DisplayDepth;
        pend_mode = SCR_OWN;
        where_changed();
    }
    FreeAslRequest(req);
}

static void close_settings(void)
{
    if (setwin) {
        CloseWindow(setwin);
        setwin = NULL;
    }
    if (setglist) {
        FreeGadgets(setglist);
        setglist = NULL;
    }
    memset(sg, 0, sizeof sg);
}

static void open_settings(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    struct TextAttr *ta = scr->Font;
    struct RastPort *srp = &scr->RastPort;
    int th = ta->ta_YSize, lh = th + 7, i;
    int ox = scr->WBorLeft + 10, oy = scr->WBorTop + th + 1 + 8;
    int lw = TextLength(srp, (STRPTR)"TrueType size", 13) + 20;
    int gx, gw = 340, bw, W, H, y;

    if (setwin) {
        WindowToFront(setwin);
        ActivateWindow(setwin);
        return;
    }
    gx = ox + lw;
    bw = TextLength(srp, (STRPTR)"TrueType font...", 16) + 20;
    for (i = 0; i < NTHEMES; i++)
        themelabels[i] = (STRPTR)themes[i].name;
    themelabels[NTHEMES] = NULL;

    pc_font(FALSE);                     /* the controls show the person's */
    snap.pcfont = pcfont;
    snap.theme = theme;
    snap.ownfg = ownfg; snap.ownbg = ownbg;
    snap.myfg = myfg; snap.mybg = mybg;
    snap.usett = usett; snap.ttsize = ttsize;
    strcpy(snap.ttfile, ttfile);
    strcpy(snap.fontname, fontname);
    snap.fontsize = fontsize;
    snap.linespace = linespace;
    snap.s_utf8 = utf8; snap.pc = pcmode;
    pend_mode = scrmode;
    pend_depth = owndepth;
    pend_id = ownmode;
    pend_w = ownw;
    pend_h = ownh;
    pend_full = fullscreen;
    pend_bar = scrollbar;
    describe_mode();
    describe_fonts();

    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = ta;
    ng.ng_VisualInfo = visual;
    ng.ng_Height = th + 5;
    g = CreateContext(&setglist);
    y = oy;
#define NG(id, x, w, txt, fl) (ng.ng_LeftEdge = (x), ng.ng_TopEdge = y, \
    ng.ng_Width = (w), ng.ng_GadgetText = (STRPTR)(txt), ng.ng_Flags = (fl), \
    ng.ng_GadgetID = (id))

    NG(S_WHERE, gx, gw, "Screen", PLACETEXT_LEFT);
    g = sg[S_WHERE] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels,
        (ULONG)wherelabels, GTCY_Active, pend_mode == SCR_OWN, TAG_END);
    y += lh;
    NG(S_MODE, gx, bw, "Screen mode...", PLACETEXT_IN);
    g = sg[S_MODE] = CreateGadget(BUTTON_KIND, g, &ng,
        GA_Disabled, pend_mode == SCR_WB, TAG_END);
    NG(S_MODETXT, gx + bw + 6, gw - bw - 6, NULL, 0);
    g = sg[S_MODETXT] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
        (ULONG)modetxt, GTTX_Border, TRUE, TAG_END);
    y += lh;
    NG(S_FULL, gx, 26, "Full screen", PLACETEXT_LEFT);
    g = sg[S_FULL] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked,
        pend_full, GTCB_Scaled, TRUE, GA_Disabled, pend_mode == SCR_WB,
        TAG_END);
    NG(S_BAR, gx + gw - 26, 26, "Scroll bar", PLACETEXT_LEFT);
    g = sg[S_BAR] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked,
        pend_bar, GTCB_Scaled, TRUE, TAG_END);
    y += lh + 8;

    NG(S_LETTERS, gx, gw, "Letters", PLACETEXT_LEFT);
    g = sg[S_LETTERS] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels,
        (ULONG)letterlabels, GTCY_Active, usett, TAG_END);
    y += lh;
    NG(S_FONT, gx, bw, "Amiga font...", PLACETEXT_IN);
    g = sg[S_FONT] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    NG(S_FONTTXT, gx + bw + 6, gw - bw - 6, NULL, 0);
    g = sg[S_FONTTXT] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
        (ULONG)fonttxt, GTTX_Border, TRUE, TAG_END);
    y += lh;
    NG(S_TT, gx, bw, "TrueType font...", PLACETEXT_IN);
    g = sg[S_TT] = CreateGadget(BUTTON_KIND, g, &ng,
        GA_Disabled, !TTEngineBase, TAG_END);
    NG(S_TTTXT, gx + bw + 6, gw - bw - 6, NULL, 0);
    g = sg[S_TTTXT] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
        (ULONG)tttxt, GTTX_Border, TRUE, TAG_END);
    y += lh;
    NG(S_TTSIZE, gx, gw - 40, "TrueType size", PLACETEXT_LEFT);
    g = sg[S_TTSIZE] = CreateGadget(SLIDER_KIND, g, &ng, GTSL_Min, 6,
        GTSL_Max, 40, GTSL_Level, ttsize, GTSL_LevelFormat, (ULONG)"%2ld",
        GTSL_MaxLevelLen, 2, GTSL_LevelPlace, PLACETEXT_RIGHT,
        GA_RelVerify, TRUE, GA_Disabled, !usett, TAG_END);
    y += lh;
    NG(S_SPACE, gx, gw - 40, "Line spacing", PLACETEXT_LEFT);
    g = sg[S_SPACE] = CreateGadget(SLIDER_KIND, g, &ng, GTSL_Min, 0,
        GTSL_Max, 8, GTSL_Level, linespace, GTSL_LevelFormat, (ULONG)"%ld",
        GTSL_MaxLevelLen, 2, GTSL_LevelPlace, PLACETEXT_RIGHT,
        GA_RelVerify, TRUE, TAG_END);
    y += lh;
    NG(S_CHARS, gx, gw, "Characters", PLACETEXT_LEFT);
    g = sg[S_CHARS] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels,
        (ULONG)charlabels, GTCY_Active, chars_now(), TAG_END);
    y += lh;
    NG(S_PCFONT, gx, 26, "PC font", PLACETEXT_LEFT);
    g = sg[S_PCFONT] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked,
        pcfont, GTCB_Scaled, TRUE, TAG_END);
    NG(0, gx + 34, gw - 34, NULL, 0);
    g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
        (ULONG)"the IBM VGA font in PC BBS mode", TAG_END);
    y += lh + 8;

    NG(S_THEME, gx, gw, "Theme", PLACETEXT_LEFT);
    g = sg[S_THEME] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels,
        (ULONG)themelabels, GTCY_Active, theme, TAG_END);
    y += lh;
    NG(S_PART, gx, gw, "Colour of", PLACETEXT_LEFT);
    g = sg[S_PART] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels,
        (ULONG)partlabels, GTCY_Active, setpart, TAG_END);
    y += lh;
    {
        static const char *const rgbname[3] = { "Red", "Green", "Blue" };
        ULONG c = setpart ? defbg : deffg;
        int k;
        for (k = 0; k < 3; k++) {
            NG(S_R + k, gx, gw - 40, rgbname[k], PLACETEXT_LEFT);
            g = sg[S_R + k] = CreateGadget(SLIDER_KIND, g, &ng,
                GTSL_Min, 0, GTSL_Max, 255,
                GTSL_Level, (c >> (16 - 8 * k)) & 255,
                GTSL_LevelFormat, (ULONG)"%3ld", GTSL_MaxLevelLen, 3,
                GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE,
                TAG_END);
            y += lh;
        }
    }
    NG(S_RESET, gx, gw, "Back to the theme's colours", PLACETEXT_IN);
    g = sg[S_RESET] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    y += lh + 10;
    W = gx + gw + 10 + scr->WBorRight;
    {
        int b3 = (W - scr->WBorLeft - scr->WBorRight - 20 - 2 * 20) / 3;
        if (b3 > 110) b3 = 110;
        NG(S_SAVE, ox, b3, "Save", PLACETEXT_IN);
        g = sg[S_SAVE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
        NG(S_USE, (W - b3) / 2, b3, "Use", PLACETEXT_IN);
        g = sg[S_USE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
        NG(S_CANCEL, W - scr->WBorRight - 10 - b3, b3, "Cancel",
           PLACETEXT_IN);
        g = sg[S_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    }
#undef NG
    H = y + lh + 4 + scr->WBorBottom;
    if (!g) {
        close_settings();
        return;
    }
    setwin = OpenWindowTags(NULL,
        WA_Left, (scr->Width - W) / 2, WA_Top, (scr->Height - H) / 2,
        WA_Width, W, WA_Height, H, WA_Title, (ULONG)"CTerm Settings",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)setglist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_IDCMP, BUTTONIDCMP | CYCLEIDCMP | CHECKBOXIDCMP | SLIDERIDCMP |
                  IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW, TAG_END);
    if (!setwin) {
        close_settings();
        return;
    }
    GT_RefreshWindow(setwin, NULL);
}

static void finish_settings_(int how);

/* Use (1), Save (2) or Cancel (3). */
static void finish_settings(int how)
{
    finish_settings_(how);
    pc_font(pcmode);                    /* PC BBS mode wears the PC's */
}

static void finish_settings_(int how)
{
    close_settings();
    if (how == 3) {
        BOOL letters = snap.usett != usett || snap.ttsize != ttsize ||
                       strcmp(snap.ttfile, ttfile) ||
                       strcmp(snap.fontname, fontname) ||
                       snap.fontsize != fontsize ||
                       snap.linespace != linespace;
        theme = snap.theme;
        pcfont = snap.pcfont;
        ownfg = snap.ownfg; ownbg = snap.ownbg;
        myfg = snap.myfg; mybg = snap.mybg;
        if (snap.s_utf8 != utf8 || snap.pc != pcmode)
            set_chars(snap.pc ? 2 : snap.s_utf8 ? 0 : 1);
        if (letters) {
            usett = snap.usett;
            ttsize = snap.ttsize;
            strcpy(ttfile, snap.ttfile);
            strcpy(fontname, snap.fontname);
            fontsize = snap.fontsize;
            linespace = snap.linespace;
            load_font();
            letters_changed();
        }
        colours_changed();
        return;
    }
    saved_utf8 = utf8;
    saved_pc = pcmode;
    if (pend_mode != scrmode || pend_full != fullscreen ||
        pend_bar != scrollbar ||
        (pend_mode == SCR_OWN && (pend_id != ownmode ||
         pend_depth != owndepth || pend_w != ownw || pend_h != ownh))) {
        if (pend_mode == SCR_OWN && scrmode == SCR_OWN &&
            (pend_id != ownmode || pend_depth != owndepth ||
             pend_w != ownw || pend_h != ownh)) {
            /* a different own screen: leave this one first */
            want_mode = SCR_WB;
            want_full = FALSE;
            want_depth = owndepth;
            want_bar = scrollbar;
            switch_display();
        }
        ownmode = pend_id;
        ownw = pend_w;
        ownh = pend_h;
        want_mode = pend_mode;
        want_depth = pend_depth;
        want_full = pend_mode == SCR_OWN && pend_full;
        want_bar = pend_bar;
        want_switch = TRUE;             /* the main loop does it */
    }
    used_only = how == 1;               /* Use: until a reboot, even on quit */
    envonly = used_only;
    save_prefs();
    envonly = FALSE;
}

static void settings_events(void)
{
    struct IntuiMessage *m;
    int how = 0;
    while (setwin && (m = GT_GetIMsg(setwin->UserPort))) {
        ULONG cl = m->Class;
        UWORD code = m->Code;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        if (cl == IDCMP_REFRESHWINDOW) {
            GT_BeginRefresh(setwin);
            GT_EndRefresh(setwin, TRUE);
            continue;
        }
        if (cl == IDCMP_CLOSEWINDOW) {
            how = 3;
            break;
        }
        if (cl != IDCMP_GADGETUP || !g)
            continue;
        switch (g->GadgetID) {
        case S_WHERE:
            pend_mode = code ? SCR_OWN : SCR_WB;
            where_changed();
            break;
        case S_MODE: ask_mode(); break;
        case S_FULL: pend_full = (g->Flags & GFLG_SELECTED) != 0; break;
        case S_BAR: pend_bar = (g->Flags & GFLG_SELECTED) != 0; break;
        case S_LETTERS:
            usett = code && TTEngineBase;
            if (!usett)
                load_font();
            letters_changed();
            break;
        case S_FONT:
            ask_font();
            letters_changed();
            break;
        case S_TT:
            ask_ttfont();
            letters_changed();
            break;
        case S_TTSIZE:
            if (usett && code != ttsize) {
                ttsize = code;
                letters_changed();
            }
            break;
        case S_SPACE:
            if (code != linespace) {
                linespace = code;
                letters_changed();
            }
            break;
        case S_CHARS:
            set_chars(code);
            pc_font(FALSE);             /* shown on closing */
            break;
        case S_PCFONT: pcfont = (g->Flags & GFLG_SELECTED) != 0; break;
        case S_THEME:
            theme = code;
            ownfg = ownbg = FALSE;
            colours_changed();
            set_sliders();
            break;
        case S_PART: setpart = code; set_sliders(); break;
        case S_R: case S_G: case S_B: {
            int sh = g->GadgetID == S_R ? 16 : g->GadgetID == S_G ? 8 : 0;
            ULONG c = setpart ? defbg : deffg;
            c = (c & ~(255UL << sh)) | ((ULONG)(code & 255) << sh);
            if (setpart) { mybg = c; ownbg = TRUE; }
            else { myfg = c; ownfg = TRUE; }
            colours_changed();
            break;
        }
        case S_RESET:
            ownfg = ownbg = FALSE;
            colours_changed();
            set_sliders();
            break;
        case S_SAVE: how = 2; break;
        case S_USE: how = 1; break;
        case S_CANCEL: how = 3; break;
        }
        if (how)
            break;
    }
    if (how)
        finish_settings(how);
}

static BOOL window_events(void)
{
    struct IntuiMessage *m;
    BOOL go = TRUE;
    while (!want_switch && (m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        struct IntuiMessage copy = *m;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        if (copy.Class == IDCMP_RAWKEY) {
            key(&copy);                 /* IAddress valid only until reply */
            ReplyMsg((struct Message *)m);
            continue;
        }
        ReplyMsg((struct Message *)m);
        switch (copy.Class) {
        case IDCMP_CLOSEWINDOW:
            if (local) {
                if (vt_raw_events(vt) & (1UL << 11))
                    raw_report(11, 0, 0);
                else
                    con_close_gadget(T->con);
            } else if (ntabs > 1)
                T->ended = TRUE;        /* this host's tab goes */
            else
                go = FALSE;
            break;
        case IDCMP_NEWSIZE:
            layout_and_resize();
            painter_setup();
            redraw_all();
            RefreshWindowFrame(win);
            {
                struct Tab *keep = T;
                int i;
                for (i = 0; i < ntabs; i++) {
                    T = tabs[i];
                    if (sock >= 0)
                        send_naws();
                    if (T->con)
                        raw_report(12, 0, 0);
                }
                T = keep;
            }
            break;
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(win);
            EndRefresh(win, TRUE);
            redraw_all();
            break;
        case IDCMP_MENUPICK: {
            UWORD code = copy.Code;
            if (win->MenuStrip != menu) {
                /* Ed's own menus on our window: its business */
                raw_report(10, code, copy.Qualifier);
                break;
            }
            while (code != MENUNULL && go && !want_switch) {
                struct MenuItem *it = ItemAddress(menu, code);
                if (!it) break;
                go = menu_pick((ULONG)GTMENUITEM_USERDATA(it));
                code = it->NextSelect;
            }
            break;
        }
        case IDCMP_MOUSEBUTTONS:
            if (copy.Code == SELECTDOWN && tabbar_h() &&
                copy.MouseY >= win->BorderTop &&
                copy.MouseY < win->BorderTop + tabbar_h()) {
                int wd = win->Width - win->BorderLeft - win->BorderRight, i;
                i = (copy.MouseX - win->BorderLeft) * ntabs / wd;
                if (i >= 0 && i < ntabs)
                    tab_show(i);
                break;
            }
            if (copy.Code == SELECTDOWN && copy.MouseX >= ox &&
                copy.MouseY >= oy && copy.MouseY < oy + rows * ch &&
                copy.MouseX < ox + cols * cw) {
                int r, c;
                sel_clear();
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                sa_r = sb_r = r;
                sa_c = sb_c = c;
                sel_drag = TRUE;        /* a selection only once it moves */
                ReportMouse(TRUE, win);
            } else if (copy.Code == SELECTUP && sel_drag) {
                sel_drag = FALSE;
                ReportMouse(FALSE, win);
            }
            break;
        case IDCMP_GADGETDOWN:
            if (g == &agup || g == &agdn) {
                arrowheld = g == &agup ? 1 : -1;
                arrowticks = 0;
                scroll_view(back + arrowheld);
            }
            break;
        case IDCMP_GADGETUP:
        case IDCMP_MOUSEMOVE:
            if (copy.Class == IDCMP_MOUSEMOVE && sel_drag) {
                int r, c, r0, c0, r1, c1, n0, n1, m0, m1;
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                if (sel_on && r == sb_r && c == sb_c)
                    break;
                if (!sel_on && r == sa_r && c == sa_c)
                    break;              /* a click that has not moved */
                if (sel_on) {
                    sel_bounds(&r0, &c0, &r1, &c1);
                } else
                    r0 = r1 = sa_r;
                sb_r = r;
                sb_c = c;
                sel_on = TRUE;
                sel_bounds(&n0, &m0, &n1, &m1);
                (void)c0; (void)c1; (void)m0; (void)m1;
                sel_redraw(r0 < n0 ? r0 : n0, r1 > n1 ? r1 : n1);
                break;
            }
            if (g == &vgad || (vpi.Flags & KNOBHIT)) {
                LONG max = vt_history(vt);
                LONG top = ((ULONG)vpi.VertPot * max + 0x7FFF) / 0xFFFF;
                scroll_view(max - top);
            }
            if (copy.Class == IDCMP_GADGETUP)
                arrowheld = 0;
            break;
        case IDCMP_INTUITICKS:
            if (vpi.Flags & KNOBHIT) {
                LONG max = vt_history(vt);
                LONG top = ((ULONG)vpi.VertPot * max + 0x7FFF) / 0xFFFF;
                scroll_view(max - top);
            }
            if (arrowheld) {
                struct Gadget *a = arrowheld > 0 ? &agup : &agdn;
                if (!(a->Flags & GFLG_SELECTED)) arrowheld = 0;
                else if (++arrowticks > 2) scroll_view(back + arrowheld);
            }
            break;
        }
    }
    return go;
}

/* ======================================================================
 * The main loop
 * ==================================================================== */

static BOOL title_force;

static void title_update_now(void)
{
    title_force = TRUE;
    title_update();
}

static void title_update(void)
{
    int changed;
    const char *t = vt_title(vt, &changed);
    static char title[160];
    if (title_force)
        changed = TRUE, title_force = FALSE;
    if (!changed || !win)
        return;
    strcpy(title, "CTerm");
    if (dialhost[0]) {
        strcat(title, sshmode ? ": ssh " : ": ");
        strcat(title, dialhost);
    }
    if (t[0]) {
        UBYTE *d;
        int i = 8;
        strcat(title, " - ");
        d = (UBYTE *)title + strlen(title);
        /* the title is UTF-8: keep what Latin-1 can show */
        while (*t && i++ < 140 && d < (UBYTE *)title + sizeof title - 2) {
            UBYTE c = (UBYTE)*t++;
            if (c < 0x80) *d++ = c;
            else if ((c & 0xE0) == 0xC0 && *t) {
                ULONG cp = ((c & 0x1F) << 6) | (*t++ & 0x3F);
                *d++ = cp < 256 ? (UBYTE)cp : '?';
            } else if (c >= 0xC0) {
                while ((*t & 0xC0) == 0x80) t++;
            }
        }
        *d = 0;
    }
    SetWindowTitles(win, fullscreen ? (UBYTE *)~0 : (UBYTE *)title,
                    (UBYTE *)title);
}

static char fromfile[256];              /* FROM: the Shell's first script */

/* ---- tabs ---------------------------------------------------------- */

/* Every tab's Shell: the signals to wait for, the packets to answer. */
static ULONG shells_sigmask(void)
{
    ULONG m = 0;
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con)
            m |= con_sigmask(tabs[i]->con);
    return m;
}

static void shells_handle(ULONG sigs)
{
    struct Tab *keep = T;
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con) {
            T = tabs[i];
            con_handle(T->con, sigs);
        }
    T = keep;
}

static int tabbar_h(void);
static void tab_label(struct Tab *t, char *out);

/* A tab of its own: an emulator the window's size, no Shell or host yet. */
static struct Tab *tab_new(void)
{
    struct Tab *t;
    ULONG lines;
    if (ntabs >= MAXTABS)
        return NULL;
    t = AllocVec(sizeof *t, MEMF_ANY | MEMF_CLEAR);
    if (!t)
        return NULL;
    lines = AvailMem(MEMF_ANY) / 4 / (80 * sizeof(VtCell));
    t->t_vt = vt_new(cols > 0 ? cols : 80, rows > 0 ? rows : 25,
                   lines < SCROLLBACK ? (int)lines : SCROLLBACK);
    if (!t->t_vt) {
        FreeVec(t);
        return NULL;
    }
    t->t_sock = t->t_lfd = -1;
    t->t_local = TRUE;
    t->t_utf8 = saved_utf8;
    vt_set_utf8(t->t_vt, t->t_utf8);
    tabs[ntabs++] = t;
    return t;
}

/* Show tab i: its text in the window, its font (a PC BBS wears the PC's),
 * its title. */
static void tab_show(int i)
{
    BOOL bar = tabbar_h() > 0;
    if (i < 0 || i >= ntabs)
        return;
    shown = T = tabs[i];
    pc_font(pcmode);
    if (bar != (tabbar_h() > 0) || TRUE) {
        layout_and_resize();
        painter_forget();
    }
    redraw_all();
    title_update_now();
}

/* Written backwards on purpose: the forward search, inlined into
 * tab_drop, came out of bebbo's gcc -O2 stepping the pointer twice after
 * tabs[0] - tabs[1] was never found and closing tab 2 spun for ever
 * (seen in the disassembly, 2026-10-10). */
static int tab_index(struct Tab *t)
{
    int i = ntabs;
    while (--i >= 0)
        if (tabs[i] == t)
            break;
    return i;
}

/* A tab goes: its host hung up on, its emulator freed (a local tab only
 * once its Shell has ended). */
static void tab_drop(struct Tab *t)
{
    struct Tab *keep = T;
    int i = tab_index(t);
    if (i < 0)
        return;
    T = t;
    if (sock >= 0) {
        if (sshmode) {
            ssh_close(T->ssh);
            T->ssh = NULL;
        } else
            CloseSocket(sock);
        sock = -1;
    }
    if (lfd >= 0) {
        CloseSocket(lfd);
        lfd = -1;
    }
    if (T->con) {
        /* kept until CTerm ends: nothing about a Shell's console (its
         * port, its signal, its timer) is torn down while processes may
         * still be finishing - freed all at once at the end */
        if (nspent < MAXSPENT)
            spent[nspent++] = T->con;
        T->con = NULL;
    }
    vt_free(T->t_vt);
    memmove(tabs + i, tabs + i + 1, (ntabs - i - 1) * sizeof tabs[0]);
    ntabs--;
    if (t != &tab0)
        FreeVec(t);
    T = keep == t ? NULL : keep;
    if (shown == t) {
        shown = NULL;
        if (ntabs > 0)
            tab_show(i < ntabs ? i : ntabs - 1);
    } else if (ntabs > 0) {
        T = shown;
        layout_and_resize();            /* the tab bar may have gone */
        redraw_all();
    }
    if (!T && ntabs > 0)
        T = shown;
}

/* A new tab with the Amiga Shell in it, shown. */
static void tab_new_shell(void)
{
    struct Tab *t = tab_new();
    if (!t) {
        DisplayBeep(scr);
        return;
    }
    T = t;
    vt_set_amiga(vt, 1);
    T->con = con_start("S:Shell-Startup", T);
    if (!T->con)
        say("CTerm could not start a Shell.\r\n");
    tab_show(ntabs - 1);
}

/* Close the shown tab: its Shell is told the input ended (the tab goes
 * when the Shell has), a host's is hung up on. */
static void tab_close_current(void)
{
    if (T->con)
        con_stop(T->con);
    T->ended = TRUE;
}

/* Everyone out: the Shells are told the input ended, hosts hung up on. */
static void quit_all(void)
{
    int i;
    quitting = TRUE;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con)
            con_stop(tabs[i]->con);
        else
            tabs[i]->ended = TRUE;
}

/* A host's bytes for tab t (T is t): take what has come, and what keeps
 * coming without a wait, before drawing - a burst draws once. */
static void host_input(UBYTE *buf, LONG size)
{
    if (lfd >= 0 && sock < 0) {
        connected();
        return;
    }
    {
        int rounds = 0;
        for (;;) {
            struct timeval tv;
            LONG k;
            if (sshmode) {
                /* drain it: libssh2 may hold decrypted data the socket
                 * no longer shows */
                while ((k = ssh_read(T->ssh, buf, size)) > 0)
                    vt_write(vt, buf, k);
                if (k < 0)
                    hung_up();
                break;
            }
            k = recv(sock, buf, size, 0);
            tv.tv_sec = tv.tv_usec = 0;
            if (k <= 0) {
                hung_up();
                break;
            }
            from_host(buf, k);
            if (++rounds > 16)
                break;
            {
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(sock, &rd);
                if (WaitSelect(sock + 1, &rd, NULL, NULL, &tv, NULL) <= 0)
                    break;
            }
        }
    }
    {
        char r[256];
        int k = vt_take_reply(vt, r, sizeof r);
        if (k > 0)
            send_data((UBYTE *)r, k);
    }
}

static void run(void)
{
    static UBYTE buf[16384];
#ifdef PROFILE
    {   /* what one clock read costs, to take it out of the buckets */
        int k;
        ULONG t0 = ticks();
        for (k = 0; k < 1000; k++)
            (void)ticks();
        pf_calib = ticks() - t0;
    }
    pf_all0 = ticks();
    term_now(&pf_tv0);
#endif
    T = shown = tabs[0];
    if (local) {
        const char *script = fromfile[0] ? fromfile : "S:Shell-Startup";
        BPTR l = Lock((STRPTR)script, ACCESS_READ);
        if (l)
            UnLock(l);
        T->con = con_start(l ? script : NULL, T);
        if (!T->con) {
            /* no Shell: say so and wait for the window to be closed -
             * never fall into the network loop, there is no network */
            say("CTerm could not start a Shell.\r\n");
            refresh();
            for (;;) {
                struct IntuiMessage *m;
                BOOL done = FALSE;
                WaitPort(win->UserPort);
                while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                    if (m->Class == IDCMP_CLOSEWINDOW)
                        done = TRUE;
                    ReplyMsg((struct Message *)m);
                }
                if (done)
                    return;
            }
        }
    } else if (dialhost[0]) {
        say("\x1b[2m[calling ");
        say(dialhost);
        say("...]\x1b[0m\r\n");
        refresh();
        if (!connect_out())
            say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    } else {
        lfd = listen_on();
        if (lfd < 0)
            say("\x1b[1;31mCTerm cannot listen on its port - is it already "
                "running?\x1b[0m\r\n");
        else
            say("\x1b[1mCTerm\x1b[0m - waiting for the PC (cterm-relay)..."
                "\r\n");
    }
    refresh();

    for (;;) {
        fd_set rd;
        LONG maxfd = -1, n = 0;
        ULONG winsig = 1UL << win->UserPort->mp_SigBit;
        ULONG setsig = setwin ? 1UL << setwin->UserPort->mp_SigBit : 0;
        ULONG sigs = winsig | setsig | SIGBREAKF_CTRL_C | con_timer_sig();
        BOOL hostdata = FALSE;
        int i;
        FD_ZERO(&rd);
        for (i = 0; i < ntabs; i++) {
            struct Tab *t = tabs[i];
            LONG fd = t->t_sock >= 0 ? t->t_sock : t->t_lfd;
            if (t->con)
                sigs |= con_sigmask(t->con);
            if (fd >= 0) {
                FD_SET(fd, &rd);
                if (fd > maxfd)
                    maxfd = fd;
            }
        }
        if (maxfd < 0) {
            /* no network: the plain Amiga wait - the socket library's
             * WaitSelect costs far more per round, and a flood of tiny
             * writes is all rounds */
            PF_START(tw);
            sigs = Wait(sigs);
            PF_ADD(pf_wait, tw);
        } else
            n = WaitSelect(maxfd + 1, &rd, NULL, NULL, NULL, &sigs);
        T = shown;
        if ((sigs & SIGBREAKF_CTRL_C) && !quitting)
            quit_all();
        if (setwin && (sigs & setsig))
            settings_events();
        T = shown;
        if ((sigs & winsig) && !window_events() && !quitting)
            quit_all();
        /* every tab: its Shell's packets, its host's bytes */
        for (i = 0; i < ntabs; i++) {
            T = tabs[i];
            if (T->con) {
                char r[256];
                int k;
                PF_START(tp);
                con_handle(T->con, sigs);
                PF_ADD(pf_pkt, tp);
                k = vt_take_reply(vt, r, sizeof r);
                if (k > 0)
                    con_reply(T->con, (UBYTE *)r, k);
                if (!con_alive(T->con))
                    T->ended = TRUE;    /* the Shell ended */
            }
            if (n > 0) {
                LONG fd = sock >= 0 ? sock : lfd;
                if (fd >= 0 && FD_ISSET(fd, &rd)) {
                    host_input(buf, sizeof buf);
                    if (T == shown)
                        hostdata = TRUE;
                }
            }
            if (vt_take_bell(vt) && T == shown)
                DisplayBeep(scr);
        }
        /* tabs whose Shell has ended go */
        for (i = 0; i < ntabs; i++)
            if (tabs[i]->ended && !(tabs[i]->con &&
                                    con_alive(tabs[i]->con))) {
                tab_drop(tabs[i]);
                i = -1;                 /* the list moved: again */
            }
        if (ntabs == 0)
            break;
        T = shown;
        title_update();
        if (win && !win->MenuStrip && menu)
            SetMenuStrip(win, menu);    /* Ed has gone */
        if (con_render_due() && owed)
            draw_now();
        if (hostdata)
            refresh();
        if (want_switch)
            switch_display();
        if (!win)
            break;
    }
}

static BOOL arg_pc, arg_latin1;

int main(int argc, char **argv)
{
    tab0.t_utf8 = TRUE;
    int rc = RETURN_OK;
    (void)version;
    (void)argv;
    if (argc > 0) {
        LONG args[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        struct RDArgs *rda = ReadArgs("PORT/N,LATIN1/S,LISTEN/S,COLS/N,ROWS/N,"
                                      "FROM/K,HOST/K,PC/S,SSH/K,KEY/K",
                                      args, NULL);
        if (!rda) {
            PrintFault(IoErr(), "CTerm");
            return RETURN_FAIL;
        }
        if (args[0]) port = *(LONG *)args[0];
        if (args[1]) arg_latin1 = TRUE;
        if (args[2]) local = FALSE;     /* wait for the PC's relay */
        if (args[3]) want_cols = *(LONG *)args[3];
        if (args[4]) want_rows = *(LONG *)args[4];
        if (args[2] || args[3] || args[4] || args[5] || args[6])
            remember = FALSE;
        if (args[5]) {                  /* the Shell runs this script first */
            strncpy(fromfile, (char *)args[5], sizeof fromfile - 1);
            fromfile[sizeof fromfile - 1] = 0;
        }
        if (args[6]) {                  /* call a host (a BBS...) */
            strncpy(dialhost, (char *)args[6], sizeof dialhost - 1);
            strcpy(dialtitle, "CTerm: ");
            strcat(dialtitle, dialhost);
            local = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[7]) arg_pc = TRUE;
        if (args[8]) {                  /* ssh to user@host[:port] */
            strncpy(dialhost, (char *)args[8], sizeof dialhost - 1);
            strcpy(dialtitle, "CTerm: ssh ");
            strcat(dialtitle, dialhost);
            sshmode = TRUE;
            local = FALSE;
            remember = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[9])
            ssh_set_key((char *)args[9]);
        FreeArgs(rda);
    }
#ifdef NO_SSH
    if (sshmode) {
        /* ssh is CTermSSH's: start it with the same words, beside us */
        char cmd[600], path[256];
        STRPTR a = GetArgStr();
        int n;
        BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
        own_path(path, sizeof path, "CTermSSH");
        strcpy(cmd, "Run >NIL: \"");
        strcat(cmd, path);
        strcat(cmd, "\" ");
        strncat(cmd, a ? (char *)a : "", sizeof cmd - strlen(cmd) - 1);
        n = strlen(cmd);
        while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == ' '))
            cmd[--n] = 0;
        if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                       TAG_END) != 0) {
            if (nil) Close(nil);
            PutStr("CTerm: ssh needs CTermSSH beside CTerm\n");
            return RETURN_FAIL;
        }
        return RETURN_OK;
    }
#endif
    /* the network only when there is one to talk to: the local Shell
     * needs no TCP/IP stack at all */
    if (!local)
        SocketBase = OpenLibrary("bsdsocket.library", 4);
    KeymapBase = OpenLibrary("keymap.library", 37);
    GadToolsBase = OpenLibrary("gadtools.library", 37);
    AslBase = OpenLibrary("asl.library", 38);
    DiskfontBase = OpenLibrary("diskfont.library", 36);
    TTEngineBase = OpenLibrary("ttengine.library", 6);
    CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    load_prefs();
    apply_theme();
    if (arg_pc)
        pcmode = TRUE;
    if (arg_latin1)
        utf8 = FALSE;
    /* first start: TrueType if it is installed (DejaVu Sans Mono) */
    if (!hadprefs)
        usett = TTEngineBase && file_exists(ttfile);
    con_timer_init();
    {
        /* history takes at most a quarter of free memory (a line of 80
         * cells is 1280 bytes): a stock A1200 keeps room to run things */
        ULONG lines = AvailMem(MEMF_ANY) / 4 / (80 * sizeof(VtCell));
        vt = vt_new(80, 25, lines < SCROLLBACK ? (int)lines : SCROLLBACK);
    }
    if (vt) {
        vt_set_utf8(vt, utf8);
        if (pcmode)
            vt_set_cp437(vt, 1);
        if (local)
            vt_set_amiga(vt, 1);        /* the Amiga console's ways */
    }
    want_bar = scrollbar;
    if (!local && !SocketBase) {
        PutStr("CTerm HOST and LISTEN need a TCP/IP stack (bsdsocket.library)\n");
        rc = RETURN_FAIL;
    } else if (!vt || !KeymapBase || !GadToolsBase ||
        !open_display()) {
        rc = RETURN_FAIL;
    } else {
        if (pcmode)
            pc_font(TRUE);
        run();
    }
    if (win) {
        if (remember && !fullscreen && scr != ownscr) {
            wl = win->LeftEdge; wt = win->TopEdge;
            ww = win->Width; wh = win->Height;
        }
        envonly = used_only;
        save_prefs();
    }
    /* nothing may still be open on a Shell's port when it goes */
    {
        int i;
        for (i = 0; i < ntabs; i++)
            if (tabs[i]->con && con_alive(tabs[i]->con))
                con_stop(tabs[i]->con);
        for (;;) {
            ULONG sg = shells_sigmask();
            BOOL any = FALSE;
            for (i = 0; i < ntabs; i++)
                if (tabs[i]->con && con_alive(tabs[i]->con))
                    any = TRUE;
            if (!any)
                break;
            Wait(sg);
            shells_handle(sg);
        }
        while (ntabs > 0)
            tab_drop(tabs[0]);
        while (nspent > 0)
            con_free(spent[--nspent]);
    }
    close_settings();
    clip_close();
    close_display(FALSE);
    if (ownfont) CloseFont(ownfont);
    cache_free();
    if (rowbuf) FreeVec(rowbuf);
    close_tt();
    if (TTEngineBase) CloseLibrary(TTEngineBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    con_timer_free();
    vt_free(vt);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (KeymapBase) CloseLibrary(KeymapBase);
    if (SocketBase) CloseLibrary(SocketBase);
#ifdef PROFILE
    {
        BPTR f = Open((STRPTR)"SYS:T/cterm.prof", MODE_NEWFILE);
        if (f) {
            struct timeval t1;
            term_now(&t1);
            FPrintf(f, (STRPTR)"real s %ld; ticks: all %ld wait %ld packets %ld vt %ld draw %ld (scroll %ld text %ld cursor %ld knob %ld rows %ld fill %ld) nrows %ld nall %ld nscroll %ld calib1000 %ld draws %ld writes %ld\n",
                    (LONG)(t1.tv_secs - pf_tv0.tv_secs),
                    (ticks() - pf_all0), pf_wait, pf_pkt,
                    pf_vt, pf_draw, pf_scroll, pf_text, pf_cur, pf_knob, pf_rows, pf_fill, pf_nrows, pf_nall, pf_nscroll, pf_calib, pf_draws, pf_writes);
            Close(f);
        }
    }
#endif
    return rc;
}
