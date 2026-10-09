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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>

#include <string.h>
#include <stdlib.h>

#include "vt.h"
#include "boxtable.h"

static const char version[] = "$VER: CTerm 0.1b1 (9.10.2026)";

struct Library *SocketBase;
struct Library *KeymapBase;
struct Library *GadToolsBase;
struct Library *AslBase;
struct Library *DiskfontBase;

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
static struct Window *win;
static struct RastPort *rp;
static struct TextFont *font, *ownfont;
static char fontname[48];
static int fontsize;
static int cw, ch, cbase;               /* the cell */
static int ox, oy, cols, rows;          /* where the grid starts, its size */
static APTR visual;
static struct Menu *menu;
static BOOL want_switch;
static int want_mode, want_depth;
static BOOL want_full, want_bar;

static Vt *vt;
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
    for (i = 0; i < npc; i++)
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

static void release_pens(void)
{
    while (npc > 0)
        ReleasePen(scr->ViewPort.ColorMap, pc_pen[--npc]);
}

/* What a cell's colours come to, as RGB. */
static void cell_rgb(const VtCell *c, ULONG *fg, ULONG *bg)
{
    ULONG f = c->fg == VT_DEFAULT ? deffg : VT_IS_INDEX(c->fg)
            ? index_rgb(c->fg & 255) : c->fg & 0xFFFFFF;
    ULONG b = c->bg == VT_DEFAULT ? defbg : VT_IS_INDEX(c->bg)
            ? index_rgb(c->bg & 255) : c->bg & 0xFFFFFF;
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

static void hline(int x0, int x1, int y, int th)
{
    RectFill(rp, x0, y, x1, y + th - 1);
}

static void vline(int x, int y0, int y1, int th)
{
    RectFill(rp, x, y0, x + th - 1, y1);
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
            reach = th - th / 2;
        }
        for (k = 0; k < n; k++) {
            int o = off[k];
            switch (i) {
            case 0: RectFill(rp, cx + o, y, cx + o + th - 1, cy + reach); break;
            case 2: RectFill(rp, cx + o, cy - reach, cx + o + th - 1,
                             y + ch - 1); break;
            case 1: RectFill(rp, cx - reach, cy + o, x + cw - 1,
                             cy + o + th - 1); break;
            case 3: RectFill(rp, x, cy + o, cx + reach, cy + o + th - 1); break;
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
    if (cp == 0x2580) { RectFill(rp, x, y, x + cw - 1, y + hh - 1); return; }
    if (cp >= 0x2581 && cp <= 0x2588) {          /* lower eighths */
        i = (cp - 0x2580) * ch / 8;
        RectFill(rp, x, y + ch - i, x + cw - 1, y + ch - 1);
        return;
    }
    if (cp >= 0x2589 && cp <= 0x258F) {          /* left eighths */
        i = (0x2590 - cp) * cw / 8;
        RectFill(rp, x, y, x + i - 1, y + ch - 1);
        return;
    }
    if (cp == 0x2590) { RectFill(rp, x + hw, y, x + cw - 1, y + ch - 1); return; }
    if (cp >= 0x2591 && cp <= 0x2593) {          /* shades: blended */
        SetAPen(rp, pen_for(mix(bg, fg, (int)(cp - 0x2590) * 25)));
        RectFill(rp, x, y, x + cw - 1, y + ch - 1);
        return;
    }
    if (cp == 0x2594) { RectFill(rp, x, y, x + cw - 1, y + ch / 8); return; }
    if (cp == 0x2595) { RectFill(rp, x + cw - cw / 8 - 1, y, x + cw - 1,
                                 y + ch - 1); return; }
    if (cp >= 0x2596) {
        UBYTE q = quads[cp - 0x2596];          /* 8 UL, 4 UR, 2 LL, 1 LR */
        if (q & 8) RectFill(rp, x, y, x + hw - 1, y + hh - 1);
        if (q & 4) RectFill(rp, x + hw, y, x + cw - 1, y + hh - 1);
        if (q & 2) RectFill(rp, x, y + hh, x + hw - 1, y + ch - 1);
        if (q & 1) RectFill(rp, x + hw, y + hh, x + cw - 1, y + ch - 1);
    }
}

static void disc(int cx, int cy, int r, BOOL leftonly)
{
    int dy;
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r)
            dx++;
        RectFill(rp, cx - dx, cy + dy, leftonly ? cx : cx + dx, cy + dy);
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
        Move(rp, cx + r, cy);
        return TRUE;
    case 0x25CB: case 0x25EF:                            /* ○ */
        disc(cx, cy, r, FALSE);
        return TRUE;
    case 0x25B6: case 0x23F5: case 0x25B8: case 0x25BA:  /* ▶ ⏵ */
        for (i = 0; i <= r; i++)
            RectFill(rp, cx - r / 2, cy - (r - i), cx - r / 2 + i, cy + (r - i));
        return TRUE;
    case 0x276F: case 0x203A: case 0x276D:               /* ❯ */
        for (i = 0; i <= r; i++) {
            RectFill(rp, cx - r / 2 + i, cy - r + i, cx - r / 2 + i + 1,
                     cy - r + i);
            RectFill(rp, cx - r / 2 + i, cy + r - i, cx - r / 2 + i + 1,
                     cy + r - i);
        }
        return TRUE;
    case 0x2026:                                         /* … */
        for (i = 0; i < 3; i++)
            RectFill(rp, x + 1 + i * (cw - 2) / 3, y + cbase - 1,
                     x + 1 + i * (cw - 2) / 3, y + cbase - 1);
        return TRUE;
    case 0x2193: case 0x2191:                            /* ↓ ↑ */
        vline(cx, y + 1, y + ch - 2, 1);
        for (i = 1; i <= r; i++) {
            int yy = cp == 0x2193 ? y + ch - 2 - i : y + 1 + i;
            RectFill(rp, cx - i, yy, cx + i, yy);
        }
        return TRUE;
    case 0x2192: case 0x2190:                            /* → ← */
        hline(x + 1, x + cw - 2, cy, 1);
        for (i = 1; i <= r; i++) {
            int xx = cp == 0x2192 ? x + cw - 2 - i : x + 1 + i;
            RectFill(rp, xx, cy - i, xx, cy + i);
        }
        return TRUE;
    case 0x23BF:                                         /* ⎿ like └ */
        draw_box(0x50, x, y);
        return TRUE;
    case 0x25A0: case 0x25AA: case 0x25FC:               /* ■ */
        RectFill(rp, cx - r, cy - r, cx + r, cy + r);
        return TRUE;
    case 0x25C6: case 0x2666:                            /* ◆ */
        for (i = -r; i <= r; i++)
            RectFill(rp, cx - (r - abs(i)), cy + i, cx + (r - abs(i)), cy + i);
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

static void draw_cells(int r, int x0, int x1)
{
    static const VtCell empty = { 0, VT_DEFAULT, VT_DEFAULT, 0 };
    static UBYTE text[512];
    int w, x, y = oy + r * ch;
    const VtCell *line = vt_line(vt, r, back, &w);

    SetDrMd(rp, JAM2);
    x = x0;
    while (x <= x1) {
        const VtCell *c = x < w && line ? &line[x] : &empty;
        ULONG fg, bg, cp = c->cp;
        int n = 0, start = x;
        UWORD style = (c->attr & VT_BOLD ? FSF_BOLD : 0) |
                      (c->attr & VT_ITALIC ? FSF_ITALIC : 0) |
                      (c->attr & VT_UNDERLINE ? FSF_UNDERLINED : 0);

        cell_rgb(c, &fg, &bg);
        if (cp > 0xFF && cp != VT_WIDE_TAIL) {
            /* one special cell: its background, then the shape */
            SetAPen(rp, pen_for(bg));
            RectFill(rp, ox + x * cw, y, ox + x * cw + cw - 1, y + ch - 1);
            SetAPen(rp, pen_for(fg));
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
                SetBPen(rp, pen_for(bg));
                SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
                Move(rp, ox + x * cw, y + cbase);
                Text(rp, (STRPTR)&ch1, 1);
            }
            x++;
            continue;
        }
        /* a run of ordinary characters that look the same */
        while (x <= x1 && n < (int)sizeof text) {
            const VtCell *d = x < w && line ? &line[x] : &empty;
            ULONG f2, b2;
            if (d->cp > 0xFF && d->cp != VT_WIDE_TAIL)
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
        SetAPen(rp, pen_for(fg));
        SetBPen(rp, pen_for(bg));
        SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        Move(rp, ox + start * cw, y + cbase);
        Text(rp, (STRPTR)text, n);
        if (c->attr & VT_STRIKE)
            hline(ox + start * cw, ox + x * cw - 1, y + ch / 2, 1);
    }
    SetSoftStyle(rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

static void draw_row(int r)
{
    if (r >= 0 && r < rows)
        draw_cells(r, 0, cols - 1);
}

static void draw_cursor(BOOL on)
{
    int x, y, vis;
    vt_cursor(vt, &x, &y, &vis);
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
        SetAPen(rp, pen_for(fg));
        RectFill(rp, ox + x * cw, oy + y * ch, ox + x * cw + cw - 1,
                 oy + y * ch + ch - 1);
        if (c.cp > ' ' && c.cp <= 0xFF) {
            t = (UBYTE)c.cp;
            SetDrMd(rp, JAM1);
            SetAPen(rp, pen_for(bg));
            Move(rp, ox + x * cw, oy + y * ch + cbase);
            Text(rp, (STRPTR)&t, 1);
        }
        curx = x;
        cury = y;
    }
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
        SetAPen(rp, pen_for(defbg));
        RectFill(rp, x0 - 1, y0, win->Width - 1, y0 + h - 1);
        if (vt_history(vt) > 0) {
            a = y0 + (LONG)h * top / total;
            b = y0 + (LONG)h * (top + rows) / total - 1;
            if (b < a + 3) b = a + 3;
            SetAPen(rp, pen_for(mix(defbg, deffg, 15)));
            RectFill(rp, x0, y0, win->Width - 3, y0 + h - 1);
            SetAPen(rp, pen_for(mix(defbg, deffg, 45)));
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
    w = win->Width - win->BorderLeft - win->BorderRight - 4 -
        (fullscreen ? POSBAR + 2 : 0);
    h = win->Height - win->BorderTop - win->BorderBottom - 2;
    cols = w / cw;
    rows = h / ch;
    if (cols < 10) cols = 10;
    if (rows < 3) rows = 3;
    if (cols != vt_cols(vt) || rows != vt_rows(vt))
        vt_resize(vt, cols, rows);
}

static void redraw_all(void)
{
    int r;
    VtChanges chg;
    SetAPen(rp, pen_for(defbg));
    RectFill(rp, win->BorderLeft, win->BorderTop,
             win->Width - win->BorderRight - 1,
             win->Height - win->BorderBottom - 1);
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
    draw_cursor(FALSE);
    if (chg.all || back > 0) {
        for (r = 0; r < rows; r++)
            draw_row(r);
    } else {
        if (chg.scroll_n) {
            SetBPen(rp, pen_for(defbg));
            ScrollRaster(rp, 0, chg.scroll_n * ch, ox,
                         oy + chg.scroll_top * ch, ox + cols * cw - 1,
                         oy + (chg.scroll_bottom + 1) * ch - 1);
        }
        for (r = 0; r < rows; r++)
            if (chg.dirty[r])
                draw_row(r);
    }
    draw_cursor(TRUE);
    update_knob();
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

static LONG lfd = -1, sock = -1;
static LONG port = DEFAULT_PORT;
static int tstate, tcmd;
static BOOL utf8 = TRUE;

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
        r[1] = (opt == O_NAWS || opt == O_BINARY) ? WILL : WONT;
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
            else if (c == SB) tstate = 3;
            else tstate = 0;
            break;
        case 2:
            if (k) { vt_write(vt, b, k); k = 0; }
            telnet_option(tcmd, c);
            tstate = 0;
            break;
        case 3:                         /* subnegotiation: skipped */
            if (c == IAC) tstate = 4;
            break;
        case 4:
            tstate = c == SE ? 0 : 3;
            break;
        }
    }
    if (k)
        vt_write(vt, b, k);
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
       M_SCR_DEEP, M_UTF8, M_ABOUT };

static struct NewMenu newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"About", 0, 0, 0, (APTR)M_ABOUT },
    {  NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Font...", 0, 0, 0, (APTR)M_FONT },
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
        case M_UTF8: on = utf8; break;
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
    if (!font && !load_font())
        return FALSE;

    if (fullscreen) {
        win = OpenWindowTags(NULL,
            WA_Left, 0, WA_Top, 0, WA_Width, scr->Width,
            WA_Height, scr->Height, WA_CustomScreen, (ULONG)scr,
            WA_Backdrop, TRUE, WA_Borderless, TRUE,
            WA_SimpleRefresh, TRUE, WA_Activate, TRUE,
            WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_END);
    } else {
        int top0 = scr->BarHeight + 1;
        int w = 80 * cw + 40, h = 25 * ch + 40;
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
    make_menus();
    if (!fullscreen && scrollbar)
        add_scroller();
    layout_and_resize();
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
            SetFont(rp, font);
            layout_and_resize();
            redraw_all();
            if (sock >= 0)
                send_naws();
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
    case M_ABOUT:
        say("\r\n\x1b[1mCTerm\x1b[0m 0.1b1 - a VT100/xterm terminal for the "
            "Amiga.\r\n");
        break;
    case M_UTF8:
        utf8 = !utf8;
        vt_set_utf8(vt, utf8);
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
        case IDCMP_CLOSEWINDOW: go = FALSE; break;
        case IDCMP_NEWSIZE:
            layout_and_resize();
            redraw_all();
            RefreshWindowFrame(win);
            if (sock >= 0)
                send_naws();
            break;
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(win);
            EndRefresh(win, TRUE);
            redraw_all();
            break;
        case IDCMP_MENUPICK: {
            UWORD code = copy.Code;
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

static void run(void)
{
    static UBYTE buf[16384];
    lfd = listen_on();
    if (lfd < 0)
        say("\x1b[1;31mCTerm cannot listen on its port - is it already "
            "running?\x1b[0m\r\n");
    else
        say("\x1b[1mCTerm\x1b[0m - waiting for the PC (cterm-relay)...\r\n");
    refresh();

    for (;;) {
        fd_set rd;
        LONG fd = sock >= 0 ? sock : lfd, n;
        ULONG winsig = 1UL << win->UserPort->mp_SigBit;
        ULONG sigs = winsig | SIGBREAKF_CTRL_C;
        FD_ZERO(&rd);
        if (fd >= 0)
            FD_SET(fd, &rd);
        n = WaitSelect(fd >= 0 ? fd + 1 : 0, fd >= 0 ? &rd : NULL, NULL,
                       NULL, NULL, &sigs);
        if (sigs & SIGBREAKF_CTRL_C)
            break;
        if ((sigs & winsig) && !window_events())
            break;
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

int main(int argc, char **argv)
{
    int rc = RETURN_OK;
    (void)version;
    (void)argv;
    if (argc > 0) {
        LONG args[2] = { 0, 0 };
        struct RDArgs *rda = ReadArgs("PORT/N,LATIN1/S", args, NULL);
        if (!rda) {
            PrintFault(IoErr(), "CTerm");
            return RETURN_FAIL;
        }
        if (args[0]) port = *(LONG *)args[0];
        if (args[1]) utf8 = FALSE;
        FreeArgs(rda);
    }
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    KeymapBase = OpenLibrary("keymap.library", 37);
    GadToolsBase = OpenLibrary("gadtools.library", 37);
    AslBase = OpenLibrary("asl.library", 38);
    DiskfontBase = OpenLibrary("diskfont.library", 36);
    vt = vt_new(80, 25, SCROLLBACK);
    if (vt)
        vt_set_utf8(vt, utf8);
    want_bar = scrollbar;
    if (!vt || !KeymapBase || !GadToolsBase || !SocketBase ||
        !open_display()) {
        rc = RETURN_FAIL;
    } else {
        run();
    }
    close_display(FALSE);
    if (ownfont) CloseFont(ownfont);
    vt_free(vt);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (KeymapBase) CloseLibrary(KeymapBase);
    if (SocketBase) CloseLibrary(SocketBase);
    return rc;
}
