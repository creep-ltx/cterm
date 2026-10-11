/*
 * pane - the window split into panes, each showing a tab: side by side
 * or above each other, and each half split again (four at most), as
 * tmux and iTerm do. The panes are the leaves of a small tree; the
 * layout gives each its rectangle and its text's size, and the tab in it
 * hears that size (NAWS, the ssh pty, the Shell's window bounds).
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
 */
#include "cterm.h"

#define DIVW 2                          /* the divider between two panes */
#define MAXNODES (2 * MAXPANES - 1)

struct Pane panes[MAXPANES];
int npanes = 1;
struct Pane *apane = &panes[0];         /* the active pane: the shown tab's */
struct Pane *cpane = &panes[0];         /* the pane drawn now */

/* The tree: a node is a pane (a leaf) or a split in two. Node 0 is the
 * root. */
static struct PNode {
    BYTE used, split;                   /* split: 0 a pane, 'h' side by
                                           side, 'v' above each other */
    BYTE pane;                          /* a leaf: its pane */
    BYTE a, b, up;                      /* a split's halves (left or top
                                           first); the parent, -1 none */
    WORD at;                            /* the divider, per mille */
    WORD x0, y0, x1, y1;                /* where it is now */
    WORD m;                             /* a split: the divider's first pixel */
} nodes[MAXNODES] = { { 1, 0, 0, -1, -1, -1, 500 } };

/* The first pane: the first tab, its cursor drawn nowhere yet. */
void panes_init(struct Tab *t)
{
    panes[0].tab = t;
    panes[0].curx = panes[0].cury = -1;
    panes[0].pgen = 1;
}

static int drag = -1;                   /* the split whose divider is pulled */
static int dragpos = -1;                /* where its line is drawn now */

void pane_use(struct Pane *p)
{
    cpane = p;
    ox = p->ox;
    oy = p->oy;
    cols = p->cols;
    rows = p->rows;
}

struct Pane *pane_of(struct Tab *t)
{
    int i;
    for (i = 0; i < npanes; i++)
        if (panes[i].tab == t)
            return &panes[i];
    return NULL;
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

/* ---- the layout ---- */

static void lay(int n, int x0, int y0, int x1, int y1)
{
    struct PNode *d = &nodes[n];
    d->x0 = x0; d->y0 = y0; d->x1 = x1; d->y1 = y1;
    if (!d->split) {
        struct Pane *p = &panes[(int)d->pane];
        int edge = npanes > 1, minc = edge ? 2 : 10, minr = edge ? 2 : 3;
        p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
        /* the text: 2 pixels in from the left and right, 1 from the top
         * and bottom - as the window always had */
        p->ox = x0 + 2;
        p->oy = y0 + 1;
        p->cols = (x1 + 1 - p->ox - 2) / cw;
        p->rows = (y1 - y0 + 1 - 2) / ch;
        if (p->cols < minc) p->cols = minc;
        if (p->rows < minr) p->rows = minr;
        if (p->rows > PROWS) p->rows = PROWS;   /* the rows' marks */
        /* what a row's fills cover: the pane, but its frame (split) */
        p->fx0 = x0 + edge;
        p->fx1 = x1 - edge;
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

/* Every pane's place and size, from the window (layout_and_resize). */
void panes_layout(void)
{
    lay(0, win->BorderLeft, win->BorderTop + tabbar_h(),
        win->Width - win->BorderRight - 1 - (fullscreen ? POSBAR + 2 : 0),
        win->Height - win->BorderBottom - 1);
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
    apen(rgb);
    RectFill(rp, p->x0, p->y0, p->x1, p->y0);
    RectFill(rp, p->x0, p->y1, p->x1, p->y1);
    RectFill(rp, p->x0, p->y0, p->x0, p->y1);
    RectFill(rp, p->x1, p->y0, p->x1, p->y1);
}

/* The dividers, and a thin frame in the theme's highlight round the
 * active pane (the others' in the background). Every plane, the text's
 * mask kept (as the tab bar). */
void panes_frames(void)
{
    int n, i;
    if (npanes < 2 || !win)
        return;
    planes_save();
    for (n = 0; n < MAXNODES; n++) {
        struct PNode *d = &nodes[n];
        if (!d->used || !d->split)
            continue;
        apen(mix(defbg, deffg, 25));
        if (d->split == 'h')
            RectFill(rp, d->m, d->y0, d->m + DIVW - 1, d->y1);
        else
            RectFill(rp, d->x0, d->m, d->x1, d->m + DIVW - 1);
    }
    for (i = 0; i < npanes; i++)
        frame(&panes[i], &panes[i] == apane ? themes[theme].wbpens[1]
                                            : defbg);
    planes_restore();
}

/* ---- which pane ---- */

struct Pane *pane_at(int x, int y)
{
    int i;
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
    }
    pane_use(apane);
    T = shown;
    title_update_now();
    mouse_reporting();
}

void pane_next(void)
{
    int k = apane - panes;
    pane_activate(&panes[(k + 1) % npanes]);
}

/* ---- splitting and closing ---- */

/* The active pane in two (dir 'h': side by side, 'v': above each
 * other), a new Shell in the new half, which becomes the active pane.
 * FALSE: not now (four panes, no room, no tab free). */
BOOL pane_split(int dir)
{
    struct Pane *p = apane, *q;
    struct Tab *t;
    int n = leaf_of(p), a, b;
    if (npanes >= MAXPANES || !win)
        return FALSE;
    /* each half at least 10 x 3 letters */
    if (dir == 'h' ? (p->x1 - p->x0 + 1 - DIVW) / 2 < 10 * cw + 4
                   : (p->y1 - p->y0 + 1 - DIVW) / 2 < 3 * ch + 2)
        return FALSE;
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
    q = &panes[npanes];
    memset(q, 0, sizeof *q);
    q->tab = t;
    q->curx = q->cury = -1;
    q->pgen = 1;
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

/* Close the active pane: its tab goes on in the tab bar. */
BOOL pane_close(void)
{
    if (npanes < 2)
        return FALSE;
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
    if (!p)
        return;
    p->tab = NULL;
    for (i = at < ntabs ? at : ntabs - 1; i >= 0; i--)
        if (!pane_of(tabs[i]))
            break;
    if (i < 0)
        for (i = at + 1; i < ntabs; i++)
            if (!pane_of(tabs[i]))
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

/* Where the divider may go: each side keeps 10 x 3 letters. */
static int drag_clamp(int pos)
{
    struct PNode *d = &nodes[drag];
    int lo, hi;
    if (d->split == 'h') {
        lo = d->x0 + 10 * cw + 4;
        hi = d->x1 - DIVW - 10 * cw - 4;
    } else {
        lo = d->y0 + 3 * ch + 2;
        hi = d->y1 - DIVW - 3 * ch - 2;
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
