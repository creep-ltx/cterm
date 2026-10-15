/*
 * draw - colours, drawing the text (pens, Text() and the fast painter),
 * the scroll bar, the layout and the picture behind the text.
 */
#include "cterm.h"
#include "ppaint.h"

/* A cell's look (its colours and style), from the shown tab's table */
#define PEN(c) (&vt_pens(vt)[(c)->pen])
#include <graphics/regions.h>
#include <proto/layers.h>

struct CEntry;
static ULONG index_rgb(int i);
static LONG pen_for(ULONG rgb);
static void planes_reset(void);
static void mask_pen(LONG p);
static void drmd(int m);
static void softstyle(int st);
static ULONG pen_rgb(int i);
static void cell_rgb_(const VtCell *c, ULONG *fg, ULONG *bg);
static void cell_rgb(const VtCell *c, ULONG *fg, ULONG *bg);
static void cell_rgb_(const VtCell *c, ULONG *fg, ULONG *bg);
static void RF(int x0, int y0, int x1, int y1);
static void SETCOL(ULONG rgb);
static void hline(int x0, int x1, int y, int th);
static void vline(int x, int y0, int y1, int th);
static void draw_box(UBYTE arms, int x, int y);
static void draw_block(ULONG cp, int x, int y, ULONG fg, ULONG bg);
static void draw_braille(ULONG cp, int x, int y);
static void disc(int cx, int cy, int r, BOOL leftonly);
static BOOL draw_shape(ULONG cp, int x, int y);
static UBYTE fallback(ULONG cp);
static BOOL is_special(ULONG cp);
static ULONG tt_stand_in(ULONG cp);
static APTR tt_for(UWORD attr);
static void tt_run(const ULONG *u, const int *cols, int n, int y, ULONG fg, UWORD attr);
static void tt_glyphs(const ULONG *u, const int *cols, int n, int y);
static BOOL sel_span(int r, int *s0, int *s1);
static void draw_cells(int r, int x0, int x1);
static BOOL pp_cells(int r, int a, int b);
static BOOL ppon;                       /* the planar painter in use */
#ifndef PPRUN
#define PPRUN 2                         /* chip only: the painter from this run on */
#endif
static int area_right(void);
static void paint_row(int r);
static BOOL backdrop_put(LONG x, LONG y, LONG w, LONG h);
static void draw_span(int r, int a, int b);
static void cursor_mark(int x, int y);
static BOOL cache_alloc(void);
static void cell_bitmap_glyph(UBYTE c, ULONG fg, UWORD attr);
static void cell_tt_glyph(ULONG cp, ULONG fg, ULONG bg, UWORD attr, int w);
static void make_cell(struct CEntry *e);
static struct CEntry *cell_get(ULONG cp, ULONG fg, ULONG bg, UWORD attr, UBYTE wide);
static void shift_shown(int top, int bottom, int n);
static void forget_shown(void);
static void paint_row(int r);
static APTR mksysi(ULONG which, int sysz);
static void mkarrow(struct Gadget *g, APTR im, int le, int te, int id);
static void __attribute__((noinline)) load_pic_(void);
static BOOL backdrop_put(LONG x, LONG y, LONG w, LONG h);
static void forget_shown(void);

const struct Theme themes[NTHEMES] = {
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

/* The PC's own 16 colours (VGA): PC BBS art is drawn for these. */
static const ULONG vga16[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA,
    0xAAAAAA, 0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF,
    0x55FFFF, 0xFFFFFF
};

int theme;
BOOL ownfg, ownbg;               /* the person's own text/background */
ULONG myfg, mybg;
ULONG deffg = 0xD8DEE9, defbg = 0x1A1D23;
static const ULONG *ansi16 = themes[0].ansi;


void apply_theme(void)
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

ULONG mix(ULONG a, ULONG b, int pct)              /* pct of b into a */
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

int scrmode = SCR_WB, owndepth = 8;
ULONG ownmode = INVALID_ID;      /* the own screen's mode; INVALID_ID:
                                           the Workbench's, at owndepth */
LONG ownw, ownh;                 /* its size; 0: the mode's own */
BOOL fullscreen, scrollbar = TRUE;
struct Screen *ownscr, *scr;
struct DrawInfo *dri;
struct Window *win;                     /* console.c hands it to More/Ed */
struct RastPort *rp;
struct TextFont *font, *ownfont;
char fontname[48];
int fontsize;
int cw, ch, cbase;               /* the cell */
int linespace;                   /* extra pixels between lines */

/* TrueType, through ttengine.library: a .ttf file at a pixel size, with
 * its bold and italic files where there are any. */
BOOL usett;
char ttfile[160] = "SYS:Fonts/TrueType/DejaVuSansMono.ttf";
/* The PC's own font for PC BBS mode: the IBM VGA 8x16 as pixel outlines
 * (int10h.org's Ultimate Oldschool PC Font Pack, CC BY-SA 4.0), at its
 * one true size and not smoothed. */
BOOL pcfont = TRUE;              /* the setting */
BOOL pics = TRUE;                /* pictures in the terminal (sixel...) */
BOOL ownprog = TRUE;             /* raw Amiga programs: own screen */
BOOL altmeta = TRUE;             /* Alt is Meta in a host's tab */
char userkey[2] = "U";           /* Right Amiga + it: send the name */
char passkey[2] = "P";           /* ... the password */
char dldir[160], uldir[160];     /* where files come, go from */
char pcttfile[160] = "PROGDIR:Fonts/PxPlus_IBM_VGA_8x16.ttf";
int fontover;                    /* worn for the tab: BL_* (0: none) */
BOOL ttaa = TRUE;                /* smoothing; off for pixel fonts */
struct KeepFont keepfont;
int ttsize = 14;
APTR ttface[4];                  /* regular, bold, italic, both */
BOOL ttruns;                     /* whole cells per glyph: runs */
int ox, oy, cols, rows;          /* where the grid starts, its size */
APTR visual;
struct Menu *menu;
BOOL want_switch;
int want_mode, want_depth;
BOOL want_full, want_bar;

LONG want_cols = 80, want_rows = 25; /* the first window, in cells */
LONG wl = -1, wt, ww, wh;        /* the Workbench window, remembered */
char calls[300];                 /* places called, newest first:
                                           "host[:port][!];..." (! = PC) */
BOOL remember = TRUE;            /* FALSE: a one-off start (COLS,
                                           HOST...) - its window is not kept */
BOOL quitting;
BOOL owed;                       /* drawing owed: a frame timer runs */
BOOL flood;                      /* the last draw was a scroll */
/* where the cursor was drawn: the pane's (pane.c) */
#define curx (cpane->curx)
#define cury (cpane->cury)

/* The pen cache: an RGB value asked of the screen once. */
#define NPEN 256
static ULONG pc_rgb[NPEN];
static LONG pc_pen[NPEN];
int npc;

/* Every colour asked for, and its pen - the pens obtained and the
 * nearest ones given once the screen had no more: found by hash. Before,
 * an exact match was a walk of up to 256 entries, and with the pens all
 * taken (a 16-colour Workbench after a page of colours) every new colour
 * was the nearest-pen search again, every time it came: a 24-bit
 * animation on a 14 MHz Amiga went from 4 frames a second to under 1. */
#define PHASH 1024
static ULONG ph_rgb[PHASH];             /* rgb + 1 (0: free) */
static LONG ph_pen[PHASH];
static int ph_n;

static LONG pen_find(ULONG rgb, int *slot)
{
    ULONG h = (rgb * 2654435761UL) >> 22;   /* 10 bits */
    while (ph_rgb[h]) {
        if (ph_rgb[h] == rgb + 1)
            return ph_pen[h];
        h = (h + 1) & (PHASH - 1);
    }
    *slot = (int)h;
    return -1;
}

static LONG pen_for_(ULONG rgb);

static LONG pen_for(ULONG rgb)
{
    int slot;
    LONG p = pen_find(rgb, &slot);
    if (p >= 0)
        return p;
    p = pen_for_(rgb);
    if (ph_n >= PHASH * 3 / 4) {
        /* full (a page of 24-bit gradients is ~1000 colours): start
         * afresh, so the colours in use now are found again - kept full,
         * every new colour was the slow way for good */
        memset(ph_rgb, 0, sizeof ph_rgb);
        ph_n = 0;
        pen_find(rgb, &slot);
    }
    ph_rgb[slot] = rgb + 1;
    ph_pen[slot] = p;
    ph_n++;
    return p;
}

static LONG pen_for_(ULONG rgb)
{
    int i, best = -1;
    LONG d, bd = 0x7FFFFFFF;
    for (i = 0; i < npc; i++)           /* (only when the hash is full) */
        if (pc_rgb[i] == rgb)
            return pc_pen[i];
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
BOOL painter;                    /* rows built in memory (RTG) */

/* A picture behind the text (the painter only: it composes rows in
 * memory): bgsrc the picture as read, backdrop the window's inner area
 * with the picture mixed into the background (bgsee per cent of it). */
char bgfile[256];
int bgsee = 40;
ULONG *bgsrc;
static LONG bgsw, bgsh;
static ULONG *backdrop;
static LONG bkw, bkh, bkx, bky;
#define MASKKEY 0x01000000UL            /* a cell drawn as a mask: no RGB */
static LONG pbase = -1;                 /* the clear's pen; -1: no mask */
static UBYTE pmask;

static void planes_reset(void)
{
    /* only real planes: a graphics card's pixels are not planes */
    if (planar && !painter && !usett && rp->BitMap) {
        pbase = pen_for(defbg);
        pmask = 0;
    } else {
        pbase = -1;
        pmask = 0xFF;
    }
    SetWriteMask(rp, pmask);
    rp_forget();                        /* every pen must pass mask_pen */
}

void planes_full(void)
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
static ULONG ppp_gen = 1;               /* pp_pens' answers of this draw */
static APTR c_ttfont;                   /* TrueType: the face and style set */
static LONG c_ttstyle = -1;

void rp_forget(void)
{
    c_apen = c_bpen = -1;
    c_argb = c_brgb = ~0UL;
    c_drmd = c_style = -1;
    rgb_memo = FALSE;
    ppp_gen++;
    c_ttfont = NULL;                    /* also when faces close: fonts.c */
    c_ttstyle = -1;
}

void apen(ULONG rgb)
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

void bpen(ULONG rgb)
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

ULONG pen_gen;                   /* counts release_pens: pens asked for
                                    before (the pictures' cube) are gone */

void release_pens(void)
{
    while (npc > 0)
        ReleasePen(scr->ViewPort.ColorMap, pc_pen[--npc]);
    memset(ph_rgb, 0, sizeof ph_rgb);   /* their numbers mean nothing now */
    ph_n = 0;
    pen_gen++;
    ppp_gen++;
}

/* For Parts/Pictures: a pen for a colour of its cube, exactly that
 * colour while the screen has pens free (ObtainBestPen's image
 * precision took a near one already given out: the cube fell into its
 * neighbours - yellow came out yellow-green); kept in the cache, so the
 * text shares it and release_pens gives it back. */
LONG pic_pen(ULONG rgb)
{
    int slot;
    LONG p = pen_find(rgb, &slot);
    if (p >= 0 || npc >= NPEN || ph_n >= PHASH * 3 / 4)
        return p >= 0 ? p : pen_for(rgb);
    p = ObtainBestPen(scr->ViewPort.ColorMap,
                      ((rgb >> 16) & 255) * 0x01010101,
                      ((rgb >> 8) & 255) * 0x01010101,
                      (rgb & 255) * 0x01010101,
                      OBP_Precision, PRECISION_EXACT, TAG_END);
    if (p < 0)
        return pen_for(rgb);
    pc_rgb[npc] = rgb;
    pc_pen[npc++] = p;
    ph_rgb[slot] = rgb + 1;
    ph_pen[slot] = p;
    ph_n++;
    return p;
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

/* Runs next to each other mostly share colours: the last answer is kept
 * (forgotten with the RastPort state, at every draw). */
static void cell_rgb(const VtCell *c, ULONG *fg, ULONG *bg)
{
    static ULONG rf, rb;
    static UWORD kp;
    static const void *kv;
    if (rgb_memo && c->pen == kp && kv == vt) {
        *fg = rf;
        *bg = rb;
        return;
    }
    cell_rgb_(c, fg, bg);
    kp = c->pen;
    kv = vt;
    rf = *fg; rb = *bg;
    rgb_memo = TRUE;
}

static void cell_rgb_(const VtCell *c, ULONG *fg, ULONG *bg)
{
    const VtPen *pn = PEN(c);
    ULONG f, b, cf = pn->fg, cb = pn->bg;
    if (pcmode) {
        /* a PC: bold is the bright half of the 16 colours, blink the
         * bright backgrounds; the default is grey on black */
        if (cf == VT_DEFAULT)
            cf = VT_INDEX(7);
        if (cb == VT_DEFAULT)
            cb = VT_INDEX(0);
        if ((pn->attr & VT_BOLD) && VT_IS_INDEX(cf) && (cf & 255) < 8)
            cf += 8;
        if ((pn->attr & VT_BLINK) && VT_IS_INDEX(cb) && (cb & 255) < 8)
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
    BOOL inv = (pn->attr & VT_INVERSE) != 0;
    if (vt_reverse_video(vt))
        inv = !inv;
    if (inv) {
        ULONG t = f;
        f = b;
        b = t;
    }
    if (pn->attr & VT_DIM)
        f = mix(f, b, 40);
    if (pn->attr & VT_HIDDEN)
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

/* Braille U+2800-28FF: 2 x 4 dots on the cell's grid, as foot draws
 * them, so btop-style graphs line up from cell to cell - in an Amiga
 * font as in TrueType. Bits: 1 2 3 7 down the left, 4 5 6 8 the right. */
static void draw_braille(ULONG cp, int x, int y)
{
    static const UBYTE bit[2][4] = { { 0x01, 0x02, 0x04, 0x40 },
                                     { 0x08, 0x10, 0x20, 0x80 } };
    int dw = cw / 4 > 0 ? cw / 4 : 1, dh = ch / 8 > 0 ? ch / 8 : 1;
    int i, j;
    if (dh > dw)
        dh = dw;                        /* dots, not dashes */
    for (i = 0; i < 2; i++) {
        int x0 = x + (2 * i + 1) * cw / 4 - dw / 2;
        for (j = 0; j < 4; j++)
            if (cp & bit[i][j]) {
                int y0 = y + (2 * j + 1) * ch / 8 - dh / 2;
                RF(x0, y0, x0 + dw - 1, y0 + dh - 1);
            }
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
    if (cp <= 0xFF || cp == VT_WIDE_TAIL)
        return FALSE;
    if ((cp >= 0x2500 && cp <= 0x259F) || (cp >= 0x2800 && cp <= 0x28FF))
        return TRUE;                    /* lines, blocks, braille */
    return !usett || VT_IS_PIC(cp);     /* a picture's cell too */
}

/* ---- pictures in the text (vt.h): a cell may show a picture's tile ----
 * The RTG painter copies the tile into its row; an 8-bit card's painter
 * and plain screens take pens for its pixels from Parts/Pictures (a
 * colour cube, dithered); the Text() ways write a run of such cells
 * straight to the window. */

/* The tile a picture cell shows: its first pixel, how much of the cell
 * it covers (the rest is background), the picture's row in bytes.
 * NULL: the picture is gone. */
static __attribute__((cold, noinline)) const UBYTE *pic_tile(ULONG cp, int *tw, int *th, int *stride)
{
    const VtPic *p = vt_pic(vt, cp);
    int px, py;
    if (!p)
        return NULL;
    px = VT_PIC_TX(cp) * p->cw;
    py = VT_PIC_TY(cp) * p->ch;
    if (px >= p->w || py >= p->h)
        return NULL;                    /* past its pixels: a new letter
                                           size cut it so (vt.h) */
    *tw = p->w - px < p->cw ? p->w - px : p->cw;
    *th = p->h - py < p->ch ? p->h - py : p->ch;
    if (*tw > cw) *tw = cw;             /* the letters got smaller since */
    if (*th > ch) *th = ch;
    *stride = p->w * 3;
    return p->rgb + ((long)py * p->w + px) * 3;
}

/* A picture cell into a row of ARGB rw pixels wide. What the tile does
 * not cover (a picture's last row or column of cells, one gone) is the
 * background: bk, the picture behind the text at this cell (its rows
 * bkw apart), or NULL - the plain colour. */
static __attribute__((cold, noinline)) void pic_argb(ULONG *d, int rw, ULONG cp,
                                                     const ULONG *bk, int bkw)
{
    int tw = 0, th = 0, st = 0, x, y;
    const UBYTE *s = pic_tile(cp, &tw, &th, &st);
    if (!s)
        th = 0;
    for (y = 0; y < ch; y++, d += rw) {
        x = 0;
        if (y < th) {
            const UBYTE *q = s + y * st;
            for (; x < tw; x++, q += 3)
                d[x] = (ULONG)q[0] << 16 | (ULONG)q[1] << 8 | q[2];
        }
        if (bk) {
            for (; x < cw; x++)
                d[x] = bk[x];
            bk += bkw;
        } else
            for (; x < cw; x++)
                d[x] = defbg;
    }
}

/* ... into a row of pens rw bytes wide; sx, sy: the cell's place on the
 * screen (the dither's pattern is the screen's) */
static __attribute__((cold, noinline)) void pic_pens8(UBYTE *d, int rw, ULONG cp, int sx, int sy)
{
    int tw = 0, th = 0, st = 0, x, y;
    const UBYTE *s = pic_tile(cp, &tw, &th, &st);
    UBYTE bgp = (UBYTE)pen_for(defbg);
    if (!s)
        th = 0;
    for (y = 0; y < ch; y++, d += rw) {
        x = 0;
        if (y < th && pics_pens(s + y * st, tw, sx, sy + y, d))
            x = tw;
        for (; x < cw; x++)
            d[x] = bgp;
    }
}

static BOOL chunky;

/* Cells a..b of row r, picture cells all, straight to the window: a
 * card's pixels, or pens (plain screens: every plane from now on, so a
 * scroll carries the picture's too) */
static __attribute__((cold, noinline)) void pic_put(int r, int a, int b)
{
    int w, x, rw = (b - a + 1) * cw, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);
    BOOL deep = !planar && GetCyberMapAttr(rp->BitMap, CYBRMATTR_DEPTH) >= 15;
    UBYTE *buf = AllocVec(deep ? rw * ch * 4 : rw * ch, MEMF_ANY);
    if (!buf) {
        apen(defbg);
        RectFill(rp, ox + a * cw, y, ox + a * cw + rw - 1, y + ch - 1);
        return;
    }
    for (x = a; x <= b; x++) {
        ULONG cp = line && x < w ? line[x].cp : 0;
        if (deep)
            pic_argb((ULONG *)buf + (x - a) * cw, rw, cp, NULL, 0);
        else
            pic_pens8(buf + (x - a) * cw, rw, cp, ox + x * cw, y);
    }
    if (deep)
        WritePixelArray(buf, 0, 0, rw * 4, rp, ox + a * cw, y, rw, ch,
                        RECTFMT_ARGB);
    else {
        if (pmask != 0xFF) {
            pmask = 0xFF;
            SetWriteMask(rp, 0xFF);
        }
        WriteChunkyPixels(rp, ox + a * cw, y, ox + a * cw + rw - 1,
                          y + ch - 1, buf, rw);
    }
    FreeVec(buf);
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

/* A row's TrueType letters and their cells (512 each), then those the
 * fallback font draws (wide, narrow), made when TrueType first draws:
 * 12 KB a stock A1200 need not keep */
static ULONG *ttbuf;

/* A run of TrueType text on its background; cols[i]: glyph i's cell. */
static APTR tt_over;                    /* set: tt_run draws with this face */

static void tt_run(const ULONG *u, const int *cols, int n, int y,
                   ULONG fg, UWORD attr)
{
    int i;
    APTR f = tt_over ? tt_over : tt_for(attr);
    LONG st = attr & VT_UNDERLINE ? TT_SoftStyle_Underlined
                                  : TT_SoftStyle_None;
    drmd(JAM1);
    apen(fg);
    if (f != c_ttfont) {
        TT_SetFont(rp, f);
        c_ttfont = f;
    }
    if (st != c_ttstyle) {
        TT_SetAttrs(rp, TT_SoftStyle, st, TAG_END);
        c_ttstyle = st;
    }
    /* The row is as tall as the Latin-1 letters (fonts.c): a taller
     * glyph (Vietnamese stacked accents) would leave pixels in the rows
     * above and below, so such a run is drawn clipped to its row - as a
     * PC terminal clips. Only those: a clip costs a layer rebuild. */
    for (i = 0; i < n && u[i] < 0x100; i++)
        ;
    if (i < n) {
        struct TextExtent te;
        TT_TextExtent(rp, (APTR)u, n, &te);
        if (-te.te_Extent.MinY > cbase || te.te_Extent.MaxY >= ch - cbase) {
            static struct Region *rg;
            struct Region *old;
            struct Rectangle r;
            if (!rg && !(rg = NewRegion()))
                goto plain;
            ClearRegion(rg);
            r.MinX = 0; r.MaxX = win->Width - 1;
            r.MinY = y; r.MaxY = y + ch - 1;
            if (!OrRectRegion(rg, &r))
                goto plain;
            old = InstallClipRegion(win->WLayer, rg);
            tt_glyphs(u, cols, n, y);
            InstallClipRegion(win->WLayer, old);
            drmd(JAM2);
            return;
        }
    }
plain:
    tt_glyphs(u, cols, n, y);
    drmd(JAM2);
}

/* tt_run's drawing: the font and pen are set. */
static void tt_glyphs(const ULONG *u, const int *cols, int n, int y)
{
    int i;
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
}

/* Plain (planar) screens: Text() draws the letters. A CPU writer into
 * the planes was measured on a stock A1200 (FS-UAE, 2026-10-10) and lost:
 * in C, 13000 instructions for a 76-letter row (~17 ms) against Text()'s
 * ~3 ms; only assembly could win, and only on many-coloured rows. */
BOOL planar;                     /* a native screen, real planes */

/* ---- the selection: text marked with the mouse ------------------------
 * In screen cells, reading order from the anchor (a) to the end (b),
 * both included; it goes when the text under it moves (a scroll). */

void sel_bounds(int *r0, int *c0, int *r1, int *c1)
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
    static const VtCell empty = { 0, 0 };
    static UBYTE text[512];
    int w, x, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);

    int s0 = -1, s1 = -2, xend = x1, runs = 0;
    BOOL insel, rpics = line && VT_ROW_PICS(line);
    sel_span(r, &s0, &s1);
    drmd(JAM2);
    x = x0;
    while (x <= xend) {
        /* a colourful row: the rest by the planar painter, which costs
         * the same for any number of colours (Text() is a call a run) */
        if (++runs == PPRUN && ppon && !rpics && pp_cells(r, x, xend))
            break;
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        int n = 0, start = x;
        /* a run stays on one side of the selection's edges */
        insel = x >= s0 && x <= s1;
        x1 = insel ? (s1 < xend ? s1 : xend)
                   : (x < s0 && s0 - 1 < xend ? s0 - 1 : xend);
        UWORD cat = PEN(c)->attr;
        UWORD style = (cat & VT_BOLD && !pcmode ? FSF_BOLD : 0) |
                      (cat & VT_ITALIC ? FSF_ITALIC : 0) |
                      (cat & VT_UNDERLINE ? FSF_UNDERLINED : 0);

        cell_rgb(c, &fg, &bg);
        if (insel) {
            ULONG t = fg; fg = bg; bg = t;
        }
        if (usett)
            cp = tt_stand_in(cp);
        if (is_special(cp)) {
            if (VT_IS_PIC(cp)) {        /* a run of picture cells */
                int e = x;
                while (e < xend && e + 1 < w && VT_IS_PIC(line[e + 1].cp))
                    e++;
                pic_put(r, x, e);
                x = e + 1;
                continue;
            }
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
            } else if (cp >= 0x2800 && cp <= 0x28FF) {
                draw_braille(cp, ox + x * cw, y);
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
            UWORD cpn = c->pen;
            if (lim - x >= (int)sizeof text)
                lim = x + (int)sizeof text - 1;
            while (x <= lim) {
                ULONG dc = d->cp;
                if (dc > 0xFF || d->pen != cpn)
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
            if (d != c && d->pen != c->pen) {
                cell_rgb(d, &f2, &b2);
                /* the strike is drawn once for the run: a part of it */
                if (f2 != fg || b2 != bg ||
                    (PEN(d)->attr & (VT_BOLD | VT_ITALIC | VT_UNDERLINE |
                                     VT_STRIKE)) !=
                    (cat & (VT_BOLD | VT_ITALIC | VT_UNDERLINE | VT_STRIKE)))
                    break;
            }
            text[n++] = d->cp == VT_WIDE_TAIL ? ' ' : d->cp < 0x20 ? ' '
                      : (UBYTE)d->cp;
            x++;
        }
        if (usett) {
            ULONG *u;
            int *ucol, k, m = 0;
            apen(bg);
            RectFill(rp, ox + start * cw, y, ox + x * cw - 1, y + ch - 1);
            if (!ttbuf && !(ttbuf = AllocVec(512 * 24, MEMF_ANY)))
                continue;               /* no memory: the background only */
            u = ttbuf;
            ucol = (int *)(ttbuf + 512);
            {
                /* the letters the font lacks: the fallback font's (fonts.c),
                 * the wide ones (two cells) and the narrow ones apart */
                ULONG *fu = ttbuf + 1024, *nu = ttbuf + 2048;
                int *fcol = (int *)(ttbuf + 1536), fm = 0;
                int *ncol = (int *)(ttbuf + 2560), nm = 0;
                for (k = start; k < x; k++) {
                    const VtCell *d = k < w && line ? &line[k] : &empty;
                    ULONG cp2;
                    if (d->cp == VT_WIDE_TAIL || d->cp <= ' ')
                        continue;       /* spaces: the background did it */
                    cp2 = tt_stand_in(d->cp);
                    if (tt_missing(cp2)) {
                        if (k + 1 < w && line && line[k + 1].cp == VT_WIDE_TAIL) {
                            fu[fm] = tt_fallcp(cp2);
                            fcol[fm++] = k;
                        } else {
                            nu[nm] = tt_fallcp(cp2);
                            ncol[nm++] = k;
                        }
                        continue;
                    }
                    u[m] = cp2;
                    ucol[m++] = k;
                }
                if (m)
                    tt_run(u, ucol, m, y, fg, cat);
                if (fm) {
                    tt_over = ttfall;
                    tt_run(fu, fcol, fm, y, fg, cat);
                }
                if (nm) {
                    tt_over = ttfall1;
                    tt_run(nu, ncol, nm, y, fg, cat);
                }
                tt_over = NULL;
            }
        } else {
            if (linespace) {            /* Text() fills the font's rows only */
                apen(bg);
                RectFill(rp, ox + start * cw, y, ox + x * cw - 1, y + ch - 1);
            }
            apen(fg);
            bpen(bg);
            softstyle(style);
            Move(rp, ox + start * cw, y + cbase);
            Text(rp, (STRPTR)text, n);
        }
        if (cat & VT_STRIKE)
            hline(ox + start * cw, ox + x * cw - 1, y + ch / 2, 1);
    }
    softstyle(0);
}

#define CMAX 4096
struct CEntry {
    ULONG cp, fg, bg;
    UWORD attr;
    UBYTE wide, used;                   /* used: 1, 2 asked for again */
    ULONG *pix;
};

/* ======================================================================
 * The planar painter: letters straight into the bitplanes
 *
 * On a plain (OCS/ECS/AGA) screen with an 8-pixel-wide Amiga font, a row
 * is written by the CPU (ppaint.s) instead of a Text() per colour run.
 * Every look a cell can have - a letter in a style, a box line, a block,
 * a shade - is drawn once by make_cell (as the RTG painter's pictures
 * are) and kept as one bit per pixel in ppaint.c's glyph store; colours
 * are not part of a glyph, the asm puts them in per plane. Only where
 * the window is not covered: else Text() as before.
 * ==================================================================== */

/* chunky (above): the painter in pens, an 8-bit card */
BOOL nopp;                              /* TEXTDRAW: Text() as before */
static struct TextFont *pp_font;        /* what the glyphs were made for */
static int pp_fch, pp_fbase;
static ULONG *pp_lat[256];              /* plain Latin-1: its block (NULL: none) */
static BOOL pp_all;                     /* every row (fast RAM); else colourful ones */
#define PPH 512                         /* the rest: (cp, style) -> slot */
#define PPSLOTS 256                     /* a stock A1200 has no room to spare */
static struct { ULONG cp; UWORD st, slot; } pph[PPH];

static void pp_reset(void)
{
    int i;
    pp_clear();
    for (i = 0; i < 256; i++)
        pp_lat[i] = i <= ' ' ? pp_blank : NULL;
    for (i = 0; i < PPH; i++)
        pph[i].slot = 0xFFFF;
}

/* A cell's look as one bit per pixel: drawn white on black. */
static UWORD pp_make(ULONG cp, UWORD st)
{
    static ULONG pix[8 * 32];
    struct CEntry e;
    UBYTE lines[32];
    int x, y, slot;
    e.cp = cp; e.fg = 0xFFFFFF; e.bg = 0; e.attr = st; e.wide = 0;
    e.pix = pix;
    make_cell(&e);
    for (y = 0; y < ch; y++) {
        UBYTE v = 0;
        for (x = 0; x < 8; x++)
            if (pix[y * 8 + x] == 0xFFFFFF)
                v |= 0x80 >> x;
        lines[y] = v;
    }
    slot = pp_add(lines);
    return slot < 0 ? 0 : slot;
}

static ULONG *pp_glyph(ULONG cp, UWORD st)
{
    ULONG h;
    if ((cp <= ' ' || cp == VT_WIDE_TAIL) &&
        !(st & (VT_UNDERLINE | VT_STRIKE)))
        return pp_blank;
    if (cp == VT_WIDE_TAIL || cp < ' ')
        cp = ' ';
    if (cp < 256 && !st) {
        if (!pp_lat[cp])
            pp_lat[cp] = PP_BLOCK(pp_make(cp, 0));
        return pp_lat[cp];
    }
    h = (cp * 2654435761UL) ^ ((ULONG)st << 7);
    h = (h ^ (h >> 15)) & (PPH - 1);
    while (pph[h].slot != 0xFFFF) {
        if (pph[h].cp == cp && pph[h].st == st)
            return PP_BLOCK(pph[h].slot);
        h = (h + 1) & (PPH - 1);
    }
    pph[h].cp = cp;
    pph[h].st = st;
    pph[h].slot = pp_make(cp, st);
    return PP_BLOCK(pph[h].slot);
}

/* For this screen and font? (painter_setup) */
static void pp_setup(void)
{
    struct BitMap *bm = scr->RastPort.BitMap;
    ppon = (planar || chunky) && !usett && cw == 8 && ch <= 32 && bm &&
           (chunky || (bm->Depth <= 8 &&
                       /* real planes: a card's bitmap with no
                        * cybergraphics emulation is not */
                       (GetBitMapAttr(bm, BMA_FLAGS) & BMF_STANDARD))) &&
           cols < PPSLOTS - 8 && panes_maxcols() < PPSLOTS - 8 && !nopp;
    if (!ppon) {
        pp_free();
        pp_font = NULL;
        return;
    }
    if (pp_font != font || pp_fch != ch || pp_fbase != cbase) {
        if (!pp_init(ch, PPSLOTS)) {
            ppon = FALSE;
            pp_font = NULL;
            return;
        }
        pp_font = font;
        pp_fch = ch;
        pp_fbase = cbase;
        pp_reset();
    }
    /* with no fast RAM (a stock A1200) this code and its data are read
     * from chip RAM against the display, and for a row in one colour
     * the blitter behind Text() is as quick; the painter wins where the
     * colours change (measured, conbench sgr-perchar 5.4 -> 4.0 s) */
    pp_all = (TypeOfMem((APTR)pp_setup) & MEMF_FAST) != 0;
#ifdef PPALL
    pp_all = TRUE;                      /* to measure: the painter always */
#endif
#ifdef PPCHIP
    pp_all = FALSE;                     /* to test: as with chip RAM only */
#endif
}

/* A run's pens as the asm wants them (fx<<8 | fb), its glyph style, and
 * the planes they need: called only where the colours change. Kept per
 * look for the draw (rp_forget starts a new one): a page of colour runs
 * is the same few looks row after row, and working one out (its RGB, two
 * pens) was ~1000 cycles on a 14 MHz 68020 - most of a colourful row's
 * set-up (conbench sgr-perchar: 40 a row). */
static struct {
    const void *v;                      /* the emulator: its pen numbers */
    ULONG gen;
    UWORD pen, pw, st;
    UBYTE used;
} ppp[64];

static __attribute__((noinline)) UWORD pp_pens(const VtCell *c, BOOL insel,
                                               UWORD *st, UBYTE *used)
{
    ULONG fg, bg;
    LONG fp, bp;
    UWORD s, pw;
    UBYTE u;
    int k = c->pen & 63;
    if (!insel && ppp[k].gen == ppp_gen && ppp[k].pen == c->pen &&
        ppp[k].v == vt) {
        *st = ppp[k].st;
        *used |= ppp[k].used;
        return ppp[k].pw;
    }
    cell_rgb(c, &fg, &bg);
    if (insel) {
        ULONG t = fg; fg = bg; bg = t;
    }
    fp = pen_for(fg);
    bp = pen_for(bg);
    u = (UBYTE)(fp ^ pbase) | (UBYTE)(bp ^ pbase);
    *used |= u;
    s = PEN(c)->attr & (VT_BOLD | VT_ITALIC | VT_UNDERLINE | VT_STRIKE);
    if (pcmode)
        s &= ~VT_BOLD;
    *st = s;
    pw = (UWORD)(((fp ^ bp) & 255) << 8 | (bp & 255));
    if (!insel) {
        ppp[k].v = vt;
        ppp[k].gen = ppp_gen;
        ppp[k].pen = c->pen;
        ppp[k].pw = pw;
        ppp[k].st = s;
        ppp[k].used = u;
    }
    return pw;
}

/* Columns a..b of row r into the planes. FALSE: not here (the window
 * is covered there, say) - the caller draws them with Text(). From
 * column 0 the margin left of it is painted with the row when it lies in
 * the row's first long (pp_lm: done; draw_span's RectFill was ~0.3 ms a
 * row on a 14 MHz 68020, ~1.4% of conbench). */
static BOOL pp_lm;

static BOOL pp_cells(int r, int a, int b)
{
    if (chunky)
        return FALSE;
    static const VtCell empty = { 0, 0 };
    struct Layer *ly = rp->Layer;
    struct BitMap *bm = rp->BitMap;
    struct ClipRect *cr;
    const VtCell *line, *prev = NULL;
    UWORD pw = 0, st = 0;
    UBYTE used = 0;
    BOOL psel = FALSE;
    LONG X, Y, L, R;
    int n = b - a + 1, i, w, s0 = -1, s1 = -2, a0 = a, b0 = b;
    if (!ppon || !ly || !bm || n <= 0 || n > PP_MAXCOLS)
        return FALSE;
    PF_START(tset);
    X = ly->bounds.MinX + ox + a * 8;
    Y = ly->bounds.MinY + oy + r * ch;
    LockLayerRom(ly);
    for (cr = ly->ClipRect; cr; cr = cr->Next)
        if (!cr->obscured && cr->bounds.MinX <= X &&
            cr->bounds.MaxX >= X + 8 * n - 1 && cr->bounds.MinY <= Y &&
            cr->bounds.MaxY >= Y + ch - 1)
            break;
    if (!cr) {
        UnlockLayerRom(ly);
        return FALSE;
    }
    /* The asm writes whole longs: the first and last reach up to 31
     * pixels past the span, read and written back. Those must be ours
     * (this layer, locked) and on this line of the bitmap (a row of
     * 2 mod 4 bytes: the last long's end is the next line's start) -
     * else another window's drawing between the read and the write was
     * undone. Cells whose long is not all ours go to Text(). */
    L = cr->bounds.MinX;
    R = cr->bounds.MaxX;
    if (R > bm->BytesPerRow * 8 - 1)
        R = bm->BytesPerRow * 8 - 1;
    /* panes: nor into the pane beside (its pixels are ours, but its
     * drawing is its own); the window's outer edges as before */
    if (npanes > 1) {
        if (cpane->x0 > win->BorderLeft &&
            ly->bounds.MinX + cpane->x0 > L)
            L = ly->bounds.MinX + cpane->x0;
        if (cpane->x1 < panes_right() && ly->bounds.MinX + cpane->x1 < R)
            R = ly->bounds.MinX + cpane->x1;
    }
    while (a <= b && ((ly->bounds.MinX + ox + a * 8) & ~31) < L)
        a++;
    while (b >= a && ((ly->bounds.MinX + ox + b * 8 + 7) | 31) > R)
        b--;
    if (a > b) {
        UnlockLayerRom(ly);
        return FALSE;
    }
    n = b - a + 1;
    X = ly->bounds.MinX + ox + a * 8;
    if (pp_room() < n)
        pp_reset();
    line = vt_line(vt, r, back, &w);
    sel_span(r, &s0, &s1);
    if (s1 >= a && s0 <= b) {
        /* marked cells in the span: cell by cell */
        for (i = 0; i < n; i++) {
            int x = a + i;
            const VtCell *c = x < w && line ? &line[x] : &empty;
            BOOL insel = x >= s0 && x <= s1;
            if (!prev || c->pen != prev->pen || insel != psel) {
                pw = pp_pens(c, insel, &st, &used);
                psel = insel;
            }
            prev = c;
            pp_gp[i] = pp_glyph(c->cp, st);
            pp_pw[i] = pw;
        }
    } else {
        /* the common case, kept small and in registers: on a stock
         * A1200 every memory access is a chip access (the stack too) */
        ULONG **gp = pp_gp, **lat = pp_lat;
        UWORD *pwp = pp_pw;
        const VtCell *c = line ? line + a : &empty;
        int k = line ? (w - 1 < b ? w - a : n) : 0;
        ULONG cpn = ~0UL;
        for (i = 0; i < k; i++, c++) {
            ULONG cp, *g;
            if (c->pen != cpn) {
                pw = pp_pens(c, FALSE, &st, &used);
                cpn = c->pen;
            }
            cp = c->cp;
            if (cp > 255 || st || !(g = lat[cp]))
                g = pp_glyph(cp, st);
            *gp++ = g;
            *pwp++ = pw;
        }
        if (i < n) {                    /* past the row's end: blanks */
            pw = pp_pens(&empty, FALSE, &st, &used);
            for (; i < n; i++) {
                *gp++ = pp_blank;
                *pwp++ = pw;
            }
        }
    }
    if (pbase >= 0 && (pmask | used) != pmask) {
        pmask |= used;                  /* as mask_pen: scrolls carry them */
        SetWriteMask(rp, pmask);
    }
    WaitBlit();                         /* a scroll may still be moving */
    PF_ADD(pf_ppset, tset);
    {
    int lead = a == 0 ? ox - cpane->fx0 : 0;
    PF_START(tasm);
    pp_lm = pp_paint(bm->Planes, bm->Depth, pbase >= 0 ? pmask : 0xFF,
                     bm->BytesPerRow, X, Y, n, lead,
                     (UWORD)(pen_for(defbg) & 255));
    PF_ADD(pf_ppasm, tasm);
    }
#ifdef PROFILE
    pf_ppnum++;
#endif
    UnlockLayerRom(ly);
    if (a > a0 || b < b0) {             /* the edges' cells: Text() */
        BOOL keep = ppon;
        ppon = FALSE;
        if (a > a0)
            draw_cells(r, a0, a - 1);
        if (b < b0)
            draw_cells(r, b + 1, b0);
        ppon = keep;
    }
    return TRUE;
}

/* The text area's edges: the margin left of column 0 and whatever is
 * right of the last column. TrueType letters can reach a pixel into the
 * margin (a "y" at the start of a line), so a row clears its edges too. */
int panes_right(void)
{
    return win->Width - win->BorderRight - 1 - (fullscreen && scrollbar ? POSBAR + 2 : 0);
}

/* The pane's: the whole text area, or the pane's inside its frame. */
static int area_right(void)
{
    return cpane->fx1;
}
/* The pane's marks: rows shown blank; the painter's row r painted in
 * draw pass pgen */
#define shown_blank (cpane->blank)
#define painted (cpane->painted)
#define pgen (cpane->pgen)

static inline BOOL fresh_row(int r)
{
    return r < (int)sizeof painted && painted[r] == pgen;
}

/* A cell that shows only the background: both ways of drawing skip
 * these. Blink is in: a PC's blink is a bright background. The text's
 * colour does not show on a space. The painter draws strike and
 * underline into its cell pictures, Text() with the font's style and a
 * line - the same places, two ways (planes against pixels). */
static inline BOOL blank_cell(const VtCell *c)
{
    const VtPen *p;
    if (c->cp && c->cp != ' ')
        return FALSE;
    if (!c->pen)
        return TRUE;                    /* the default look */
    p = PEN(c);
    return p->bg == VT_DEFAULT &&
           !(p->attr & (VT_INVERSE | VT_UNDERLINE | VT_STRIKE | VT_BLINK));
}

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
            while (last >= a && blank_cell(&line[last]))
                last--;
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
    pp_lm = FALSE;
    if (last >= a) {
        PF_START(tt);
        if (!pp_all || (line && VT_ROW_PICS(line)) || !pp_cells(r, a, last))
            draw_cells(r, a, last);
        PF_ADD(pf_text, tt);
    }
    {
    PF_START(tf);
    apen(defbg);
    if (a == 0 && !pp_lm)
        RectFill(rp, cpane->fx0, y, ox - 1, y + ch - 1);
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

void draw_row(int r)
{
    if (r < 0 || r >= rows || !win)     /* hidden: nothing is drawn */
        return;
    if (painter) {
        paint_row(r);                   /* the cursor comes with the row */
        return;
    }
    draw_span(r, 0, cols - 1);
}

/* The cursor's shape: the program's (0 block, 1 underline, 2 bar), or
 * 3 - an outline - in a pane that is not the active one. */
static int cur_shape(int *blink)
{
    int s = vt_cursor_shape(vt, blink);
    return npanes > 1 && cpane != apane ? 3 : s;
}

/* An underline, bar (DECSCUSR) or outline cursor over cell x,y, in its
 * letter's colour. */
static void cursor_mark(int x, int y)
{
    int w, blink, shape = cur_shape(&blink);
    const VtCell *line = vt_line(vt, y, 0, &w);
    VtCell c;
    ULONG fg, bg;
    int x0 = ox + x * cw, y0 = oy + y * ch;
    if (line && x < w)
        c = line[x];
    else
        memset(&c, 0, sizeof c);
    cell_rgb(&c, &fg, &bg);
    apen(fg);
    if (shape == 1) {
        int t = ch / 8 > 2 ? ch / 8 : 2;
        RectFill(rp, x0, y0 + ch - t, x0 + cw - 1, y0 + ch - 1);
    } else if (shape == 3) {
        RectFill(rp, x0, y0, x0 + cw - 1, y0);
        RectFill(rp, x0, y0 + ch - 1, x0 + cw - 1, y0 + ch - 1);
        RectFill(rp, x0, y0, x0, y0 + ch - 1);
        RectFill(rp, x0 + cw - 1, y0, x0 + cw - 1, y0 + ch - 1);
    } else {
        int t = cw / 4 > 2 ? cw / 4 : 2;
        RectFill(rp, x0, y0, x0 + t - 1, y0 + ch - 1);
    }
}

void draw_cursor(BOOL on)
{
    int x, y, vis;
    if (!win)
        return;
    if (!on)
        rp_forget();                    /* a draw starts: Ed or More may
                                           have used the window since */
    vt_cursor(vt, &x, &y, &vis);
    if (painter) {
        /* the rows carry the cursor: repaint where it was and where it
         * is - unless painted since draw_cursor(FALSE), when the cursor
         * already stood where it stands now */
        if (on) {
            if (cury >= 0 && cury < rows && cury != y && !fresh_row(cury))
                paint_row(cury);
            if (y < rows && !fresh_row(y))
                paint_row(y);
            curx = x;
            cury = y;
        }
        if (++pgen == 0) {              /* rows painted from here on are
                                           not known to carry the cursor */
            memset(painted, 0, sizeof painted);
            pgen = 1;                   /* old marks never match again */
        }
        return;
    }
    if (curx >= 0 && cury >= 0 && cury < rows && curx < cols) {
        draw_cells(cury, curx, curx);       /* where it was: plain again */
        curx = cury = -1;
    }
    if (!on || !vis || back > 0 || x >= cols || y >= rows)
        return;
    {
        int blink;
        if (cur_shape(&blink)) {      /* an underline, a bar, an outline */
            draw_cells(y, x, x);
            cursor_mark(x, y);
            curx = x;
            cury = y;
            return;
        }
    }
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
        if (usett)
            c.cp = tt_stand_in(c.cp);   /* as the row has it: no box */
        if (usett && c.cp > ' ' && !is_special(c.cp)) {
            ULONG u = c.cp;
            int col = x;
            tt_run(&u, &col, 1, oy + y * ch, bg, PEN(&c)->attr);
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

ULONG *rowbuf;                   /* one text row: cols*cw x ch */
static LONG rowbuf_n;                   /* its size in bytes */
/* The cell cache: one table of single cells and a smaller one of wide
 * ones (each picture only as big as its cell). Entries: about one per
 * two cells of the window, a power of 2 from 64 to 4096, and never more
 * than a quarter of the largest free block - smaller again if that
 * fails. 8x16 at 80x25: 1024 + 128 wide, 0.6 MB (it was 4096 pictures
 * of 2 cells each, 4 MB; TrueType at 72 pixels 120 MB). */
static struct CEntry *ccache, *wcache;
static ULONG *cpool, *wpool;
static int cmask, wmask;                /* entries - 1 */
static BOOL cache_small;                /* cut down: memory was short */

/* What the pictures were made for: the same again needs no new ones
 * (a tab switch asks; colours are part of each entry's key). */
static struct {
    struct TextFont *font;
    UWORD fx, fy;
    int cw, ch, cbase, ttsize;
    BOOL usett, ttaa;
    char ttfile[sizeof ttfile];
    int fallgen;                        /* the fallback face (fonts.c) */
} made;

void cache_free(void)
{
    if (ttbuf) { FreeVec(ttbuf); ttbuf = NULL; }
    if (ccache) { FreeVec(ccache); ccache = NULL; }
    if (wcache) { FreeVec(wcache); wcache = NULL; }
    if (cpool) { FreeVec(cpool); cpool = NULL; }
    if (wpool) { FreeVec(wpool); wpool = NULL; }
}

static LONG cache_want(void)
{
    LONG n = 64, cells = (LONG)cols * rows / 2;
    while (n < cells && n < CMAX)
        n <<= 1;
    return n;
}

/* The cache still right for the letters and the window? */
static BOOL cache_fits(void)
{
    if (!ccache || made.font != font || made.fx != font->tf_XSize ||
        made.fy != font->tf_YSize || made.cw != cw || made.ch != ch ||
        made.cbase != cbase || made.usett != usett)
        return FALSE;
    if (usett && (made.ttsize != ttsize || made.ttaa != ttaa ||
                  strcmp(made.ttfile, ttfile) || made.fallgen != fallgen))
        return FALSE;
    return cache_small || cache_want() <= cmask + 1;
}

static BOOL cache_alloc(void)
{
    LONG cl = (LONG)cw * ch, n, nw, i;
    ULONG room = AvailMem(MEMF_ANY | MEMF_LARGEST) / 4;
    cache_free();
    cache_small = FALSE;
    for (n = cache_want(); n >= 64; n >>= 1, cache_small = TRUE) {
        nw = n / 8 < 16 ? 16 : n / 8;
        if ((ULONG)(n + 2 * nw) * cl * 4 > room)
            continue;
        ccache = AllocVec(n * sizeof *ccache, MEMF_ANY | MEMF_CLEAR);
        wcache = AllocVec(nw * sizeof *wcache, MEMF_ANY | MEMF_CLEAR);
        cpool = AllocVec(n * cl * 4, MEMF_ANY);
        wpool = AllocVec(nw * 2 * cl * 4, MEMF_ANY);
        if (ccache && wcache && cpool && wpool)
            break;
        cache_free();
    }
    if (!ccache)
        return FALSE;
    for (i = 0; i < n; i++)
        ccache[i].pix = cpool + i * cl;
    for (i = 0; i < nw; i++)
        wcache[i].pix = wpool + i * 2 * cl;
    cmask = n - 1;
    wmask = nw - 1;
    made.font = font;
    made.fx = font->tf_XSize;
    made.fy = font->tf_YSize;
    made.cw = cw;
    made.ch = ch;
    made.cbase = cbase;
    made.usett = usett;
    made.ttaa = ttaa;
    made.ttsize = ttsize;
    strcpy(made.ttfile, ttfile);
    made.fallgen = fallgen;
    return TRUE;
}

/* A letter of the bitmap font, from its own bits (no window needed). */
static void cell_bitmap_glyph(UBYTE c, ULONG fg, UWORD attr)
{
    struct TextFont *f = font;
    int lo = f->tf_LoChar, hi = f->tf_HiChar, y, b, w, off, i = c;
    ULONG loc;
    if (i < lo || i > hi)
        i = hi + 1;                     /* the font's "missing" glyph: an
                                           int, 256 when HiChar is 255 */
    loc = ((ULONG *)f->tf_CharLoc)[i - lo];
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
    APTR face = tt_for(attr);
    if (tt_missing(cp)) {               /* the fallback font's (fonts.c) */
        face = w > cw ? ttfall : ttfall1;
        u = tt_fallcp(cp);
    }
    pm = TT_GetPixmap(face, (APTR)&u, 1, TT_Encoding,
                      TT_Encoding_UTF32_BE, TT_Antialias,
                      ttaa ? TT_Antialias_On : TT_Antialias_Off,
                      TAG_END);
    if (!pm)
        return;
    /* the picture is the letter's ink only: it goes where the ink sits
     * on the baseline (measured: an "n" is 8 rows, an "E" 10 - put both
     * at the cell's top and the text jitters) */
    TT_SetFont(rp, face);
    c_ttfont = face;                    /* tt_run's memo: what is set now */
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
    } else if (cp >= 0x2800 && cp <= 0x28FF) {
        draw_braille(cp, 0, 0);
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
    struct CEntry *t = wide ? wcache : ccache, *e;
    int m = wide ? wmask : cmask, k;
    ULONG h;
    /* only what the picture shows (inverse, dim, hidden are in the
     * colours already): the same look is one entry */
    attr &= VT_BOLD | VT_ITALIC | VT_UNDERLINE | VT_STRIKE;
    h = (cp * 2654435761UL) ^ (fg * 0x9E37UL) ^ (bg * 0x85EBUL) ^
        ((ULONG)attr << 7);
    h ^= h >> 15;                       /* the multiply's good bits down */
    for (k = 0; k < 8; k++) {
        e = &t[(h + k) & m];
        if (!e->used)
            goto make;
        if (e->cp == cp && e->fg == fg && e->bg == bg && e->attr == attr) {
            e->used = 2;                /* asked for again: kept longer */
            return e;
        }
    }
    /* full there: the first not asked for again since it was made or
     * last passed over (a second chance), else the first */
    for (k = 0; k < 8; k++) {
        e = &t[(h + k) & m];
        if (e->used == 1)
            goto make;
        e->used = 1;
    }
    e = &t[h & m];
make:
    e->cp = cp; e->fg = fg; e->bg = bg; e->attr = attr;
    e->wide = wide; e->used = 1;
    make_cell(e);
    return e;
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

/* One text row on a 256-colour card: per cell and line, the glyph's byte
 * becomes eight pens through a 16-entry table of nibbles (four pens a
 * long) for the cell's colours, made again only when the colours change. */
static void paint_row8(int r, int cx, int cy, int cshape, int ps0, int ps1)
{
    static const VtCell empty = { 0, 0 };
    static ULONG nib[16];
    static LONG nfg = -1, nbg = -1;
    int w, x, rw = cols * 8, ml = rw / 4;
    const VtCell *line = vt_line(vt, r, back, &w), *prev = NULL;
    UBYTE *rb = (UBYTE *)rowbuf;
    LONG fp = 0, bp = 0;
    UWORD st = 0;
    BOOL rpics = line && VT_ROW_PICS(line);
    if (pp_room() < cols)
        pp_reset();
    for (x = 0; x < cols; x++) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        BOOL rev = (r == cy && x == cx && cshape == 0) ^ (x >= ps0 && x <= ps1);
        const UBYTE *g;
        ULONG *d;
        int y;
        if (rpics && VT_IS_PIC(c->cp)) {
            pic_pens8(rb + x * 8, rw, c->cp, ox + x * 8, oy + r * ch);
            prev = NULL;
            continue;
        }
        if (rev || !prev || c->pen != prev->pen) {
            ULONG fg, bg;
            cell_rgb(c, &fg, &bg);
            if (rev) {
                ULONG t = fg; fg = bg; bg = t;
            }
            fp = pen_for(fg) & 255;
            bp = pen_for(bg) & 255;
            st = PEN(c)->attr & (VT_BOLD | VT_ITALIC | VT_UNDERLINE | VT_STRIKE);
            if (pcmode)
                st &= ~VT_BOLD;
            if (fp != nfg || bp != nbg) {
                int k;
                for (k = 0; k < 16; k++)
                    nib[k] = (k & 8 ? fp : bp) << 24 | (k & 4 ? fp : bp) << 16 |
                             (k & 2 ? fp : bp) << 8 | (k & 1 ? fp : bp);
                nfg = fp;
                nbg = bp;
            }
        }
        prev = rev ? NULL : c;
        g = pp_lines(pp_glyph(c->cp, st));
        d = (ULONG *)(rb + x * 8);
        for (y = 0; y < ch; y++, d += ml) {
            UBYTE b = g[y];
            d[0] = nib[b >> 4];
            d[1] = nib[b & 15];
        }
    }
    WriteChunkyPixels(rp, ox, oy + r * ch, ox + rw - 1, oy + r * ch + ch - 1,
                      rb, rw);
    if (r == cy && cshape && cx < cols)
        cursor_mark(cx, cy);            /* an underline or a bar on top */
}

/* One text row: put together from cell pictures, written in one go. */
static void paint_row(int r)
{
    int ps0 = -1, ps1 = -2, cshape, blink;
    static const VtCell empty = { 0, 0 };
    int w, x, cx = -1, cy = -1, vis = 0, rw = cols * cw;
    const VtCell *line = vt_line(vt, r, back, &w);
    const VtCell *prev = NULL;
    ULONG pfg = 0, pbg = 0;
    struct CEntry *pe = NULL;
    BOOL bk, rpics = line && VT_ROW_PICS(line);
    if (r < 0 || r >= rows)
        return;
    if (r < (int)sizeof painted)
        painted[r] = pgen;
    /* the picture under this row - all of it: a tiny window keeps at
     * least 10x3 cells, which can reach past the window (and the
     * backdrop); such a row goes without */
    bk = backdrop && ox >= bkx && ox + rw <= bkx + bkw &&
         oy + r * ch >= bky && oy + r * ch + ch <= bky + bkh;
    vt_cursor(vt, &cx, &cy, &vis);
    cshape = cur_shape(&blink);
    if (!vis || back > 0)
        cy = -1;
    sel_span(r, &ps0, &ps1);
    /* a blank row (all default spaces, no cursor): one fill, or nothing */
    if (r != cy && !vt_reverse_video(vt) && ps1 < 0) {
        int blank = 1;
        for (x = 0; x < w && x < cols && line; x++)
            if (!blank_cell(&line[x])) {
                blank = 0;
                break;
            }
        if (blank) {
            if (r < (int)sizeof shown_blank && shown_blank[r])
                return;
            if (bk)
                backdrop_put(ox, oy + r * ch, rw, ch);
            else {
                apen(defbg);
                RectFill(rp, ox, oy + r * ch, ox + rw - 1,
                         oy + r * ch + ch - 1);
            }
            if (r < (int)sizeof shown_blank)
                shown_blank[r] = 1;
            return;
        }
    }
    if (r < (int)sizeof shown_blank)
        shown_blank[r] = 0;
    if (chunky) {
        paint_row8(r, cx, cy, cshape, ps0, ps1);
        return;
    }
    for (x = 0; x < cols; x++) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        UBYTE wide = 0;
        struct CEntry *e;
        int y, n;
        ULONG *src, *dst;
        if (cp == VT_WIDE_TAIL)
            continue;                   /* drawn with its left half */
        if (rpics && VT_IS_PIC(cp)) {
            pic_argb(rowbuf + x * cw, rw, cp, bk ? backdrop + (oy + r * ch -
                     bky) * bkw + (ox + x * cw - bkx) : NULL, bkw);
            continue;
        }
        if (x + 1 < cols && x + 1 < w && line && line[x + 1].cp == VT_WIDE_TAIL)
            wide = 1;
        /* neighbours mostly share colours: work them out once a run */
        if (prev && c->pen == prev->pen) {
            fg = pfg;
            bg = pbg;
        } else {
            cell_rgb(c, &fg, &bg);
            pfg = fg;
            pbg = bg;
            pe = NULL;
        }
        prev = c;
        if ((r == cy && x == cx && cshape == 0) ^ (x >= ps0 && x <= ps1)) {
            /* the cursor (a block), or a marked cell: reversed */
            ULONG t = fg; fg = bg; bg = t;
            pe = NULL;
            prev = NULL;
        }
        if (usett)
            cp = tt_stand_in(cp);
        if (cp < ' ')
            cp = ' ';
        n = wide ? 2 * cw : cw;
        if (x * cw + n > rw)
            n = rw - x * cw;
        dst = rowbuf + x * cw;
        if (bk && bg == defbg) {
            /* over the picture: the cell as a mask (white on nothing),
             * its letter's colour mixed onto the backdrop by it */
            const ULONG *bk = backdrop + (oy + r * ch - bky) * bkw +
                              (ox + x * cw - bkx);
            e = cell_get(cp, 0xFFFFFF, MASKKEY,
                         pcmode ? PEN(c)->attr & ~VT_BOLD : PEN(c)->attr, wide);
            pe = NULL;
            src = e->pix;
            for (y = 0; y < ch; y++) {
                int k;
                for (k = 0; k < n; k++) {
                    int a = src[k] & 255;
                    ULONG b = bk[k];
                    if (a == 0)
                        dst[k] = b;
                    else if (a == 255)
                        dst[k] = fg;
                    else {
                        ULONG o = 0;
                        int sh;
                        for (sh = 0; sh <= 16; sh += 8) {
                            int bb = (b >> sh) & 255, ff = (fg >> sh) & 255;
                            o |= (ULONG)(bb + (ff - bb) * a / 255) << sh;
                        }
                        dst[k] = o;
                    }
                }
                src += wide ? 2 * cw : cw;
                dst += rw;
                bk += bkw;
            }
            continue;
        }
        if (pe && pe->cp == cp && !wide && !pe->wide && pe->fg == fg &&
            pe->bg == bg)
            e = pe;                     /* the same cell again (spaces) */
        else
            e = pe = cell_get(cp, fg, bg,
                              pcmode ? PEN(c)->attr & ~VT_BOLD : PEN(c)->attr, wide);
        src = e->pix;
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
    if (r == cy && cshape && cx < cols)
        cursor_mark(cx, cy);            /* an underline or a bar on top */
}

/* Use the painter on this screen? And the buffers it needs. */
void painter_setup(void)
{
    struct BitMap *bm;
    LONG need = (LONG)(panes_maxcols() > cols ? panes_maxcols() : cols) *
                cw * ch;                /* a row of the widest pane */
    if (!scr)
        return;
    bm = scr->RastPort.BitMap;
    painter = chunky = FALSE;
    planar = !CyberGfxBase || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX);
    if (!planar && GetCyberMapAttr(bm, CYBRMATTR_DEPTH) <= 8 && !usett &&
        cw == 8 && !nopp) {
        /* a graphics card in 256 colours: rows of pens, made from the
         * glyph store, one WriteChunkyPixels each - never ScrollRaster,
         * which reads the card's memory back (~10 ms a line, PiStorm) */
        cache_free();
        backdrop_free();
        chunky = TRUE;
        pp_setup();
        if (ppon && need > rowbuf_n) {
            if (rowbuf) FreeVec(rowbuf);
            rowbuf = AllocVec(need, MEMF_ANY);  /* a byte a pixel */
            rowbuf_n = rowbuf ? need : 0;
        }
        painter = ppon && rowbuf_n >= need;
        if (!painter)
            chunky = FALSE;
        return;
    }
    if (planar || GetCyberMapAttr(bm, CYBRMATTR_DEPTH) < 15) {
        cache_free();                   /* no pictures: the planes, or Text() */
        pp_setup();
        return;
    }
    pp_setup();                         /* off: not planar */
    if (!cache_fits() && !cache_alloc())
        return;
    /* sizes in bytes: a 256-colour screen's buffer (a byte a pixel) is
     * a quarter of what this one needs at the same size */
    if (need * 4 > rowbuf_n) {
        if (rowbuf) FreeVec(rowbuf);
        rowbuf = AllocVec(need * 4, MEMF_ANY);
        rowbuf_n = rowbuf ? need * 4 : 0;
    }
    painter = rowbuf != NULL && rowbuf_n >= need * 4;
    backdrop_make();                    /* the picture for this window */
}

/* After a font, colour or tab change: new pictures only if the letters
 * differ (colours are in each entry's key), the backdrop only if the
 * background or window did. Cheap when nothing changed. */
void painter_forget(void)
{
    if (win)
        painter_setup();
    else
        cache_free();
}

/* ---- the scroll bar (from AmiClaude: see intuition-border-gadgets.md) - */

/* What the knob or the place bar shows now: every refresh asks, and
 * mostly nothing moved (NewModifyProp redraws the gadget). Forgotten
 * when the window is drawn afresh. */
static LONG k_total = -1, k_top, k_rows;

struct Gadget vgad, agup, agdn;
struct PropInfo vpi;
static struct Image vim;
APTR iup, idn;
BOOL gadsok;
int arrowheld, arrowticks;

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

void add_scroller(void)
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
    k_total = -1;                       /* a new knob: set it */
}

/* A pane's own bar (split, Scroll bar on): the history as a track, the
 * part shown as the knob in its border's colour; nothing while there is
 * no history. Drawn when what it shows changed. */
static void pane_bar(struct Pane *p, LONG total, LONG top)
{
    int x0 = p->bx0, x1 = p->bx1, y0 = p->oy, h = rows * ch, act = p == apane, a, b;
    LONG keepbase = pbase;
    UBYTE keepmask = pmask;
    if (!x0 || (p->btotal == total && p->btop == top && p->brows == rows && p->bact == act))
        return;
    p->btotal = total; p->btop = top; p->brows = rows; p->bact = act;
    planes_full();
    rp_forget();
    if (!backdrop_put(x0, y0, x1 - x0 + 1, h)) {
        apen(defbg);
        RectFill(rp, x0, y0, x1, y0 + h - 1);
    }
    if (total > rows) {
        a = y0 + (LONG)h * top / total;
        b = y0 + (LONG)h * (top + rows) / total - 1;
        if (b < a + 3) b = a + 3;
        if (b > y0 + h - 1) b = y0 + h - 1;
        apen(mix(defbg, deffg, 15));
        if (a > y0)
            RectFill(rp, x0, y0, x1, a - 1);
        if (b < y0 + h - 1)
            RectFill(rp, x0, b + 1, x1, y0 + h - 1);
        apen(pane_border_rgb(act));
        RectFill(rp, x0, a, x1, b);
    }
    pbase = keepbase;
    pmask = keepmask;
    SetWriteMask(rp, pmask);
    rp_forget();
}

/* The scroll bar's knob, or full screen's place bar, says where we are;
 * split, each pane's own bar. */
void update_knob(void)
{
    LONG total = vt_history(vt) + rows, top = vt_history(vt) - back;
    if (!win)
        return;
    if (npanes > 1 && scrollbar) {
        pane_bar(cpane, total, top);
        if (fullscreen)
            return;                     /* no window bar to keep */
    }
    if (cpane != apane)
        return;                         /* the active pane's, only */
    if (total == k_total && top == k_top && rows == k_rows)
        return;
    if (fullscreen && !scrollbar)
        return;                         /* no bar: Settings, Scroll bar off */
    if (fullscreen && win) {
        int x0 = win->Width - posbar_w(), y0 = oy, h = rows * ch, a, b;
        LONG keepbase = pbase;
        UBYTE keepmask = pmask;
        /* all planes, the text's mask kept: the bar's own pens would
         * widen it, and every scroll would then move more planes (the
         * bar is outside what scrolls and clears) */
        planes_full();
        rp_forget();
        if (npanes > 1) {
            /* panes above each other: the bar beside the active one,
             * the rest of its column cleared */
            int ty = win->BorderTop + tabbar_h(),
                by = win->Height - win->BorderBottom - 1;
            if (!backdrop_put(x0 - 1, ty, win->Width - x0 + 1, by - ty + 1)) {
                apen(defbg);
                RectFill(rp, x0 - 1, ty, win->Width - 1, by);
            }
        } else if (!backdrop_put(x0 - 1, y0, win->Width - x0 + 1, h)) {
            apen(defbg);
            RectFill(rp, x0 - 1, y0, win->Width - 1, y0 + h - 1);
        }
        if (vt_history(vt) > 0) {
            a = y0 + (LONG)h * top / total;
            b = y0 + (LONG)h * (top + rows) / total - 1;
            if (b < a + 3) b = a + 3;
            apen(mix(defbg, deffg, 15));
            if (a > y0)                 /* above and below the knob */
                RectFill(rp, x0, y0, win->Width - 3, a - 1);
            if (b < y0 + h - 1)
                RectFill(rp, x0, b + 1, win->Width - 3, y0 + h - 1);
            apen(mix(defbg, deffg, 60));
            RectFill(rp, x0, a, win->Width - 3, b);
        }
        pbase = keepbase;
        pmask = keepmask;
        SetWriteMask(rp, pmask);
        rp_forget();
    } else if (!gadsok || (vpi.Flags & KNOBHIT)) {
        k_total = -1;                   /* dragged: set it when let go */
        return;
    } else
        NewModifyProp(&vgad, win, NULL, vpi.Flags, 0,
                      total > rows ? (0xFFFFUL * top) / (total - rows) : 0, 0,
                      (0xFFFFUL * rows) / total, 1);
    k_total = total;
    k_top = top;
    k_rows = rows;
}

void knob_forget(void)
{
    k_total = -1;                       /* another tab's: set it afresh */
}

/* Every plane for a drawing outside the text (the tab bar, the panes'
 * frames), the text's mask kept: drawing those pens would widen it, and
 * every scroll would then move more planes. */
static LONG sv_base;
static UBYTE sv_mask;

void planes_save(void)
{
    sv_base = pbase;
    sv_mask = pmask;
    planes_full();
    rp_forget();
}

void planes_restore(void)
{
    pbase = sv_base;
    pmask = sv_mask;
    SetWriteMask(rp, pmask);
    rp_forget();
}

/* ---- layout ------------------------------------------------------------ */

/* The tab bar: a row of names above the text, while there are tabs -
 * not with panes (the user: in pane mode the tabs are on show already) */
/* workspaces with tabs, the shown one counted */
static int ws_inuse(void)
{
    int n, k = 0;
    for (n = 0; n < NWS; n++)
        if (n == curws || ws_used(n))
            k++;
    return k;
}

int tabbar_h(void)
{
    struct Tab *here[MAXTABS];
    if (!font)
        return 0;
    return (ws_tabs(here) > 1 && npanes < 2) || ws_inuse() > 1 ? font->tf_YSize + 4 : 0;
}

/* The bar's parts: the workspaces' boxes at the left (while more than one
 * is in use), each " n "; the shown workspace's tabs share the rest (not
 * with panes: they are on show). */
static int wsbox_w(void) { return 3 * (font && font->tf_XSize ? font->tf_XSize : 8) + 2; }

int tabbar_hit(int x, int *ws)
{
    struct Tab *here[MAXTABS];
    int nh = ws_tabs(here), x0 = win->BorderLeft, x1 = win->Width - win->BorderRight - 1, n, i;
    *ws = -1;
    if (ws_inuse() > 1) {
        for (n = 0; n < NWS; n++) {
            if (n != curws && !ws_used(n))
                continue;
            if (x < x0 + wsbox_w()) { *ws = n; return -1; }
            x0 += wsbox_w();
        }
        x0 += 4;
    }
    if (nh < 2 || npanes > 1 || x < x0)
        return -1;
    i = (x - x0) * nh / (x1 - x0 + 1);
    return i >= 0 && i < nh ? tab_number(here[i]) : -1;
}

void layout_and_resize(void)
{
    int i;
    if (!win)
        return;                         /* hidden: the sizes stay */
    panes_layout();                     /* every pane's place and size */
    for (i = 0; i < ntabs; i++) {
        /* a tab in a pane takes its size; one in none the active pane's
         * (it is shown there when chosen) */
        struct Pane *p = pane_of(tabs[i]);
        if (tabs[i]->t_ws != curws)
            continue;                   /* another workspace's: its size kept */
        if (!p)
            p = apane;
        if (tabs[i]->t_vt)              /* pictures: the cell in pixels */
            vt_set_cell_px(tabs[i]->t_vt, cw, ch, defbg);
        /* (one behind a filled window keeps its size) */
        if (tabs[i]->t_vt && !pane_kept(tabs[i]) &&
            (p->cols != vt_cols(tabs[i]->t_vt) ||
             p->rows != vt_rows(tabs[i]->t_vt))) {
            vt_resize(tabs[i]->t_vt, p->cols, p->rows);
            con_resized(tabs[i]->con);  /* the line being typed moved */
        }
    }
    pane_use(apane);
}

void tab_label(struct Tab *t, char *out)
{
    if (t->t_local)
        strcpy(out, "Shell");
    else if (t->t_bookname[0])
        strcpy(out, t->t_bookname);
    else if (t->t_dialhost[0]) {
        strcpy(out, t->t_fkind == FK_SFTP ? "sftp " : t->t_fkind == FK_FTPS
                    ? "ftps " : t->t_fkind ? "ftp "
                    : t->t_sshmode ? "ssh " : "");
        strncat(out, t->t_dialhost, 40);
    } else
        strcpy(out, "relay");
    if (t->t_xfer) {                    /* a transfer: how far, in the name */
        struct Xfer *x = t->t_xfer;
        ULONG all = x->bsize ? x->bsize : x->size,
              got = x->bsize ? x->bdone : x->done;
        strcat(out, x->dir == 1 ? " v" : x->dir == 2 ? " ^" : " ^v");
        if (all) {
            numstr_cat(out, (LONG)pct100(got, all));
            strcat(out, "%");
        }
    }
}

void draw_tabbar(void)
{
    int h = tabbar_h(), x0, y0, x1, wd, i;
    if (!h || !win)
        return;
    x0 = win->BorderLeft;
    y0 = win->BorderTop;
    x1 = win->Width - win->BorderRight - 1;
    wd = x1 - x0 + 1;
    planes_save();
    apen(mix(defbg, deffg, 12));
    RectFill(rp, x0, y0, x1, y0 + h - 1);
    SetFont(rp, font);
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (ws_inuse() > 1) {               /* the workspaces: the shown one lit */
        int n;
        for (n = 0; n < NWS; n++) {
            char t[4];
            BOOL on = n == curws;
            if (!on && !ws_used(n))
                continue;
            t[0] = ' '; t[1] = '1' + n; t[2] = ' '; t[3] = 0;
            apen(on ? pane_border_rgb(TRUE) : mix(defbg, deffg, 25));
            RectFill(rp, x0 + 1, y0 + 1, x0 + wsbox_w() - 2, y0 + h - 1);
            apen(on ? defbg : deffg);
            bpen(on ? pane_border_rgb(TRUE) : mix(defbg, deffg, 25));
            SetDrMd(rp, JAM2);
            Move(rp, x0 + 1, y0 + 2 + font->tf_Baseline);
            Text(rp, (STRPTR)t, 3);
            x0 += wsbox_w();
        }
        x0 += 4;
        wd = x1 - x0 + 1;
    }
    {
    struct Tab *here[MAXTABS];
    int nh = npanes > 1 ? 0 : ws_tabs(here);
    if (nh < 2)
        nh = 0;                         /* one tab: no names to show */
    for (i = 0; i < nh; i++) {
        char label[64], t[76];
        /* the width shared equally: a half each, a third each... */
        int sx = x0 + i * wd / nh, slot = x0 + (i + 1) * wd / nh - sx;
        int n, fit;
        BOOL on = here[i] == shown;
        /* in another pane: as the shown one, its name a little dimmer */
        ULONG bg = on || pane_of(here[i]) ? defbg : mix(defbg, deffg, 12);
        tab_label(here[i], label);
        t[0] = 0;
        numstr_cat(t, i + 1);
        strcat(t, " ");
        strcat(t, label);
        apen(bg);
        RectFill(rp, sx + 1, y0 + 1, sx + slot - 2, y0 + h - 1);
        apen(on ? deffg : mix(defbg, deffg, pane_of(here[i]) ? 80 : 55));
        bpen(bg);
        SetDrMd(rp, JAM2);
        n = strlen(t);
        fit = (slot - 8) / (font->tf_XSize ? font->tf_XSize : 8);
        if (n > fit) n = fit;
        Move(rp, sx + 4, y0 + 2 + font->tf_Baseline);
        Text(rp, (STRPTR)t, n > 0 ? n : 0);
    }
    }
    if (usett)
        tt_attach();                    /* the window's font for TrueType */
    /* the text's mask again, as it was: narrowing it afresh here would
     * stop scrolls and clears from reaching planes the text uses (it is
     * drawn during transfers, not only after a full clear) */
    planes_restore();
}

/* ---- a picture behind the text --------------------------------------- */

struct Library *DataTypesBase = NULL;
static ULONG *ld_buf;                   /* the loader's result */
static ULONG bggen;                     /* counts pictures loaded */
static LONG ld_w, ld_h;
static const char *ld_path;

/* Read a picture whole, as ARGB, through datatypes (picture.datatype V43:
 * PDTM_READPIXELARRAY), on the stack it was given. */
static void __attribute__((noinline)) load_pic_(void)
{
    Object *o;
    struct BitMapHeader *bmh = NULL;
    LONG y;
    ld_buf = NULL;
    if (!DataTypesBase)
        DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    if (!DataTypesBase)
        return;
    o = NewDTObject((APTR)ld_path, DTA_SourceType, DTST_FILE,
                    DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
                    PDTA_Remap, FALSE, TAG_DONE);
    if (!o)
        return;
    DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, NULL, 1);
    GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
    if (bmh && bmh->bmh_Width > 0 && bmh->bmh_Height > 0) {
        ld_w = bmh->bmh_Width;
        ld_h = bmh->bmh_Height;
        ld_buf = AllocVec(ld_w * ld_h * 4, MEMF_ANY);
        for (y = 0; ld_buf && y < ld_h; y++)
            if (!DoMethod(o, PDTM_READPIXELARRAY, (ULONG)(ld_buf + y * ld_w),
                          PBPAFMT_ARGB, ld_w * 4, 0, y, ld_w, 1)) {
                FreeVec(ld_buf);
                ld_buf = NULL;
            }
    }
    DisposeDTObject(o);
}

/* The datatype classes run on the caller's stack and fail quietly on a
 * small one (Knowledge/amiga/datatypes-and-media.md): a 64K stack of our
 * own when ours is smaller. */
BOOL load_picture(const char *path)
{
    struct Task *me = FindTask(NULL);
    LONG have = (char *)me->tc_SPUpper - (char *)me->tc_SPLower;
    ld_path = path;
    if (have >= 32768)
        load_pic_();
    else {
        static struct StackSwapStruct sss;
        UBYTE *stk = AllocVec(65536, MEMF_ANY);
        if (!stk)
            return FALSE;
        sss.stk_Lower = stk;
        sss.stk_Upper = (ULONG)(stk + 65536);
        sss.stk_Pointer = (APTR)(stk + 65536);
        StackSwap(&sss);
        load_pic_();
        StackSwap(&sss);
        FreeVec(stk);
    }
    if (!ld_buf)
        return FALSE;
    if (bgsrc)
        FreeVec(bgsrc);
    bgsrc = ld_buf;
    bgsw = ld_w;
    bgsh = ld_h;
    bggen++;                            /* maybe at the old one's address */
    return TRUE;
}

/* A piece of the picture behind the text (a gap between panes): FALSE
 * when there is none there. */
BOOL backdrop_area(LONG x, LONG y, LONG w, LONG h)
{
    return backdrop_put(x, y, w, h);
}

void backdrop_free(void)
{
    if (backdrop)
        FreeVec(backdrop);
    backdrop = NULL;
}

/* The window's inner area: the picture covering it (centred, cut, not
 * stretched out of shape), mixed into the background colour. */
void backdrop_make(void)
{
    LONG x, y, sx0, sy0, scw, sch;
    static ULONG mbg, mgen;             /* what the backdrop was made of */
    static int msee;
    if (!painter || !bgsrc || !win || bgsee <= 0) {
        backdrop_free();
        return;
    }
    /* the same picture, window and colour (a tab switch): kept - mixing
     * it costs a multiply per pixel */
    if (backdrop && bkx == win->BorderLeft && bky == win->BorderTop &&
        bkw == win->Width - win->BorderLeft - win->BorderRight &&
        bkh == win->Height - win->BorderTop - win->BorderBottom &&
        mbg == defbg && msee == bgsee && mgen == bggen)
        return;
    backdrop_free();
    bkx = win->BorderLeft;
    bky = win->BorderTop;
    bkw = win->Width - win->BorderLeft - win->BorderRight;
    bkh = win->Height - win->BorderTop - win->BorderBottom;
    mbg = defbg;
    msee = bgsee;
    mgen = bggen;
    if (bkw <= 0 || bkh <= 0)
        return;
    backdrop = AllocVec(bkw * bkh * 4, MEMF_ANY);
    if (!backdrop)
        return;
    /* cover: the part of the picture with the window's shape */
    if (bgsw * bkh > bgsh * bkw) {
        sch = bgsh;
        scw = bgsh * bkw / bkh;
    } else {
        scw = bgsw;
        sch = bgsw * bkh / bkw;
    }
    sx0 = (bgsw - scw) / 2;
    sy0 = (bgsh - sch) / 2;
    for (y = 0; y < bkh; y++) {
        const ULONG *srow = bgsrc + (sy0 + y * sch / bkh) * bgsw + sx0;
        ULONG *d = backdrop + y * bkw;
        for (x = 0; x < bkw; x++) {
            ULONG p = srow[x * scw / bkw], o = 0;
            int sh;
            for (sh = 0; sh <= 16; sh += 8) {
                int a = (defbg >> sh) & 255, b = (p >> sh) & 255;
                o |= (ULONG)(a + (b - a) * bgsee / 100) << sh;
            }
            d[x] = o;
        }
    }
}

/* The backdrop's piece at window x,y, w x h, onto the window. */
static BOOL backdrop_put(LONG x, LONG y, LONG w, LONG h)
{
    if (!backdrop || x < bkx || y < bky || x + w > bkx + bkw ||
        y + h > bky + bkh)
        return FALSE;                   /* not all on the picture */
    WritePixelArray(backdrop, x - bkx, y - bky, bkw * 4, rp, x, y, w, h,
                    RECTFMT_ARGB);
    return TRUE;
}

/* The pane in use (T its tab) drawn whole, over a cleared place. */
static void redraw_pane(void)
{
    int r;
    VtChanges chg;
    term_sync(T);
    forget_shown();
    vt_take_changes(vt, &chg);
    curx = cury = -1;
    for (r = 0; r < rows; r++)
        draw_row(r);
    draw_cursor(TRUE);
    update_knob();
    if (T->t_xfer)
        xfer_panel();
    find_bar();
}

/* The whole window afresh: the tab bar, every pane, what is between. */
void redraw_all(void)
{
    struct Tab *keep = T;
    int i;
    term_sync(T);
    if (!win)
        return;                         /* hidden: drawn when shown */
    rp_forget();
    planes_full();
    if (!backdrop_put(bkx, bky, bkw, bkh)) {
        apen(defbg);
        RectFill(rp, win->BorderLeft, win->BorderTop,
                 win->Width - win->BorderRight - 1,
                 win->Height - win->BorderBottom - 1);
    }
    planes_reset();                     /* all background: no plane yet */
    draw_tabbar();
    k_total = -1;                       /* the place bar was cleared */
    for (i = 0; i < npanes; i++)
        if (panes[i].tab) {
            pane_use(&panes[i]);
            T = panes[i].tab;
            redraw_pane();
        }
    pane_use(apane);
    T = keep;
    panes_frames();
    float_draw();                       /* the scratchpad, on top */
}

void redraw_pane_now(void) { redraw_pane(); }

/* IDCMP_REFRESHWINDOW: only what the damage reaches is drawn again (a
 * requester or another window moved off a corner, not the whole text).
 * The damage is read between BeginRefresh and EndRefresh, then drawn
 * without the damage clip, as an ordinary draw - whole rows, with the
 * cursor handled as refresh() does. */
void repair_damage(void)
{
    struct Region *dl;
    struct Rectangle d;
    struct Tab *keep = T;
    BOOL some;
    int i, x0, x1;
    term_sync(T);
    BeginRefresh(win);
    dl = win->WLayer->DamageList;       /* in the window's coordinates */
    some = dl && dl->RegionRectangle;
    if (some)
        d = dl->bounds;
    EndRefresh(win, TRUE);
    if (!some) {                        /* nothing known: all of it */
        redraw_all();
        return;
    }
    x0 = d.MinX > win->BorderLeft ? d.MinX : win->BorderLeft;
    x1 = d.MaxX < win->Width - win->BorderRight - 1 ? d.MaxX
       : win->Width - win->BorderRight - 1;
    if (d.MinY < win->BorderTop) d.MinY = win->BorderTop;
    if (d.MaxY > win->Height - win->BorderBottom - 1)
        d.MaxY = win->Height - win->BorderBottom - 1;
    rp_forget();
    if (x0 <= x1 && d.MinY <= d.MaxY) {
        /* the damage afresh in the background, with all planes: the
         * planes outside the text's mask must hold the background */
        planes_save();
        if (!backdrop_put(x0, d.MinY, x1 - x0 + 1, d.MaxY - d.MinY + 1)) {
            apen(defbg);
            RectFill(rp, x0, d.MinY, x1, d.MaxY);
        }
        planes_restore();
    }
    if (d.MinY < win->BorderTop + tabbar_h())
        draw_tabbar();
    for (i = 0; i < npanes; i++) {
        struct Pane *p = &panes[i];
        VtChanges chg;
        BOOL changed;
        int r, r0, r1;
        if (!p->tab)
            continue;
        pane_use(p);
        T = p->tab;
        term_sync(T);
        /* text that moved and is not drawn yet (a scroll then would
         * carry rows drawn here): the whole pane, as before */
        changed = vt_take_changes(vt, &chg);
        if (changed && (chg.all || chg.scroll_n)) {
            redraw_pane();
            continue;
        }
        if (x1 < p->x0 || x0 > p->x1 || d.MaxY < p->y0 || d.MinY > p->y1)
            r0 = rows, r1 = -1;         /* not damaged: its changes only */
        else {
            r0 = d.MinY < oy ? 0 : (d.MinY - oy) / ch;
            r1 = (d.MaxY - oy) / ch;
        }
        if (r1 >= rows) r1 = rows - 1;
        if (r0 > r1 && !changed)
            continue;
        draw_cursor(FALSE);
        for (r = 0; r < rows; r++)
            if ((r >= r0 && r <= r1) || (changed && chg.dirty[r])) {
                if (r < (int)sizeof shown_blank)
                    shown_blank[r] = 0; /* what it showed is gone */
                draw_row(r);
            }
        draw_cursor(TRUE);
        update_knob();
        if (T->t_xfer)
            xfer_panel();
        find_bar();
    }
    pane_use(apane);
    T = keep;
    panes_frames();
    float_draw();
}

/* Bring the window up to date with the emulator: one blit for a scroll,
 * then only the rows that changed. */
static void refresh_pane(void);

/* T's pane up to date, if it is in one (else drawn when it is shown). */
void refresh(void)
{
    struct Pane *p, *was = cpane;
    term_sync(T);                       /* what the Shell wrote, parsed */
    if (!win || !(p = pane_of(T)) || pane_dragging())
        return;                         /* hidden, or a divider pulled (its
                                         * line would be drawn over): the
                                         * changes wait */
    if (p == was)
        refresh_pane();
    else {
        pane_use(p);
        refresh_pane();
        pane_use(was);
    }
}

/* Every pane up to date (the frame timer, a host's bytes). */
void refresh_all(void)
{
    struct Tab *keep = T;
    int i;
    for (i = 0; i < npanes; i++)
        if (panes[i].tab) {
            T = panes[i].tab;
            refresh();
        }
    if (fshown && fpane.tab) {          /* the scratchpad */
        T = fpane.tab;
        refresh();
    }
    T = keep;
}

/* After a draw of the only pane: the plane mask down to the planes the
 * text on show needs. The mask only ever grew: after one page of colours
 * every scroll moved all four planes of a 16-colour Workbench (a 616x224
 * ScrollRaster: 12 ms a plane on a 14 MHz A1200) until the next full
 * clear. Nothing is cleared: every row was drawn with all the mask's
 * planes, so a plane none of its looks uses holds the background's bits
 * there, as the planes outside the mask do - only the mask shrinks, for
 * the next scrolls and draws (were it done before this draw, a changed
 * span drawn without a plane its old letters used would leave them).
 * Looked at only while the mask holds more than plain text needs, at
 * every full redraw and draw that scrolled - after a look that found
 * colours still on show, not at the next two scrolls (a look at all the
 * cells is ~3 ms, a scroll 12 ms a plane). Not with pictures, panes, the scratchpad or a panel
 * over the rows (their own pens and areas). */
static void planes_fit(BOOL full)
{
    static const VtCell empty = { 0, 0 };
    static int skip;
    UBYTE need = 0;
    UWORD st;
    int r, x, w;
    if (pbase < 0 || npanes > 1 || fshown)
        return;
    pp_pens(&empty, FALSE, &st, &need);     /* blanks, the rows' tails */
    if (!(pmask & ~need))
        return;
    if (!full && skip > 0) {
        skip--;                         /* colours were on show just now */
        return;
    }
    skip = 2;
    for (r = 0; r < rows; r++) {
        const VtCell *line = vt_line(vt, r, back, &w);
        UWORD kp = 0;                   /* the default look: counted */
        if (!line)
            continue;
        if (VT_ROW_PICS(line))
            return;
        if (w > cols)
            w = cols;
        for (x = 0; x < w; x++)
            if (line[x].pen != kp) {
                kp = line[x].pen;
                pp_pens(&line[x], FALSE, &st, &need);
            }
        if (!(pmask & ~need))
            return;                     /* nothing to give up */
    }
    skip = 0;
    pmask &= need;
    SetWriteMask(rp, pmask);
}

/* The pane in use, T its tab. */
static void refresh_pane(void)
{
    VtChanges chg;
    int r;
    BOOL xp;
    static int syncwait;
    /* the program is between "begin" and "end" of a frame (2026): show
     * the finished one - but never hold the window more than ~8 frames */
    if (vt_sync(vt) && syncwait < 8) {
        syncwait++;
        owed = TRUE;
        con_arm_render(20000);
        return;
    }
    syncwait = 0;
    if (!vt_take_changes(vt, &chg))
        return;
    if (chg.scroll_n && float_over(cpane))
        chg.all = 1;                    /* a blit would carry what is under the scratchpad */
    xp = (T->t_xfer && !T->t_fkind) || find_prompt(T);
    if (xp)
        chg.all = 1;                    /* a scroll would carry the panel */
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
            ScrollRaster(rp, 0, chg.scroll_n * ch, cpane->fx0,
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
    if (xp) {
        xfer_panel();                   /* over the last rows again */
        find_bar();
    } else if (chg.all || back > 0 || chg.scroll_n)
        planes_fit(chg.all || back > 0);
}

/* What came from the host, as it came (escape codes and all: a BBS
 * screen plays back from it), into the tab's log. */
/* A host's bytes on their way to the screen: into the log, and to an
 * ARexx WAITFOR. */
void log_bytes(const UBYTE *b, LONG n)
{
    if (T->t_log)
        FWrite(T->t_log, (APTR)b, 1, n);
    if (T->t_watch)
        rexx_feed(T, b, n);
}

/* ---- a transfer's panel: over the tab's last three rows ---------------
 * The text under it is left as it is (no resize: the host would hear of
 * it), and drawn again when the transfer ends. */

/* done of all in per cent, in whole numbers (no floating point: a stock
 * A1200 has no FPU, and CTerm links no maths library) */
ULONG pct100(ULONG done, ULONG all)
{
    if (!all)
        return 0;
    if (done >= all)
        return 100;
    return all > 0x00FFFFFFUL ? done / (all / 100) : done * 100 / all;
}

static void bar(int x, int y, int w, int h, ULONG done, ULONG all)
{
    int k = all ? (int)((ULONG)w * pct100(done, all) / 100) : 0;
    apen(mix(defbg, deffg, 25));
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    if (k > 2) {
        apen(deffg);                    /* apart from the rest on 4 pens too */
        RectFill(rp, x + 1, y + 1, x + k - 2, y + h - 2);
    }
}

static void ptext(int x, int y, const char *s, int maxw)
{
    int n = strlen(s), fit = font->tf_XSize ? maxw / font->tf_XSize : n;
    if (n > fit)
        n = fit > 0 ? fit : 0;
    Move(rp, x, y + font->tf_Baseline);
    Text(rp, (STRPTR)s, n);
}

static void kb(char *out, ULONG v)
{
    if (v >= 10UL * 1024 * 1024) {
        numstr_cat(out, (LONG)(v >> 20));
        strcat(out, " MB");
    } else {
        numstr_cat(out, (LONG)(v >> 10));
        strcat(out, " KB");
    }
}

static void xfer_panel_(void);

/* Find in history's line, over the last row of T's pane while it lasts
 * (as less's prompt): the title is not seen in full screen. */
void find_bar(void)
{
    const char *s = find_prompt(T);
    struct Pane *p = pane_of(T), *was = cpane;
    int y0;
    if (!s || !p || !win || rows < 2)
        return;
    if (p != was)
        pane_use(p);
    y0 = oy + (rows - 1) * ch;
    rp_forget();
    SetWriteMask(rp, 0xFF);
    SetDrMd(rp, JAM1);
    apen(mix(defbg, deffg, 20));
    RectFill(rp, ox, y0, ox + cols * cw - 1, y0 + ch - 1);
    apen(deffg);
    ptext(ox + 2, y0 + (ch > font->tf_YSize ? (ch - font->tf_YSize) / 2 : 0),
          s, cols * cw - 4);
    if (p != was)
        pane_use(was);
}

/* T's transfer panel, over the last rows of its pane (if in one). */
void xfer_panel(void)
{
    struct Pane *p = pane_of(T), *was = cpane;
    if (!p || !win || !T->t_xfer || T->t_fkind)
        return;                         /* a browser shows its own */
    if (p == was)
        xfer_panel_();
    else {
        pane_use(p);
        xfer_panel_();
        pane_use(was);
    }
}

static void xfer_panel_(void)
{
    struct Xfer *x = T->t_xfer;
    int lh, y0, x0, w, bw, tx;
    ULONG secs, rate = 0, left;
    char s[200];
    if (!x || !win || rows < 4)
        return;
    lh = ch > font->tf_YSize + 2 ? ch : font->tf_YSize + 2;
    y0 = oy + rows * ch - 3 * lh;
    x0 = ox;
    w = cols * cw;
    rp_forget();
    SetWriteMask(rp, 0xFF);             /* all planes: the text's mask would */
    SetDrMd(rp, JAM1);                  /* keep old letters under the fill */
    apen(mix(defbg, deffg, 10));
    RectFill(rp, x0, y0, x0 + w - 1, y0 + 3 * lh - 1);
    apen(mix(defbg, deffg, 35));
    RectFill(rp, x0, y0, x0 + w - 1, y0);
    secs = (ticks50() - x->t0) / 50;
    if (secs)
        rate = x->bdone / secs;
    /* 1: what, where */
    apen(deffg);
    strcpy(s, x->dir == 1 ? "Receiving " : x->dir == 2 ? "Sending "
                                                       : "Hydra: ");
    strcat(s, x->name[0] ? x->name : "...");
    if (x->dir != 2 && x->where[0]) {
        strcat(s, " into ");
        strncat(s, x->where, 60);
    }
    strcat(s, "  (");
    strcat(s, x->proto);
    strcat(s, ")");
    ptext(x0 + 4, y0 + 2, s, w - 8);
    /* 2: the file */
    bw = w / 3;
    tx = x0 + 4 + bw + 8;
    bar(x0 + 4, y0 + lh + 2, bw, lh - 4, x->done, x->size);
    s[0] = 0;
    kb(s, x->done);
    if (x->size) {
        strcat(s, " of ");
        kb(s, x->size);
        strcat(s, "  ");
        numstr_cat(s, (LONG)pct100(x->done, x->size));
        strcat(s, "%");
    }
    if (rate) {
        strcat(s, "  ");
        numstr_cat(s, (LONG)(rate / 1024));
        strcat(s, " KB/s");
        if (x->size > x->done) {
            left = (x->size - x->done) / rate;
            strcat(s, "  ");
            numstr_cat(s, (LONG)(left / 60));
            strcat(s, left % 60 < 10 ? ":0" : ":");
            numstr_cat(s, (LONG)(left % 60));
            strcat(s, " left");
        }
    }
    apen(deffg);
    ptext(tx, y0 + lh + 1, s, x0 + w - tx - 4);
    /* 3: the batch, and Stop */
    bar(x0 + 4, y0 + 2 * lh + 2, bw, lh - 4, x->bdone, x->bsize);
    strcpy(s, "file ");
    numstr_cat(s, x->files + 1);
    if (x->nfiles) {
        strcat(s, " of ");
        numstr_cat(s, x->nfiles);
    }
    strcat(s, ", ");
    kb(s, x->bdone);
    if (x->bsize) {
        strcat(s, " of ");
        kb(s, x->bsize);
    }
    strcat(s, " in all");
    apen(deffg);
    {
        int sw = 8 * (font->tf_XSize ? font->tf_XSize : 8);
        x->stopx1 = x0 + w - 4;
        x->stopx0 = x->stopx1 - sw;
        x->stopy0 = y0 + 2 * lh + 1;
        x->stopy1 = y0 + 3 * lh - 2;
        ptext(tx, y0 + 2 * lh + 1, s, x->stopx0 - tx - 8);
        apen(mix(defbg, deffg, 30));
        RectFill(rp, x->stopx0, x->stopy0, x->stopx1, x->stopy1);
        apen(deffg);
        ptext(x->stopx0 + (sw - 4 * font->tf_XSize) / 2, x->stopy0, "Stop",
              sw);
    }
    SetWriteMask(rp, pmask);            /* the text's own again */
    rp_forget();
}

/* A click on the shown tab's Stop: TRUE when it was one. */
BOOL xfer_click(int mx, int my)
{
    int i;
    for (i = 0; i < npanes; i++) {      /* a Stop in any pane */
        struct Xfer *x = panes[i].tab && !panes[i].tab->t_fkind
                         ? panes[i].tab->t_xfer : NULL;
        if (x && mx >= x->stopx0 && mx <= x->stopx1 && my >= x->stopy0 &&
            my <= x->stopy1) {
            x->stop = 1;
            return TRUE;
        }
    }
    return FALSE;
}
