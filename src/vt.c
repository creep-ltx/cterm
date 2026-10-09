/*
 * vt - the emulator: bytes from the host in, a grid of cells out.
 * See vt.h. Plain C, no Amiga headers.
 */
#include "vt.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAXPARAM 16
#define OSCMAX   512
#define REPLYMAX 256

enum { S_GROUND, S_ESC, S_ESC_INT, S_CSI, S_OSC, S_OSC_ESC, S_STR,
       S_STR_ESC };

typedef struct {
    int x, y, pending;
    VtCell pen;
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

    /* scrollback: a ring of lines, each as wide as the screen then was */
    VtCell **hist;
    int *hist_w;
    int hist_cap, hist_len, hist_head;   /* head: the oldest line */

    int x, y, pending;              /* pending: at the last column, the
                                       next character wraps first */
    VtCell pen;                     /* what new characters look like */
    int top, bot;                   /* the scroll region, inclusive */
    int autowrap, origin, insert, lnm, cursor_on, app_cursor, app_keypad;
    int bracketed, reverse, utf8;
    int amigamode;                  /* the Amiga console's ways (local) */
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
    uint32_t uacc;
    int uneed;

    uint8_t *dirty;
    int all_dirty, any_dirty;
    int s_top, s_bot, s_n;

    char reply[REPLYMAX];
    int rlen;
    char title[128];
    int title_changed, bell;
};

/* ---- small things ---------------------------------------------------- */

static VtCell blank_of(const Vt *v)
{
    VtCell c;
    c.cp = 0;
    c.fg = v->pen.fg;
    c.bg = v->pen.bg;          /* erased cells take the background (BCE) */
    c.attr = 0;
    return c;
}

static void fill(VtCell *p, int n, VtCell c)
{
    while (n-- > 0)
        *p++ = c;
}

static VtCell *row(Vt *v, int y)
{
    return v->cur[y];
}

static void reply(Vt *v, const char *s)
{
    int n = strlen(s);
    if (v->rlen + n <= REPLYMAX) {
        memcpy(v->reply + v->rlen, s, n);
        v->rlen += n;
    }
}

static void dirty_row(Vt *v, int y)
{
    if (y >= 0 && y < v->rows) {
        v->dirty[y] = 1;
        v->any_dirty = 1;
    }
}

static void dirty_all(Vt *v)
{
    v->all_dirty = 1;
    v->any_dirty = 1;
}

/* How many cells a character takes: 0 (a combining mark), 1 or 2. */
static int cp_width(uint32_t c)
{
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

/* The row leaving the top becomes the newest history line, and the
 * oldest history line's buffer (when it fits) becomes the new row: no
 * copy, no allocation. FALSE: no history - the row is simply reused. */
static int swap_into_history(Vt *v, VtCell **pline)
{
    int slot;
    VtCell *old, *fresh;
    if (v->hist_cap <= 0)
        return 0;
    if (v->hist_len < v->hist_cap)
        slot = (v->hist_head + v->hist_len) % v->hist_cap;
    else
        slot = v->hist_head;
    old = v->hist[slot];
    fresh = old && v->hist_w[slot] == v->cols ? old
          : malloc(v->cols * sizeof(VtCell));
    if (!fresh)
        return 0;
    if (fresh != old)
        free(old);
    if (v->hist_len < v->hist_cap)
        v->hist_len++;
    else
        v->hist_head = (v->hist_head + 1) % v->hist_cap;
    v->hist[slot] = *pline;
    v->hist_w[slot] = v->cols;
    *pline = fresh;
    return 1;
}

static void push_history(Vt *v, const VtCell *line)
{
    int slot;
    VtCell *copy;
    if (v->hist_cap <= 0)
        return;
    if (v->hist_len < v->hist_cap) {
        slot = (v->hist_head + v->hist_len) % v->hist_cap;
        v->hist_len++;
    } else {
        slot = v->hist_head;            /* full: the oldest goes */
        v->hist_head = (v->hist_head + 1) % v->hist_cap;
    }
    copy = realloc(v->hist[slot], v->cols * sizeof(VtCell));
    if (!copy) {
        v->hist_w[slot] = 0;
        return;
    }
    memcpy(copy, line, v->cols * sizeof(VtCell));
    v->hist[slot] = copy;
    v->hist_w[slot] = v->cols;
}

/* Record a scroll of [top,bot] by n (up > 0) for the display: one blit
 * then the rows that came in - or, if it cannot be one blit, all of it. */
static void note_scroll(Vt *v, int top, int bot, int n)
{
    int h = bot - top + 1, y;
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
    /* the rows' dirty marks travel with their contents */
    if (n > 0) {
        for (y = top; y <= bot - n; y++)
            v->dirty[y] = v->dirty[y + n];
        for (y = bot - n + 1; y <= bot; y++)
            v->dirty[y] = 1;
    } else {
        for (y = bot; y >= top - n; y--)
            v->dirty[y] = v->dirty[y + n];
        for (y = top; y < top - n; y++)
            v->dirty[y] = 1;
    }
}

static void scroll_up(Vt *v, int top, int bot, int n)
{
    int h = bot - top + 1;
    VtCell b = blank_of(v);
    if (n > h)
        n = h;
    if (n <= 0)
        return;
    {
        VtCell *gone[1024];
        int i, k = n < 1024 ? n : 1024;
        for (i = 0; i < k; i++) {
            gone[i] = v->cur[top + i];
            if (!v->alt && top == 0)
                swap_into_history(v, &gone[i]);
        }
        memmove(&v->cur[top], &v->cur[top + k], (h - k) * sizeof(VtCell *));
        for (i = 0; i < k; i++) {
            v->cur[bot - k + 1 + i] = gone[i];
            fill(gone[i], v->cols, b);
        }
    }
    note_scroll(v, top, bot, n);
}

static void scroll_down(Vt *v, int top, int bot, int n)
{
    int h = bot - top + 1;
    VtCell b = blank_of(v);
    if (n > h)
        n = h;
    if (n <= 0)
        return;
    {
        VtCell *gone[1024];
        int i, k = n < 1024 ? n : 1024;
        for (i = 0; i < k; i++)
            gone[i] = v->cur[bot - k + 1 + i];
        memmove(&v->cur[top + k], &v->cur[top], (h - k) * sizeof(VtCell *));
        for (i = 0; i < k; i++) {
            v->cur[top + i] = gone[i];
            fill(gone[i], v->cols, b);
        }
    }
    note_scroll(v, top, bot, -n);
}

static void linefeed(Vt *v)
{
    v->pending = 0;
    if (v->y == v->bot)
        scroll_up(v, v->top, v->bot, 1);
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

static void save_cursor(Vt *v)
{
    Saved *s = &v->saved[v->alt];
    s->x = v->x;
    s->y = v->y;
    s->pending = v->pending;
    s->pen = v->pen;
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
    v->pen = s->pen;
    v->origin = s->origin;
    v->autowrap = s->autowrap;
    v->g[0] = s->g[0];
    v->g[1] = s->g[1];
    v->gl = s->gl;
}

/* ---- printing ---------------------------------------------------------- */

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
        v->x = 0;
        linefeed(v);
    }
    v->pending = 0;
    if (w == 2 && v->x == v->cols - 1) {
        if (!v->autowrap)
            return;
        row(v, v->y)[v->x] = blank_of(v);   /* no room: wrap first */
        dirty_row(v, v->y);
        v->x = 0;
        linefeed(v);
    }
    r = row(v, v->y);
    if (v->insert) {
        int n = v->cols - v->x - w;
        if (n > 0)
            memmove(r + v->x + w, r + v->x, n * sizeof(VtCell));
    }
    /* overwriting half of a wide character clears its other half */
    if (r[v->x].cp == VT_WIDE_TAIL && v->x > 0)
        r[v->x - 1] = blank_of(v);
    if (v->x + w < v->cols && r[v->x + w].cp == VT_WIDE_TAIL)
        r[v->x + w] = blank_of(v);
    r[v->x] = v->pen;
    r[v->x].cp = cp;
    if (w == 2) {
        r[v->x + 1] = v->pen;
        r[v->x + 1].cp = VT_WIDE_TAIL;
    }
    dirty_row(v, v->y);
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
        fill(row(v, y) + x0, x1 - x0, blank_of(v));
        dirty_row(v, y);
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
    } else if (how == 3) {                  /* the scrollback too */
        int i;
        for (i = 0; i < v->hist_cap; i++) {
            free(v->hist[i]);
            v->hist[i] = NULL;
            v->hist_w[i] = 0;
        }
        v->hist_len = v->hist_head = 0;
        dirty_all(v);
    }
}

static void insert_chars(Vt *v, int n)
{
    VtCell *r = row(v, v->y);
    int keep = v->cols - v->x - n;
    if (n <= 0)
        return;
    if (keep > 0)
        memmove(r + v->x + n, r + v->x, keep * sizeof(VtCell));
    fill(r + v->x, n < v->cols - v->x ? n : v->cols - v->x, blank_of(v));
    dirty_row(v, v->y);
}

static void delete_chars(Vt *v, int n)
{
    VtCell *r = row(v, v->y);
    int keep = v->cols - v->x - n;
    if (n <= 0)
        return;
    if (keep > 0)
        memmove(r + v->x, r + v->x + n, keep * sizeof(VtCell));
    else
        keep = 0;
    fill(r + v->x + keep, v->cols - v->x - keep, blank_of(v));
    dirty_row(v, v->y);
}

/* ---- the alternate screen --------------------------------------------- */

static void use_alt(Vt *v, int on, int clear)
{
    if (on == v->alt)
        return;
    v->alt = on;
    v->cur = v->lines[on];
    if (on && clear) {
        int y;
        for (y = 0; y < v->rows; y++)
            fill(v->cur[y], v->cols, blank_of(v));
    }
    dirty_all(v);
}

/* ---- resetting --------------------------------------------------------- */

static void reset_pen(Vt *v)
{
    v->pen.cp = 0;
    v->pen.fg = VT_DEFAULT;
    v->pen.bg = VT_DEFAULT;
    v->pen.attr = 0;
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
    reset_pen(v);
    use_alt(v, 0, 0);
    for (i = 0; i < 2; i++) {
        int y;
        for (y = 0; y < v->rows; y++)
            fill(v->lines[i][y], v->cols, blank_of(v));
    }
    v->x = v->y = v->pending = 0;
    v->top = 0;
    v->bot = v->rows - 1;
    v->autowrap = 1;
    v->origin = v->insert = 0;
    v->lnm = v->amigamode;                  /* the Amiga: LF is a new line */
    v->cursor_on = 1;
    v->app_cursor = v->app_keypad = v->bracketed = v->reverse = 0;
    v->g[0] = v->g[1] = 'B';
    v->gl = 0;
    reset_tabs(v);
    save_cursor(v);
    v->saved[1] = v->saved[0];
    v->state = S_GROUND;
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
    for (i = 0; i < v->nparam; i++) {
        int p = v->param[i];
        if (p < 0)
            p = 0;
        switch (p) {
        case 0: reset_pen(v); break;
        case 1: v->pen.attr |= VT_BOLD; break;
        case 2: v->pen.attr |= VT_DIM; break;
        case 3: v->pen.attr |= VT_ITALIC; break;
        case 4: v->pen.attr |= VT_UNDERLINE; break;
        case 5: case 6: v->pen.attr |= VT_BLINK; break;
        case 7: v->pen.attr |= VT_INVERSE; break;
        case 8: v->pen.attr |= VT_HIDDEN; break;
        case 9: v->pen.attr |= VT_STRIKE; break;
        case 21: v->pen.attr |= VT_UNDERLINE; break;
        case 22: v->pen.attr &= ~(VT_BOLD | VT_DIM); break;
        case 23: v->pen.attr &= ~VT_ITALIC; break;
        case 24: v->pen.attr &= ~VT_UNDERLINE; break;
        case 25: v->pen.attr &= ~VT_BLINK; break;
        case 27: v->pen.attr &= ~VT_INVERSE; break;
        case 28: v->pen.attr &= ~VT_HIDDEN; break;
        case 29: v->pen.attr &= ~VT_STRIKE; break;
        case 38: i += ext_colour(v, i, &v->pen.fg); break;
        case 39: v->pen.fg = VT_DEFAULT; break;
        case 48: i += ext_colour(v, i, &v->pen.bg); break;
        case 49: v->pen.bg = VT_DEFAULT; break;
        default:
            if (p >= 30 && p <= 37) v->pen.fg = VT_INDEX(p - 30);
            else if (p >= 40 && p <= 47) v->pen.bg = VT_INDEX(p - 40);
            else if (p >= 90 && p <= 97) v->pen.fg = VT_INDEX(p - 90 + 8);
            else if (p >= 100 && p <= 107) v->pen.bg = VT_INDEX(p - 100 + 8);
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
            default: break;             /* mouse, focus, sync: later */
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
    if (v->priv == '?' && final != 'h' && final != 'l' && final != 'J' &&
        final != 'K' && final != 'n')
        return;                         /* kitty "CSI ? u" and friends */

    switch (final) {
    case '@': insert_chars(v, n); break;
    case 'A': move_to(v, v->x, v->y - n - (v->origin ? v->top : 0)); break;
    case 'B': case 'e':
        move_to(v, v->x, v->y + n - (v->origin ? v->top : 0)); break;
    case 'C': case 'a': move_to(v, v->x + n,
                                v->y - (v->origin ? v->top : 0)); break;
    case 'D': move_to(v, v->x - n, v->y - (v->origin ? v->top : 0)); break;
    case 'E': move_to(v, 0, v->y + n - (v->origin ? v->top : 0)); break;
    case 'F': move_to(v, 0, v->y - n - (v->origin ? v->top : 0)); break;
    case 'G': case '`':
        move_to(v, n - 1, v->y - (v->origin ? v->top : 0)); break;
    case 'H': case 'f': move_to(v, P(1, 1) - 1, P(0, 1) - 1); break;
    case 'd': move_to(v, v->x, n - 1); break;
    case 'I':
        for (i = 0; i < n; i++) {
            do v->x++; while (v->x < v->cols - 1 && !v->tabs[v->x]);
        }
        if (v->x >= v->cols) v->x = v->cols - 1;
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
            int saved_alt = v->alt;
            v->alt = 1;
            scroll_up(v, v->y, v->bot, n);
            v->alt = saved_alt;
        }
        v->x = 0;
        v->pending = 0;
        break;
    case 'P': delete_chars(v, n); break;
    case 'X': erase(v, v->y, v->x, v->x + n); break;
    case 'S': scroll_up(v, v->top, v->bot, n); break;
    case 'T': scroll_down(v, v->top, v->bot, n); break;
    case 'b':
        if (v->last_cp)
            for (i = 0; i < n && i < 1000; i++)
                put(v, v->last_cp);
        break;
    case 'c':
        if (P(0, 0) == 0)
            reply(v, "\x1b[?62;22c");   /* DA1: VT220 with ANSI colour */
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
    if (v->inter == '#') {
        if (c == '8') {                 /* DECALN: the screen full of E */
            int y, x;
            for (y = 0; y < v->rows; y++)
                for (x = 0; x < v->cols; x++) {
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

static void osc_done(Vt *v)
{
    int n = 0, i = 0;
    v->osc[v->osclen] = 0;
    while (v->osc[i] >= '0' && v->osc[i] <= '9')
        n = n * 10 + (v->osc[i++] - '0');
    if (v->osc[i] == ';' && (n == 0 || n == 2)) {
        strncpy(v->title, v->osc + i + 1, sizeof v->title - 1);
        v->title[sizeof v->title - 1] = 0;
        v->title_changed = 1;
    }
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

/* C1 controls, as bytes in Latin-1 mode or as ESC + letter. */
static int c1(Vt *v, int c)
{
    switch (c) {
    case 0x84: linefeed(v); return 1;
    case 0x85: v->x = 0; linefeed(v); return 1;
    case 0x88: v->tabs[v->x] = 1; return 1;
    case 0x8D: reverse_index(v); return 1;
    case 0x90: v->state = S_STR; return 1;     /* DCS */
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

static void byte(Vt *v, int c)
{
    switch (v->state) {
    case S_GROUND:
        if (v->utf8 && c >= 0x80) {
            if (v->uneed) {
                if ((c & 0xC0) == 0x80) {
                    v->uacc = (v->uacc << 6) | (c & 0x3F);
                    if (--v->uneed == 0)
                        put(v, v->uacc);
                    return;
                }
                v->uneed = 0;
                put(v, 0xFFFD);         /* a broken sequence */
            }
            if ((c & 0xE0) == 0xC0) { v->uacc = c & 0x1F; v->uneed = 1; }
            else if ((c & 0xF0) == 0xE0) { v->uacc = c & 0x0F; v->uneed = 2; }
            else if ((c & 0xF8) == 0xF0) { v->uacc = c & 0x07; v->uneed = 3; }
            else put(v, 0xFFFD);
            return;
        }
        if (v->uneed) {
            v->uneed = 0;
            put(v, 0xFFFD);
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
        if (c == 'P' || c == 'X' || c == '^' || c == '_') {
            v->state = S_STR;
            return;
        }
        if (c == '\\')
            return;                     /* a lone ST */
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
        if (v->osclen < OSCMAX - 1)
            v->osc[v->osclen++] = (char)c;
        return;

    case S_OSC_ESC:
        osc_done(v);
        v->state = S_GROUND;
        if (c != '\\')
            byte(v, c);
        return;

    case S_STR:
        if (c == 0x1B) v->state = S_STR_ESC;
        else if (c == 0x9C && !v->utf8) v->state = S_GROUND;
        return;

    case S_STR_ESC:
        v->state = c == '\\' ? S_GROUND : S_STR;
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
         * put()'s general checks (width, line drawing, insert) cost. */
        if (c >= 0x20 && c < 0x7F && v->state == S_GROUND && !v->uneed &&
            v->g[v->gl] != '0' && !v->insert) {
            VtCell *r = row(v, v->y);
            for (;;) {
                if (v->pending && v->autowrap) {
                    v->x = 0;
                    linefeed(v);
                    r = row(v, v->y);
                }
                v->pending = 0;
                if (r[v->x].cp == VT_WIDE_TAIL && v->x > 0)
                    r[v->x - 1] = blank_of(v);
                if (v->x + 1 < v->cols && r[v->x + 1].cp == VT_WIDE_TAIL)
                    r[v->x + 1] = blank_of(v);
                r[v->x] = v->pen;
                r[v->x].cp = (uint32_t)c;
                v->dirty[v->y] = 1;
                if (v->x + 1 >= v->cols) {
                    v->x = v->cols - 1;
                    v->pending = 1;
                } else
                    v->x++;
                if (i + 1 >= len)
                    break;
                c = buf[i + 1];
                if (c < 0x20 || c >= 0x7F)
                    break;
                i++;
            }
            v->any_dirty = 1;
            v->last_cp = (uint32_t)buf[i];
            continue;
        }
        /* Latin-1 (the Amiga's ANSI): bytes 0x80-0x9F are C1 controls */
        if (!v->utf8 && v->state == S_GROUND && c >= 0x80 && c < 0xA0) {
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
        free(l[y]);
    free(l);
}

static VtCell **alloc_screen(int cols, int rows)
{
    VtCell **l = calloc(rows, sizeof *l);
    int y;
    if (!l)
        return NULL;
    for (y = 0; y < rows; y++)
        if (!(l[y] = calloc(cols, sizeof(VtCell)))) {
            free_screen(l, rows);
            return NULL;
        }
    return l;
}

Vt *vt_new(int cols, int rows, int scrollback)
{
    Vt *v = calloc(1, sizeof *v);
    if (!v)
        return NULL;
    v->cols = cols;
    v->rows = rows;
    v->lines[0] = alloc_screen(cols, rows);
    v->lines[1] = alloc_screen(cols, rows);
    v->tabs = calloc(cols, 1);
    v->dirty = calloc(rows, 1);
    v->hist_cap = scrollback;
    if (scrollback > 0) {
        v->hist = calloc(scrollback, sizeof *v->hist);
        v->hist_w = calloc(scrollback, sizeof *v->hist_w);
    }
    if (!v->lines[0] || !v->lines[1] || !v->tabs || !v->dirty ||
        (scrollback > 0 && (!v->hist || !v->hist_w))) {
        vt_free(v);
        return NULL;
    }
    v->cur = v->lines[0];
    v->utf8 = 1;
    full_reset(v);
    return v;
}

void vt_free(Vt *v)
{
    int i;
    if (!v)
        return;
    for (i = 0; i < v->hist_cap && v->hist; i++)
        free(v->hist[i]);
    free(v->hist);
    free(v->hist_w);
    free_screen(v->lines[0], v->rows);
    free_screen(v->lines[1], v->rows);
    free(v->tabs);
    free(v->dirty);
    free(v);
}

void vt_resize(Vt *v, int cols, int rows)
{
    VtCell **ng[2];
    uint8_t *nt, *nd;
    int i, y, shift = 0, w;

    if (cols == v->cols && rows == v->rows)
        return;
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    ng[0] = alloc_screen(cols, rows);
    ng[1] = alloc_screen(cols, rows);
    nt = calloc(cols, 1);
    nd = calloc(rows, 1);
    if (!ng[0] || !ng[1] || !nt || !nd) {
        free_screen(ng[0], rows); free_screen(ng[1], rows);
        free(nt); free(nd);
        return;
    }
    /* fewer rows: lines above the cursor go to the history first, so the
     * cursor's line stays on screen (as xterm does) */
    if (v->y >= rows) {
        shift = v->y - rows + 1;
        if (!v->alt)
            for (i = 0; i < shift; i++)
                push_history(v, v->lines[0][i]);
    }
    w = cols < v->cols ? cols : v->cols;
    for (i = 0; i < 2; i++) {
        for (y = 0; y < rows && y + shift < v->rows; y++)
            memcpy(ng[i][y], v->lines[i][y + shift], w * sizeof(VtCell));
        free_screen(v->lines[i], v->rows);
        v->lines[i] = ng[i];
    }
    free(v->tabs);
    free(v->dirty);
    v->tabs = nt;
    v->dirty = nd;
    v->cols = cols;
    v->rows = rows;
    v->cur = v->lines[v->alt];
    reset_tabs(v);
    v->y -= shift;
    if (v->x >= cols) v->x = cols - 1;
    if (v->y >= rows) v->y = rows - 1;
    v->pending = 0;
    v->top = 0;
    v->bot = rows - 1;
    for (i = 0; i < 2; i++) {
        if (v->saved[i].x >= cols) v->saved[i].x = cols - 1;
        if (v->saved[i].y >= rows) v->saved[i].y = rows - 1;
    }
    v->s_n = 0;
    dirty_all(v);
}

/* ---- for the display ------------------------------------------------- */

void vt_set_utf8(Vt *v, int on) { v->utf8 = on; v->uneed = 0; }

/* The Amiga console's ways, for the local Shell: Latin-1 with 0x9B as
 * CSI, LF as a new line, FF a clear page, VT a line up, and its own
 * sequences (window bounds report, cursor rendition, raw events). */
void vt_set_amiga(Vt *v, int on)
{
    v->amigamode = on;
    if (on) {
        v->utf8 = 0;
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
    *width = v->cols;
    return v->cur[idx];
}

void vt_cursor(const Vt *v, int *x, int *y, int *visible)
{
    *x = v->x;
    *y = v->y;
    *visible = v->cursor_on;
}

int vt_take_changes(Vt *v, VtChanges *ch)
{
    static uint8_t copy[1024];
    int any = v->any_dirty;
    int n = v->rows < (int)sizeof copy ? v->rows : (int)sizeof copy;
    memcpy(copy, v->dirty, n);
    ch->dirty = copy;
    ch->all = v->all_dirty || v->rows > (int)sizeof copy;
    ch->scroll_top = v->s_top;
    ch->scroll_bottom = v->s_bot;
    ch->scroll_n = v->s_n;
    memset(v->dirty, 0, v->rows);
    v->all_dirty = v->any_dirty = 0;
    v->s_n = 0;
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
