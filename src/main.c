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
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/gfxmacros.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <devices/inputevent.h>
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
 * Colours
 * ==================================================================== */

/* The default text and background, and the 16 ANSI colours (Nord-like,
 * with brighter brights). */
static ULONG deffg = 0xD8DEE9, defbg = 0x1A1D23;
static const ULONG ansi16[16] = {
    0x3B4252, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0,
    0xE5E9F0, 0x4C566A, 0xD57780, 0xB5D09E, 0xF0D399, 0x94B4D6, 0xC5A3C0,
    0x8FBCBB, 0xFFFFFF
};

static ULONG index_rgb(int i)
{
    static const UBYTE cube[6] = { 0, 95, 135, 175, 215, 255 };
    if (i < 16)
        return ansi16[i];
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
static BOOL fullscreen, scrollbar = TRUE;
static struct Screen *ownscr, *scr;
static struct DrawInfo *dri;
struct Window *win;                     /* console.c hands it to More/Ed */
static struct RastPort *rp;
static struct TextFont *font, *ownfont;
static char fontname[48];
static int fontsize;
static int cw, ch, cbase;               /* the cell */

/* TrueType, through ttengine.library: a .ttf file at a pixel size, with
 * its bold and italic files where there are any. */
static BOOL usett;
static char ttfile[160] = "SYS:Fonts/TrueType/DejaVuSansMono.ttf";
static int ttsize = 14;
static APTR ttface[4];                  /* regular, bold, italic, both */
static BOOL ttruns;                     /* whole cells per glyph: runs */
static int ox, oy, cols, rows;          /* where the grid starts, its size */
static APTR visual;
static struct Menu *menu;
static BOOL want_switch;
static int want_mode, want_depth;
static BOOL want_full, want_bar;

static Vt *vt;
static BOOL local = TRUE;               /* the Amiga Shell, not the PC */
static LONG want_cols = 80, want_rows = 25; /* the first window, in cells */
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
static int back;                        /* lines scrolled back */
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
static BOOL pcmode;                     /* a PC BBS: CP437, PC colours, bold
                                           is bright, blink a bright
                                           background (iCE colours) */
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
    static const ULONG wb[8] = { 0, 0, 0xFFFFFF, 0x88C0D0, 0x4C566A,
                                 0xB48EAD, 0xA3BE8C, 0xEBCB8B };
    if (local && i < 8)
        return i == 0 ? defbg : i == 1 ? deffg : wb[i];
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

static void draw_cells(int r, int x0, int x1)
{
    static const VtCell empty = { 0, VT_DEFAULT, VT_DEFAULT, 0 };
    static UBYTE text[512];
    int w, x, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);

    drmd(JAM2);
    x = x0;
    while (x <= x1) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        int n = 0, start = x;
        UWORD style = (c->attr & VT_BOLD && !pcmode ? FSF_BOLD : 0) |
                      (c->attr & VT_ITALIC ? FSF_ITALIC : 0) |
                      (c->attr & VT_UNDERLINE ? FSF_UNDERLINED : 0);

        cell_rgb(c, &fg, &bg);
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
    for (y = 0; y < f->tf_YSize && y < ch; y++) {
        const UBYTE *row = (const UBYTE *)f->tf_CharData + y * f->tf_Modulo;
        ULONG *dst = cellbuf + y * cellw;
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
    pm = TT_GetPixmap(tt_for(attr), (APTR)&u, 1, TT_Encoding,
                      TT_Encoding_UTF32_BE, TT_Antialias, TT_Antialias_On,
                      TAG_END);
    if (!pm)
        return;
    ph = pm->ttp_Height < (ULONG)ch ? (int)pm->ttp_Height : ch;
    pw = pm->ttp_Width < (ULONG)w ? (int)pm->ttp_Width : w;
    for (y = 0; y < ph; y++) {
        const UBYTE *a = pm->ttp_Data + y * pm->ttp_Width;
        ULONG *dst = cellbuf + y * cellw;
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
    /* a blank row (all default spaces, no cursor): one fill, or nothing */
    if (r != cy && !vt_reverse_video(vt)) {
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
        if (r == cy && x == cx) {       /* the cursor: the cell reversed */
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

static void layout_and_resize(void)
{
    int w, h;
    ox = win->BorderLeft + 2;
    oy = win->BorderTop + 1;
    w = win->Width - ox - win->BorderRight - 2 -
        (fullscreen ? POSBAR + 2 : 0);
    h = win->Height - win->BorderTop - win->BorderBottom - 2;
    cols = w / cw;
    rows = h / ch;
    if (cols < 10) cols = 10;
    if (rows < 3) rows = 3;
    if (cols != vt_cols(vt) || rows != vt_rows(vt))
        vt_resize(vt, cols, rows);
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

void term_write(const UBYTE *b, LONG n)
{
    PF_START(t0);
    vt_write(vt, b, n);
    PF_ADD(pf_vt, t0);
#ifdef PROFILE
    pf_writes++;
#endif
}
int term_cols(void) { return cols; }
int term_rows(void) { return rows; }
int term_cursor_x(void) { int x, y, v; vt_cursor(vt, &x, &y, &v); return x; }
int term_cursor_y(void) { int x, y, v; vt_cursor(vt, &x, &y, &v); return y; }

static LONG lastcost;                   /* us: the last draw, planar only */

void term_flush(void)
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
void term_render_soon(void)
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

static LONG lfd = -1, sock = -1;
static LONG port = DEFAULT_PORT;
static int tstate, tcmd;
static BOOL utf8 = TRUE;
static char dialhost[128];              /* HOST: CTerm calls out */
static UBYTE sbuf[64];                  /* a subnegotiation coming in */
static int sblen;

static void send_raw(const UBYTE *b, LONG n)
{
    LONG done = 0;
    if (sock < 0)
        return;
    while (done < n) {
        LONG k = send(sock, (APTR)(b + done), n - done, 0);
        if (k <= 0)
            return;
        done += k;
    }
}

/* Data to the host: 0xFF doubled. */
static void send_data(const UBYTE *b, LONG n)
{
    static UBYTE buf[1024];
    LONG i, k = 0;
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
    CloseSocket(sock);
    sock = -1;
    if (dialhost[0]) {
        say("\r\n\x1b[0;2m[the connection closed]\x1b[0m\r\n");
        return;
    }
    say("\r\n\x1b[0;2m[the connection closed - waiting for it again]"
        "\x1b[0m\r\n");
    lfd = listen_on();
}

/* ======================================================================
 * Keys
 * ==================================================================== */

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
            con_keys((UBYTE *)buf, n);
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
        send_typed((UBYTE *)buf, n);
        return;
    }
    if (back)
        scroll_view(0);
    send_str(buf);
}

/* ======================================================================
 * Screens, windows and menus
 * ==================================================================== */

enum { M_QUIT = 1, M_FONT, M_BAR, M_FULL, M_SCR_WB, M_SCR_16, M_SCR_256,
       M_SCR_DEEP, M_UTF8, M_ABOUT, M_TTFONT, M_BIGGER, M_SMALLER, M_SPEED,
       M_PC };

static struct NewMenu newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"About", 0, 0, 0, (APTR)M_ABOUT },
    {  NM_ITEM, (STRPTR)"Speed test", 0, 0, 0, (APTR)M_SPEED },
    {  NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Amiga font...", 0, 0, 0, (APTR)M_FONT },
    {  NM_ITEM, (STRPTR)"TrueType font...", (STRPTR)"T", 0, 0,
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
        if (depth > 8)
            depth = wb->RastPort.BitMap->Depth > 8
                  ? wb->RastPort.BitMap->Depth : 8;
        UnlockPubScreen(NULL, wb);
    }
    if (depth <= 8) {
        ULONG best = BestModeID(BIDTAG_SourceID, id, BIDTAG_Depth, depth,
                                TAG_END);
        if (best != INVALID_ID)
            id = best;
    }
    ownscr = OpenScreenTags(NULL,
        SA_DisplayID, id, SA_Depth, depth,
        SA_Width, STDSCREENWIDTH, SA_Height, STDSCREENHEIGHT,
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
    cw = f->tf_XSize;
    ch = f->tf_YSize;
    cbase = f->tf_Baseline;
    return TRUE;
}

/* ======================================================================
 * Settings: ENV:CTerm.prefs (and ENVARC:), one "key=value" per line.
 * ==================================================================== */

#define PREFS_VAR "CTerm.prefs"
static BOOL hadprefs;

static void load_prefs(void)
{
    static char buf[800];
    char *p = buf;
    LONG n = GetVar(PREFS_VAR, buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (n <= 0)
        return;
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
            else if (!stricmp(p, "utf8")) utf8 = val[0] == '1';
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
    static char buf[800];
    buf[0] = 0;
    strcat(buf, "truetype="); strcat(buf, usett ? "1" : "0");
    strcat(buf, "\nttfile="); strcat(buf, ttfile);
    strcat(buf, "\nttsize="); numstr_cat(buf, ttsize);
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
    strcat(buf, "\nutf8="); strcat(buf, utf8 ? "1" : "0");
    strcat(buf, "\n");
    SetVar(PREFS_VAR, buf, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
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
    ch = top + bot;
    cbase = top;
    return TRUE;
}

/* After the window opens or the font changes: tell ttengine about it. */
static void tt_attach(void)
{
    if (usett && win)
        TT_SetAttrs(rp, TT_Window, (ULONG)win, TT_Antialias,
                    TT_Antialias_On, TT_Encoding, TT_Encoding_UTF32_BE,
                    TAG_END);
}

static BOOL open_display(void)
{
    ULONG idcmp = IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY |
                  IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE |
                  IDCMP_INTUITICKS | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW |
                  IDCMP_INACTIVEWINDOW;
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
        cw = font->tf_XSize;
        ch = font->tf_YSize;
        cbase = font->tf_Baseline;
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
        if (w > scr->Width) w = scr->Width;
        if (h > scr->Height - top0) h = scr->Height - top0;
        win = OpenWindowTags(NULL,
            WA_Left, 0, WA_Top, top0, WA_Width, w, WA_Height, h,
            WA_Title, (ULONG)"CTerm",
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
            cw = font->tf_XSize;
            ch = font->tf_YSize;
            cbase = font->tf_Baseline;
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
        utf8 = !utf8;
        pcmode = FALSE;
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
    con_reply(p, strlen(r));
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
                    con_close_gadget();
            } else
                go = FALSE;
            break;
        case IDCMP_NEWSIZE:
            layout_and_resize();
            painter_setup();
            redraw_all();
            RefreshWindowFrame(win);
            if (sock >= 0)
                send_naws();
            raw_report(12, 0, 0);
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
        case IDCMP_GADGETDOWN:
            if (g == &agup || g == &agdn) {
                arrowheld = g == &agup ? 1 : -1;
                arrowticks = 0;
                scroll_view(back + arrowheld);
            }
            break;
        case IDCMP_GADGETUP:
        case IDCMP_MOUSEMOVE:
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

static void title_update(void)
{
    int changed;
    const char *t = vt_title(vt, &changed);
    static char title[160];
    if (!changed || !win)
        return;
    strcpy(title, "CTerm");
    if (t[0]) {
        UBYTE *d;
        int i = 8;
        strcat(title, " - ");
        d = (UBYTE *)title + strlen(title);
        /* the title is UTF-8: keep what Latin-1 can show */
        while (*t && i++ < 140) {
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
    if (local) {
        const char *script = fromfile[0] ? fromfile : "S:Shell-Startup";
        BPTR l = Lock((STRPTR)script, ACCESS_READ);
        if (l)
            UnLock(l);
        if (!con_start(l ? script : NULL)) {
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
        dial();
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
        LONG fd = sock >= 0 ? sock : lfd, n, wait = -1, c;
        ULONG winsig = 1UL << win->UserPort->mp_SigBit;
        ULONG sigs = winsig | SIGBREAKF_CTRL_C | (local ? con_sigmask() : 0);
        struct timeval tv;
        FD_ZERO(&rd);
        if (fd >= 0)
            FD_SET(fd, &rd);
        (void)c;
        if (local) {
            /* no network here: the plain Amiga wait, and the timer for
             * what is due - the socket library's WaitSelect costs far
             * more per round, and a flood of tiny writes is all rounds */
            /* the frame timer and WaitForChar's timer signal the timer
             * port, whose bit con_sigmask() includes */
            {
                PF_START(tw);
                sigs = Wait(sigs);
                PF_ADD(pf_wait, tw);
            }
            n = 0;
        } else {
            if (wait >= 0) {
                tv.tv_sec = wait / 1000;
                tv.tv_usec = (wait % 1000) * 1000;
            }
            n = WaitSelect(fd >= 0 ? fd + 1 : 0, fd >= 0 ? &rd : NULL, NULL,
                           NULL, wait >= 0 ? &tv : NULL, &sigs);
        }
        if (sigs & SIGBREAKF_CTRL_C) {
            if (!local)
                break;
            quitting = TRUE;
            con_stop();
        }
        if ((sigs & winsig) && !window_events()) {
            if (!local)
                break;
            quitting = TRUE;            /* end the Shell, then us */
            con_stop();
        }
        if (local) {
            char r[256];
            int k;
            {
                PF_START(tp);
                con_handle(sigs);
                PF_ADD(pf_pkt, tp);
            }
            k = vt_take_reply(vt, r, sizeof r);
            if (k > 0)
                con_reply((UBYTE *)r, k);
            if (vt_take_bell(vt))
                DisplayBeep(scr);
            title_update();
            if (win && !win->MenuStrip && menu)
                SetMenuStrip(win, menu);        /* Ed has gone */
            if (con_render_due() && owed)
                term_flush();
            if (!con_alive())
                break;                          /* the Shell ended */
            if (want_switch)
                switch_display();
            if (!win)
                break;
            continue;
        }
        if (want_switch)
            switch_display();
        if (!win)
            break;
        if (n > 0 && fd >= 0 && FD_ISSET(fd, &rd)) {
            if (fd == lfd) {
                connected();
            } else {
                /* take what has come, and what keeps coming without a
                 * wait, before drawing: a burst draws once */
                int rounds = 0;
                for (;;) {
                    struct timeval tv;
                    LONG k = recv(sock, buf, sizeof buf, 0);
                    tv.tv_sec = tv.tv_usec = 0;
                    if (k <= 0) {
                        hung_up();
                        break;
                    }
                    from_host(buf, k);
                    if (++rounds > 16)
                        break;
                    FD_ZERO(&rd);
                    FD_SET(sock, &rd);
                    if (WaitSelect(sock + 1, &rd, NULL, NULL, &tv, NULL) <= 0)
                        break;
                }
                {
                    char r[256];
                    int k = vt_take_reply(vt, r, sizeof r);
                    if (k > 0)
                        send_data((UBYTE *)r, k);
                }
                if (vt_take_bell(vt))
                    DisplayBeep(scr);
                title_update();
            }
        }
        refresh();
    }
    if (sock >= 0) CloseSocket(sock);
    if (lfd >= 0) CloseSocket(lfd);
}

static BOOL arg_pc;

int main(int argc, char **argv)
{
    int rc = RETURN_OK;
    (void)version;
    (void)argv;
    if (argc > 0) {
        LONG args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        struct RDArgs *rda = ReadArgs("PORT/N,LATIN1/S,LISTEN/S,COLS/N,ROWS/N,"
                                      "FROM/K,HOST/K,PC/S",
                                      args, NULL);
        if (!rda) {
            PrintFault(IoErr(), "CTerm");
            return RETURN_FAIL;
        }
        if (args[0]) port = *(LONG *)args[0];
        if (args[1]) utf8 = FALSE;
        if (args[2]) local = FALSE;     /* wait for the PC's relay */
        if (args[3]) want_cols = *(LONG *)args[3];
        if (args[4]) want_rows = *(LONG *)args[4];
        if (args[5]) {                  /* the Shell runs this script first */
            strncpy(fromfile, (char *)args[5], sizeof fromfile - 1);
            fromfile[sizeof fromfile - 1] = 0;
        }
        if (args[6]) {                  /* call a host (a BBS...) */
            strncpy(dialhost, (char *)args[6], sizeof dialhost - 1);
            local = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[7]) arg_pc = TRUE;
        FreeArgs(rda);
    }
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
    if (arg_pc)
        pcmode = TRUE;
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
        run();
    }
    if (win)
        save_prefs();
    if (local) {
        /* nothing may still be open on our port when it goes */
        while (con_alive()) {
            ULONG sg = con_sigmask();
            Wait(sg);
            con_handle(sg);
        }
        con_free();
    }
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
