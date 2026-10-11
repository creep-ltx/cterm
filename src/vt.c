/*
 * vt - the emulator: bytes from the host in, a grid of cells out.
 * See vt.h. Plain C, no Amiga headers.
 */
#include "vt.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAXPARAM 16
#define OSCMAX   8192                   /* OSC 52 brings copied text */
#define REPLYMAX 256

enum { S_GROUND, S_ESC, S_ESC_INT, S_CSI, S_OSC, S_OSC_ESC, S_STR,
       S_STR_ESC, S_DCS, S_SIXEL, S_SIXEL_ESC, S_FILE, S_FILE_ESC };

struct Sixel;
struct FileIn;

typedef struct {
    int x, y, pending;
    VtPen look;
    int origin, autowrap;
    char g[2];
    int gl;
} Saved;

struct Vt {
    int cols, rows;
    /* each screen is a list of rows, so a scroll moves pointers, not cells
     * (a whole-screen memmove per new line cost ~190 KB at 127x92) */
    VtCell **lines[2], **cur;       /* main, alternate; the one in use */
    int alt;
    int base[2];                    /* a ring: screen row y is list entry
                                       base+y (wrapped), so scrolling the
                                       whole screen moves base, not rows */

    /* scrollback: a ring of lines, each as wide as the screen then was */
    VtCell **hist;
    int *hist_w;
    int hist_cap, hist_len, hist_head;   /* head: the oldest line */
    int hist_max;                   /* hist's slots: the cap it may grow
                                       back to after a clear */

    int x, y, pending;              /* pending: at the last column, the
                                       next character wraps first */
    VtPen look;                     /* what new characters look like */
    uint16_t pen;                   /* its number, when !pen_stale */
    int pen_stale;
    uint16_t bpen;                  /* a blank's pen (the look's bg) */
    uint32_t bpen_bg;
    /* the pen table: every look in use, once; phash: its index + 1 by
     * hash (0: free), open addressing */
    VtPen *pens;
    int npens, pcap;
    int pen_nomem;                  /* the table could not grow: new looks
                                       are the default, not asked again */
    uint16_t *phash;
    int top, bot;                   /* the scroll region, inclusive */
    int autowrap, origin, insert, lnm, cursor_on, app_cursor, app_keypad;
    int bracketed, reverse, utf8;
    int mouse, mouse_sgr;               /* 9, 1000, 1002, 1003; SGR form */
    int focus, sync;                    /* 1004 focus reports; 2026 */
    int cshape, cblink;                 /* DECSCUSR: 0 block, 1 underline,
                                           2 bar; blinking */
    int amigamode;                  /* the Amiga console's ways (local) */
    int cp437;                      /* bytes 0x80-0xFF are the PC's */
    int utf8_set, cp437_set;        /* as the menu set them: ESC % @ */
    unsigned long rawev;            /* raw event classes asked for: CSI n{ */
    int winbg;                      /* CSI >n m: the window's own colour */
    char g[2];                      /* G0, G1: 'B' ASCII, '0' DEC lines */
    int gl;
    uint8_t *tabs;
    Saved saved[2];
    uint32_t last_cp;

    int state;
    int param[MAXPARAM], nparam, have_digit;
    char priv, priv2, inter;        /* priv2: a second one, as in Ed's >? */
    char osc[OSCMAX];
    int osclen;
    uint32_t uacc, umin;                /* umin: the least a sequence of
                                           its length may hold */
    int uneed;
    uint8_t ubuf[4];                /* the sequence's bytes so far */
    int ulen;

    uint8_t *dirty;
    uint16_t *dx0, *dx1;            /* a dirty row's changed columns */
    int all_dirty, any_dirty;
    int s_top, s_bot, s_n;
    int s_full;                     /* every row of the scroll's region is
                                       marked whole: marks need not move */

    char reply[REPLYMAX];
    int rlen;
    char title[128];
    int title_changed, bell;
    char *clip;                         /* OSC 52: text for the clipboard */
    int cliplen;

    /* pictures (vt.h): sixel and iTerm2's files */
    int pic_on;
    long pic_cap, pic_bytes;            /* the most they may hold; held now
                                           (one coming in too) */
    int pcw, pch;                       /* a cell in pixels */
    uint32_t pic_bg;                    /* unset pixels */
    VtPic **pics;                       /* the oldest first */
    int npics, pics_cap, pic_id, pic_last;
    int sdm, sright;                    /* DECSET 80: sixels do not scroll;
                                           8452: the cursor right of one */
    struct Sixel *sx;                   /* a sixel picture coming in */
    struct FileIn *fi;                  /* an iTerm2 file coming in */
};

/* ---- small things ---------------------------------------------------- */

static int pen_gc(Vt *v);
static void pic_gc(Vt *v, long need);
static void sixel_end(Vt *v, int keep);
static void file_end(Vt *v, int keep);

static unsigned pen_hash(const VtPen *p)
{
    uint32_t h = p->fg * 2654435761u ^ p->bg * 0x9E3779B1u ^ p->attr;
    return (h ^ (h >> 15)) * 0x85EBCA6Bu >> 8;
}

/* A look's number: found, or added (the table grows; when it is full, the
 * looks no cell uses any more are dropped first). 0: no memory - the
 * default look, so text still shows. */
static uint16_t intern(Vt *v, const VtPen *p)
{
    unsigned m, h;
    if (!p->fg && !p->bg && !p->attr)
        return 0;
    for (;;) {
        m = 2 * v->pcap - 1;
        for (h = pen_hash(p) & m; v->phash[h]; h = (h + 1) & m) {
            const VtPen *q = &v->pens[v->phash[h] - 1];
            if (q->fg == p->fg && q->bg == p->bg && q->attr == p->attr)
                return (uint16_t)(v->phash[h] - 1);
        }
        if (v->npens < v->pcap) {
            v->pens[v->npens] = *p;
            v->phash[h] = (uint16_t)(v->npens + 1);
            return (uint16_t)v->npens++;
        }
        if (v->pen_nomem || !pen_gc(v)) {
            v->pen_nomem = 1;
            return 0;
        }
    }
}

static uint16_t cur_pen(Vt *v)
{
    if (v->pen_stale) {
        v->pen = intern(v, &v->look);
        v->pen_stale = 0;
    }
    return v->pen;
}

static VtCell blank_of(Vt *v)
{
    VtCell c;
    c.cp = 0;
    /* a blank shows no letter: only its background counts (BCE) */
    if (!v->look.bg)
        c.pen = 0;
    else {
        if (v->bpen_bg != v->look.bg) {
            VtPen b;
            b.fg = VT_DEFAULT;
            b.bg = v->look.bg;
            b.attr = 0;
            v->bpen = intern(v, &b);
            v->bpen_bg = v->look.bg;
        }
        c.pen = v->bpen;
    }
    return c;
}

static void fill(VtCell *p, int n, VtCell c)
{
    while (n-- > 0)
        *p++ = c;
}

/* A row knows how far it is in use: cells from RLEN on are blank (all
 * zero: a space in the default colours) whatever memory holds there.
 * Clearing a row or scrolling in a new one is then one store, not a row
 * of cells written - on a 14 MHz Amiga that is most of a new line's cost.
 * The count and the row's capacity live in a cell just before it. */
#define RLEN(r) ((r)[-1].cp)
#define RCAP(r) ((r)[-2].cp)
/* RWRAP: the row ran on into the next (the text wrapped there, it did
 * not end): what lets a resize join and wrap the lines again */
#define RWRAP(r) ((r)[-1].pen)
/* RPIC: the row may hold picture cells (VT_ROW_PICS): rows without are
 * drawn and walked as before */
#define RPIC(r) ((r)[-2].pen)

static VtCell *row_new(int cols)
{
    VtCell *b = calloc(cols + 2, sizeof(VtCell));
    if (!b)
        return NULL;
    b[0].cp = cols;
    return b + 2;
}

static void row_free(VtCell *r)
{
    if (r)
        free(r - 2);
}

/* Cells up to x1 (exclusive) about to be read or written: make them real. */
static void grow(VtCell *r, int x1)
{
    int l = (int)RLEN(r);
    if (l < x1) {
        memset(r + l, 0, (x1 - l) * sizeof(VtCell));
        RLEN(r) = x1;
    }
}

/* Cells x0..x1-1 become blank b; the default blank past the end shortens
 * the row instead. */
static void blank_range(VtCell *r, int x0, int x1, VtCell b)
{
    int l = (int)RLEN(r);
    if (x0 == 0 && x1 >= l)
        RPIC(r) = 0;                    /* all it held is gone */
    if (!b.pen) {
        if (x1 >= l) {
            if (x0 < l)
                RLEN(r) = x0;
            return;
        }
        memset(r + x0, 0, (x1 - x0) * sizeof(VtCell));
        return;
    }
    grow(r, x1);
    fill(r + x0, x1 - x0, b);
}

static int ri_main(const Vt *v, int y)
{
    int i = v->base[0] + y;
    return i >= v->rows ? i - v->rows : i;
}

static int ri(const Vt *v, int y)
{
    int i = v->base[v->alt] + y;
    return i >= v->rows ? i - v->rows : i;
}

static VtCell *row(Vt *v, int y)
{
    return v->cur[ri(v, y)];
}

static void reply(Vt *v, const char *s)
{
    int n = strlen(s);
    if (v->rlen + n <= REPLYMAX) {
        memcpy(v->reply + v->rlen, s, n);
        v->rlen += n;
    }
}

/* Columns a..b of row y changed (clipped to the row). */
static void dirty_span(Vt *v, int y, int a, int b)
{
    if (y < 0 || y >= v->rows)
        return;
    if (a < 0) a = 0;
    if (b >= v->cols) b = v->cols - 1;
    if (!v->dirty[y]) {
        v->dirty[y] = 1;
        v->dx0[y] = a;
        v->dx1[y] = b;
    } else {
        if (a < v->dx0[y]) v->dx0[y] = a;
        if (b > v->dx1[y]) v->dx1[y] = b;
    }
    v->any_dirty = 1;
}

static void dirty_row(Vt *v, int y)
{
    dirty_span(v, y, 0, v->cols - 1);
}

static void dirty_all(Vt *v)
{
    v->all_dirty = 1;
    v->any_dirty = 1;
}

/* How many cells a character takes: 0 (a combining mark), 1 or 2. */
static int cp_width(uint32_t c)
{
    /* the common ones first: Latin-1, and box lines, blocks, arrows and
     * symbols (U+2100-2E7F) - one or two compares, not twenty */
    if (c < 0x300 || (c >= 0x2100 && c < 0x2E80))
        return 1;
    if ((c >= 0x300 && c <= 0x36F) || (c >= 0x200B && c <= 0x200F) ||
        (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) ||
        (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF))
        return 0;
    if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x303E) ||
        (c >= 0x3041 && c <= 0x33FF) || (c >= 0x3400 && c <= 0x4DBF) ||
        (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF) ||
        (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
        (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
        (c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x20000 && c <= 0x3FFFD))
        return 2;
    return 1;
}

/* DEC special graphics: what '`'..'~' become in line-drawing mode. */
static const uint16_t decgfx[31] = {
    0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1,
    0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA,
    0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C,
    0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7
};

/* ---- scrolling --------------------------------------------------------- */

/* The host's word on memory: may the history take n more bytes? (CTerm:
 * only while a reserve stays free.) NULL: always. */
int (*vt_room)(long n);

/* No memory for another history line: the history stops growing where
 * it is (it is a ring from slot 0 while it grows) - no allocation tried
 * again on every new line, which on a full Amiga runs the low-memory
 * handlers each time. A clear lets it grow again. */
static void hist_stop(Vt *v)
{
    if (v->hist_len < v->hist_cap && v->hist_head == 0)
        v->hist_cap = v->hist_len;
}

static VtCell *hist_row(Vt *v, VtCell *old, int cols)
{
    if (old && (int)RCAP(old) >= cols)
        return old;
    if (!old && vt_room && !vt_room((long)(cols + 2) * sizeof(VtCell)))
        return NULL;
    return row_new(cols);
}

/* The row leaving the top becomes the newest history line, and the
 * oldest history line's buffer (when it fits) becomes the new row: no
 * copy, no allocation. FALSE: no history - the row is simply reused. */
static int swap_into_history(Vt *v, VtCell **pline)
{
    int slot;
    VtCell *old, *fresh;
    if (v->hist_cap <= 0)
        return 0;
    if (v->hist_len < v->hist_cap) {
        slot = v->hist_head + v->hist_len;
        if (slot >= v->hist_cap)
            slot -= v->hist_cap;
    } else
        slot = v->hist_head;
    old = v->hist[slot];
    fresh = hist_row(v, old, v->cols);
    if (!fresh) {
        hist_stop(v);
        return 0;
    }
    if (fresh != old)
        row_free(old);
    if (v->hist_len < v->hist_cap)
        v->hist_len++;
    else if (++v->hist_head >= v->hist_cap)
        v->hist_head = 0;
    v->hist[slot] = *pline;
    v->hist_w[slot] = RLEN(*pline);
    RLEN(fresh) = 0;                    /* the caller blanks it */
    RWRAP(fresh) = 0;
    RPIC(fresh) = 0;
    *pline = fresh;
    return 1;
}

static void push_history(Vt *v, const VtCell *line)
{
    int slot;
    VtCell *copy;
    if (v->hist_cap <= 0)
        return;
    if (v->hist_len < v->hist_cap && !v->hist[v->hist_len] &&
        vt_room && !vt_room((long)(RLEN(line) + 2) * sizeof(VtCell))) {
        hist_stop(v);                   /* growing (head 0): no room */
        if (v->hist_cap <= 0)
            return;
    }
    if (v->hist_len < v->hist_cap) {
        slot = (v->hist_head + v->hist_len) % v->hist_cap;
        v->hist_len++;
    } else {
        slot = v->hist_head;            /* full: the oldest goes */
        v->hist_head = (v->hist_head + 1) % v->hist_cap;
    }
    copy = v->hist[slot];
    if (!copy || (int)RCAP(copy) < (int)RLEN(line)) {
        row_free(copy);
        copy = row_new(RLEN(line));
    }
    v->hist[slot] = copy;
    if (!copy) {
        v->hist_w[slot] = 0;
        return;
    }
    memcpy(copy, line, RLEN(line) * sizeof(VtCell));
    RLEN(copy) = RLEN(line);
    RWRAP(copy) = RWRAP(line);          /* a reflow joins it on again */
    RPIC(copy) = RPIC(line);
    v->hist_w[slot] = RLEN(line);
}

/* Record a scroll of [top,bot] by n (up > 0) for the display: one blit
 * then the rows that came in - or, if it cannot be one blit, all of it. */
static void note_scroll(Vt *v, int top, int bot, int n)
{
    int h = bot - top + 1;
    v->any_dirty = 1;
    if (v->all_dirty)
        return;
    if (v->s_n != 0 && (v->s_top != top || v->s_bot != bot ||
                        (v->s_n > 0) != (n > 0))) {
        dirty_all(v);
        return;
    }
    if (abs(v->s_n + n) >= h) {
        dirty_all(v);
        return;
    }
    v->s_top = top;
    v->s_bot = bot;
    v->s_n += n;
    /* a flood: once every row of the region is to be drawn whole, the
     * marks are the same wherever they move (on a stock A1200 the moves
     * were ~500 instructions a new line) */
    if (v->s_full)
        return;
    /* the rows' dirty marks travel with their contents */
    if (n > 0) {
        int y;
        memmove(v->dirty + top, v->dirty + top + n, h - n);
        memmove(v->dx0 + top, v->dx0 + top + n, (h - n) * 2);
        memmove(v->dx1 + top, v->dx1 + top + n, (h - n) * 2);
        for (y = bot - n + 1; y <= bot; y++) {
            v->dirty[y] = 1;
            v->dx0[y] = 0;
            v->dx1[y] = v->cols - 1;
        }
    } else {
        int y;
        memmove(v->dirty + top - n, v->dirty + top, h + n);
        memmove(v->dx0 + top - n, v->dx0 + top, (h + n) * 2);
        memmove(v->dx1 + top - n, v->dx1 + top, (h + n) * 2);
        for (y = top; y < top - n; y++) {
            v->dirty[y] = 1;
            v->dx0[y] = 0;
            v->dx1[y] = v->cols - 1;
        }
    }
    {
        int y;
        for (y = top; y <= bot; y++)
            if (!v->dirty[y] || v->dx0[y] || v->dx1[y] != v->cols - 1)
                break;
        v->s_full = y > bot;
    }
}

/* Turn rows [a,b] end for end - three of these rotate a part of the
 * screen in place, with no buffer on the stack */
static void rows_reverse(Vt *v, int a, int b)
{
    for (; a < b; a++, b--) {
        VtCell **pa = &v->cur[ri(v, a)], **pb = &v->cur[ri(v, b)], *t = *pa;
        *pa = *pb;
        *pb = t;
    }
}

/* Rotate rows [top,bot] up by k (0 < k < h): row top+k becomes top */
static void rows_rotate(Vt *v, int top, int bot, int k)
{
    rows_reverse(v, top, top + k - 1);
    rows_reverse(v, top + k, bot);
    rows_reverse(v, top, bot);
}

/* Scroll [top,bot] up by n. Rows leaving the top of the main screen go to
 * the history unless nohist (deleting lines never feeds it). */
static void scroll_up(Vt *v, int top, int bot, int n, int nohist)
{
    int h = bot - top + 1, i;
    int hist = !v->alt && !nohist;
    VtCell b = blank_of(v);
    if (n > h)
        n = h;
    if (n <= 0)
        return;
    if (h == v->rows && n < h) {
        /* the whole screen: the top rows go to the history, their
         * buffers come back blank as the bottom rows - the ring turns */
        for (i = 0; i < n; i++) {
            VtCell **p = &v->cur[ri(v, i)];
            if (hist)
                swap_into_history(v, p);
            blank_range(*p, 0, v->cols, b);
            RWRAP(*p) = 0;
        }
        v->base[v->alt] = ri(v, n);
    } else {
        if (hist && top == 0)
            for (i = 0; i < n; i++)
                swap_into_history(v, &v->cur[ri(v, i)]);
        if (n < h)
            rows_rotate(v, top, bot, n);
        for (i = bot - n + 1; i <= bot; i++) {
            VtCell *r = v->cur[ri(v, i)];
            blank_range(r, 0, v->cols, b);
            RWRAP(r) = 0;
        }
    }
    note_scroll(v, top, bot, n);
}

static void scroll_down(Vt *v, int top, int bot, int n)
{
    int h = bot - top + 1, i;
    VtCell b = blank_of(v);
    if (n > h)
        n = h;
    if (n <= 0)
        return;
    if (h == v->rows && n < h) {
        v->base[v->alt] = ri(v, v->rows - n);
        for (i = 0; i < n; i++) {
            blank_range(v->cur[ri(v, i)], 0, v->cols, b);
            RWRAP(v->cur[ri(v, i)]) = 0;
        }
    } else {
        if (n < h)
            rows_rotate(v, top, bot, h - n);
        for (i = top; i < top + n; i++) {
            VtCell *r = v->cur[ri(v, i)];
            blank_range(r, 0, v->cols, b);
            RWRAP(r) = 0;
        }
    }
    note_scroll(v, top, bot, -n);
}

static void linefeed(Vt *v)
{
    v->pending = 0;
    if (v->y == v->bot)
        scroll_up(v, v->top, v->bot, 1, 0);
    else if (v->y < v->rows - 1)
        v->y++;
}

static void reverse_index(Vt *v)
{
    v->pending = 0;
    if (v->y == v->top)
        scroll_down(v, v->top, v->bot, 1);
    else if (v->y > 0)
        v->y--;
}

/* ---- the cursor ------------------------------------------------------- */

static void move_to(Vt *v, int x, int y)
{
    int lo = v->origin ? v->top : 0, hi = v->origin ? v->bot : v->rows - 1;
    if (v->origin)
        y += v->top;
    if (x < 0) x = 0;
    if (x >= v->cols) x = v->cols - 1;
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    v->x = x;
    v->y = y;
    v->pending = 0;
}

/* Up (n < 0) or down n lines: from inside the scroll region the cursor
 * stops at its margin, from outside at the screen's edge (xterm's rule
 * for CUU, CUD, CNL, CPL) */
static void move_lines(Vt *v, int n)
{
    int y = v->y + n, lim;
    if (n < 0) {
        lim = v->y >= v->top ? v->top : 0;
        if (y < lim) y = lim;
    } else {
        lim = v->y <= v->bot ? v->bot : v->rows - 1;
        if (y > lim) y = lim;
    }
    v->y = y;
    v->pending = 0;
}

static void save_cursor(Vt *v)
{
    Saved *s = &v->saved[v->alt];
    s->x = v->x;
    s->y = v->y;
    s->pending = v->pending;
    s->look = v->look;
    s->origin = v->origin;
    s->autowrap = v->autowrap;
    s->g[0] = v->g[0];
    s->g[1] = v->g[1];
    s->gl = v->gl;
}

static void restore_cursor(Vt *v)
{
    Saved *s = &v->saved[v->alt];
    v->x = s->x < v->cols ? s->x : v->cols - 1;
    v->y = s->y < v->rows ? s->y : v->rows - 1;
    v->pending = s->pending;
    v->look = s->look;
    v->pen_stale = 1;
    v->origin = s->origin;
    v->autowrap = s->autowrap;
    v->g[0] = s->g[0];
    v->g[1] = s->g[1];
    v->gl = s->gl;
}

/* ---- printing ---------------------------------------------------------- */

/* An edit is about to cut row r between columns x-1 and x: a wide
 * character there would lose a half, so both halves go blank (xterm) */
static void unsplit(Vt *v, VtCell *r, int x)
{
    if (x > 0 && x < (int)RLEN(r) && r[x].cp == VT_WIDE_TAIL) {
        r[x - 1] = blank_of(v);
        r[x] = blank_of(v);
    }
}

static void put(Vt *v, uint32_t cp)
{
    int w;
    VtCell *r;

    if (cp >= 0x20 && cp < 0x7F && v->g[v->gl] == '0' && cp >= 0x5F) {
        cp = cp == 0x5F ? 0xA0 : decgfx[cp - 0x60];
    }
    w = cp_width(cp);
    if (w == 0)
        return;                         /* combining marks: not drawn */
    if (v->pending && v->autowrap) {
        RWRAP(row(v, v->y)) = 1;
        v->x = 0;
        linefeed(v);
    }
    v->pending = 0;
    if (w == 2 && v->x == v->cols - 1) {
        if (!v->autowrap)
            return;
        grow(row(v, v->y), v->cols);
        row(v, v->y)[v->x] = blank_of(v);   /* no room: wrap first */
        RWRAP(row(v, v->y)) = 1;
        dirty_row(v, v->y);
        v->x = 0;
        linefeed(v);
    }
    r = row(v, v->y);
    grow(r, v->insert || v->x + w >= v->cols ? v->cols : v->x + w + 1);
    if (v->insert) {
        int n = v->cols - v->x - w;
        if (n > 0) {
            unsplit(v, r, v->x + n);    /* the one pushed off the edge */
            memmove(r + v->x + w, r + v->x, n * sizeof(VtCell));
        }
    }
    /* overwriting half of a wide character clears its other half */
    if (r[v->x].cp == VT_WIDE_TAIL && v->x > 0)
        r[v->x - 1] = blank_of(v);
    if (v->x + w < v->cols && r[v->x + w].cp == VT_WIDE_TAIL)
        r[v->x + w] = blank_of(v);
    r[v->x].cp = cp;
    r[v->x].pen = cur_pen(v);
    if (w == 2) {
        r[v->x + 1].cp = VT_WIDE_TAIL;
        r[v->x + 1].pen = r[v->x].pen;
    }
    if (v->insert)
        dirty_row(v, v->y);
    else
        dirty_span(v, v->y, v->x - 1, v->x + w);
    v->last_cp = cp;
    if (v->x + w >= v->cols) {
        v->x = v->cols - 1;
        v->pending = 1;
    } else
        v->x += w;
}

/* ---- erasing and editing ----------------------------------------------- */

static void erase(Vt *v, int y, int x0, int x1)
{
    if (x0 < 0) x0 = 0;
    if (x1 > v->cols) x1 = v->cols;
    if (x1 > x0) {
        VtCell *r = row(v, y);
        unsplit(v, r, x0);
        unsplit(v, r, x1);
        blank_range(r, x0, x1, blank_of(v));
        if (x1 >= v->cols)
            RWRAP(r) = 0;
        dirty_span(v, y, x0 - 1, x1);
    }
}

static void erase_display(Vt *v, int how)
{
    int y;
    if (how == 0) {
        erase(v, v->y, v->x, v->cols);
        for (y = v->y + 1; y < v->rows; y++)
            erase(v, y, 0, v->cols);
    } else if (how == 1) {
        for (y = 0; y < v->y; y++)
            erase(v, y, 0, v->cols);
        erase(v, v->y, 0, v->x + 1);
    } else if (how == 2) {
        for (y = 0; y < v->rows; y++)
            erase(v, y, 0, v->cols);
        v->pen_nomem = 0;               /* looks may have gone: try again */
        if (v->npics)
            pic_gc(v, 0);                   /* pictures too */
    } else if (how == 3) {                  /* the scrollback too */
        int i;
        for (i = 0; i < v->hist_cap; i++) {
            row_free(v->hist[i]);
            v->hist[i] = NULL;
            v->hist_w[i] = 0;
        }
        v->hist_len = v->hist_head = 0;
        v->hist_cap = v->hist_max;      /* may grow again */
        v->pen_nomem = 0;
        if (v->npics)
            pic_gc(v, 0);
        dirty_all(v);
    }
}

static void insert_chars(Vt *v, int n)
{
    VtCell *r = row(v, v->y);
    int keep = v->cols - v->x - n;
    if (n <= 0)
        return;
    grow(r, v->cols);
    unsplit(v, r, v->x);
    if (keep > 0) {
        unsplit(v, r, v->x + keep);     /* the one pushed off the edge */
        memmove(r + v->x + n, r + v->x, keep * sizeof(VtCell));
    }
    fill(r + v->x, n < v->cols - v->x ? n : v->cols - v->x, blank_of(v));
    dirty_span(v, v->y, v->x - 1, v->cols - 1);
}

static void delete_chars(Vt *v, int n)
{
    VtCell *r = row(v, v->y);
    int keep = v->cols - v->x - n;
    if (n <= 0)
        return;
    grow(r, v->cols);
    unsplit(v, r, v->x);
    if (keep > 0) {
        unsplit(v, r, v->x + n);        /* the end of what goes */
        memmove(r + v->x, r + v->x + n, keep * sizeof(VtCell));
    } else
        keep = 0;
    fill(r + v->x + keep, v->cols - v->x - keep, blank_of(v));
    dirty_span(v, v->y, v->x - 1, v->cols - 1);
}

/* ---- the alternate screen --------------------------------------------- */

static VtCell **alloc_screen(int cols, int rows);
static void free_screen(VtCell **l, int rows);

/* The alternate screen exists only while it is in use: on a stock A1200
 * a whole screen of cells (37 KB at 77x30) is a lot to keep for a
 * program that may never come. No memory for it: the main screen stays. */
static void use_alt(Vt *v, int on, int clear)
{
    if (on == v->alt)
        return;
    if (on && !v->lines[1]) {
        if (!(v->lines[1] = alloc_screen(v->cols, v->rows)))
            return;
        v->base[1] = 0;
    }
    v->alt = on;
    v->cur = v->lines[on];
    if (on && clear) {
        int y;
        for (y = 0; y < v->rows; y++)
            blank_range(v->cur[y], 0, v->cols, blank_of(v));
    }
    if (!on) {
        free_screen(v->lines[1], v->rows);
        v->lines[1] = NULL;
        if (v->npics)
            pic_gc(v, 0);                   /* its pictures went with it */
    }
    dirty_all(v);
}

/* ---- resetting --------------------------------------------------------- */

static void reset_pen(Vt *v)
{
    v->look.fg = VT_DEFAULT;
    v->look.bg = VT_DEFAULT;
    v->look.attr = 0;
    v->pen = 0;
    v->pen_stale = 0;
}

static void reset_tabs(Vt *v)
{
    int i;
    for (i = 0; i < v->cols; i++)
        v->tabs[i] = i > 0 && i % 8 == 0;
}

static void full_reset(Vt *v)
{
    int i;
    if (v->sx || v->fi) {
        sixel_end(v, 0);
        file_end(v, 0);
    }
    v->sdm = v->sright = 0;
    v->pen_nomem = 0;
    reset_pen(v);
    use_alt(v, 0, 0);
    for (i = 0; i < 2; i++) {
        int y;
        for (y = 0; v->lines[i] && y < v->rows; y++)
            blank_range(v->lines[i][y], 0, v->cols, blank_of(v));
    }
    v->x = v->y = v->pending = 0;
    v->top = 0;
    v->bot = v->rows - 1;
    v->autowrap = 1;
    v->origin = v->insert = 0;
    v->lnm = v->amigamode;                  /* the Amiga: LF is a new line */
    v->cursor_on = 1;
    v->app_cursor = v->app_keypad = v->bracketed = v->reverse = 0;
    v->mouse = v->mouse_sgr = v->focus = v->sync = 0;
    v->cshape = v->cblink = 0;
    v->g[0] = v->g[1] = 'B';
    v->gl = 0;
    reset_tabs(v);
    save_cursor(v);
    v->saved[1] = v->saved[0];
    v->state = S_GROUND;
    if (v->npics)
        pic_gc(v, 0);
    dirty_all(v);
}

/* ---- SGR ------------------------------------------------------------ */

/* 38/48 ; 5 ; n  or  38/48 ; 2 ; r ; g ; b - returns params used */
static int ext_colour(Vt *v, int i, uint32_t *out)
{
    if (i + 1 < v->nparam && v->param[i + 1] == 5 && i + 2 < v->nparam) {
        *out = VT_INDEX(v->param[i + 2] & 255);
        return 2;
    }
    if (i + 1 < v->nparam && v->param[i + 1] == 2 && i + 4 < v->nparam) {
        *out = VT_RGB(v->param[i + 2] & 255, v->param[i + 3] & 255,
                      v->param[i + 4] & 255);
        return 4;
    }
    return v->nparam - i - 1;           /* malformed: eat the rest */
}

static void sgr(Vt *v)
{
    int i;
    if (v->nparam == 0) {
        reset_pen(v);
        return;
    }
    v->pen_stale = 1;                   /* the look changes: a number later */
    for (i = 0; i < v->nparam; i++) {
        int p = v->param[i];
        if (p < 0)
            p = 0;
        switch (p) {
        case 0: reset_pen(v); v->pen_stale = 1; break;
        case 1: v->look.attr |= VT_BOLD; break;
        case 2: v->look.attr |= VT_DIM; break;
        case 3: v->look.attr |= VT_ITALIC; break;
        case 4: v->look.attr |= VT_UNDERLINE; break;
        case 5: case 6: v->look.attr |= VT_BLINK; break;
        case 7: v->look.attr |= VT_INVERSE; break;
        case 8: v->look.attr |= VT_HIDDEN; break;
        case 9: v->look.attr |= VT_STRIKE; break;
        case 21: v->look.attr |= VT_UNDERLINE; break;
        case 22: v->look.attr &= ~(VT_BOLD | VT_DIM); break;
        case 23: v->look.attr &= ~VT_ITALIC; break;
        case 24: v->look.attr &= ~VT_UNDERLINE; break;
        case 25: v->look.attr &= ~VT_BLINK; break;
        case 27: v->look.attr &= ~VT_INVERSE; break;
        case 28: v->look.attr &= ~VT_HIDDEN; break;
        case 29: v->look.attr &= ~VT_STRIKE; break;
        case 38: i += ext_colour(v, i, &v->look.fg); break;
        case 39: v->look.fg = VT_DEFAULT; break;
        case 48: i += ext_colour(v, i, &v->look.bg); break;
        case 49: v->look.bg = VT_DEFAULT; break;
        default:
            if (p >= 30 && p <= 37) v->look.fg = VT_INDEX(p - 30);
            else if (p >= 40 && p <= 47) v->look.bg = VT_INDEX(p - 40);
            else if (p >= 90 && p <= 97) v->look.fg = VT_INDEX(p - 90 + 8);
            else if (p >= 100 && p <= 107) v->look.bg = VT_INDEX(p - 100 + 8);
            break;
        }
    }
}

/* ---- modes ------------------------------------------------------------ */

static void set_mode(Vt *v, int on)
{
    int i;
    for (i = 0; i < v->nparam; i++) {
        int p = v->param[i];
        if (v->priv == '?') {
            switch (p) {
            case 1: v->app_cursor = on; break;
            case 5: v->reverse = on; dirty_all(v); break;
            case 6: v->origin = on; move_to(v, 0, 0); break;
            case 7: v->autowrap = on; break;
            case 25: v->cursor_on = on; v->any_dirty = 1; break;
            case 47: case 1047: use_alt(v, on, on); break;
            case 1048: if (on) save_cursor(v); else restore_cursor(v); break;
            case 1049:
                if (on) {
                    save_cursor(v);
                    use_alt(v, 1, 1);
                    v->saved[1] = v->saved[0];
                } else {
                    use_alt(v, 0, 0);
                    restore_cursor(v);
                }
                break;
            case 2004: v->bracketed = on; break;
            case 9: case 1000: case 1002: case 1003:
                v->mouse = on ? p : 0;  /* the last one asked wins */
                break;
            case 1006: v->mouse_sgr = on; break;
            case 1004: v->focus = on; break;
            case 2026: v->sync = on; v->any_dirty = 1; break;
            case 80: v->sdm = on; break;        /* sixels do not scroll */
            case 8452: v->sright = on; break;   /* cursor right of a sixel */
            default: break;
            }
        } else if (v->priv == 0) {
            if (p == 4) v->insert = on;
            else if (p == 20) v->lnm = on;
        }
    }
}

static int mode_state(Vt *v, int p)
{
    switch (p) {
    case 1: return v->app_cursor ? 1 : 2;
    case 5: return v->reverse ? 1 : 2;
    case 6: return v->origin ? 1 : 2;
    case 7: return v->autowrap ? 1 : 2;
    case 25: return v->cursor_on ? 1 : 2;
    case 1049: return v->alt ? 1 : 2;
    case 2004: return v->bracketed ? 1 : 2;
    case 9: case 1000: case 1002: case 1003: return v->mouse == p ? 1 : 2;
    case 1006: return v->mouse_sgr ? 1 : 2;
    case 1004: return v->focus ? 1 : 2;
    case 2026: return v->sync ? 1 : 2;
    case 80: return v->sdm ? 1 : 2;
    case 8452: return v->sright ? 1 : 2;
    default: return 0;                  /* not recognised */
    }
}

/* ---- CSI ------------------------------------------------------------- */

#define P(i, d) ((i) < v->nparam && v->param[i] > 0 ? v->param[i] : (d))

static void csi(Vt *v, int final)
{
    char buf[48];
    int n = P(0, 1), i;

    if (v->priv2)                       /* Ed's CSI >?NN v/l/h/w: private */
        return;
    if (v->inter == ' ' && final == 'q' &&
        (!v->amigamode || P(0, 0) >= 1)) {
        /* DECSCUSR: 0/1 blinking block, 2 block, 3/4 underline, 5/6 bar
         * (the Amiga console's CSI 0 SP q is a window report: in the
         * Amiga Shell 1-6 set the shape, 0 or none asks for the report) */
        int p = P(0, 0);
        v->cshape = p <= 2 ? 0 : p <= 4 ? 1 : 2;
        v->cblink = p == 0 || p == 1 || p == 3 || p == 5;
        v->any_dirty = 1;
        return;
    }
    if (v->amigamode) {
        /* the Amiga console's own sequences (console.device) */
        if (v->priv == '>' && final == 'm') {   /* the window's colour */
            v->winbg = P(0, 0);
            dirty_all(v);
            return;
        }
        if (v->inter == ' ' && final == 'q') {  /* window bounds report */
            sprintf(buf, "\x9b" "1;1;%d;%d r", v->rows, v->cols);
            reply(v, buf);
            return;
        }
        if (v->inter == ' ' && final == 'p') {  /* cursor on/off */
            v->cursor_on = v->nparam > 0 && v->param[0] == 0 ? 0 : 1;
            v->any_dirty = 1;
            return;
        }
        if (!v->inter && !v->priv && (final == '{' || final == '}')) {
            for (i = 0; i < v->nparam; i++)     /* raw event classes */
                if (v->param[i] >= 0 && v->param[i] < 32) {
                    if (final == '{') v->rawev |= 1UL << v->param[i];
                    else v->rawev &= ~(1UL << v->param[i]);
                }
            return;
        }
        if (!v->inter && !v->priv &&
            (final == 't' || final == 'u' || final == 'x' || final == 'y'))
            return;             /* page/line length, offsets: the window's */
        if (!v->priv && final == 'n' && P(0, 0) == 6) {
            sprintf(buf, "\x9b%d;%dR", v->y + 1, v->x + 1);
            reply(v, buf);
            return;
        }
    }
    if (v->priv == '>' || v->priv == '<' || v->priv == '=') {
        if (final == 'c' && v->priv == '>')
            reply(v, "\x1b[>1;10;0c");          /* DA2: a VT220-ish */
        else if (final == 'q' && v->priv == '>')
            reply(v, "\x1bP>|CTerm 0.1\x1b\\"); /* XTVERSION */
        return;                         /* kitty keys etc.: not here */
    }
    if (v->inter == '$' && final == 'p') {      /* DECRQM */
        sprintf(buf, "\x1b[%s%d;%d$y", v->priv == '?' ? "?" : "",
                P(0, 0), v->priv == '?' ? mode_state(v, P(0, 0)) : 0);
        reply(v, buf);
        return;
    }
    if (v->inter == '!' && final == 'p') {      /* DECSTR soft reset */
        reset_pen(v);
        v->top = 0;
        v->bot = v->rows - 1;
        v->origin = v->insert = 0;
        v->autowrap = v->cursor_on = 1;
        v->app_cursor = v->app_keypad = 0;
        return;
    }
    if (v->inter)                       /* DECSCUSR (" q") and others */
        return;
    if (v->priv == '?' && final == 'S') {
        /* XTSMGRAPHICS: 1 colour registers, 2 the sixel picture's most
         * pixels (read, or reset/set: the same, it cannot change) */
        int what = P(0, 0);
        if (!v->pic_on)
            return;
        if (what == 1)
            reply(v, "\x1b[?1;0;256S");
        else if (what == 2) {
            sprintf(buf, "\x1b[?2;0;%d;%dS", v->cols * v->pcw,
                    v->rows * v->pch);
            reply(v, buf);
        } else {
            sprintf(buf, "\x1b[?%d;1;0S", what);
            reply(v, buf);
        }
        return;
    }
    if (v->priv == '?' && final != 'h' && final != 'l' && final != 'J' &&
        final != 'K' && final != 'n')
        return;                         /* kitty "CSI ? u" and friends */

    switch (final) {
    case '@': insert_chars(v, n); break;
    case 'A': move_lines(v, -n); break;
    case 'B': case 'e': move_lines(v, n); break;
    case 'C': case 'a': move_to(v, v->x + n,
                                v->y - (v->origin ? v->top : 0)); break;
    case 'D': move_to(v, v->x - n, v->y - (v->origin ? v->top : 0)); break;
    case 'E': v->x = 0; move_lines(v, n); break;
    case 'F': v->x = 0; move_lines(v, -n); break;
    case 'G': case '`':
        move_to(v, n - 1, v->y - (v->origin ? v->top : 0)); break;
    case 'H': case 'f': move_to(v, P(1, 1) - 1, P(0, 1) - 1); break;
    case 'd': move_to(v, v->x, n - 1); break;
    case 'I':                           /* stops at the last column, so
                                           CSI 999999 I is not a long loop */
        for (i = 0; i < n && v->x < v->cols - 1; i++) {
            do v->x++; while (v->x < v->cols - 1 && !v->tabs[v->x]);
        }
        v->pending = 0;
        break;
    case 'Z':
        for (i = 0; i < n && v->x > 0; i++) {
            do v->x--; while (v->x > 0 && !v->tabs[v->x]);
        }
        v->pending = 0;
        break;
    case 'J': erase_display(v, P(0, 0)); break;
    case 'K': {
        int how = P(0, 0);
        if (how == 0) erase(v, v->y, v->x, v->cols);
        else if (how == 1) erase(v, v->y, 0, v->x + 1);
        else if (how == 2) erase(v, v->y, 0, v->cols);
        break;
    }
    case 'L':
        if (v->y >= v->top && v->y <= v->bot)
            scroll_down(v, v->y, v->bot, n);
        v->x = 0;
        v->pending = 0;
        break;
    case 'M':
        if (v->y >= v->top && v->y <= v->bot) {
            /* deleting lines is a scroll of the part below - but never
             * into the history, even at the top of the screen */
            scroll_up(v, v->y, v->bot, n, 1);
        }
        v->x = 0;
        v->pending = 0;
        break;
    case 'P': delete_chars(v, n); break;
    case 'X': erase(v, v->y, v->x, v->x + n); break;
    case 'S': scroll_up(v, v->top, v->bot, n, 0); break;
    case 'T': scroll_down(v, v->top, v->bot, n); break;
    case 'b':
        if (v->last_cp)
            for (i = 0; i < n && i < 1000; i++)
                put(v, v->last_cp);
        break;
    case 'c':
        if (P(0, 0) == 0)
            /* DA1: VT220 with ANSI colour, and sixel (4) when pictures
             * are on - what programs look for */
            reply(v, v->pic_on ? "\x1b[?62;4;22c" : "\x1b[?62;22c");
        break;
    case 'g':
        if (P(0, 0) == 0) v->tabs[v->x] = 0;
        else if (P(0, 0) == 3) memset(v->tabs, 0, v->cols);
        break;
    case 'h': set_mode(v, 1); break;
    case 'l': set_mode(v, 0); break;
    case 'm': sgr(v); break;
    case 'n':
        if (P(0, 0) == 5) {
            reply(v, "\x1b[0n");
        } else if (P(0, 0) == 6) {
            sprintf(buf, "\x1b[%s%d;%dR", v->priv == '?' ? "?" : "",
                    v->y + 1 - (v->origin ? v->top : 0), v->x + 1);
            reply(v, buf);
        }
        break;
    case 'r': {
        int t = P(0, 1) - 1, b = P(1, v->rows) - 1;
        if (b >= v->rows) b = v->rows - 1;
        if (t < b) {
            v->top = t;
            v->bot = b;
            move_to(v, 0, 0);
        }
        break;
    }
    case 's': save_cursor(v); break;
    case 'u': restore_cursor(v); break;
    case 't':
        if (P(0, 0) == 18) {
            sprintf(buf, "\x1b[8;%d;%dt", v->rows, v->cols);
            reply(v, buf);
        } else if (P(0, 0) == 14) {     /* the text area in pixels */
            sprintf(buf, "\x1b[4;%d;%dt", v->rows * v->pch, v->cols * v->pcw);
            reply(v, buf);
        } else if (P(0, 0) == 16) {     /* a cell in pixels */
            sprintf(buf, "\x1b[6;%d;%dt", v->pch, v->pcw);
            reply(v, buf);
        }
        break;
    default:
        break;
    }
}

/* ---- ESC ------------------------------------------------------------- */

static void esc(Vt *v, int c)
{
    if (v->inter == '(' || v->inter == ')') {
        v->g[v->inter == ')'] = (char)c;
        return;
    }
    if (v->inter == '%') {
        /* DOCS (ISO 2022, as xterm): ESC % G UTF-8, ESC % @ back to the
         * default - here the tab's own setting (the menu's), so a program
         * (CTermTour) that showed UTF-8 leaves a UTF-8 tab as it was and
         * a Latin-1 one Latin-1 (in the Amiga Shell 0x9B is CSI again) */
        if (c == 'G' || c == '@') {
            v->utf8 = c == 'G' ? 1 : v->utf8_set;
            v->cp437 = c == 'G' ? 0 : v->cp437_set;
            v->uneed = 0;
        }
        return;
    }
    if (v->inter == '#') {
        if (c == '8') {                 /* DECALN: the screen full of E */
            int y, x;
            for (y = 0; y < v->rows; y++)
                for (grow(row(v, y), v->cols), x = 0; x < v->cols; x++) {
                    row(v, y)[x] = blank_of(v);
                    row(v, y)[x].cp = 'E';
                }
            dirty_all(v);
        }
        return;
    }
    if (v->inter)
        return;
    switch (c) {
    case '7': save_cursor(v); break;
    case '8': restore_cursor(v); break;
    case 'D': linefeed(v); break;
    case 'E': v->x = 0; linefeed(v); break;
    case 'M': reverse_index(v); break;
    case 'H': v->tabs[v->x] = 1; break;
    case 'c': full_reset(v); break;
    case '=': v->app_keypad = 1; break;
    case '>': v->app_keypad = 0; break;
    default: break;
    }
}

/* ---- pictures ----------------------------------------------------------
 * Sixel (DCS q) is decoded here; iTerm2's files (OSC 1337;File=) are
 * gathered here and decoded by the host (vt_decode). A picture is cut
 * into cell tiles; the cells hold VT_PIC_CP. The pixels count against
 * the tab's cap: what no cell shows any more goes first, then the
 * oldest picture, its cells blanked (vt.h). */

int (*vt_decode)(const uint8_t *, long, uint32_t **, int *, int *);
void (*vt_decode_free)(uint32_t *);
void *(*vt_palloc)(long n);
void (*vt_pfree)(void *p);

static void *palloc(long n)
{
    return vt_palloc ? vt_palloc(n) : malloc(n);
}

static void pfree(void *p)
{
    if (p)
        vt_pfree ? vt_pfree(p) : free(p);
}

/* small before quick: a picture comes seldom, and its inner loops are
 * plain at any optimisation (CTerm's size: the rule is a few KB) */
#pragma GCC push_options
#pragma GCC optimize("Os")
/* out of the text's way: never inlined into the parser, kept apart
 * (a 68020's 256-byte cache; conbench lost 1.6 % without this) */
#define COLD __attribute__((cold, noinline))

#define PIC_MAXT 511                    /* tiles a side (9 bits a cell) */
#define PIC_KEEP 32                     /* pictures before a clean-up */

/* May pictures take n more bytes? Within the cap (making room) and the
 * host's reserve. Counted at once. */
static COLD int pic_room(Vt *v, long n)
{
    if (n <= 0)
        return 1;
    if (v->pic_bytes + n > v->pic_cap)
        pic_gc(v, n);
    if (v->pic_bytes + n > v->pic_cap || (vt_room && !vt_room(n)))
        return 0;
    v->pic_bytes += n;
    return 1;
}

static COLD void pic_free(Vt *v, VtPic *p)
{
    v->pic_bytes -= (long)p->w * p->h * 3;
    pfree(p->rgb);
    free(p);
}

/* fn on every row that may hold picture cells */
static COLD void pic_rows(Vt *v, void (*fn)(VtCell *, uint8_t *), uint8_t *map)
{
    int i, y;
    for (i = 0; i < 2; i++)
        for (y = 0; v->lines[i] && y < v->rows; y++)
            if (RPIC(v->lines[i][y]))
                fn(v->lines[i][y], map);
    for (i = 0; v->hist && i < v->hist_cap; i++)
        if (v->hist[i] && RPIC(v->hist[i]))
            fn(v->hist[i], map);
}

#define MAPPED(m, id) ((m)[(id) >> 3] & (1 << ((id) & 7)))

static COLD void pic_mark(VtCell *r, uint8_t *map)
{
    int x, n = (int)RLEN(r), any = 0;
    for (x = 0; x < n; x++)
        if (VT_IS_PIC(r[x].cp)) {
            unsigned id = VT_PIC_ID(r[x].cp);
            map[id >> 3] |= 1 << (id & 7);
            any = 1;
        }
    if (!any)
        RPIC(r) = 0;                    /* written over: none there now */
}

static COLD void pic_blank(VtCell *r, uint8_t *map)
{
    int x, n = (int)RLEN(r);
    for (x = 0; x < n; x++)
        if (VT_IS_PIC(r[x].cp) && MAPPED(map, VT_PIC_ID(r[x].cp)))
            r[x].cp = r[x].pen = 0;
}

/* The pictures no cell shows go; then, while need more bytes would pass
 * the cap, the oldest, and their cells become blanks. */
static COLD void pic_gc(Vt *v, long need)
{
    uint8_t *map;
    int i, n = 0, drop = 0;
    if (!v->npics || !(map = calloc(1024, 1)))
        return;
    pic_rows(v, pic_mark, map);
    for (i = 0; i < v->npics; i++)
        if (MAPPED(map, v->pics[i]->id))
            v->pics[n++] = v->pics[i];
        else
            pic_free(v, v->pics[i]);
    v->npics = n;
    memset(map, 0, 1024);
    while (v->npics && v->pic_bytes + need > v->pic_cap) {
        VtPic *p = v->pics[0];
        map[p->id >> 3] |= 1 << (p->id & 7);
        pic_free(v, p);
        memmove(v->pics, v->pics + 1, --v->npics * sizeof *v->pics);
        drop = 1;
    }
    if (drop) {
        pic_rows(v, pic_blank, map);
        dirty_all(v);
    }
    v->pic_last = -1;
    free(map);
}

const VtPic *vt_pic(const Vt *v, uint32_t cp)
{
    int id = VT_PIC_ID(cp), i = v->pic_last;
    if (i >= 0 && i < v->npics && v->pics[i]->id == id)
        return v->pics[i];
    for (i = v->npics - 1; i >= 0; i--)
        if (v->pics[i]->id == id) {
            ((Vt *)v)->pic_last = i;    /* the next cell asks for it too */
            return v->pics[i];
        }
    return NULL;
}

/* Into the table with an id no cell holds. 0: no memory (freed). */
static COLD int pic_add(Vt *v, VtPic *p)
{
    int i;
    if (v->npics >= PIC_KEEP)
        pic_gc(v, 0);
    if (v->npics == v->pics_cap) {
        int nc = v->pics_cap ? v->pics_cap * 2 : 8;
        VtPic **np = realloc(v->pics, nc * sizeof *np);
        if (!np) {
            pic_free(v, p);
            return 0;
        }
        v->pics = np;
        v->pics_cap = nc;
    }
    do {
        v->pic_id = v->pic_id % 8190 + 1;   /* 8191 with 511, 511 would be
                                               VT_WIDE_TAIL */
        for (i = 0; i < v->npics && v->pics[i]->id != v->pic_id; i++)
            ;
    } while (i < v->npics);
    p->id = v->pic_id;
    v->pics[v->npics++] = p;
    return 1;
}

/* At the cursor (sixel scrolling off: the screen's top left), as rows of
 * tiles; the screen scrolls for it. The cursor goes below it (sixel), or
 * right of its last row (8452, iTerm2). */
static COLD void pic_place(Vt *v, VtPic *p, int right)
{
    int nc = (p->w + p->cw - 1) / p->cw, nr = (p->h + p->ch - 1) / p->ch;
    int fixed = !right && v->sdm, x0 = fixed ? 0 : v->x, y = fixed ? 0 : v->y;
    int i, j, nx = nc;
    if (!pic_add(v, p))
        return;
    if (x0 + nx > v->cols)
        nx = v->cols - x0;              /* cut at the right edge */
    v->pending = 0;
    for (i = 0; i < nr && i <= PIC_MAXT; i++) {
        VtCell *r;
        if (i) {
            if (fixed) {
                if (++y >= v->rows)
                    break;              /* cut at the bottom */
            } else {
                linefeed(v);
                y = v->y;
            }
        }
        r = row(v, y);
        grow(r, x0 + nx);
        unsplit(v, r, x0);
        unsplit(v, r, x0 + nx);
        for (j = 0; j < nx; j++) {
            r[x0 + j].cp = VT_PIC_CP(p->id, i, j);
            r[x0 + j].pen = 0;
        }
        RPIC(r) = 1;
        dirty_span(v, y, x0 - 1, x0 + nx);
    }
    if (fixed)
        return;                         /* the cursor stays */
    if (right || v->sright) {
        v->x = x0 + nx;
        if (v->x >= v->cols) {
            v->x = v->cols - 1;
            v->pending = 1;
        }
    } else {
        linefeed(v);
        v->x = x0;
    }
}

/* ---- sixel ---- */

struct Sixel {
    uint8_t *px;                        /* aw x ah, R G B */
    int aw, ah;                         /* made so far */
    int maxw, maxh;                     /* the most it may be */
    int w, h;                           /* drawn so far */
    int rw, rh;                         /* the raster attributes' size */
    int x, y;                           /* the next sixel: column, band */
    uint32_t col, reg[256];
    int cmd, par[5], npar, rep;
    int full;                           /* memory ran out: the rest cut */
};

/* The VT340's 16 colours, in per cent */
static const uint8_t vt340[16][3] = {
    { 0, 0, 0 }, { 20, 20, 80 }, { 80, 13, 13 }, { 20, 80, 20 },
    { 80, 20, 80 }, { 20, 80, 80 }, { 80, 80, 20 }, { 53, 53, 53 },
    { 26, 26, 26 }, { 33, 33, 60 }, { 60, 26, 26 }, { 33, 60, 33 },
    { 60, 33, 60 }, { 33, 60, 60 }, { 60, 60, 33 }, { 80, 80, 80 }
};

static COLD uint32_t pct_rgb(int r, int g, int b)
{
    if (r > 100) r = 100;
    if (g > 100) g = 100;
    if (b > 100) b = 100;
    return (uint32_t)(r * 255 / 100) << 16 | (uint32_t)(g * 255 / 100) << 8 |
           (uint32_t)(b * 255 / 100);
}

/* DEC's HLS: hue 0 is blue, 120 red, 240 green; l, s in per cent */
static COLD uint32_t hls_rgb(int h, int l, int s)
{
    int q, p, k, c[3];
    if (l > 100) l = 100;
    if (s > 100) s = 100;
    h = (h + 240) % 360;
    q = l < 50 ? l * (100 + s) / 100 : l + s - l * s / 100;
    p = 2 * l - q;
    for (k = 0; k < 3; k++) {
        int t = (h + 120 - k * 120 + 360) % 360;
        c[k] = t < 60 ? p + (q - p) * t / 60 : t < 180 ? q
             : t < 240 ? p + (q - p) * (240 - t) / 60 : p;
    }
    return pct_rgb(c[0], c[1], c[2]);
}

/* Room for nw x nh pixels (within the most): FALSE when there is none
 * (s->full: what does not fit is cut). By halves more at a time: a
 * picture without raster attributes comes a band at a time. */
static COLD int sixel_grow(Vt *v, struct Sixel *s, int nw, int nh)
{
    int aw = s->aw, ah = s->ah, y, k, tries;
    uint8_t *np = NULL, bg[3];
    if (nw > s->maxw) nw = s->maxw;
    if (nh > s->maxh) nh = s->maxh;
    if (nw <= aw && nh <= ah)
        return 1;
    if (s->full)
        return 0;
    for (tries = 0; tries < 2 && !np; tries++) {
        int ww = nw < aw ? aw : nw, hh = nh < ah ? ah : nh;
        long add;
        if (!tries && !s->rh) {         /* first by half again (no size
                                           was given) */
            if (ww > aw && ww < aw + aw / 2) ww = aw + aw / 2;
            if (hh > ah && hh < ah + ah / 2) hh = ah + ah / 2;
            if (ww > s->maxw) ww = s->maxw;
            if (hh > s->maxh) hh = s->maxh;
        }
        add = (long)ww * hh * 3 - (long)aw * ah * 3;
        if (!pic_room(v, add))
            continue;
        if (!(np = palloc((long)ww * hh * 3))) {
            v->pic_bytes -= add;
            continue;
        }
        bg[0] = v->pic_bg >> 16; bg[1] = v->pic_bg >> 8; bg[2] = v->pic_bg;
        for (k = 0; k < ww; k++)        /* unset pixels: the background */
            memcpy(np + k * 3, bg, 3);
        for (y = 1; y < hh; y++)
            memcpy(np + (long)y * ww * 3, np, ww * 3);
        for (y = 0; y < ah; y++)
            memcpy(np + (long)y * ww * 3, s->px + (long)y * aw * 3, aw * 3);
        pfree(s->px);
        s->px = np;
        s->aw = ww;
        s->ah = hh;
    }
    if (!np)
        s->full = 1;
    return np != NULL;
}

/* One sixel: a column of six pixels (bits, top first), rep times */
static COLD void sixel_put(Vt *v, struct Sixel *s, int bits)
{
    int n = s->rep, x = s->x, m, i, k;
    s->rep = 1;
    s->x += n;
    if (s->x > (1 << 20))
        s->x = 1 << 20;                 /* !999999 a thousand times */
    if (!bits || x >= s->maxw || s->y >= s->maxh)
        return;
    if (x + n > s->maxw)
        n = s->maxw - x;
    i = bits & 32 ? 6 : bits & 16 ? 5 : bits & 8 ? 4 : bits & 4 ? 3
      : bits & 2 ? 2 : 1;               /* the rows it reaches */
    if (x + n > s->aw || s->y + i > s->ah)
        sixel_grow(v, s, x + n > s->aw ? x + n : s->aw,
                   s->y + i > s->ah ? s->y + i : s->ah);
    m = x + n <= s->aw ? n : s->aw - x;
    if (m <= 0)
        return;
    for (i = 0; i < 6 && s->y + i < s->ah; i++)
        if (bits & 1 << i) {
            uint8_t *p = s->px + ((long)(s->y + i) * s->aw + x) * 3;
            uint8_t r = s->col >> 16, g = s->col >> 8, b = s->col;
            for (k = 0; k < m; k++, p += 3) {
                p[0] = r; p[1] = g; p[2] = b;
            }
            if (s->y + i + 1 > s->h)
                s->h = s->y + i + 1;
        }
    if (x + m > s->w)
        s->w = x + m;
}

/* A command's parameters are all there: " (raster), # (colour), ! */
static COLD void sixel_cmd(Vt *v, struct Sixel *s)
{
    int *a = s->par;
    if (s->cmd == '!')
        s->rep = a[0] > 0 ? a[0] : 1;
    else if (s->cmd == '"') {
        if (s->npar >= 4 && a[2] > 0 && a[3] > 0) {
            s->rw = a[2] < s->maxw ? a[2] : s->maxw;
            s->rh = a[3] < s->maxh ? a[3] : s->maxh;
            sixel_grow(v, s, s->rw, s->rh);
        }
    } else if (s->cmd == '#') {
        uint32_t *r = &s->reg[a[0] & 255];
        if (s->npar >= 5 && a[1] == 2)
            *r = pct_rgb(a[2], a[3], a[4]);
        else if (s->npar >= 5 && a[1] == 1)
            *r = hls_rgb(a[2] % 361, a[3], a[4]);
        s->col = *r;
    }
    s->cmd = 0;
}

static COLD void sixel_byte(Vt *v, int c)
{
    struct Sixel *s = v->sx;
    if (s->cmd) {
        if (c >= '0' && c <= '9') {
            int *p;
            if (!s->npar)
                s->npar = 1;
            p = &s->par[s->npar - 1];
            if (*p < 100000)
                *p = *p * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (!s->npar)
                s->npar = 1;
            if (s->npar < 5)
                s->par[s->npar++] = 0;
            return;
        }
        sixel_cmd(v, s);
    }
    if (c >= '?' && c <= '~')
        sixel_put(v, s, c - '?');
    else if (c == '"' || c == '#' || c == '!') {
        s->cmd = c;
        s->npar = 0;
        memset(s->par, 0, sizeof s->par);
    } else if (c == '$')
        s->x = 0;
    else if (c == '-') {
        s->x = 0;
        if (s->y < (1 << 20))
            s->y += 6;
    }
}

/* DCS P1;P2;P3 q: a sixel picture begins. 0: not now (swallowed). */
static COLD int sixel_begin(Vt *v)
{
    struct Sixel *s;
    int x0 = v->sdm ? 0 : v->x, i;
    if (!v->pic_on || v->pcw <= 0 || v->pch <= 0 ||
        !(s = calloc(1, sizeof *s)))
        return 0;
    s->maxw = (v->cols - x0) * v->pcw;
    if (s->maxw > PIC_MAXT * v->pcw)
        s->maxw = PIC_MAXT * v->pcw;
    s->maxh = (v->sdm ? v->rows : PIC_MAXT) * v->pch;
    for (i = 0; i < 256; i++)
        s->reg[i] = i < 16 ? pct_rgb(vt340[i][0], vt340[i][1], vt340[i][2])
                           : 0;
    s->col = s->reg[7];
    s->rep = 1;
    v->sx = s;
    return 1;
}

/* ST: the picture shown (keep), or dropped */
static COLD void sixel_end(Vt *v, int keep)
{
    struct Sixel *s = v->sx;
    VtPic *p = NULL;
    int w, h, y;
    if (!s)
        return;
    v->sx = NULL;
    if (s->cmd)
        sixel_cmd(v, s);
    w = s->w > s->rw ? s->w : s->rw;
    h = s->h > s->rh ? s->h : s->rh;
    if (w > s->aw) w = s->aw;
    if (h > s->ah) h = s->ah;
    if (keep && w > 0 && h > 0)
        p = calloc(1, sizeof *p);
    if (!p) {
        v->pic_bytes -= (long)s->aw * s->ah * 3;
        pfree(s->px);
        free(s);
        return;
    }
    if (w < s->aw)                      /* the rows close together */
        for (y = 1; y < h; y++)
            memmove(s->px + (long)y * w * 3, s->px + (long)y * s->aw * 3,
                    w * 3);
    /* the rest given back (when there is room for the copy; else it is
     * kept, the picture's size counted) */
    if (w * h < s->aw * s->ah && (p->rgb = palloc((long)w * h * 3))) {
        memcpy(p->rgb, s->px, (size_t)w * h * 3);
        pfree(s->px);
    } else
        p->rgb = s->px;
    v->pic_bytes -= (long)s->aw * s->ah * 3 - (long)w * h * 3;
    p->w = w;
    p->h = h;
    p->cw = v->pcw;
    p->ch = v->pch;
    free(s);
    pic_place(v, p, 0);
}

/* ---- iTerm2's inline files: OSC 1337 ; File = args : base64 ST ---- */

struct FileIn {
    uint8_t *buf;
    long len, cap;
    uint32_t acc;
    int bits, full, inl, keep;          /* inline=1; preserveAspectRatio */
    int wv, hv;                         /* width=, height=: how many */
    char wu, hu;                        /* ... of what: c cells, p pixels,
                                           % of the screen; 0 auto */
};

static COLD void file_arg(int *val, char *unit, const char *s)
{
    int n = 0;
    while (*s >= '0' && *s <= '9' && n < 100000)
        n = n * 10 + (*s++ - '0');
    *val = n;
    *unit = !n ? 0 : *s == 'p' ? 'p' : *s == '%' ? '%' : 'c';
}

/* The arguments before ':' are in v->osc after "1337;File=". */
static COLD int file_begin(Vt *v)
{
    struct FileIn *f;
    char *a = v->osc + 10, *e = v->osc + v->osclen;
    if (!v->pic_on || !vt_decode || v->pcw <= 0 ||
        !(f = calloc(1, sizeof *f)))
        return 0;
    f->keep = 1;
    *e = 0;
    while (a < e) {
        char *k = a, *val, *end = strchr(a, ';');
        if (!end)
            end = e;
        *end = 0;
        val = strchr(k, '=');
        if (val) {
            *val++ = 0;
            if (!strcmp(k, "inline"))
                f->inl = *val == '1';
            else if (!strcmp(k, "preserveAspectRatio"))
                f->keep = *val != '0';
            else if (!strcmp(k, "width"))
                file_arg(&f->wv, &f->wu, val);
            else if (!strcmp(k, "height"))
                file_arg(&f->hv, &f->hu, val);
        }
        a = end + 1;
    }
    v->fi = f;
    return 1;
}

static COLD void file_byte(Vt *v, int c)
{
    struct FileIn *f = v->fi;
    int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26
          : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62
          : c == '/' ? 63 : -1;
    if (d < 0 || f->full || !f->inl)
        return;                         /* '=', line breaks; a download */
    f->acc = f->acc << 6 | d;
    if ((f->bits += 6) < 8)
        return;
    f->bits -= 8;
    if (f->len == f->cap) {
        long nc = f->cap ? f->cap * 2 : 16384;
        uint8_t *nb;
        if (!pic_room(v, nc - f->cap)) {
            f->full = 1;
            return;
        }
        if (!(nb = palloc(nc))) {
            v->pic_bytes -= nc - f->cap;
            f->full = 1;
            return;
        }
        if (f->len)
            memcpy(nb, f->buf, f->len);
        pfree(f->buf);
        f->buf = nb;
        f->cap = nc;
    }
    f->buf[f->len++] = (uint8_t)(f->acc >> f->bits);
}

static COLD int file_size(int val, char unit, int cell, int whole)
{
    return unit == 'c' ? val * cell : unit == 'p' ? val
         : unit == '%' ? (int)((long)whole * val / 100) : 0;
}

/* ST: decoded by the host, sized as asked (and to fit the width left),
 * shown at the cursor */
static COLD void file_end(Vt *v, int keep)
{
    struct FileIn *f = v->fi;
    uint32_t *argb = NULL;
    int w = 0, h = 0, tw, th, x, y, ok;
    VtPic *p;
    if (!f)
        return;
    v->fi = NULL;
    ok = keep && f->inl && !f->full && f->len && vt_decode &&
         vt_decode(f->buf, f->len, &argb, &w, &h) && argb;
    pfree(f->buf);
    v->pic_bytes -= f->cap;
    if (!ok || w <= 0 || h <= 0) {
        if (argb && vt_decode_free)
            vt_decode_free(argb);
        free(f);
        return;
    }
    tw = file_size(f->wv, f->wu, v->pcw, v->cols * v->pcw);
    th = file_size(f->hv, f->hu, v->pch, v->rows * v->pch);
    if (!tw && !th) {
        tw = w;
        th = h;
    } else if (!tw)
        tw = f->keep ? (int)((long)w * th / h) : w;
    else if (!th)
        th = f->keep ? (int)((long)h * tw / w) : h;
    else if (f->keep) {                 /* inside the box, in shape */
        if ((long)tw * h > (long)th * w)
            tw = (int)((long)w * th / h);
        else
            th = (int)((long)h * tw / w);
    }
    x = (v->cols - v->x) * v->pcw;      /* the width left of the screen */
    if (tw > x) {
        th = (int)((long)th * x / tw);
        tw = x;
    }
    if (th > PIC_MAXT * v->pch) {
        tw = (int)((long)tw * PIC_MAXT * v->pch / th);
        th = PIC_MAXT * v->pch;
    }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    free(f);
    p = calloc(1, sizeof *p);
    if (p && pic_room(v, (long)tw * th * 3)) {
        if ((p->rgb = palloc((long)tw * th * 3))) {
            /* nearest pixel, in 16.16 steps (no divide a pixel) */
            uint32_t sx, dx = ((uint32_t)w << 16) / tw;
            uint8_t *d = p->rgb;
            for (y = 0; y < th; y++) {
                const uint32_t *srow = argb + (long)y * h / th * w;
                for (x = 0, sx = 0; x < tw; x++, sx += dx) {
                    uint32_t c = srow[sx >> 16];
                    *d++ = c >> 16;
                    *d++ = c >> 8;
                    *d++ = c;
                }
            }
            p->w = tw;
            p->h = th;
            p->cw = v->pcw;
            p->ch = v->pch;
        } else
            v->pic_bytes -= (long)tw * th * 3;
    }
    if (vt_decode_free)
        vt_decode_free(argb);
    if (p && p->rgb)
        pic_place(v, p, 1);
    else
        free(p);
}

/* A picture's data in one go (the state machine for each byte costs):
 * the bytes taken. */
static void byte(Vt *v, int c);
static COLD int pic_run(Vt *v, const uint8_t *b, int n)
{
    int i = 0;
    if (v->state == S_SIXEL)
        while (i < n && v->state == S_SIXEL) {
            int c = b[i++];
            if (c >= 0x20 && c < 0x7F)
                sixel_byte(v, c);
            else
                byte(v, c);
        }
    else
        while (i < n && v->state == S_FILE) {
            int c = b[i++];
            if (c > 0x20 && c < 0x7F)
                file_byte(v, c);
            else
                byte(v, c);
        }
    return i;
}

void vt_set_pictures(Vt *v, int on, long cap)
{
    v->pic_on = on;
    v->pic_cap = cap;
    pic_gc(v, 0);                       /* a smaller cap: the oldest go */
}

void vt_set_cell_px(Vt *v, int cw, int ch, uint32_t bg)
{
    v->pcw = cw;
    v->pch = ch;
    v->pic_bg = bg & 0xFFFFFF;
}

long vt_pic_bytes(const Vt *v) { return v->pic_bytes; }
int vt_pic_count(const Vt *v) { return v->npics; }
#pragma GCC pop_options

/* OSC 52;targets;base64: the program copied text (vim, tmux) - decoded
 * and kept for the clipboard. A query ("?") is not answered: the host
 * may not read the Amiga's clipboard. */
static void osc_clip(Vt *v, const char *p)
{
    static const char abc[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned long acc = 0;
    int bits = 0, n = 0;
    char *out;
    while (*p && *p != ';')             /* the targets (c, p, s...) */
        p++;
    if (*p++ != ';' || *p == '?')
        return;
    out = malloc(strlen(p) * 3 / 4 + 1);
    if (!out)
        return;
    for (; *p; p++) {
        const char *q = strchr(abc, *p);
        if (!q || *p == 0)
            continue;                   /* '=' and line breaks */
        acc = (acc << 6) | (unsigned long)(q - abc);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (char)((acc >> bits) & 255);
        }
    }
    free(v->clip);
    v->clip = out;
    v->cliplen = n;
}

static void osc_done(Vt *v)
{
    int n = 0, i = 0;
    v->osc[v->osclen] = 0;
    while (v->osc[i] >= '0' && v->osc[i] <= '9') {
        if (n < 100000)                 /* capped, as CSI numbers are */
            n = n * 10 + (v->osc[i] - '0');
        i++;
    }
    if (v->osc[i] == ';' && (n == 0 || n == 2)) {
        strncpy(v->title, v->osc + i + 1, sizeof v->title - 1);
        v->title[sizeof v->title - 1] = 0;
        v->title_changed = 1;
    } else if (v->osc[i] == ';' && n == 52)
        osc_clip(v, v->osc + i + 1);
}

/* ---- control characters ------------------------------------------- */

static void control(Vt *v, int c)
{
    switch (c) {
    case 7: v->bell = 1; break;
    case 8:
        if (v->x > 0) v->x--;
        v->pending = 0;
        break;
    case 9:
        do v->x++; while (v->x < v->cols - 1 && !v->tabs[v->x]);
        if (v->x >= v->cols) v->x = v->cols - 1;
        v->pending = 0;
        break;
    case 11:
        if (v->amigamode) {                 /* the Amiga's VT: one line up */
            if (v->y > 0) v->y--;
            v->pending = 0;
            break;
        }
        /* fall through */
    case 10:
        linefeed(v);
        if (v->lnm) v->x = 0;
        break;
    case 12:
        if (v->amigamode) {                 /* the Amiga's FF: a clear page */
            erase_display(v, 2);
            v->x = v->y = v->pending = 0;
            break;
        }
        linefeed(v);
        if (v->lnm) v->x = 0;
        break;
    case 13: v->x = 0; v->pending = 0; break;
    case 14: v->gl = 1; break;
    case 15: v->gl = 0; break;
    default: break;
    }
}

/* A DCS: its parameters, then sixel's q (or swallowed to ST). */
static void dcs_start(Vt *v)
{
    v->state = S_DCS;
    v->nparam = 0;
    v->param[0] = -1;
    v->priv = v->priv2 = v->inter = 0;
}

/* C1 controls, as bytes in Latin-1 mode or as ESC + letter. */
static int c1(Vt *v, int c)
{
    switch (c) {
    case 0x84: linefeed(v); return 1;
    case 0x85: v->x = 0; linefeed(v); return 1;
    case 0x88: v->tabs[v->x] = 1; return 1;
    case 0x8D: reverse_index(v); return 1;
    case 0x90: dcs_start(v); return 1;         /* DCS */
    case 0x9B:                                  /* CSI */
        v->state = S_CSI;
        v->nparam = 0;
        v->have_digit = 0;
        v->param[0] = -1;
        v->priv = v->priv2 = v->inter = 0;
        return 1;
    case 0x9D: v->state = S_OSC; v->osclen = 0; return 1;
    case 0x98: case 0x9E: case 0x9F: v->state = S_STR; return 1;
    case 0x9C: return 1;                        /* ST alone */
    }
    return 0;
}

/* The PC's character set (code page 437), 0x80-0xFF, as BBSes send it. */
static const uint16_t cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

/* ---- the byte loop --------------------------------------------------- */

static void start_csi(Vt *v)
{
    c1(v, 0x9B);
}

static void ground_char(Vt *v, uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7F) {
        if (cp == 0x1B)
            v->state = S_ESC, v->inter = 0;
        else if (cp != 0x7F)
            control(v, (int)cp);
        return;
    }
    if (cp >= 0x80 && cp < 0xA0) {
        c1(v, (int)cp);
        return;
    }
    put(v, cp);
}

/* Bytes that are not UTF-8: U+FFFD - but in the Amiga Shell, whose
 * programs write Latin-1 (dir, the Shell's own echo of an å), the Latin-1
 * letters they are; 0x80-0x9F there are the Amiga's C1 controls. */
static void not_utf8(Vt *v, const uint8_t *b, int n)
{
    int i;
    if (!v->amigamode) {
        put(v, 0xFFFD);
        return;
    }
    for (i = 0; i < n; i++)
        if (b[i] >= 0xA0)
            put(v, b[i]);
        else if (b[i] >= 0x80)
            c1(v, b[i]);
}

static void byte(Vt *v, int c)
{
    switch (v->state) {
    case S_GROUND:
        if (v->utf8 && c >= 0x80) {
            if (v->uneed) {
                /* in the Amiga Shell a Latin-1 letter C2-DF (Ä, Å, Ö)
                 * then the console's 9B CSI is not a 2-byte character
                 * (C5 9B K: Å, erase to the end - not "śK") */
                if ((c & 0xC0) == 0x80 &&
                    !(c == 0x9B && v->amigamode && v->umin == 0x80)) {
                    v->uacc = (v->uacc << 6) | (c & 0x3F);
                    v->ubuf[v->ulen++] = (uint8_t)c;
                    if (--v->uneed == 0) {
                        uint32_t u = v->uacc;
                        /* too long a form (C0 9B is not ESC), a surrogate,
                         * past Unicode: not a character. U+0080-009F are
                         * C1 controls, as in xterm. */
                        if (u < v->umin || (u >= 0xD800 && u <= 0xDFFF) ||
                            u > 0x10FFFF)
                            not_utf8(v, v->ubuf, v->ulen);
                        else
                            ground_char(v, u);
                    }
                    return;
                }
                v->uneed = 0;
                not_utf8(v, v->ubuf, v->ulen);  /* a broken sequence */
                if (v->state != S_GROUND) {
                    byte(v, c);
                    return;
                }
            }
            v->ulen = 1;
            v->ubuf[0] = (uint8_t)c;
            if ((c & 0xE0) == 0xC0) {
                v->uacc = c & 0x1F; v->uneed = 1; v->umin = 0x80;
            } else if ((c & 0xF0) == 0xE0) {
                v->uacc = c & 0x0F; v->uneed = 2; v->umin = 0x800;
            } else if ((c & 0xF8) == 0xF0) {
                v->uacc = c & 0x07; v->uneed = 3; v->umin = 0x10000;
            } else
                not_utf8(v, v->ubuf, 1);
            return;
        }
        if (v->uneed) {
            v->uneed = 0;
            not_utf8(v, v->ubuf, v->ulen);
            if (v->state != S_GROUND) {
                byte(v, c);             /* a replayed 9B began a CSI */
                return;
            }
        }
        ground_char(v, (uint32_t)c);
        return;

    case S_ESC:
        if (c >= 0x20 && c <= 0x2F) {
            v->inter = (char)c;
            v->state = S_ESC_INT;
            return;
        }
        v->state = S_GROUND;
        if (c == '[') { start_csi(v); return; }
        if (c == ']') { v->state = S_OSC; v->osclen = 0; return; }
        if (c == 'P') {
            dcs_start(v);
            return;
        }
        if (c == 'X' || c == '^' || c == '_') {
            v->state = S_STR;           /* APC too: kitty's pictures are
                                           swallowed, never shown as text */
            return;
        }
        if (c == '\\')
            return;                     /* a lone ST */
        if (c == 0x1B) { v->state = S_ESC; return; }  /* starts again */
        if (c < 0x20) { control(v, c); return; }
        esc(v, c);
        return;

    case S_ESC_INT:
        if (c >= 0x20 && c <= 0x2F)
            return;
        v->state = S_GROUND;
        esc(v, c);
        v->inter = 0;
        return;

    case S_CSI:
        if (c >= '0' && c <= '9') {
            if (v->nparam == 0) v->nparam = 1;
            if (v->param[v->nparam - 1] < 0) v->param[v->nparam - 1] = 0;
            if (v->param[v->nparam - 1] < 100000)
                v->param[v->nparam - 1] =
                    v->param[v->nparam - 1] * 10 + (c - '0');
            return;
        }
        if (c == ';' || c == ':') {
            if (v->nparam == 0) v->nparam = 1;
            if (v->nparam < MAXPARAM) v->param[v->nparam++] = -1;
            return;
        }
        if (c >= '<' && c <= '?') {
            if (v->priv)
                v->priv2 = (char)c;
            else
                v->priv = (char)c;
            return;
        }
        if (c >= 0x20 && c <= 0x2F) {
            v->inter = (char)c;
            return;
        }
        if (c >= 0x40 && c <= 0x7E) {
            v->state = S_GROUND;
            csi(v, c);
            return;
        }
        if (c == 0x18 || c == 0x1A) { v->state = S_GROUND; return; }
        if (c == 0x1B) { v->state = S_ESC; v->inter = 0; return; }
        if (c < 0x20) control(v, c);   /* C0 inside a CSI: done anyway */
        return;

    case S_OSC:
        /* 0x9C ends it only in Latin-1: in UTF-8 it is part of a letter
         * (the title "\u2733 Claude Code" holds one) */
        if (c == 7 || (c == 0x9C && !v->utf8)) {
            osc_done(v);
            v->state = S_GROUND;
            return;
        }
        if (c == 0x1B) { v->state = S_OSC_ESC; return; }
        if (c == ':' && v->osclen >= 10 && !memcmp(v->osc, "1337;File=", 10) &&
            v->osclen < OSCMAX - 1 && file_begin(v)) {
            v->state = S_FILE;          /* iTerm2's picture: its base64 */
            return;
        }
        if (v->osclen < OSCMAX - 1)
            v->osc[v->osclen++] = (char)c;
        return;

    case S_OSC_ESC:
        osc_done(v);
        /* fall through */
    case S_STR_ESC:
        /* ESC ends the string and starts a sequence: ESC \\ is ST (a lone
         * ST in S_ESC), anything else is the sequence it begins (xterm) */
        v->state = S_ESC;
        v->inter = 0;
        byte(v, c);
        return;

    case S_STR:
        if (c == 0x1B) v->state = S_STR_ESC;
        else if (c == 0x9C && !v->utf8) v->state = S_GROUND;
        return;

    case S_DCS:
        if (c >= '0' && c <= '9') {
            if (v->nparam == 0) v->nparam = 1;
            if (v->param[v->nparam - 1] < 0) v->param[v->nparam - 1] = 0;
            if (v->param[v->nparam - 1] < 100000)
                v->param[v->nparam - 1] = v->param[v->nparam - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (v->nparam == 0) v->nparam = 1;
            if (v->nparam < MAXPARAM) v->param[v->nparam++] = -1;
        } else if (c >= 0x20 && c <= 0x3F)
            v->inter = (char)c;
        else if (c >= 0x40 && c <= 0x7E)
            v->state = c == 'q' && !v->inter && sixel_begin(v) ? S_SIXEL
                                                              : S_STR;
        else if (c == 0x1B)
            v->state = S_STR_ESC;
        else if (c == 0x9C && !v->utf8)
            v->state = S_GROUND;
        return;

    case S_SIXEL:
        if (c == 0x1B)
            v->state = S_SIXEL_ESC;
        else if ((c == 0x9C && !v->utf8) || c == 0x18 || c == 0x1A) {
            sixel_end(v, c == 0x9C);    /* ST; CAN and SUB cancel it */
            v->state = S_GROUND;
        } else if (c >= 0x20 && c < 0x7F)
            sixel_byte(v, c);
        return;

    case S_FILE:
        if (c == 0x1B)
            v->state = S_FILE_ESC;
        else if (c == 7 || (c == 0x9C && !v->utf8) || c == 0x18 || c == 0x1A) {
            file_end(v, c == 7 || c == 0x9C);
            v->state = S_GROUND;
        } else
            file_byte(v, c);
        return;

    case S_SIXEL_ESC:
    case S_FILE_ESC:
        /* ESC \ is ST; anything else ends the picture too and starts its
         * own sequence */
        if (v->state == S_SIXEL_ESC)
            sixel_end(v, 1);
        else
            file_end(v, 1);
        v->state = S_ESC;
        v->inter = 0;
        byte(v, c);
        return;
    }
}

void vt_write(Vt *v, const uint8_t *buf, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        int c = buf[i];
        /* The fast lane: a run of plain ASCII text in the normal state
         * goes straight into the row - most output is just this, and
         * put()'s general checks (width, line drawing, insert) cost. A
         * PC BBS's high bytes (code page 437) take it too: all are one
         * cell wide. */
        if (((c >= 0x20 && c < 0x7F) || (c >= 0x80 && v->cp437)) &&
            v->state == S_GROUND && !v->uneed &&
            v->g[v->gl] != '0' && !v->insert) {
            /* whole runs at a time: on a 14 MHz Amiga every instruction
             * per letter shows */
            const int cols = v->cols;
            for (;;) {
                VtCell *r, *d;
                uint16_t pn;
                const uint8_t *p = buf + i, *e = buf + len;
                int lim, x, l;
                if (v->pending && v->autowrap) {
                    RWRAP(row(v, v->y)) = 1;
                    v->x = 0;
                    linefeed(v);
                }
                v->pending = 0;
                r = row(v, v->y);
                x = v->x;
                l = (int)RLEN(r);
                if (l < x)
                    grow(r, x);         /* a gap before the run: blanks */
                else if (x > 0 && x < l && r[x].cp == VT_WIDE_TAIL)
                    r[x - 1] = blank_of(v);
                /* taken after the wrap and the blanks: either may intern
                 * a look, and a full table is renumbered (pen_gc) */
                pn = cur_pen(v);
                d = r + x;
                lim = cols - x;
                /* one pass: check a byte, store its cell */
                if (v->cp437)
                    while (lim > 0 && p < e) {
                        uint8_t b = *p;
                        if (b >= 0x80)
                            d->cp = cp437[b - 0x80];
                        else if ((uint8_t)(b - 0x20) < 0x5F)
                            d->cp = b;
                        else
                            break;
                        p++;
                        d->pen = pn;
                        d++;
                        lim--;
                    }
                else
                    while (lim > 0 && p < e && (uint8_t)(*p - 0x20) < 0x5F) {
                        d->cp = *p++;
                        d->pen = pn;
                        d++;
                        lim--;
                    }
                x = (int)(d - r);
                if (x > l)
                    RLEN(r) = x;
                else if (x < l && r[x].cp == VT_WIDE_TAIL)
                    r[x] = blank_of(v);
                dirty_span(v, v->y, v->x - 1, x);
                if (x >= cols) {
                    v->x = cols - 1;
                    v->pending = 1;
                } else
                    v->x = x;
                i = (int)(p - buf);
                if (i >= len || ((uint8_t)(buf[i] - 0x20) >= 0x5F &&
                                 !(buf[i] >= 0x80 && v->cp437)))
                    break;
            }
            i--;                        /* the for loop steps past it */
            v->any_dirty = 1;
            v->last_cp = buf[i] >= 0x80 ? cp437[buf[i] - 0x80] : buf[i];
            continue;
        }
        if (v->state >= S_SIXEL && (v->state == S_SIXEL || v->state == S_FILE)) {
            i += pic_run(v, buf + i, len - i) - 1;
            continue;
        }
        /* a whole CSI with plain parameters inside this write: parsed
         * here in one go, not byte by byte through the state machine
         * (colour changes, cursor moves - most of what programs send) */
        if (v->state == S_GROUND && !v->uneed &&
            ((c == 0x1B && i + 1 < len && buf[i + 1] == '[') ||
             (c == 0x9B && (!v->utf8 || v->amigamode) && !v->cp437))) {
            int j = i + (c == 0x1B ? 2 : 1), np = 0, ok = 0;
            int par[MAXPARAM];
            par[0] = -1;
            while (j < len) {
                int d = buf[j];
                if (d >= '0' && d <= '9') {
                    if (np == 0) np = 1;
                    if (par[np - 1] < 0) par[np - 1] = 0;
                    if (par[np - 1] < 100000)
                        par[np - 1] = par[np - 1] * 10 + (d - '0');
                } else if (d == ';') {
                    if (np == 0) np = 1;
                    if (np < MAXPARAM) par[np++] = -1;
                } else {
                    ok = d >= 0x40 && d <= 0x7E;
                    break;
                }
                j++;
            }
            if (ok) {
                memcpy(v->param, par, (np ? np : 1) * sizeof par[0]);
                v->nparam = np;
                v->have_digit = 0;
                v->priv = v->priv2 = v->inter = 0;
                v->state = S_GROUND;
                csi(v, buf[j]);
                i = j;
                continue;
            }
        }
        /* a run of new lines: one scroll of them all (scroll-nl, blank
         * lines from programs) instead of a scroll per line */
        if (c == 10 && v->state == S_GROUND && !v->uneed && i + 1 < len &&
            buf[i + 1] == 10 && v->y <= v->bot) {
            int k = 1, d, s, h = v->bot - v->top + 1;
            while (i + k < len && buf[i + k] == 10)
                k++;
            i += k - 1;
            v->pending = 0;
            d = v->bot - v->y;
            if (k <= d)
                v->y += k;
            else {
                v->y = v->bot;
                for (s = k - d; s > 0; s -= h > 1 ? h - 1 : 1)
                    scroll_up(v, v->top, v->bot,
                              h > 1 && s > h - 1 ? h - 1 : s, 0);
            }
            if (v->lnm)
                v->x = 0;
            continue;
        }
        /* the PC's code page: every high byte is a character */
        if (v->cp437 && c >= 0x80 && v->state == S_GROUND) {
            put(v, cp437[c - 0x80]);
            continue;
        }
        /* Latin-1 (the Amiga's ANSI): bytes 0x80-0x9F are C1 controls -
         * in the Amiga Shell in UTF-8 too, where no character starts with
         * one (the console's 0x9B CSI: line editing, Tab's menu) */
        if ((!v->utf8 || (v->amigamode && !v->uneed)) &&
            v->state == S_GROUND && c >= 0x80 && c < 0xA0) {
            c1(v, c);
            continue;
        }
        byte(v, c);
    }
}

/* ---- life and size --------------------------------------------------- */

static void free_screen(VtCell **l, int rows)
{
    int y;
    if (!l)
        return;
    for (y = 0; y < rows; y++)
        row_free(l[y]);
    free(l);
}

static VtCell **alloc_screen(int cols, int rows)
{
    VtCell **l = calloc(rows, sizeof *l);
    int y;
    if (!l)
        return NULL;
    for (y = 0; y < rows; y++)
        if (!(l[y] = row_new(cols))) {
            free_screen(l, rows);
            return NULL;
        }
    return l;
}

/* The pen table's room: 64 looks at first, doubled as programs use more,
 * up to 32768. Full there: the looks no cell uses any more are dropped
 * and the rest numbered again (every cell rewritten) - rare: it takes a
 * picture drawn in 24-bit colours. 0: nothing could be freed. */
#ifndef PEN_MAX
#define PEN_MAX 32768                   /* tests set it low to try the clean-up */
#endif
static int pen_room(Vt *v, int cap)
{
    VtPen *np = realloc(v->pens, cap * sizeof *np);
    uint16_t *nh;
    int i;
    if (!np)
        return 0;
    v->pens = np;
    nh = calloc(2 * cap, sizeof *nh);
    if (!nh)
        return 0;
    free(v->phash);
    v->phash = nh;
    v->pcap = cap;
    for (i = 1; i < v->npens; i++) {
        unsigned m = 2 * cap - 1, h = pen_hash(&v->pens[i]) & m;
        while (nh[h])
            h = (h + 1) & m;
        nh[h] = (uint16_t)(i + 1);
    }
    return 1;
}

static void pen_mark(uint16_t *map, VtCell *r, int remap)
{
    int x, n = r ? (int)RLEN(r) : 0;
    for (x = 0; x < n; x++)
        if (remap)
            r[x].pen = map[r[x].pen];
        else
            map[r[x].pen] = 1;
}

static void pen_walk(Vt *v, uint16_t *map, int remap)
{
    int i, y;
    for (i = 0; i < 2; i++)
        for (y = 0; v->lines[i] && y < v->rows; y++)
            pen_mark(map, v->lines[i][y], remap);
    for (i = 0; v->hist && i < v->hist_cap; i++)
        pen_mark(map, v->hist[i], remap);
}

static int pen_gc(Vt *v)
{
    uint16_t *map;
    int i, n = 1;
    if (v->pcap < PEN_MAX)
        return pen_room(v, v->pcap * 2);
    map = calloc(v->pcap, sizeof *map);
    if (!map)
        return 0;
    pen_walk(v, map, 0);
    map[0] = 0;                         /* the default stays 0 */
    for (i = 1; i < v->npens; i++)
        if (map[i]) {
            v->pens[n] = v->pens[i];
            map[i] = (uint16_t)n++;
        }
    pen_walk(v, map, 1);
    free(map);
    i = n < v->npens;
    v->npens = n;
    v->bpen_bg = 0xFFFFFFFFu;           /* no look has it: made again */
    v->pen_stale = 1;
    pen_room(v, v->pcap);               /* the hash, for the new numbers */
    return i;
}

const VtPen *vt_pens(const Vt *v)
{
    return v->pens;
}

Vt *vt_new(int cols, int rows, int scrollback)
{
    Vt *v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    v->cols = cols;
    v->rows = rows;
    v->npens = 1;                       /* pen 0: the default look */
    if (!pen_room(v, 64)) {
        vt_free(v);
        return NULL;
    }
    memset(&v->pens[0], 0, sizeof v->pens[0]);
    v->bpen_bg = 0xFFFFFFFFu;
    v->pcw = 8;                         /* until the host says */
    v->pch = 16;
    v->pic_last = -1;
    v->lines[0] = alloc_screen(cols, rows);
    v->lines[1] = NULL;                 /* made when first used */
    v->tabs = calloc(cols, 1);
    v->dirty = calloc(rows, 1);
    v->dx0 = calloc(rows, 2);
    v->dx1 = calloc(rows, 2);
    v->hist_cap = v->hist_max = scrollback;
    if (scrollback > 0) {
        v->hist = calloc(scrollback, sizeof *v->hist);
        v->hist_w = calloc(scrollback, sizeof *v->hist_w);
    }
    if (!v->lines[0] || !v->tabs || !v->dirty ||
        !v->dx0 || !v->dx1 ||
        (scrollback > 0 && (!v->hist || !v->hist_w))) {
        vt_free(v);
        return NULL;
    }
    v->cur = v->lines[0];
    v->utf8 = v->utf8_set = 1;
    full_reset(v);
    return v;
}

void vt_free(Vt *v)
{
    int i;
    if (!v)
        return;
    sixel_end(v, 0);
    file_end(v, 0);
    for (i = 0; i < v->npics; i++)
        pic_free(v, v->pics[i]);
    free(v->pics);
    for (i = 0; i < v->hist_cap && v->hist; i++)
        row_free(v->hist[i]);
    free(v->clip);
    free(v->hist);
    free(v->hist_w);
    free_screen(v->lines[0], v->rows);
    free_screen(v->lines[1], v->rows);
    free(v->tabs);
    free(v->dirty);
    free(v->dx0);
    free(v->dx1);
    free(v->pens);
    free(v->phash);
    free(v);
}

/* ---- reflow: the main screen and its history joined into lines again
 * and wrapped at a new width (xterm and every modern terminal do it) - */

typedef struct {                        /* rows being made */
    VtCell **r;
    int n, cap;
} RowList;

static int rl_add(RowList *l, VtCell *r)
{
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 64;
        VtCell **nr = realloc(l->r, nc * sizeof *nr);
        if (!nr)
            return 0;
        l->r = nr;
        l->cap = nc;
    }
    l->r[l->n++] = r;
    return 1;
}

/* One logical line (cells c[0..len)) wrapped into rows of ncols; the
 * cursor at offset coff (-1: not in it) comes back in *cr, *cc. */
static int wrap_line(RowList *out, const VtCell *c, int len, int ncols,
                     int coff, int *cr, int *cc)
{
    VtCell *r = row_new(ncols);
    int j, x = 0;
    if (!r || !rl_add(out, r))
        return 0;
    for (j = 0; j <= len; j++) {
        /* a wide letter does not split over two rows: on to the next */
        int wide = j + 1 < len && c[j + 1].cp == VT_WIDE_TAIL;
        if (j < len && (x == ncols || (wide && x == ncols - 1))) {
            RLEN(r) = x;
            RWRAP(r) = 1;
            r = row_new(ncols);
            if (!r || !rl_add(out, r))
                return 0;
            x = 0;
        }
        if (j == coff) {
            *cr = out->n - 1;
            *cc = x;
        }
        if (j == len)
            break;
        r[x++] = c[j];
    }
    RLEN(r) = x;
    RWRAP(r) = 0;
    return 1;
}

/* The main screen and history at ncols wide and nrows high, into ng (the
 * new screen rows); the main screen's cursor in *px, *py, *pp (pending)
 * moves with its text. FALSE: no memory, nothing changed. */
static int reflow(Vt *v, int ncols, int nrows, VtCell **ng,
                  int *px, int *py, int *pp)
{
    int H = v->hist_len, S = v->rows, total = H + S, cur = H + *py;
    int last = cur, i, len = 0, cap = 0, coff = -1, cr = -1, cc = 0;
    int start, nkeep, ok = 1;
    VtCell *line = NULL;
    RowList out = { NULL, 0, 0 };
    VtCell **src = malloc(total * sizeof *src);
    if (!src)
        return 0;
    for (i = 0; i < H; i++)
        src[i] = v->hist[(v->hist_head + i) % v->hist_cap];
    for (i = 0; i < S; i++)
        src[H + i] = v->lines[0][ri_main(v, i)];
    for (i = cur + 1; i < total; i++)   /* the screen's last written row */
        if (src[i] && RLEN(src[i]) > 0)
            last = i;
    /* join rows into lines, wrap each again */
    for (i = 0; i <= last && ok; i++) {
        const VtCell *r = src[i];
        int n = r ? (int)RLEN(r) : 0, need = len + n + 1;
        if (i == cur && len + *px + 2 > need)
            need = len + *px + 2;       /* blanks up to the cursor */
        if (need > cap) {
            int nc = need * 2;
            VtCell *nl = realloc(line, nc * sizeof *nl);
            if (!nl) { ok = 0; break; }
            line = nl;
            cap = nc;
        }
        if (i == cur) {                 /* the cursor's place in the text */
            int x = *px + (*pp ? 1 : 0);
            coff = len + x;
            if (x > n)                  /* past the row's end: blanks */
                memset(line + len + n, 0, (x - n) * sizeof *line);
        }
        if (n)
            memcpy(line + len, r, n * sizeof *line);
        len += n;
        if (i == cur && coff > len)
            len = coff;                 /* the blanks up to the cursor */
        if (r && RWRAP(r) && i < last)
            continue;                   /* the line goes on */
        if (!wrap_line(&out, line, len, ncols, coff, &cr, &cc))
            ok = 0;
        len = 0;
        coff = -1;
    }
    free(line);
    free(src);
    if (!ok || out.n == 0) {
        for (i = 0; i < out.n; i++)
            row_free(out.r[i]);
        free(out.r);
        return 0;
    }
    if (cr < 0) {
        cr = out.n - 1;
        cc = 0;
    }
    if (v->npics)                       /* the new rows' picture marks */
        for (i = 0; i < out.n; i++) {
            int x;
            for (x = 0; x < (int)RLEN(out.r[i]); x++)
                if (VT_IS_PIC(out.r[i][x].cp)) {
                    RPIC(out.r[i]) = 1;
                    break;
                }
        }
    /* the screen is the last nrows rows, the cursor's always among them */
    start = out.n > nrows ? out.n - nrows : 0;
    if (cr < start)
        start = cr;
    /* the history: what is above the screen (the newest hist_cap) */
    for (i = 0; i < v->hist_cap; i++) {
        row_free(v->hist[i]);
        v->hist[i] = NULL;
        v->hist_w[i] = 0;
    }
    v->hist_head = 0;
    v->hist_len = 0;
    nkeep = start < v->hist_cap ? start : v->hist_cap;
    for (i = 0; i < start; i++) {
        if (i < start - nkeep) {
            row_free(out.r[i]);
            continue;
        }
        v->hist[v->hist_len] = out.r[i];
        v->hist_w[v->hist_len] = RLEN(out.r[i]);
        v->hist_len++;
    }
    for (i = 0; i < nrows; i++) {       /* the screen's rows */
        if (start + i < out.n) {
            row_free(ng[i]);
            ng[i] = out.r[start + i];
        }
    }
    for (i = start + nrows; i < out.n; i++)
        row_free(out.r[i]);             /* below the screen: gone */
    free(out.r);
    *py = cr - start;
    *px = cc;
    *pp = 0;
    if (*px >= ncols) {
        *px = ncols - 1;
        *pp = 1;
    }
    return 1;
}

void vt_resize(Vt *v, int cols, int rows)
{
    VtCell **ng[2];
    uint8_t *nt, *nd;
    uint16_t *nx0, *nx1;
    int i, y, shift[2] = { 0, 0 }, w, flowed = 0;
    /* the main screen's cursor: while the alternate screen is up, the one
     * saved on the way in (CSI ?1049h) - the main screen is resized as it
     * would be without the alternate screen */
    Saved *ms = &v->saved[0];
    int mx = v->alt ? ms->x : v->x, my = v->alt ? ms->y : v->y;
    int mp = v->alt ? ms->pending : v->pending;

    if (cols == v->cols && rows == v->rows)
        return;
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    ng[0] = alloc_screen(cols, rows);
    ng[1] = v->lines[1] ? alloc_screen(cols, rows) : NULL;
    nt = calloc(cols, 1);
    nd = calloc(rows, 1);
    nx0 = calloc(rows, 2);
    nx1 = calloc(rows, 2);
    if (!ng[0] || (v->lines[1] && !ng[1]) || !nt || !nd || !nx0 || !nx1) {
        free_screen(ng[0], rows); free_screen(ng[1], rows);
        free(nt); free(nd); free(nx0); free(nx1);
        return;
    }
    /* a new width on the main screen: the text flows again; the alternate
     * screen (vim, htop...) is its program's to redraw, so it is cut */
    if (cols != v->cols && reflow(v, cols, rows, ng[0], &mx, &my, &mp)) {
        flowed = 1;
        free_screen(v->lines[0], v->rows);
        v->lines[0] = ng[0];
        v->base[0] = 0;
    } else if (my >= rows) {
        /* fewer rows: lines above the cursor go to the history first, so
         * the cursor's line stays on screen (as xterm does) */
        shift[0] = my - rows + 1;
        for (i = 0; i < shift[0]; i++)
            push_history(v, v->lines[0][(v->base[0] + i) % v->rows]);
    }
    if (v->alt && v->y >= rows)
        shift[1] = v->y - rows + 1;     /* the alternate screen's: dropped */
    w = cols < v->cols ? cols : v->cols;
    for (i = flowed ? 1 : 0; i < 2; i++) {
        if (!v->lines[i])
            continue;
        for (y = 0; y < rows && y + shift[i] < v->rows; y++) {
            int oi = v->base[i] + y + shift[i];
            const VtCell *o;
            int n;
            if (oi >= v->rows)
                oi -= v->rows;
            o = v->lines[i][oi];
            n = (int)RLEN(o) < w ? (int)RLEN(o) : w;
            memcpy(ng[i][y], o, n * sizeof(VtCell));
            RLEN(ng[i][y]) = n;
            RWRAP(ng[i][y]) = RWRAP(o);
            RPIC(ng[i][y]) = RPIC(o);
        }
        free_screen(v->lines[i], v->rows);
        v->lines[i] = ng[i];
        v->base[i] = 0;
    }
    free(v->tabs);
    free(v->dirty);
    free(v->dx0);
    free(v->dx1);
    v->tabs = nt;
    v->dirty = nd;
    v->dx0 = nx0;
    v->dx1 = nx1;
    v->cols = cols;
    v->rows = rows;
    v->cur = v->lines[v->alt];
    reset_tabs(v);
    if (!flowed) {
        my -= shift[0];
        if (mx >= cols) mx = cols - 1;
        if (my >= rows) my = rows - 1;
        mp = 0;
    }
    if (v->alt) {
        ms->x = mx;
        ms->y = my;
        ms->pending = mp;
        v->y -= shift[1];
        if (v->x >= cols) v->x = cols - 1;
        if (v->y >= rows) v->y = rows - 1;
        v->pending = 0;
    } else {
        v->x = mx;
        v->y = my;
        v->pending = mp;
    }
    v->top = 0;
    v->bot = rows - 1;
    for (i = 0; i < 2; i++) {
        if (v->saved[i].x >= cols) v->saved[i].x = cols - 1;
        if (v->saved[i].y >= rows) v->saved[i].y = rows - 1;
    }
    v->s_n = 0;
    v->s_full = 0;
    dirty_all(v);
}

/* ---- for the display ------------------------------------------------- */

void vt_set_utf8(Vt *v, int on)
{
    v->utf8 = v->utf8_set = on;
    v->uneed = 0;
    v->cp437 = v->cp437_set = 0;
}

/* A PC BBS's bytes: code page 437 (box lines, blocks, accents). */
void vt_set_cp437(Vt *v, int on)
{
    v->cp437 = v->cp437_set = on;
    if (on) {
        v->utf8 = v->utf8_set = 0;
        v->uneed = 0;
    }
}

/* The Amiga console's ways, for the local Shell: Latin-1 with 0x9B as
 * CSI, LF as a new line, FF a clear page, VT a line up, and its own
 * sequences (window bounds report, cursor rendition, raw events). */
void vt_set_amiga(Vt *v, int on)
{
    v->amigamode = on;
    if (on) {
        v->utf8 = v->utf8_set = 0;
        v->uneed = 0;
        v->lnm = 1;
    } else
        v->lnm = 0;
}

unsigned long vt_raw_events(const Vt *v) { return v->rawev; }
int vt_window_bg(const Vt *v) { return v->winbg; }
int vt_cols(const Vt *v) { return v->cols; }
int vt_rows(const Vt *v) { return v->rows; }
int vt_history(const Vt *v) { return v->hist_len; }
int vt_app_cursor(const Vt *v) { return v->app_cursor; }
int vt_app_keypad(const Vt *v) { return v->app_keypad; }
int vt_bracketed_paste(const Vt *v) { return v->bracketed; }
int vt_alt_screen(const Vt *v) { return v->alt; }
int vt_reverse_video(const Vt *v) { return v->reverse; }
int vt_mouse(const Vt *v, int *sgr) { *sgr = v->mouse_sgr; return v->mouse; }
int vt_focus(const Vt *v) { return v->focus; }
int vt_sync(const Vt *v) { return v->sync; }
int vt_cursor_shape(const Vt *v, int *blink)
{
    *blink = v->cblink;
    return v->cshape;
}

const VtCell *vt_line(const Vt *v, int r, int back, int *width)
{
    int idx = r - back;                 /* < 0: from the history */
    if (back > v->hist_len)
        back = v->hist_len, idx = r - back;
    if (idx < 0 && !v->alt) {
        int h = v->hist_len + idx;
        int slot;
        if (h < 0) {
            *width = 0;
            return NULL;
        }
        slot = (v->hist_head + h) % v->hist_cap;
        *width = v->hist_w[slot];
        return v->hist[slot];
    }
    if (idx < 0)
        idx = 0;
    idx = ri(v, idx);
    *width = RLEN(v->cur[idx]);       /* past it: blank */
    return v->cur[idx];
}

void vt_cursor(const Vt *v, int *x, int *y, int *visible)
{
    *x = v->x;
    *y = v->y;
    *visible = v->cursor_on;
}

/* The rows' marks are copied into static buffers, one set for all tabs:
 * the next call (any tab) overwrites them. Both callers (draw.c's
 * redraw_all and refresh) use them before calling again. */
int vt_take_changes(Vt *v, VtChanges *ch)
{
    static uint8_t copy[256];           /* rows: as panes' PROWS */
    static uint16_t c0[256], c1s[256];
    int any = v->any_dirty;
    int n = v->rows < (int)sizeof copy ? v->rows : (int)sizeof copy;
    memcpy(copy, v->dirty, n);
    memcpy(c0, v->dx0, n * 2);
    memcpy(c1s, v->dx1, n * 2);
    ch->dirty = copy;
    ch->x0 = c0;
    ch->x1 = c1s;
    ch->all = v->all_dirty || v->rows > (int)sizeof copy;
    ch->scroll_top = v->s_top;
    ch->scroll_bottom = v->s_bot;
    ch->scroll_n = v->s_n;
    memset(v->dirty, 0, v->rows);
    v->all_dirty = v->any_dirty = 0;
    v->s_n = 0;
    v->s_full = 0;
    return any;
}

int vt_take_reply(Vt *v, char *buf, int max)
{
    int n = v->rlen < max ? v->rlen : max;
    memcpy(buf, v->reply, n);
    memmove(v->reply, v->reply + n, v->rlen - n);
    v->rlen -= n;
    return n;
}

char *vt_take_clip(Vt *v, int *len)
{
    char *c = v->clip;
    *len = v->cliplen;
    v->clip = NULL;
    v->cliplen = 0;
    return c;
}

const char *vt_title(Vt *v, int *changed)
{
    *changed = v->title_changed;
    v->title_changed = 0;
    return v->title;
}

int vt_take_bell(Vt *v)
{
    int b = v->bell;
    v->bell = 0;
    return b;
}
