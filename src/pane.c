/*
 * pane - the window split into panes, each showing a tab: side by side
 * or above each other, each split again as long as there is room (a
 * pane keeps 10 x 3 letters) and tabs (MAXTABS), as tmux, iTerm and i3
 * do. The panes are the leaves of a binary tree; the layout gives each
 * its rectangle and its text's size, and the tab in it hears that size
 * (NAWS, the ssh pty, the Shell's window bounds).
 *
 * Drawing (draw.c) works on one pane at a time, the pane in use (cpane):
 * ox, oy, cols, rows and the edges of its fills are that pane's, T its
 * tab. At rest that is the active pane (apane), whose tab is the shown
 * one: keys, the scroll bar, the selection and the title are its. A pane
 * drawn for another reason is taken into use, and the active one put
 * back after (pane_use).
 *
 * With the window split every pane wears the window's own letters (the
 * cells must be one size to share the window); a tab's own (the PC's
 * font, a phone book place's) come back when it is alone again.
 *
 * The i3 way (pane_do): a new Shell in a new pane split off the active
 * one in its direction (each pane keeps one, 'h' or 'v', and a pane
 * split off it inherits it), close a pane with its tab, focus and move
 * by geometry, fill the window with one pane (the layout kept aside,
 * the others running behind), all panes to tabs and all tabs to panes
 * (a grid; with an odd number the active tab has two cells).
 *
 */
#include "cterm.h"
#include <proto/layers.h>

#define DIVW gaps                       /* the room between two panes */
#define MAXNODES (2 * MAXPANES - 1)
#define MINC 10                         /* the least a pane keeps */
#define MINR 3

struct Pane panes[MAXPANES];
int npanes = 1;
BOOL dwindle;                           /* split by shape (PO_DWINDLE) */
BOOL tabmode;                           /* new Shells in tabs (M_SHNEW) */
int gaps;                               /* pixels between panes (prefs) */
BOOL outergaps = TRUE;                  /* and round them (prefs outergaps=0: not) */
int bordersize = 1;                     /* each pane's border (prefs border=) */
ULONG bordact, bordin;                  /* its colours, when set (ownbact, ownbin) */
BOOL ownbact, ownbin;

/* a pane's border colour: the active one's, or the others' */
ULONG pane_border_rgb(BOOL active)
{
    if (active)
        return ownbact ? bordact : themes[theme].wbpens[1];
    return ownbin ? bordin : mix(defbg, deffg, 25);
}

/* Full screen's position bar, at the window's right edge: POSBAR wide,
 * kept clear of the text by 2. Split with outer gaps it lives in the right
 * gap - narrower when the gap is (the right gap stays the others' size). */
int posbar_w(void)
{
    if (npanes > 1 && outergaps && gaps >= 3 && gaps < POSBAR + 2)
        return gaps - 1;
    return POSBAR;
}

/* A pane's own scroll bar under (x, y): its index, -1 none. */
int pane_bar_at(int x, int y)
{
    int i;
    for (i = 0; i < npanes; i++) {
        struct Pane *p = &panes[i];
        if (p->bx0 && x >= p->bx0 - 1 && x <= p->bx1 && y >= p->oy && y < p->oy + p->rows * ch)
            return i;
    }
    return -1;
}

/* room a pane keeps round its letters: the border, a pixel beside it, its bar */
#define PADW (2 * bordersize + 2 + (scrollbar ? PANEBAR + 1 : 0))
#define PADH (2 * bordersize)
struct Pane *apane = &panes[0];         /* the active pane: the shown tab's */
struct Pane *cpane = &panes[0];         /* the pane drawn now */

/* The tree: a node is a pane (a leaf) or a split in two. Node 0 is the
 * root. */
struct PNode {
    BYTE used, split;                   /* split: 0 a pane, 'h' side by
                                           side, 'v' above each other */
    BYTE pane;                          /* a leaf: its pane */
    BYTE a, b, up;                      /* a split's halves (left or top
                                           first); the parent, -1 none */
    WORD at;                            /* the divider, per mille */
    WORD x0, y0, x1, y1;                /* where it is now */
    WORD m;                             /* a split: the divider's first pixel */
};
static struct PNode nodes[MAXNODES] = { { 1, 0, 0, -1, -1, -1, 500 } };

/* The layout kept aside while one pane fills the window (PO_FULL): the
 * tree, each leaf's tab and direction, the active one. */
static BOOL zoomed;
static struct PNode znodes[MAXNODES];
static struct Tab *ztab[MAXPANES];
static UBYTE zdir[MAXPANES];
static int znpanes, zactive;

/* The first pane: the first tab, its cursor drawn nowhere yet. */
void panes_init(struct Tab *t)
{
    panes[0].tab = t;
    panes[0].curx = panes[0].cury = -1;
    panes[0].pgen = 1;
    panes[0].sdir = 'h';
}

static void zoom_out(void);
static void ws_forget(struct Tab *t);
static void float_tab_gone(struct Tab *t);
static int drag_clamp(int pos);
static void float_hide(void);
static void float_layout(void);
static void clip_set(int want);
void float_unfocus(void);
static void float_clip(struct Pane *p);
BOOL float_shows(struct Tab *t);
extern struct Pane fpane;
extern BOOL fshown;
static BOOL ws_switch(int n);
static BOOL ws_move(int n);
static int drag = -1;                   /* the split whose divider is pulled */
static int dragpos = -1;                /* where its line is drawn now */

void pane_use(struct Pane *p)
{
    float_clip(p);
    cpane = p;
    ox = p->ox;
    oy = p->oy;
    cols = p->cols;
    rows = p->rows;
}

struct Pane *pane_of(struct Tab *t)
{
    int i;
    if (float_shows(t))
        return &fpane;
    for (i = 0; i < npanes; i++)
        if (panes[i].tab == t)
            return &panes[i];
    return NULL;
}

/* Below: laying out, splitting, the i3 way - a key's or a resize's
 * work, never a write's: small before quick. */
#pragma GCC optimize("Os")

/* (The panes are a static array: a pointer to a block made at the
 * first split was the plan, but bebbo's gcc backend pass then drew
 * nothing after a window resize - every pane blank until the next full
 * redraw, verified in FS-UAE 2026-10-11; with -fbbb=- it drew. The
 * array costs MAXPANES panes always, ~8 KB.) */
static BOOL panes_room(int n)
{
    return n <= MAXPANES;
}

static int leaf_of(struct Pane *p)
{
    int n, k = p - panes;
    for (n = 0; n < MAXNODES; n++)
        if (nodes[n].used && !nodes[n].split && nodes[n].pane == k)
            return n;
    return 0;
}

static int node_new(void)
{
    int n;
    for (n = 1; n < MAXNODES; n++)
        if (!nodes[n].used) {
            memset(&nodes[n], 0, sizeof nodes[n]);
            nodes[n].used = 1;
            nodes[n].at = 500;
            return n;
        }
    return -1;
}

/* pane k, fresh, showing t */
static void pane_fresh(int k, struct Tab *t, int sdir)
{
    struct Pane *q = &panes[k];
    memset(q, 0, sizeof *q);
    q->tab = t;
    q->curx = q->cury = -1;
    q->pgen = 1;
    q->sdir = sdir ? sdir : 'h';
}

/* ---- the layout ---- */

static void lay(int n, int x0, int y0, int x1, int y1)
{
    struct PNode *d = &nodes[n];
    d->x0 = x0; d->y0 = y0; d->x1 = x1; d->y1 = y1;
    if (!d->split) {
        struct Pane *p = &panes[(int)d->pane];
        int edge = npanes > 1, minc = edge ? 2 : MINC, minr = edge ? 2 : MINR;
        int b = edge ? bordersize : 1;  /* alone: as the window always had */
        p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
        /* the text: inside the border and a pixel more at the sides */
        p->ox = x0 + b + 1;
        p->oy = y0 + b;
        p->bx0 = p->bx1 = 0;
        p->btotal = -1;
        if (edge && scrollbar) {        /* its own bar, inside the border at the right */
            p->bx1 = x1 - b;
            p->bx0 = p->bx1 - PANEBAR + 1;
        }
        p->cols = (x1 + 1 - p->ox - b - 1 - (p->bx0 ? PANEBAR + 1 : 0)) / cw;
        p->rows = (y1 - y0 + 1 - 2 * b) / ch;
        if (p->cols < minc) p->cols = minc;
        if (p->rows < minr) p->rows = minr;
        if (p->rows > PROWS) p->rows = PROWS;   /* the rows' marks */
        /* what a row's fills cover: the pane, but its frame (split) */
        p->fx0 = x0 + (edge ? b : 0);
        p->fx1 = x1 - (edge ? b : 0);
        return;
    }
    if (d->split == 'h') {
        int m = x0 + (LONG)(x1 - x0 + 1 - DIVW) * d->at / 1000;
        d->m = m;
        lay(d->a, x0, y0, m - 1, y1);
        lay(d->b, m + DIVW, y0, x1, y1);
    } else {
        int m = y0 + (LONG)(y1 - y0 + 1 - DIVW) * d->at / 1000;
        d->m = m;
        lay(d->a, x0, y0, x1, m - 1);
        lay(d->b, x0, m + DIVW, x1, y1);
    }
}

/* The text area's corners (all panes together). Split, with outer gaps:
 * the gap all round, the same as between two panes; the full screen's
 * position bar on the right sits in that gap when it is wide enough. */
static void area(int *x0, int *y0, int *x1, int *y1)
{
    /* split: every pane has its own bar, the window's position bar is not needed */
    int bar = fullscreen && scrollbar && npanes < 2 ? posbar_w() + 2 : 0, og = npanes > 1 && outergaps ? gaps : 0;
    if (og >= 3 && bar > og) bar = og;  /* the bar is in the gap (posbar_w) */
    *x0 = win->BorderLeft + og;
    *y0 = win->BorderTop + tabbar_h() + og;
    *x1 = win->Width - win->BorderRight - 1 - (og > bar ? og : bar);
    *y1 = win->Height - win->BorderBottom - 1 - og;
}

/* Every pane's place and size, from the window (layout_and_resize). */
void panes_layout(void)
{
    int x0, y0, x1, y1;
    area(&x0, &y0, &x1, &y1);
    lay(0, x0, y0, x1, y1);
    float_layout();
}

/* The widest pane's columns: the row buffers' size. */
int panes_maxcols(void)
{
    int i, m = 0;
    for (i = 0; i < npanes; i++)
        if (panes[i].cols > m)
            m = panes[i].cols;
    return m;
}

/* ---- drawing what is between the panes ---- */

static void frame(struct Pane *p, ULONG rgb)
{
    int b = bordersize;
    if (b <= 0)
        return;
    apen(rgb);
    RectFill(rp, p->x0, p->y0, p->x1, p->y0 + b - 1);
    RectFill(rp, p->x0, p->y1 - b + 1, p->x1, p->y1);
    RectFill(rp, p->x0, p->y0, p->x0 + b - 1, p->y1);
    RectFill(rp, p->x1 - b + 1, p->y0, p->x1, p->y1);
}

/* the window's background, or the picture behind, on a rectangle */
static void clear_rect(int x0, int y0, int x1, int y1)
{
    if (x1 < x0 || y1 < y0)
        return;
    if (!backdrop_area(x0, y0, x1 - x0 + 1, y1 - y0 + 1)) {
        apen(defbg);
        RectFill(rp, x0, y0, x1, y1);
    }
}

/* The dividers, and a thin frame in the theme's highlight round the
 * active pane (the others' in the background). Every plane, the text's
 * mask kept (as the tab bar). */
void panes_frames(void)
{
    int n, i;
    if (npanes < 2 || !win)
        return;
    float_clip(&panes[0]);              /* round the scratchpad */
    planes_save();
    if (outergaps && gaps > 0) {        /* the outer gaps (not the position bar) */
        int ax0, ay0, ax1, ay1, wx0 = win->BorderLeft, wy0 = win->BorderTop + tabbar_h();
        int wx1 = win->Width - win->BorderRight - 1;
        int wy1 = win->Height - win->BorderBottom - 1;
        area(&ax0, &ay0, &ax1, &ay1);
        clear_rect(wx0, wy0, wx1, ay0 - 1);
        clear_rect(wx0, ay1 + 1, wx1, wy1);
        clear_rect(wx0, ay0, ax0 - 1, ay1);
        clear_rect(ax1 + 1, ay0, wx1, ay1);
    }
    for (n = 0; n < MAXNODES; n++) {
        struct PNode *d = &nodes[n];
        if (!d->used || !d->split)
            continue;
        if (DIVW <= 0)
            continue;                   /* gaps 0: the borders touch */
        /* the gap: the window's background, or the picture behind */
        if (d->split == 'h') {
            if (!backdrop_area(d->m, d->y0, DIVW, d->y1 - d->y0 + 1)) {
                apen(defbg);
                RectFill(rp, d->m, d->y0, d->m + DIVW - 1, d->y1);
            }
        } else if (!backdrop_area(d->x0, d->m, d->x1 - d->x0 + 1, DIVW)) {
            apen(defbg);
            RectFill(rp, d->x0, d->m, d->x1, d->m + DIVW - 1);
        }
    }
    /* every pane its border: the active one in the highlight, the
     * others dimmed */
    for (i = 0; i < npanes; i++)
        frame(&panes[i], pane_border_rgb(&panes[i] == apane));
    planes_restore();
    float_clip(cpane);
}

/* ---- which pane ---- */

struct Pane *pane_at(int x, int y)
{
    int i;
    if (fshown && x >= fpane.x0 && x <= fpane.x1 && y >= fpane.y0 && y <= fpane.y1)
        return &fpane;
    for (i = 0; i < npanes; i++)
        if (x >= panes[i].x0 && x <= panes[i].x1 && y >= panes[i].y0 &&
            y <= panes[i].y1)
            return &panes[i];
    return NULL;
}

/* p becomes the active pane: keys go there, its cursor is the solid one,
 * the frame moves, the scroll bar and the title follow. */
void pane_activate(struct Pane *p)
{
    struct Pane *old = apane;
    if (!p || p == apane)
        return;
    apane = p;
    shown = p->tab;
    if (win) {
        /* the old cursor hollow, the new one solid */
        pane_use(old);
        T = old->tab;
        draw_cursor(FALSE);
        draw_cursor(TRUE);
        pane_use(p);
        T = p->tab;
        draw_cursor(FALSE);
        draw_cursor(TRUE);
        panes_frames();
        draw_tabbar();
        knob_forget();
        update_knob();
        pane_use(old);                  /* the old pane's own bar: no longer lit */
        T = old->tab;
        update_knob();
        pane_use(p);
        T = p->tab;
    }
    pane_use(apane);
    T = shown;
    if (fshown && (old == &fpane || p == &fpane))
        float_draw();                   /* its frame lit or not */
    title_update_now();
    mouse_reporting();
}

void pane_next(void)
{
    int k = apane - panes;
    pane_activate(&panes[(k + 1) % npanes]);
}

/* The pane next to p in direction dir, by where they are: one that lies
 * wholly that way and overlaps p along the other axis - the nearest, of
 * those the one that overlaps most (i3). NULL: none. */
static struct Pane *neighbour(struct Pane *p, int dir)
{
    struct Pane *best = NULL;
    int i, bd = 0, bo = 0;
    for (i = 0; i < npanes; i++) {
        struct Pane *q = &panes[i];
        int d, o, lo, hi;
        if (q == p)
            continue;
        switch (dir) {
        case PD_LEFT: d = p->x0 - q->x1; break;
        case PD_RIGHT: d = q->x0 - p->x1; break;
        case PD_UP: d = p->y0 - q->y1; break;
        default: d = q->y0 - p->y1; break;
        }
        if (d <= 0)
            continue;                   /* not wholly that way */
        if (dir == PD_LEFT || dir == PD_RIGHT) {
            lo = p->y0 > q->y0 ? p->y0 : q->y0;
            hi = p->y1 < q->y1 ? p->y1 : q->y1;
        } else {
            lo = p->x0 > q->x0 ? p->x0 : q->x0;
            hi = p->x1 < q->x1 ? p->x1 : q->x1;
        }
        if ((o = hi - lo + 1) <= 0)
            continue;                   /* beside it, but not alongside */
        if (!best || d < bd || (d == bd && o > bo)) {
            best = q;
            bd = d;
            bo = o;
        }
    }
    return best;
}

/* ---- splitting and closing ---- */

/* Can a pane of w x h pixels be halved that way, each half keeping its
 * letters? */
static BOOL halves_fit(int dir, int w, int h)
{
    return dir == 'h' ? (w - DIVW) / 2 >= MINC * cw + PADW
                      : (h - DIVW) / 2 >= MINR * ch + PADH;
}

/* The active pane in two (dir 'h': side by side, 'v': above each
 * other), a new Shell in the new half (right or below), which becomes
 * the active pane and keeps the old one's direction. FALSE: not now (no
 * room, every tab taken, no memory). */
BOOL pane_split(int dir)
{
    struct Pane *p, *q;
    struct Tab *t;
    int n, a, b, sdir;
    if (zoomed) {
        zoom_out();
        panes_changed();
    }
    if (npanes >= MAXPANES || !win)
        return FALSE;
    p = apane;
    if (!halves_fit(dir, p->x1 - p->x0 + 1, p->y1 - p->y0 + 1))
        return FALSE;
    if (!panes_room(npanes + 1))
        return FALSE;
    p = apane;
    n = leaf_of(p);
    sdir = p->sdir;
    if ((a = node_new()) < 0)
        return FALSE;
    if ((b = node_new()) < 0) {
        nodes[a].used = 0;
        return FALSE;
    }
    if (!(t = shell_tab())) {
        nodes[a].used = nodes[b].used = 0;
        return FALSE;
    }
    pane_fresh(npanes, t, sdir);
    q = &panes[npanes];
    nodes[a].pane = nodes[n].pane;
    nodes[b].pane = npanes;
    nodes[a].up = nodes[b].up = n;
    nodes[n].split = dir;
    nodes[n].a = a;
    nodes[n].b = b;
    nodes[n].at = 500;
    npanes++;
    apane = q;
    shown = T = t;
    panes_changed();
    return TRUE;
}

/* Pane p goes from the tree (its tab stays, in the tab bar); its
 * sibling takes the room. Nothing drawn. */
static void pane_remove(struct Pane *p)
{
    int n = leaf_of(p), up = nodes[n].up, s, k = p - panes, i, keepup;
    struct Tab *act = apane == p ? NULL : apane->tab;
    if (npanes < 2 || up < 0)
        return;
    s = nodes[up].a == n ? nodes[up].b : nodes[up].a;
    keepup = nodes[up].up;
    nodes[up] = nodes[s];               /* the sibling moves up */
    nodes[up].up = keepup;
    if (nodes[up].split) {
        nodes[(int)nodes[up].a].up = up;
        nodes[(int)nodes[up].b].up = up;
    }
    nodes[n].used = nodes[s].used = 0;
    memmove(panes + k, panes + k + 1, (npanes - k - 1) * sizeof panes[0]);
    npanes--;
    for (i = 0; i < MAXNODES; i++)
        if (nodes[i].used && !nodes[i].split && nodes[i].pane > k)
            nodes[i].pane--;
    /* the active one: as it was, or the first pane where the closed one
     * was (the panes after it moved down one) */
    for (n = up; nodes[n].split; n = nodes[n].a)
        ;
    apane = act ? pane_of(act) : &panes[(int)nodes[n].pane];
    if (!apane)
        apane = &panes[0];
    cpane = apane;
}

/* ---- one pane fills the window (i3's fullscreen) ---- */

/* The layout aside; the active pane alone in the window. */
static void zoom_in(void)
{
    int i;
    memcpy(znodes, nodes, sizeof nodes);
    for (i = 0; i < npanes; i++) {
        ztab[i] = panes[i].tab;
        zdir[i] = panes[i].sdir;
    }
    znpanes = npanes;
    zactive = apane - panes;
    i = apane->sdir;
    memset(nodes, 0, sizeof nodes);
    nodes[0].used = 1;
    nodes[0].up = nodes[0].a = nodes[0].b = -1;
    nodes[0].at = 500;
    panes[0].tab = apane->tab;
    npanes = 1;
    apane = cpane = &panes[0];
    panes_room(1);
    pane_fresh(0, shown, i);
    zoomed = TRUE;
}

/* The layout back (nothing drawn): the tab shown now in the pane that
 * shows it, else in the pane that was active; a tab gone meanwhile
 * leaves its pane to a tab in none, or the pane goes. */
static void zoom_out(void)
{
    struct Tab *now = shown;
    int i, k;
    if (!zoomed)
        return;
    zoomed = FALSE;
    if (!panes_room(znpanes))
        return;                         /* no memory: stays one pane */
    memcpy(nodes, znodes, sizeof nodes);
    npanes = znpanes;
    for (i = 0; i < npanes; i++)
        pane_fresh(i, ztab[i], zdir[i]);
    apane = &panes[zactive];
    for (i = 0; i < npanes; i++)
        if (panes[i].tab == now)
            apane = &panes[i];
    if (apane->tab != now)
        apane->tab = now;               /* chosen while filled */
    cpane = apane;
    for (i = 0; i < npanes; i++)
        if (!panes[i].tab) {
            for (k = 0; k < ntabs && (pane_of(tabs[k]) || tabs[k]->t_ws != curws); k++)
                ;
            if (k < ntabs)
                panes[i].tab = tabs[k];
            else {
                pane_remove(&panes[i]);
                i = -1;                 /* the list moved: again */
            }
        }
    shown = T = apane->tab;
}

BOOL pane_zoomed(void)
{
    return zoomed;
}

/* t is in the layout kept aside: hidden, its size stays as it was. */
BOOL pane_kept(struct Tab *t)
{
    int i;
    if (zoomed)
        for (i = 0; i < znpanes; i++)
            if (ztab[i] == t && t != panes[0].tab)
                return TRUE;
    return FALSE;
}

/* Close the active pane: its tab goes on in the tab bar. */
BOOL pane_close(void)
{
    if (zoomed)
        zoom_out();                     /* (drawn below, or now) */
    if (npanes < 2) {
        panes_changed();
        return FALSE;
    }
    pane_remove(apane);
    shown = T = apane->tab;
    panes_changed();
    return TRUE;
}

/* A tab is gone (tab_drop): its pane shows another one that is in no
 * pane, or goes. */
void pane_tab_gone(struct Tab *t, int at)
{
    struct Pane *p = pane_of(t);
    int i;
    for (i = 0; i < MAXPANES; i++)      /* the layout kept aside */
        if (ztab[i] == t)
            ztab[i] = NULL;
    ws_forget(t);
    float_tab_gone(t);
    if (!p || p == &fpane)
        return;
    p->tab = NULL;
    for (i = at < ntabs ? at : ntabs - 1; i >= 0; i--)
        if (!pane_of(tabs[i]) && tabs[i]->t_ws == curws)
            break;
    if (i < 0)
        for (i = at + 1; i < ntabs; i++)
            if (!pane_of(tabs[i]) && tabs[i]->t_ws == curws)
                break;
    if (i >= 0 && i < ntabs) {
        p->tab = tabs[i];
        p->curx = p->cury = -1;
    } else if (npanes > 1)
        pane_remove(p);
}

/* After the panes changed: letters, layout, drawing, the sizes told. */
void panes_changed(void)
{
    if (!win)
        return;
    tab_letters(TRUE);                  /* split: the window's letters */
    layout_and_resize();
    painter_forget();
    redraw_all();
    tell_sizes();
    title_update_now();
}

/* ---- all tabs to panes: a grid ---- */

/* t's place in the tabs (backwards: see main.c's tab_index) */
static int tindex(struct Tab *t)
{
    int i = ntabs;
    while (--i >= 0)
        if (tabs[i] == t)
            break;
    return i < 0 ? 0 : i;
}

/* c x r cells in the text area, each keeping its letters? */
static BOOL grid_fits(int c, int r, int w, int h)
{
    return (w - (c - 1) * DIVW) / c >= MINC * cw + PADW &&
           (h - (r - 1) * DIVW) / r >= MINR * ch + PADH;
}

/* The grid for k panes (an odd k: one cell more, the active pane has
 * two): columns c, rows r, as wide as the window is shaped. FALSE: they
 * do not fit. */
static BOOL grid_for(int k, int w, int h, int *c, int *r)
{
    int m = k + (k & 1), i, a, b;
    if (k == 1) {
        *c = *r = 1;
        return TRUE;
    }
    for (a = 1, i = 1; i * i <= m; i++)
        if (m % i == 0)
            a = i;                      /* the smaller side */
    b = m / a;
    /* a wide window more columns, a tall one more rows; else the other
     * way round, if that fits */
    if (w >= h) *c = b, *r = a;
    else *c = a, *r = b;
    if (grid_fits(*c, *r, w, h))
        return TRUE;
    i = *c; *c = *r; *r = i;
    return a != b && grid_fits(*c, *r, w, h);
}

/* Nodes in a row: a chain of splits dir over items (nodes), each its
 * weight's share. The chain's node. */
static int chain(int dir, const int *item, const int *wt, int n)
{
    int d, i, sum = 0;
    if (n == 1)
        return item[0];
    if ((d = node_new()) < 0)
        return item[0];                 /* (cannot be: nodes enough) */
    for (i = 0; i < n; i++)
        sum += wt[i];
    nodes[d].split = dir;
    nodes[d].a = item[0];
    nodes[d].b = chain(dir, item + 1, wt + 1, n - 1);
    nodes[d].at = (LONG)wt[0] * 1000 / sum;
    nodes[(int)nodes[d].a].up = d;
    nodes[(int)nodes[d].b].up = d;
    return d;
}

/* Every tab in a pane: a grid of c columns and r rows, the tabs in
 * their order row by row; with an odd number the active tab first,
 * filling the left column's top two cells. As many as fit (the active
 * one always); the others stay tabs. */
static BOOL to_panes(void)
{
    int order[MAXTABS], k, n = 0, c = 1, r = 1, i, j, x0, y0, x1, y1;
    int leaf[MAXPANES], col[MAXPANES], cw_[MAXPANES], cn, cell;
    struct Tab *act = shown;
    char note[60];
    if (!win)
        return FALSE;
    struct Tab *here[MAXTABS];
    int nh = ws_tabs(here);
    zoomed = FALSE;
    area(&x0, &y0, &x1, &y1);
    for (k = nh; k > 1 && !grid_for(k, x1 - x0 + 1, y1 - y0 + 1, &c, &r);
         k--)
        ;
    if (!panes_room(k))
        return FALSE;
    /* which tabs, in what order */
    if (k & 1)
        order[n++] = tindex(act);
    for (i = 0; i < nh && n < k; i++)
        if (here[i] != act || !(k & 1))
            order[n++] = tindex(here[i]);
    if (!(k & 1)) {                     /* the active one must be in */
        for (i = 0; i < n && tabs[order[i]] != act; i++)
            ;
        if (i == n)
            order[n - 1] = tindex(act);
    }
    memset(nodes, 0, sizeof nodes);
    nodes[0].used = 1;                  /* the root's place, kept */
    npanes = k;
    for (i = 0; i < k; i++) {
        pane_fresh(i, tabs[order[i]], 'h');
        leaf[i] = k == 1 ? 0 : node_new();
        nodes[leaf[i]].pane = i;
        if (tabs[order[i]] == act)
            apane = &panes[i];
    }
    if (k > 1) {
        /* each column, top to bottom: the cells row by row are i = r *
         * c; the active one's two (odd) are column 0, rows 0 and 1 */
        int colnode[MAXPANES], one[MAXPANES], root;
        for (j = 0; j < c; j++) {
            cn = 0;
            for (i = 0; i < r; i++) {
                cell = i * c + j;       /* the cell's pane: count the */
                if (k & 1) {            /* cells before it, the second */
                    if (j == 0 && i == 1)   /* of the active's skipped */
                        continue;
                    if (cell > c)
                        cell--;
                }
                col[cn] = leaf[cell];
                cw_[cn++] = (k & 1) && cell == 0 ? 2 : 1;
            }
            colnode[j] = chain('v', col, cw_, cn);
            one[j] = 1;
        }
        root = chain('h', colnode, one, c);
        nodes[0] = nodes[root];         /* the root into node 0 */
        nodes[root].used = 0;
        nodes[0].up = -1;
        if (nodes[0].split) {
            nodes[(int)nodes[0].a].up = 0;
            nodes[(int)nodes[0].b].up = 0;
        }
    } else {
        nodes[0].up = nodes[0].a = nodes[0].b = -1;
        nodes[0].at = 500;
        apane = &panes[0];
    }
    cpane = apane;
    shown = T = apane->tab;
    panes_changed();
    if (k < ntabs) {
        strcpy(note, "panes: ");
        numstr_cat(note, k);
        strcat(note, " fit, ");
        numstr_cat(note, ntabs - k);
        strcat(note, " stay tabs");
        title_hint(note);
    }
    return TRUE;
}

/* Every pane to a tab: one pane, the active tab in it. */
static void to_tabs(void)
{
    int dir = apane->sdir;
    struct Tab *t = shown;
    zoomed = FALSE;
    memset(nodes, 0, sizeof nodes);
    nodes[0].used = 1;
    nodes[0].up = nodes[0].a = nodes[0].b = -1;
    nodes[0].at = 500;
    panes[0].tab = t;
    npanes = 1;
    apane = cpane = &panes[0];
    panes_room(1);
    pane_fresh(0, t, dir);
    shown = T = t;
    panes_changed();
}

/* ---- workspaces (i3's) ----
 *
 * Every tab belongs to one (t_ws); the shown one's layout is the live
 * tree, the others' are kept here: the tree, each leaf's tab and
 * direction, the active one. All tabs stay in tabs[] (their Shells and
 * hosts are served whichever workspace shows), only the tab bar, the
 * tab keys and the layout look at t_ws. */
int curws;
static struct WsLayout {
    BOOL used;
    struct PNode nodes[MAXNODES];
    struct Tab *tab[MAXPANES];
    UBYTE dir[MAXPANES];
    int npanes, active;
} wsl[NWS];

int ws_used(int n)
{
    int i, k = 0;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->t_ws == n)
            k++;
    return k;
}

int ws_tabs(struct Tab **out)
{
    int i, k = 0;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->t_ws == curws)
            out[k++] = tabs[i];
    return k;
}

/* a tab ended: no kept layout shows it any more */
static void ws_forget(struct Tab *t)
{
    int w, i;
    for (w = 0; w < NWS; w++)
        for (i = 0; i < wsl[w].npanes; i++)
            if (wsl[w].tab[i] == t)
                wsl[w].tab[i] = NULL;
}

static void ws_save(void)
{
    struct WsLayout *w = &wsl[curws];
    int i;
    float_hide();                       /* the scratchpad: shown again by its key */
    zoom_out();
    memcpy(w->nodes, nodes, sizeof nodes);
    for (i = 0; i < npanes; i++) {
        w->tab[i] = panes[i].tab;
        w->dir[i] = panes[i].sdir;
    }
    w->npanes = npanes;
    w->active = apane - panes;
    w->used = TRUE;
}

/* The shown workspace's layout from what was kept (a tab gone meanwhile
 * leaves its pane to a tab of the workspace in none, or the pane goes);
 * none kept: one pane, its first tab. FALSE: it has no tab. */
static BOOL ws_load(void)
{
    struct WsLayout *w = &wsl[curws];
    struct Tab *here[MAXTABS];
    int nh = ws_tabs(here), i, k;
    if (!nh)
        return FALSE;
    if (w->used && w->npanes > 0 && panes_room(w->npanes)) {
        memcpy(nodes, w->nodes, sizeof nodes);
        npanes = w->npanes;
        for (i = 0; i < npanes; i++)
            pane_fresh(i, w->tab[i], w->dir[i]);
        apane = &panes[w->active < npanes ? w->active : 0];
        for (i = 0; i < npanes; i++)
            if (!panes[i].tab) {
                for (k = 0; k < nh && pane_of(here[k]); k++)
                    ;
                if (k < nh)
                    panes[i].tab = here[k];
                else if (npanes > 1) {
                    pane_remove(&panes[i]);
                    i = -1;
                } else
                    panes[i].tab = here[0];
            }
    } else {
        memset(nodes, 0, sizeof nodes);
        nodes[0].used = 1;
        nodes[0].up = nodes[0].a = nodes[0].b = -1;
        nodes[0].at = 500;
        npanes = 1;
        panes_room(1);
        pane_fresh(0, here[0], 'h');
        apane = &panes[0];
    }
    cpane = apane;
    shown = T = apane->tab;
    return TRUE;
}

static void ws_note(const char *what, int n)
{
    char note[40];
    strcpy(note, what);
    numstr_cat(note, n + 1);
    title_hint(note);
}

/* To workspace n: the shown one's layout kept, n's back; an empty one
 * gets a new Shell (a workspace shows something). */
static BOOL ws_switch(int n)
{
    int old = curws;
    if (n < 0 || n >= NWS || !win)
        return FALSE;
    if (n == curws)
        return TRUE;
    ws_save();
    curws = n;
    if (!ws_load()) {
        struct Tab *t = shell_tab();    /* made in curws (tab_new) */
        if (!t || !ws_load()) {
            curws = old;
            ws_load();
            panes_changed();
            return FALSE;
        }
    }
    ws_note("workspace ", n);
    panes_changed();
    return TRUE;
}

/* The shown tab to workspace n (i3's move container to workspace): out
 * of this layout, a tab of n's (shown there when n is); the last one
 * here takes the view with it. */
static BOOL ws_move(int n)
{
    struct Tab *t = shown;
    if (n < 0 || n >= NWS || n == curws || !t || !win)
        return FALSE;
    zoom_out();
    if (ws_used(curws) < 2) {           /* nothing would be left here */
        wsl[curws].used = FALSE;
        t->t_ws = n;
        curws = n;
        if (!ws_load())
            return FALSE;
        ws_note("workspace ", n);
        panes_changed();
        return TRUE;
    }
    t->t_ws = n;
    if (npanes > 1)
        pane_remove(apane);
    else {
        struct Tab *here[MAXTABS];
        ws_tabs(here);
        apane->tab = here[0];
    }
    shown = T = apane->tab;
    ws_note("moved to workspace ", n);
    panes_changed();
    return TRUE;
}

/* The shown workspace has no tab left (its last ended): the first one
 * that has, shown. */
void ws_rescue(void)
{
    int n;
    if (ws_used(curws) || !ntabs)
        return;
    wsl[curws].used = FALSE;
    for (n = 0; n < NWS && !ws_used(n); n++)
        ;
    if (n == NWS)
        return;
    curws = n;
    ws_load();
}

/* ---- the scratchpad (i3's): a pane that floats ----
 *
 * One tab (stab, in no workspace: t_ws -1) shown in fpane over the
 * layout, or hidden and running behind. While it shows, everything else
 * is drawn through a clip region that leaves its rectangle out (outreg),
 * and it is drawn last without one, so nothing paints over it. Its place
 * is kept in per mille of the panes' area: a resize keeps it in
 * proportion. */
struct Pane fpane;
BOOL fshown;
static struct Tab *stab;
static struct Pane *ftiled;             /* the tiled pane active before it */
static int ffx = 175, ffy = 150, ffw = 650, ffh = 600;
static struct Region *outreg;
static int clipped;                     /* 0 none, 1 outreg installed */

static void clip_set(int want)
{
    if (!win || want == clipped)
        return;
    InstallClipRegion(win->WLayer, want ? outreg : NULL);
    clipped = want;
}

/* Drawing for pane p (pane_use): the floating one unclipped, the rest
 * round it. */
static void float_clip(struct Pane *p)
{
    clip_set(fshown && outreg && p != &fpane);
}

/* The window (and its layer) goes: no region may stay installed. */
void float_unclip(void)
{
    if (win && clipped)
        InstallClipRegion(win->WLayer, NULL);
    clipped = 0;
}

/* Its place and its text from the per mille, and the region round it. */
static void float_layout(void)
{
    int x0, y0, x1, y1, w, h, b = bordersize > 0 ? bordersize : 1;
    struct Pane *p = &fpane;
    if (!fshown || !win)
        return;
    {   /* the whole text area, gaps or not */
        x0 = win->BorderLeft;
        y0 = win->BorderTop + tabbar_h();
        x1 = win->Width - win->BorderRight - 1;
        y1 = win->Height - win->BorderBottom - 1;
    }
    w = x1 - x0 + 1; h = y1 - y0 + 1;
    p->x0 = x0 + (LONG)w * ffx / 1000;
    p->y0 = y0 + (LONG)h * ffy / 1000;
    p->x1 = p->x0 + (LONG)w * ffw / 1000 - 1;
    p->y1 = p->y0 + (LONG)h * ffh / 1000 - 1;
    if (p->x1 > x1) p->x1 = x1;
    if (p->y1 > y1) p->y1 = y1;
    p->ox = p->x0 + b + 1;
    p->oy = p->y0 + b;
    p->bx0 = p->bx1 = 0;
    p->btotal = -1;
    if (scrollbar) {
        p->bx1 = p->x1 - b;
        p->bx0 = p->bx1 - PANEBAR + 1;
    }
    p->cols = (p->x1 + 1 - p->ox - b - 1 - (p->bx0 ? PANEBAR + 1 : 0)) / cw;
    p->rows = (p->y1 - p->y0 + 1 - 2 * b) / ch;
    if (p->cols < MINC) p->cols = MINC;
    if (p->rows < MINR) p->rows = MINR;
    if (p->rows > PROWS) p->rows = PROWS;
    p->fx0 = p->x0 + b;
    p->fx1 = p->x1 - b;
    clip_set(0);
    if (!outreg)
        outreg = NewRegion();
    if (outreg) {
        struct Rectangle r;
        ClearRegion(outreg);
        r.MinX = 0; r.MinY = 0; r.MaxX = win->Width - 1; r.MaxY = win->Height - 1;
        OrRectRegion(outreg, &r);
        r.MinX = p->x0; r.MinY = p->y0; r.MaxX = p->x1; r.MaxY = p->y1;
        ClearRectRegion(outreg, &r);
    }
}

BOOL float_shows(struct Tab *t) { return fshown && t && t == stab; }

/* Does it lie over pane p? (p scrolls by redrawing then, not blitting) */
BOOL float_over(struct Pane *p)
{
    return fshown && p != &fpane && p->x0 <= fpane.x1 && p->x1 >= fpane.x0 &&
           p->y0 <= fpane.y1 && p->y1 >= fpane.y0;
}

/* It drawn whole, on top: its background, its frame, its text. */
void float_draw(void)
{
    struct Pane *was = cpane;
    struct Tab *keep = T;
    int b = bordersize > 0 ? bordersize : 1, k;
    if (!fshown || !win || !stab)
        return;
    pane_use(&fpane);
    T = stab;
    planes_save();
    if (!backdrop_area(fpane.x0, fpane.y0, fpane.x1 - fpane.x0 + 1, fpane.y1 - fpane.y0 + 1)) {
        apen(defbg);
        RectFill(rp, fpane.x0, fpane.y0, fpane.x1, fpane.y1);
    }
    apen(pane_border_rgb(apane == &fpane));
    for (k = 0; k < b; k++) {
        RectFill(rp, fpane.x0 + k, fpane.y0 + k, fpane.x1 - k, fpane.y0 + k);
        RectFill(rp, fpane.x0 + k, fpane.y1 - k, fpane.x1 - k, fpane.y1 - k);
        RectFill(rp, fpane.x0 + k, fpane.y0 + k, fpane.x0 + k, fpane.y1 - k);
        RectFill(rp, fpane.x1 - k, fpane.y0 + k, fpane.x1 - k, fpane.y1 - k);
    }
    planes_restore();
    redraw_pane_now();
    pane_use(was);
    T = keep;
}

static void float_show(void)
{
    if (apane != &fpane)
        ftiled = apane;
    fshown = TRUE;
    memset(&fpane, 0, sizeof fpane);
    fpane.tab = stab;
    fpane.curx = fpane.cury = -1;
    fpane.pgen = 1;
    fpane.sdir = 'h';
    float_layout();
    apane = cpane = &fpane;
    shown = T = stab;
}

static void float_hide(void)
{
    if (!fshown)
        return;
    clip_set(0);
    fshown = FALSE;
    if (apane == &fpane)
        apane = ftiled && ftiled - panes < npanes ? ftiled : &panes[0];
    cpane = apane;
    shown = T = apane->tab;
}

/* The tiled pane back as the active one (an i3 key that works on the
 * layout while the scratchpad has the keys): it stays on show. */
void float_unfocus(void)
{
    if (apane == &fpane) {
        apane = ftiled && ftiled - panes < npanes ? ftiled : &panes[0];
        cpane = apane;
        shown = T = apane->tab;
    }
}

/* LA+-: shown, hidden; the first time a new Shell in it. */
static BOOL scratch_toggle(void)
{
    if (!stab) {
        struct Tab *t = shell_tab();
        if (!t)
            return FALSE;
        t->t_ws = -1;
        stab = t;
    }
    if (fshown && apane == &fpane)
        float_hide();
    else if (fshown) {                  /* on show, not in use: in use */
        ftiled = apane;
        apane = cpane = &fpane;
        shown = T = stab;
    } else
        float_show();
    panes_changed();
    return TRUE;
}

/* LA+Shift+-: the shown tab into the scratchpad (one there before goes
 * back to this workspace as a tab of its own), hidden as i3 does. */
static BOOL to_scratch(void)
{
    struct Tab *t = shown;
    if (!t || t == stab || ws_used(curws) < 2)
        return FALSE;                   /* the workspace keeps one tab */
    if (stab)
        stab->t_ws = curws;
    float_hide();
    t->t_ws = -1;
    stab = t;
    if (npanes > 1)
        pane_remove(apane);
    else {
        struct Tab *here[MAXTABS];
        ws_tabs(here);
        apane->tab = here[0];
    }
    shown = T = apane->tab;
    title_hint("to the scratchpad (LA+- shows it)");
    panes_changed();
    return TRUE;
}

/* the scratchpad's tab ended */
static void float_tab_gone(struct Tab *t)
{
    if (t != stab)
        return;
    stab = NULL;
    float_hide();
}

/* ---- resize mode (i3's): the arrows size the pane, Return ends ---- */

static BOOL resizing;

BOOL pane_resizing(void) { return resizing; }

static BOOL resize_tiled(int dir)
{
    int n = leaf_of(apane), up = -1, axis = dir == PD_LEFT || dir == PD_RIGHT ? 'h' : 'v';
    BOOL grow = dir == PD_RIGHT || dir == PD_DOWN;
    struct PNode *d;
    LONG span;
    int pos;
    if (npanes < 2)
        return FALSE;
    for (; (up = nodes[n].up) >= 0; n = up)
        if (nodes[up].split == axis)
            break;
    if (up < 0)
        return FALSE;
    d = &nodes[up];
    pos = d->m + ((d->a == n) == grow ? 1 : -1) * (axis == 'h' ? 4 * cw : 2 * ch);
    drag = up;
    pos = drag_clamp(pos);
    drag = -1;
    span = axis == 'h' ? d->x1 - d->x0 + 1 - DIVW : d->y1 - d->y0 + 1 - DIVW;
    if (span <= 0 || pos == d->m)
        return FALSE;
    d->at = (pos - (axis == 'h' ? d->x0 : d->y0)) * 1000L / span;
    panes_changed();
    return TRUE;
}

/* The floating one: the arrows its size, with Shift its place. */
static BOOL resize_float(int dir, BOOL move)
{
    int s = 25;
    if (move) {
        if (dir == PD_LEFT) ffx -= s; else if (dir == PD_RIGHT) ffx += s;
        else if (dir == PD_UP) ffy -= s; else ffy += s;
    } else {
        if (dir == PD_LEFT) ffw -= s; else if (dir == PD_RIGHT) ffw += s;
        else if (dir == PD_UP) ffh -= s; else ffh += s;
    }
    if (ffw < 200) ffw = 200;
    if (ffh < 150) ffh = 150;
    if (ffw > 1000) ffw = 1000;
    if (ffh > 1000) ffh = 1000;
    if (ffx < 0) ffx = 0;
    if (ffy < 0) ffy = 0;
    if (ffx + ffw > 1000) ffx = 1000 - ffw;
    if (ffy + ffh > 1000) ffy = 1000 - ffh;
    panes_changed();
    return TRUE;
}

/* A key in resize mode: TRUE taken (every key is, while it lasts). */
void pane_resize_key(UWORD k)
{
    UWORD key = k & KQ_KEY;
    BOOL shift = (k & KQ_S) != 0;
    int dir = key == KM_LEFT ? PD_LEFT : key == KM_RIGHT ? PD_RIGHT
            : key == KM_UP ? PD_UP : key == KM_DOWN ? PD_DOWN : -1;
    if (key == 13 || key == 27 || keys_find(KC_PANES, k) == KA_P_RESIZE) {
        resizing = FALSE;
        title_hint("resize: done");
        return;
    }
    if (dir < 0)
        return;
    if (!(apane == &fpane ? resize_float(dir, shift) : resize_tiled(dir)))
        beep();
}

static BOOL resize_mode(void)
{
    resizing = TRUE;
    title_hint(apane == &fpane ? "resize: arrows size, Shift+arrows move, Return ends"
                               : "resize: arrows move the pane's edge, Return ends");
    return TRUE;
}

/* ---- the i3 way ---- */

/* Close the active pane and its tab (the Shell told its input ended, a
 * host hung up on); the sibling takes the room. A transfer running
 * there: asked first (ask). The last pane: as Close tab. */
static BOOL kill_pane(BOOL ask)
{
    struct Tab *t = shown;
    if (ask && t->t_xfer) {
        struct EasyStruct es = {
            sizeof(struct EasyStruct), 0, (UBYTE *)"CTerm",
            (UBYTE *)"A file transfer runs in this pane.\n"
                     "Stop it and close the pane and its tab?",
            (UBYTE *)"Close|Cancel" };
        if (!EasyRequestArgs(win, &es, NULL, NULL))
            return TRUE;
    }
    T = t;
    tab_close_current();                /* the tab goes when it has ended */
    if (npanes > 1) {
        pane_remove(apane);
        shown = apane->tab;
        T = shown;
        panes_changed();
    }
    T = shown;
    return TRUE;
}

/* Show tab n in the active pane; a tab in another pane changes places
 * with the active pane's (i3's workspace on this output). */
static BOOL show_tab(int n)
{
    struct Pane *q;
    struct Tab *t;
    struct Tab *here[MAXTABS];
    if (n < 1 || n > ws_tabs(here))
        return FALSE;
    t = here[n - 1];
    if (t == apane->tab)
        return TRUE;
    if (!(q = pane_of(t))) {
        tab_show(n - 1);
        return TRUE;
    }
    q->tab = apane->tab;
    apane->tab = t;
    shown = T = t;
    panes_changed();
    return TRUE;
}

BOOL pane_do(int op, int arg)
{
    struct Pane *q;
    if (!win)
        return FALSE;
    if (op == PO_SCRATCH)
        return scratch_toggle();
    if (op == PO_TOSCRATCH)
        return to_scratch();
    if (op == PO_RESIZE)
        return resize_mode();
    if (op == PO_KILL && apane == &fpane) {
        T = stab;                       /* the scratchpad's Shell ends */
        tab_close_current();
        return TRUE;
    }
    float_unfocus();                    /* the layout's keys: the layout's pane */
    if ((op == PO_OPEN || op == PO_FOCUS || op == PO_MOVE) && zoomed) {
        zoom_out();                     /* the layout back first */
        panes_changed();
    }
    switch (op) {
    case PO_OPEN: {
        /* dwindle: along the longer side (in pixels), unless LA+H/V
         * chose this one; the other way if that one has no room */
        int w = apane->x1 - apane->x0 + 1, h = apane->y1 - apane->y0 + 1;
        int dir = apane->pre ? apane->pre
                : dwindle ? (w >= h ? 'h' : 'v') : apane->sdir;
        BOOL hand = apane->pre != 0;
        apane->pre = 0;
        if (!pane_split(dir) &&
            (!dwindle || hand || !pane_split(dir == 'h' ? 'v' : 'h'))) {
            title_hint("no room for another pane");
            return FALSE;
        }
        return TRUE;
    }
    case PO_DWINDLE:
        dwindle = !dwindle;
        title_hint(dwindle ? "new panes: split by shape (dwindle)"
                           : "new panes: split as LA+H / LA+V say");
        return TRUE;
    case PO_KILL:
        if (zoomed) {
            zoom_out();
            if (npanes < 2)
                panes_changed();
        }
        return kill_pane(arg);
    case PO_FOCUS:
    case PO_MOVE:
        if (!(q = neighbour(apane, arg)))
            return FALSE;
        if (op == PO_FOCUS) {
            pane_activate(q);
            return TRUE;
        }
        {   /* the two change places: tabs and directions */
            struct Tab *t = q->tab;
            UBYTE d = q->sdir;
            q->tab = apane->tab;
            q->sdir = apane->sdir;
            apane->tab = t;
            apane->sdir = d;
            apane = q;
            shown = T = apane->tab;
            panes_changed();
        }
        return TRUE;
    case PO_DIR:
        if (dwindle) {                  /* the next split only */
            apane->pre = arg;
            title_hint(arg == 'h' ? "next pane: beside this one"
                                  : "next pane: below this one");
            return TRUE;
        }
        apane->sdir = arg;
        if (zoomed)
            zdir[zactive] = arg;
        title_hint(arg == 'h' ? "new panes: side by side"
                              : "new panes: one below the other");
        return TRUE;
    case PO_FULL:
        if (zoomed) {
            zoom_out();
            panes_changed();
            return TRUE;
        }
        if (npanes < 2)
            return FALSE;
        zoom_in();
        panes_changed();
        title_hint("one pane fills the window");
        return TRUE;
    case PO_SHOW:
        return show_tab(arg);
    case PO_TABS:
        to_tabs();
        return TRUE;
    case PO_PANES:
        return to_panes();
    case PO_WS:
        return ws_switch(arg - 1);
    case PO_MOVEWS:
        return ws_move(arg - 1);
    }
    return FALSE;
}

/* ---- the dividers, pulled with the mouse ---- */

/* A split whose divider is at x,y (a pixel either side counts): its
 * node, or -1. */
int divider_at(int x, int y)
{
    int n;
    if (npanes < 2)
        return -1;
    for (n = 0; n < MAXNODES; n++) {
        struct PNode *d = &nodes[n];
        if (!d->used || !d->split)
            continue;
        if (d->split == 'h' ? x >= d->m - 1 && x <= d->m + DIVW &&
                              y >= d->y0 && y <= d->y1
                            : y >= d->m - 1 && y <= d->m + DIVW &&
                              x >= d->x0 && x <= d->x1)
            return n;
    }
    return -1;
}

/* The least room a node needs that way: its panes' letters, and the
 * dividers between (a split across adds nothing). */
static int need(int n, int dir)
{
    struct PNode *d = &nodes[n];
    int a, b;
    if (!d->split)
        return dir == 'h' ? MINC * cw + PADW : MINR * ch + PADH;
    a = need(d->a, dir);
    b = need(d->b, dir);
    if (d->split == dir)
        return a + DIVW + b;
    return a > b ? a : b;
}

/* Where the divider may go: each side keeps its panes' letters. */
static int drag_clamp(int pos)
{
    struct PNode *d = &nodes[drag];
    int lo, hi;
    if (d->split == 'h') {
        lo = d->x0 + need(d->a, 'h');
        hi = d->x1 + 1 - DIVW - need(d->b, 'h');
    } else {
        lo = d->y0 + need(d->a, 'v');
        hi = d->y1 + 1 - DIVW - need(d->b, 'v');
    }
    if (pos > hi) pos = hi;
    if (pos < lo) pos = lo;
    return pos;
}

/* The line that shows where the divider goes: drawn in COMPLEMENT, so
 * drawing it again takes it away. */
static void drag_line(int pos)
{
    struct PNode *d = &nodes[drag];
    planes_save();
    SetDrMd(rp, COMPLEMENT);
    if (d->split == 'h')
        RectFill(rp, pos, d->y0, pos + DIVW - 1, d->y1);
    else
        RectFill(rp, d->x0, pos, d->x1, pos + DIVW - 1);
    SetDrMd(rp, JAM2);
    planes_restore();
}

BOOL pane_dragging(void)
{
    return drag >= 0;
}

void pane_drag_start(int n)
{
    drag = n;
    dragpos = nodes[n].m;
    drag_line(dragpos);
}

void pane_drag_move(int x, int y)
{
    int pos;
    if (drag < 0)
        return;
    pos = drag_clamp(nodes[drag].split == 'h' ? x : y);
    if (pos == dragpos)
        return;
    drag_line(dragpos);
    dragpos = pos;
    drag_line(dragpos);
}

/* Let go: the divider there, the panes laid out again, their tabs told. */
void pane_drag_end(void)
{
    struct PNode *d;
    LONG span;
    if (drag < 0)
        return;
    d = &nodes[drag];
    drag_line(dragpos);
    span = d->split == 'h' ? d->x1 - d->x0 + 1 - DIVW
                           : d->y1 - d->y0 + 1 - DIVW;
    if (span > 0)
        d->at = (dragpos - (d->split == 'h' ? d->x0 : d->y0)) * 1000L / span;
    drag = -1;
    panes_changed();
}
