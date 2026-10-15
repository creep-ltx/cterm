/*
 * fileui - the file browser's panels (Parts/Files, with files.c and
 * ftp.c), drawn in the place's tab as a program draws in a terminal:
 * two panels as Midnight Commander has them (the Amiga's drawer on the
 * left, the host's on the right), a message line, and a bar of the
 * function keys. On the tab's alternate screen, so its history keeps
 * only the messages (F9 shows them).
 *
 * Drawn with escape sequences into the tab's emulator: the cursor
 * moved, the theme's colours (SGR), lines and corners as box-drawing
 * characters (which CTerm draws itself). A moving bar redraws two
 * lines; the rest is drawn whole (a new drawer, a new size).
 *
 * Every key is the person's to change: CTerm's ENVARC:CTerm/CTerm.keys,
 * sections [files] (the panels) and [filesline] (a line typed); CTerm
 * reads and writes the file, Settings edits it.
 */
#include "cterm.h"
#include "files.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

/* ---- the keys ---- */

enum { K_NONE, K_UP, K_DOWN, K_PGUP, K_PGDN, K_TOP, K_BOTTOM, K_LEFT,
       K_RIGHT, K_SWITCH, K_OPEN, K_PARENT, K_MARK, K_INVERT, K_HELP,
       K_REFRESH, K_COPY, K_RENAME, K_MKDIR, K_DELETE, K_LOG, K_QUIT,
       K_STOP, K_CMD, K_REDRAW,
       P_LEFT, P_RIGHT, P_START, P_END, P_BACK, P_DEL, P_KILL, P_ACCEPT,
       P_CANCEL, P_COMPLETE, P_OLDER, P_NEWER, K_N };

struct Act {
    const char *name, *label, *desc;
    UBYTE ctx;                          /* 1 the panels, 2 a line typed */
};

static const struct Act acts[K_N] = {
    { "none", NULL, NULL, 0 },
    { "up", NULL, "the line above", 1 },
    { "down", NULL, "the line below", 1 },
    { "pageup", NULL, "a page up", 1 },
    { "pagedown", NULL, "a page down", 1 },
    { "top", NULL, "the first line", 1 },
    { "bottom", NULL, "the last line", 1 },
    { "left", NULL, "the Amiga's panel (left)", 1 },
    { "right", NULL, "the host's panel (right)", 1 },
    { "switch", NULL, "the other panel", 1 },
    { "open", NULL, "into the drawer under the bar", 1 },
    { "parent", NULL, "the drawer above", 1 },
    { "mark", NULL, "mark or unmark, then down", 1 },
    { "invert", NULL, "the files' marks turned over", 1 },
    { "help", "Help", "these keys", 1 },
    { "refresh", "Reread", "both drawers read again", 1 },
    { "copy", "Copy", "copy the marked (or the bar's) across, drawers too", 1 },
    { "rename", "Rename", "rename the one under the bar", 1 },
    { "mkdir", "MkDir", "a new drawer", 1 },
    { "delete", "Delete", "delete the marked, drawers too (asks first)", 1 },
    { "log", "Log", "the messages (any key: back)", 1 },
    { "quit", "Quit", "hang up and close the tab (asks first)", 1 },
    { "stop", NULL, "stop a transfer or a listing; forget a search", 1 },
    { "command", NULL, "the command line (ls, cd, get, put...)", 1 },
    { "redraw", NULL, "draw everything again", 1 },
    { "left", NULL, "a letter left", 2 },
    { "right", NULL, "a letter right", 2 },
    { "start", NULL, "to the start", 2 },
    { "end", NULL, "to the end", 2 },
    { "back", NULL, "delete the letter before", 2 },
    { "delete", NULL, "delete the letter under the cursor", 2 },
    { "kill", NULL, "delete to the start", 2 },
    { "accept", NULL, "done", 2 },
    { "cancel", NULL, "nothing done", 2 },
    { "complete", NULL, "complete a name or a command", 2 },
    { "older", NULL, "the command line before", 2 },
    { "newer", NULL, "the command line after", 2 },
};

/* The bindings are CTerm's (CTerm.keys, [files] and [filesline]: action
 * K_x is CTerm's KA_FB_FIRST + K_x - 1); a key comes here as the panels
 * have always had it (keys.c browser_key: KM_SUP, a Ctrl letter as its
 * control character) and is looked up as CTerm writes keys (KQ_*). */
typedef char fb_acts_check[KA_FB_LAST - KA_FB_FIRST + 2 == K_N ? 1 : -1];

static UWORD key_kq(int k)
{
    if (k >= KM_SF1 && k < KM_SF1 + 10)
        return KQ_S | (KM_F1 + k - KM_SF1);
    if (k >= KM_SUP && k <= KM_SRIGHT)
        return KQ_S | (KM_UP + k - KM_SUP);
    if (k >= KM_CUP && k <= KM_CRIGHT)
        return KQ_C | (KM_UP + k - KM_CUP);
    if (k == KM_SBS)
        return KQ_S | 8;
    if (k == KM_CBS)
        return KQ_C | 8;
    if (k > 0 && k < 32 && k != 8 && k != 9 && k != 13 && k != 27)
        return KQ_C | (k + 96);         /* Ctrl+R: <C-r> */
    return k;
}

static int find(int ctx, int k)
{
    int a = keys_find(ctx == 1 ? KC_FILES : KC_FLINE, key_kq(k));
    return a >= KA_FB_FIRST ? a - KA_FB_FIRST + 1 : K_NONE;
}

/* The first key bound to an action, as it is written on the keys
 * ("F5", "Esc", "':'"; "" none). */
static void first_key(int ctx, int a, char *o)
{
    UWORD k = keys_first(KA_FB_FIRST + a - 1);
    int l;
    (void)ctx;
    o[0] = 0;
    if (!k)
        return;
    keys_name(k, o, FALSE);
    l = strlen(o);
    if (o[0] == '<' && l > 2) {
        memmove(o, o + 1, l - 2);
        o[l - 2] = 0;
    } else if (l == 1) {
        o[2] = '\'';
        o[1] = o[0];
        o[0] = '\'';
        o[3] = 0;
    }
}

/* ---- writing into the tab ---- */

static char ob[1200];
static int obn;
static struct Tab *otab;

static void o_flush(void)
{
    struct Tab *keep = T;
    if (!obn)
        return;
    ob[obn] = 0;
    T = otab;
    say(ob);
    T = keep;
    obn = 0;
}

static void o_byte(int b)
{
    if (obn >= (int)sizeof ob - 1)
        o_flush();
    ob[obn++] = b;
}

static void o_str(const char *s)
{
    while (*s)
        o_byte(*s++);
}

static void o_num(int n)
{
    char t[12];
    t[0] = 0;
    numstr_cat(t, n);
    o_str(t);
}

static void o_at(int r, int col)
{
    o_str("\x1b[");
    o_num(r + 1);
    o_byte(';');
    o_num(col + 1);
    o_byte('H');
}

static void o_sgr(const char *s)
{
    o_str("\x1b[");
    o_str(s);
    o_byte('m');
}

/* a Latin-1 letter, as UTF-8 */
static void o_ch(UBYTE u)
{
    if (u < 32 || u == 127)
        u = '?';
    if (u >= 0x80) {
        o_byte(0xC0 | (u >> 6));
        o_byte(0x80 | (u & 0x3F));
    } else
        o_byte(u);
}

/* a line or corner (U+25xx), n times; 0: spaces */
static void o_box(int u, int n)
{
    while (n-- > 0) {
        if (!u) {
            o_byte(' ');
            continue;
        }
        o_byte(0xE0 | (u >> 12));
        o_byte(0x80 | ((u >> 6) & 0x3F));
        o_byte(0x80 | (u & 0x3F));
    }
}

/* Latin-1 text in w cells: how 0 left, 1 right, 2 a path (its end
 * kept), 3 cut short; too long, a name loses its middle (a ~ there) */
static void o_field(const char *s, int w, int how)
{
    int n = strlen(s), i;
    if (w <= 0)
        return;
    if (n > w && how == 3)
        n = w;
    if (n > w) {
        if (how == 2) {
            o_ch('~');
            for (i = n - w + 1; i < n; i++)
                o_ch(s[i]);
        } else {
            int head = w / 2;
            for (i = 0; i < head; i++)
                o_ch(s[i]);
            o_ch('~');
            for (i = n - (w - head - 1); i < n; i++)
                o_ch(s[i]);
        }
        return;
    }
    if (how == 1)
        o_box(0, w - n);
    for (i = 0; i < n; i++)
        o_ch(s[i]);
    if (how != 1)
        o_box(0, w - n);
}

/* ---- where things are ---- */

static int W, H, LW, PH, LH;            /* the tab; the left panel's width;
                                           the panels' height; lines */

static BOOL geom(struct RConn *c)
{
    struct Tab *keep = T;
    T = c->tab;
    W = vt_cols(vt);
    H = vt_rows(vt);
    T = keep;
    LW = W / 2;
    PH = H - 2;
    LH = PH - 2;
    return W >= 24 && H >= 6;
}

static int px(int s)
{
    return s ? LW : 0;
}

static int pw(int s)
{
    return s ? W - LW : LW;
}

static int nrows(struct Panel *p)
{
    return p->nent + (p->up ? 1 : 0);
}

/* the columns of a panel inner iw wide: name, size, date */
static void columns(int iw, int *nw, int *sw, int *dw)
{
    *sw = iw >= 20 ? 7 : 0;
    *dw = iw >= 52 ? 16 : iw >= 34 ? 11 : 0;
    *nw = iw - (*sw ? *sw + 1 : 0) - (*dw ? *dw + 1 : 0);
}

static void clamp(struct Panel *p)
{
    int n = nrows(p);
    if (p->cur >= n)
        p->cur = n - 1;
    if (p->cur < 0)
        p->cur = 0;
    if (p->top > p->cur)
        p->top = p->cur;
    if (LH > 0 && p->cur >= p->top + LH)
        p->top = p->cur - LH + 1;
    if (LH > 0 && p->top > n - LH)
        p->top = n - LH;
    if (p->top < 0)
        p->top = 0;
}

/* The name of a panel's entry, as the Amiga shows it */
static void shown_name(struct RConn *c, int s, int e, char *o, int max)
{
    struct Panel *p = &c->pn[s];
    const char *n = pname(p, e);
    if (s)
        to_latin1(n, strlen(n), o, max, '?');
    else {
        strncpy(o, n, max - 1);
        o[max - 1] = 0;
    }
}

/* ---- drawing ---- */

static void begin(struct RConn *c)
{
    o_flush();
    otab = c->tab;
}

/* Done: the cursor where a line is typed (else hidden), into the tab,
 * drawn. */
static void end(struct RConn *c)
{
    struct Tab *keep = T;
    if (c->mode == U_LINE) {
        int hl = c->lkind == L_CMD ? (c->kind == FK_FTP ? 5 : 6)
               : c->lkind == L_RENAME ? 10 : 12;
        int avail = W - hl - 1, start = c->lpos > avail ? c->lpos - avail : 0;
        o_at(H - 2, hl + c->lpos - start);
        o_str("\x1b[?25h");
    } else
        o_str("\x1b[?25l");
    o_flush();
    T = c->tab;
    refresh();
    T = keep;
}

static void draw_item(struct RConn *c, int s, int i)
{
    struct Panel *p = &c->pn[s];
    int iw = pw(s) - 2, row = p->top + i, e = row - (p->up ? 1 : 0);
    int nw, sw, dw;
    BOOL bar = s == c->side && row == p->cur, mk = FALSE;
    char name[256], size[16], date[18];
    const struct REnt *r = NULL;
    columns(iw, &nw, &sw, &dw);
    o_at(1 + i, px(s) + 1);
    if (row >= nrows(p)) {
        o_sgr("0");
        o_box(0, iw);
        return;
    }
    memset(date, 0, sizeof date);
    if (e < 0) {
        strcpy(name, "..");
        strcpy(size, "Parent");
    } else {
        r = &p->ent[e];
        mk = r->mark;
        shown_name(c, s, e, name, sizeof name);
        if (r->dir == 1)
            strcpy(size, "Drawer");
        else if (r->dir == 2)
            strcpy(size, "Link");
        else
            size_text(r->size, size);
        if (dw == 16 || strlen(r->date) < 16)
            strncpy(date, r->date, 10 + (dw == 16) * 6);
        else if (atol(r->date) == today() / 10000)
            strcpy(date, r->date + 5);  /* this year: "10-11 07:50" */
        else
            strncpy(date, r->date, 10);
        date[dw] = 0;
    }
    o_sgr(bar ? (mk ? "0;1;33;7" : "0;7") : mk ? "0;1;33"
          : !r || r->dir == 1 ? "0;1" : r->dir == 2 ? "0;36" : "0");
    o_field(name, nw, 0);
    if (sw) {
        o_byte(' ');
        o_field(size, sw, 1);
    }
    if (dw) {
        o_byte(' ');
        o_field(date, dw, 0);
    }
}

/* the panel's bottom line: what is marked, or what it holds */
static void draw_info(struct RConn *c, int s)
{
    struct Panel *p = &c->pn[s];
    BOOL act = s == c->side;
    int h = act ? 0x2550 : 0x2500, x0 = px(s), w = pw(s), i, n = 0, d = 0;
    ULONG bytes = 0;
    char t[80], z[16];
    for (i = 0; i < p->nent; i++) {
        if (p->ent[i].mark) {
            n++;
            bytes += p->ent[i].size;
        }
        d += p->ent[i].dir == 1;
    }
    t[0] = ' ';
    t[1] = 0;
    if (n) {
        numstr_cat(t, n);
        strcat(t, " marked, ");
        size_text(bytes, z);
        strcat(t, z);
    } else {
        numstr_cat(t, p->nent - d);
        strcat(t, p->nent - d == 1 ? " file, " : " files, ");
        numstr_cat(t, d);
        strcat(t, d == 1 ? " drawer" : " drawers");
    }
    strcat(t, " ");
    n = strlen(t);
    if (n > w - 4)
        n = w - 4;
    o_at(PH - 1, x0);
    o_sgr(act ? "0;36" : "0;2");
    o_box(act ? 0x255A : 0x2514, 1);
    o_box(h, 1);
    if (n > 0) {
        t[n] = 0;
        o_sgr(act && strstr(t, "marked") ? "0;1;33" : act ? "0;36" : "0;2");
        o_field(t, n, 0);
        o_sgr(act ? "0;36" : "0;2");
    }
    o_box(h, w - 3 - (n > 0 ? n : 0));
    o_box(act ? 0x255D : 0x2518, 1);
}

static void draw_frame(struct RConn *c, int s)
{
    struct Panel *p = &c->pn[s];
    BOOL act = s == c->side;
    int h = act ? 0x2550 : 0x2500, v = act ? 0x2551 : 0x2502;
    int x0 = px(s), w = pw(s), i, tl;
    char t[400];
    const char *col = act ? "0;36" : "0;2";
    if (s)
        to_latin1(c->cwd, strlen(c->cwd), t, sizeof t, '?');
    else
        strcpy(t, p->path[0] ? p->path : "Volumes");
    tl = strlen(t);
    if (tl > w - 6)
        tl = w - 6;
    o_at(0, x0);
    o_sgr(col);
    o_box(act ? 0x2554 : 0x250C, 1);
    o_box(h, 1);
    if (tl > 0) {
        o_sgr(act ? "0;7" : "0");
        o_byte(' ');
        o_field(t, tl, 2);
        o_byte(' ');
        o_sgr(col);
    }
    o_box(h, w - 3 - (tl > 0 ? tl + 2 : 0));
    o_box(act ? 0x2557 : 0x2510, 1);
    for (i = 0; i < LH; i++) {
        o_at(1 + i, x0);
        o_box(v, 1);
        o_at(1 + i, x0 + w - 1);
        o_box(v, 1);
    }
    draw_info(c, s);
}

static void draw_list(struct RConn *c, int s)
{
    int i;
    clamp(&c->pn[s]);
    for (i = 0; i < LH; i++)
        draw_item(c, s, i);
}

/* Text over the panels (help, ls): from its line vtop on. */
static void draw_view(struct RConn *c)
{
    const char *t = c->view, *e = c->view + c->vlen;
    int i, l;
    o_at(0, 0);
    o_sgr("0;36");
    o_box(0x250C, 1);
    o_box(0x2500, 1);
    l = strlen(c->vtitle);
    if (l > W - 6)
        l = W - 6;
    o_sgr("0;7");
    o_byte(' ');
    o_field(c->vtitle, l, 0);
    o_byte(' ');
    o_sgr("0;36");
    o_box(0x2500, W - 5 - l);
    o_box(0x2510, 1);
    for (i = 0; t && i < c->vtop && t < e; t++)
        if (*t == '\n')
            i++;
    for (i = 0; i < LH; i++) {
        int n = 0;
        o_at(1 + i, 0);
        o_sgr("0;36");
        o_box(0x2502, 1);
        o_sgr("0");
        while (t && t < e && *t != '\n') {
            if (n < W - 2)
                o_ch(*t);
            n++;
            t++;
        }
        if (t && t < e)
            t++;
        o_box(0, W - 2 - (n < W - 2 ? n : W - 2));
        o_sgr("0;36");
        o_box(0x2502, 1);
    }
    o_at(PH - 1, 0);
    o_box(0x2514, 1);
    o_box(0x2500, W - 2);
    o_box(0x2518, 1);
}

/* The message line: the line typed, a question, a transfer, a search,
 * or the last message. */
static void draw_msg(struct RConn *c)
{
    char t[300];
    o_at(H - 2, 0);
    o_sgr("0");
    if (c->mode == U_LINE) {
        static const char *const heads[] = { "ftp> ", "sftp> ",
                                             "New name: ", "New drawer: " };
        const char *h = heads[c->lkind == L_CMD ? (c->kind == FK_FTP ? 0 : 1)
                              : c->lkind == L_RENAME ? 2 : 3];
        int hl = strlen(h), avail = W - hl - 1, i;
        int start = c->lpos > avail ? c->lpos - avail : 0;
        o_sgr("0;1");
        o_str(h);
        o_sgr("0");
        for (i = start; i < c->llen && i < start + avail; i++)
            o_ch(c->line[i]);
    } else if (c->mode == U_ASK) {
        o_sgr("0;1;33");
        o_field(c->line, strlen(c->line) < (size_t)W ? (int)strlen(c->line)
                                                   : W - 1, 3);
    } else if (c->x) {
        struct Xfer *x = c->x;
        ULONG secs = (ticks50() - x->t0) / 50, pct = pct100(x->done, x->size);
        char n[24];
        int fill, l;
        strcpy(t, x->dir == 2 ? " Sending " : " Receiving ");
        strncat(t, x->name, 60);
        strcat(t, "  ");
        size_text(x->done, n);
        strcat(t, n);
        if (x->size) {
            strcat(t, " of ");
            size_text(x->size, n);
            strcat(t, n);
            strcat(t, "  ");
            numstr_cat(t, pct);
            strcat(t, "%");
        }
        if (secs) {
            strcat(t, "  ");
            numstr_cat(t, c->moved / secs / 1024);
            strcat(t, " KB/s");
        }
        if (x->nfiles > 1) {
            strcat(t, "  file ");
            numstr_cat(t, x->files + 1);
            strcat(t, " of ");
            numstr_cat(t, x->nfiles);
        }
        first_key(1, K_STOP, n);
        if (n[0]) {
            strcat(t, "  ");
            strcat(t, n);
            strcat(t, " stops");
        }
        l = strlen(t);
        while (l < W - 1 && l < (int)sizeof t - 1)
            t[l++] = ' ';
        if (l > W - 1)
            l = W - 1;                  /* the last column: never a wrap */
        t[l] = 0;
        fill = x->size ? (int)((ULONG)l * pct / 100) : 0;
        if (fill) {
            char k = t[fill];
            o_sgr("0;7");
            t[fill] = 0;
            o_field(t, fill, 0);
            t[fill] = k;
            o_sgr("0");
        }
        o_field(t + fill, l - fill, 0);
    } else if (c->mode == U_VIEW) {
        o_sgr("0;2");
        o_field("Up, Down: scroll - any other key: back to the panels",
                W - 1, 3);
    } else if (c->qn) {
        o_sgr("0;1");
        o_str("Find: ");
        o_sgr("0");
        o_field(c->qs, c->qn, 0);
    } else if (c->msg[0]) {
        if (c->msgred)
            o_sgr("0;1;31");
        o_field(c->msg, strlen(c->msg) < (size_t)W ? (int)strlen(c->msg)
                                                   : W - 1, 3);
    } else {
        char k1[24], k2[24], k3[24];
        first_key(1, K_SWITCH, k1);
        first_key(1, K_COPY, k2);
        first_key(1, K_CMD, k3);
        strcpy(t, k1);
        strcat(t, ": the other side  ");
        strcat(t, k2);
        strcat(t, ": copy  ");
        strcat(t, k3);
        strcat(t, ": the command line");
        o_sgr("0;2");
        o_field(t, strlen(t) < (size_t)W ? (int)strlen(t) : W - 1, 3);
    }
    o_sgr("0");
    o_str("\x1b[K");
}

/* The bar of the function keys, as they are bound - while Shift is held,
 * Shift+F1..F10's (as mc's bar shows another row's) */
static void draw_keys(struct RConn *c)
{
    int i, f = c->shifted ? KM_SF1 : KM_F1;
    for (i = 0; i < 10; i++) {
        int x = i * W / 10, w = (i + 1) * W / 10 - x, a = find(1, f + i);
        char num[4];
        num[0] = 0;
        numstr_cat(num, i + 1);
        o_at(H - 1, x);
        o_sgr("0");
        o_field(num, w, 0);
        o_at(H - 1, x + strlen(num));
        o_sgr("0;7");
        o_field(a && acts[a].label ? acts[a].label : "",
                w - strlen(num), 3);
    }
    o_sgr("0");
}

void ui_draw(struct RConn *c)
{
    begin(c);
    o_str("\x1b[0m\x1b[H\x1b[2J");
    if (!geom(c)) {
        o_str("Too small for the panels: make the window bigger.");
        c->dc = W;
        c->dr = H;
        end(c);
        return;
    }
    c->dc = W;
    c->dr = H;
    if (c->mode == U_VIEW)
        draw_view(c);
    else {
        int s;
        for (s = 0; s < 2; s++) {
            clamp(&c->pn[s]);
            draw_frame(c, s);
            draw_list(c, s);
        }
    }
    draw_msg(c);
    draw_keys(c);
    end(c);
}

/* Drawn at the size it has now? Else all of it again. FALSE: done. */
static BOOL same_size(struct RConn *c)
{
    BOOL ok = geom(c);
    if (ok && W == c->dc && H == c->dr)
        return TRUE;
    ui_draw(c);
    return FALSE;
}

static void redraw_msg(struct RConn *c)
{
    if (!c->ui || c->mode == U_LOG || !same_size(c))
        return;
    begin(c);
    draw_msg(c);
    end(c);
}

void ui_note(struct RConn *c, const char *text, BOOL red)
{
    strncpy(c->msg, text, sizeof c->msg - 1);
    c->msg[sizeof c->msg - 1] = 0;
    c->msgred = red;
    if (c->mode != U_LINE && c->mode != U_ASK)
        redraw_msg(c);
}

void ui_progress(struct RConn *c)
{
    if (c->mode == U_PANELS || c->mode == U_VIEW)
        redraw_msg(c);
}

/* The panels up on the alternate screen: the tab's letters as UTF-8
 * (lines and corners are Unicode). */
void ui_start(struct RConn *c)
{
    struct Tab *keep = T;
    c->ui = TRUE;
    c->tab->t_fui = TRUE;
    c->mode = U_PANELS;
    if (!c->plog)
        c->plog = AllocVec(4096, MEMF_ANY);
    T = c->tab;
    utf8 = TRUE;
    pcmode = FALSE;
    vt_set_cp437(vt, 0);
    vt_set_utf8(vt, 1);
    T = keep;
    begin(c);
    o_str("\x1b[?1049h");
    ui_draw(c);
}

/* Back to the main screen, the messages that waited written there. */
static void to_main(struct RConn *c)
{
    begin(c);
    if (c->mode != U_LOG)
        o_str("\x1b[0m\x1b[?1049l");
    o_str("\x1b[?25h");
    o_flush();
    if (c->ploglen) {
        struct Tab *keep = T;
        c->plog[c->ploglen] = 0;
        T = c->tab;
        vt_write(vt, (const uint8_t *)c->plog, c->ploglen);
        T = keep;
        c->ploglen = 0;
    }
}

void ui_stop(struct RConn *c)
{
    struct Tab *keep = T;
    if (!c->ui)
        return;
    to_main(c);
    c->ui = FALSE;
    c->tab->t_fui = FALSE;
    c->mode = U_PANELS;
    T = c->tab;
    refresh();
    T = keep;
}

void ui_free(struct RConn *c)
{
    if (c->plog)
        FreeVec(c->plog);
    if (c->view)
        FreeVec(c->view);
    c->plog = c->view = NULL;
    job_free(&c->job);
}

/* ---- moving about ---- */

static void move(struct RConn *c, int s, int to)
{
    struct Panel *p = &c->pn[s];
    int was = p->cur, top = p->top;
    p->cur = to;
    clamp(p);
    if (!same_size(c))
        return;
    begin(c);

    if (p->top != top)
        draw_list(c, s);
    else if (p->cur != was) {
        draw_item(c, s, was - p->top);
        draw_item(c, s, p->cur - p->top);
    }
    end(c);
}

static void to_side(struct RConn *c, int s)
{
    if (s == c->side)
        return;
    c->side = s;
    if (!same_size(c))
        return;
    begin(c);
    draw_frame(c, 0);
    draw_frame(c, 1);
    draw_list(c, 0);
    draw_list(c, 1);
    draw_msg(c);
    end(c);
}

/* The entry under the bar: its index, -1 "..", -2 none */
static int under(struct Panel *p)
{
    if (p->cur >= nrows(p))
        return -2;
    return p->cur - (p->up ? 1 : 0);
}

/* The Amiga's drawer: path from where it is (NULL: the one above). */
static BOOL loc_go(struct RConn *c, const char *to)
{
    struct Panel *p = &c->pn[0];
    char np[300], from[300];
    BPTR l;
    from[0] = 0;
    if (!to) {
        char *sl = strrchr(p->path, '/'), *co = strchr(p->path, ':');
        if (!p->path[0])
            return FALSE;
        strcpy(np, p->path);
        if (sl) {
            strcpy(from, sl + 1);
            np[sl - p->path] = 0;
        } else if (co && co[1]) {
            strcpy(from, co + 1);
            np[co - p->path + 1] = 0;
        } else {
            strcpy(from, p->path);      /* a volume: to the list of them */
            np[0] = 0;
        }
    } else if (strchr(to, ':') || !p->path[0]) {
        strncpy(np, to, sizeof np - 1);
        np[sizeof np - 1] = 0;
    } else {
        strcpy(np, p->path);
        if (!AddPart((STRPTR)np, (STRPTR)to, sizeof np))
            return FALSE;
    }
    if (np[0]) {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        BOOL dir = FALSE;
        if (!(l = Lock((STRPTR)np, ACCESS_READ))) {
            if (fib)
                FreeDosObject(DOS_FIB, fib);
            tsay(c, "\x1b[1;31m[", np, ": not there]\x1b[0m\r\n");
            return FALSE;
        }
        if (fib) {
            dir = Examine(l, fib) && fib->fib_DirEntryType > 0;
            FreeDosObject(DOS_FIB, fib);
        }
        if (!dir || !NameFromLock(l, (STRPTR)np, sizeof np)) {
            UnLock(l);
            if (!dir)
                tsay(c, "\x1b[1;31m[", np, ": not a drawer]\x1b[0m\r\n");
            return FALSE;
        }
        UnLock(l);
    }
    strcpy(p->path, np);
    p->cur = p->top = 0;
    p->nent = 0;
    loc_list(c);
    if (from[0])
        pan_seek(p, from);
    return TRUE;
}

/* The host's drawer: into name (NULL: the one above). */
static BOOL rem_go(struct RConn *c, const char *to)
{
    char from[256], *sl;
    BOOL ok;
    from[0] = 0;
    if (!to && (sl = strrchr(c->cwd, '/')) && sl[1]) {
        strncpy(from, sl + 1, sizeof from - 1);
        from[sizeof from - 1] = 0;
    }
    work(c, TRUE);
    ok = rem_cd(c, to);
    work(c, FALSE);
    if (ok && from[0])
        pan_seek(&c->pn[1], from);
    return ok;
}

static void open_it(struct RConn *c)
{
    int s = c->side;
    struct Panel *p = &c->pn[s];
    int e = under(p);
    char nm[300];
    if (e == -2)
        return;
    if (e == -1) {
        if (s ? rem_go(c, NULL) : loc_go(c, NULL))
            ui_draw(c);
        return;
    }
    if (!p->ent[e].dir) {
        char m[200], z[16];
        shown_name(c, s, e, m, 120);
        strcat(m, ": ");
        size_text(p->ent[e].size, z);
        strcat(m, z);
        strcat(m, ", ");
        strcat(m, p->ent[e].date);
        ui_note(c, m, FALSE);
        return;
    }
    strncpy(nm, pname(p, e), sizeof nm - 1);
    nm[sizeof nm - 1] = 0;
    if (s ? rem_go(c, nm) : loc_go(c, nm))
        ui_draw(c);
    else
        redraw_msg(c);
}

static void parent(struct RConn *c)
{
    if (c->side ? rem_go(c, NULL) : loc_go(c, NULL))
        ui_draw(c);
}

static void mark(struct RConn *c)
{
    struct Panel *p = &c->pn[c->side];
    int e = under(p);
    if (e >= 0) {
        p->ent[e].mark = !p->ent[e].mark;
        if (same_size(c)) {
            begin(c);
            draw_item(c, c->side, p->cur - p->top);
            draw_info(c, c->side);
            end(c);
        }
    }
    move(c, c->side, p->cur + 1);
}

static void invert(struct RConn *c)
{
    struct Panel *p = &c->pn[c->side];
    int i;
    for (i = 0; i < p->nent; i++)
        if (p->ent[i].dir != 1)
            p->ent[i].mark = !p->ent[i].mark;
    ui_draw(c);
}

static void unmark(struct Panel *p)
{
    int i;
    for (i = 0; i < p->nent; i++)
        p->ent[i].mark = 0;
}

/* A name looked for as it is typed: the first that starts so. */
static void quick(struct RConn *c, int k)
{
    struct Panel *p = &c->pn[c->side];
    char n[256];
    int i;
    if (c->qn >= (int)sizeof c->qs - 1)
        return;
    c->qs[c->qn] = k;
    c->qs[c->qn + 1] = 0;
    for (i = 0; i < p->nent; i++) {
        shown_name(c, c->side, i, n, sizeof n);
        if (!strnicmp(n, c->qs, c->qn + 1))
            break;
    }
    if (i == p->nent) {
        c->qs[c->qn] = 0;               /* no such name: not taken */
        beep();
        return;
    }
    c->qn++;
    move(c, c->side, i + (p->up ? 1 : 0));
    redraw_msg(c);
}

/* ---- the text view (help, ls) ---- */

static void view_show(struct RConn *c, const char *title, char *text,
                      LONG len)
{
    int i;
    if (c->view)
        FreeVec(c->view);
    c->view = text;
    c->vlen = len;
    c->vtop = 0;
    c->vn = 1;
    for (i = 0; i < len; i++)
        c->vn += text[i] == '\n';
    strncpy(c->vtitle, title, sizeof c->vtitle - 1);
    c->vtitle[sizeof c->vtitle - 1] = 0;
    c->mode = U_VIEW;
    ui_draw(c);
}

static void view_key(struct RConn *c, int a, BOOL any)
{
    int top = c->vtop, max = c->vn - LH;
    switch (a) {
    case K_UP: top--; break;
    case K_DOWN: top++; break;
    case K_PGUP: top -= LH - 1; break;
    case K_PGDN: top += LH - 1; break;
    case K_TOP: top = 0; break;
    case K_BOTTOM: top = max; break;
    default:
        if (!any)
            return;
        c->mode = U_PANELS;             /* any other key: back */
        if (c->view)
            FreeVec(c->view);
        c->view = NULL;
        ui_draw(c);
        return;
    }
    if (top > max)
        top = max;
    if (top < 0)
        top = 0;
    if (top != c->vtop) {
        c->vtop = top;
        if (same_size(c)) {
            begin(c);
            draw_view(c);
            end(c);
        }
    }
}

/* A growing text: lines added (Latin-1). */
struct Text {
    char *t;
    LONG n, cap;
};

static void tx_add(struct Text *x, const char *s)
{
    LONG l = strlen(s);
    if (x->n + l + 1 > x->cap) {
        LONG cap = x->cap ? x->cap * 2 : 2048;
        char *n;
        while (cap < x->n + l + 1)
            cap *= 2;
        if (!(n = AllocVec(cap, MEMF_ANY)))
            return;
        if (x->t) {
            CopyMem(x->t, n, x->n);
            FreeVec(x->t);
        }
        x->t = n;
        x->cap = cap;
    }
    CopyMem((APTR)s, x->t + x->n, l);
    x->n += l;
    x->t[x->n] = 0;
}

static const char cmdhelp[] =
"\nThe command line (the command key, : as it comes) takes, as Linux's\n"
"ftp and sftp do (names with a space in \"quotes\"; #? and * match any\n"
"letters, ? one; Tab completes a name or a command):\n"
"  ls [pattern]         the host's drawer listed (lls: the Amiga's)\n"
"  cd drawer            into the host's drawer (cd .. goes up)\n"
"  lcd drawer           the Amiga's (lcd Work:, lcd .., lcd sub)\n"
"  pwd, lpwd            where the host's and the Amiga's panels are\n"
"  get name..., mget pattern...   from the host into the Amiga's drawer\n"
"  put name..., mput pattern...   from the Amiga's drawer to the host\n"
"                       (a drawer named goes with all in it; -r: the\n"
"                       drawers a pattern matches too)\n"
"  mkdir name           a new drawer on the host\n"
"  rm name..., rmdir name         delete on the host (asks first; a\n"
"                       drawer with all in it)\n"
"  rename old new       rename on the host\n"
"  passive              FTP: active (PORT) or passive again\n"
"  bye                  hang up and close the tab\n"
"  help                 this\n"
"\nThe keys are CTerm's: Settings, Keys, or ENVARC:CTerm/CTerm.keys\n"
"([files] and [filesline]).\n";

static void help(struct RConn *c)
{
    struct Text x;
    int ctx, a, i;
    memset(&x, 0, sizeof x);
    for (ctx = 1; ctx <= 2; ctx++) {
        tx_add(&x, ctx == 1 ? "Keys in the panels (letters not bound here "
                              "find a name as you type it):\n"
                            : "\nKeys while a line is typed:\n");
        for (a = 1; a < K_N; a++) {
            char ks[60], k[24];
            if (acts[a].ctx != ctx)
                continue;
            ks[0] = 0;
            for (i = 0; i < keys_n; i++)
                if (keys_binds[i].act == KA_FB_FIRST + a - 1 &&
                    strlen(ks) < 40) {
                    keys_name(keys_binds[i].key, k, FALSE);
                    if (ks[0])
                        strcat(ks, " ");
                    strcat(ks, k);
                }
            if (!ks[0])
                continue;
            tx_add(&x, "  ");
            tx_add(&x, ks);
            for (i = strlen(ks); i < 22; i++)
                tx_add(&x, " ");
            tx_add(&x, " ");
            tx_add(&x, acts[a].desc);
            tx_add(&x, "\n");
        }
    }
    tx_add(&x, cmdhelp);
    if (keys_bad[0]) {
        tx_add(&x, "\nCTerm.keys: ");
        tx_add(&x, keys_bad);
        tx_add(&x, "\n");
    }
    if (x.t)
        view_show(c, "Help", x.t, x.n);
}

/* ---- the files acted on: marked, else the one under the bar ---- */

/* Entry i of side s's panel into the job (its date with it) */
static BOOL job_ent(struct RConn *c, int s, int i)
{
    struct Panel *p = &c->pn[s];
    const struct REnt *e = &p->ent[i];
    struct XItem *it;
    if (!job_add(&c->job, s ? NULL : p->path, pname(p, i),
                 e->dir == 1 ? 0 : e->size, e->dir))
        return FALSE;
    it = &c->job.it[c->job.n - 1];
    it->when = e->when;
    it->tz = e->tz;
    return TRUE;
}

/* What the keys act on, as a job: the marked entries, else the one
 * under the bar - drawers too (all that is in them goes with them). */
static BOOL job_of(struct RConn *c, int s)
{
    struct Panel *p = &c->pn[s];
    int i, n = 0, e = under(p);
    job_free(&c->job);
    c->job.side = s;
    if (!s && !p->path[0])
        return FALSE;                   /* the volumes */
    for (i = 0; i < p->nent; i++)
        n += p->ent[i].mark;
    for (i = 0; i < p->nent; i++)
        if ((n ? p->ent[i].mark : i == e) && !job_ent(c, s, i))
            break;
    if (!c->job.n) {
        job_free(&c->job);
        return FALSE;
    }
    return TRUE;
}

/* The job's files and drawers to the host. Those there already: how 0
 * left out, 1 replaced, 2 gone on from where they end (a drawer: what
 * it has already left as it is, what is shorter there gone on). */
static void send_job(struct RConn *c, int how)
{
    struct Job *j = &c->job;
    int i, k = 0;
    if (!how) {
        for (i = 0; i < j->n; i++)
            if (!j->it[i].exist)
                j->it[k++] = j->it[i];
        j->n = k;
    }
    c->upmode = how == 2;
    if (j->n) {
        work(c, TRUE);
        if (job_walk(c, j))
            xfer_batch(c, TRUE, j, NULL);
        rem_list(c);
        work(c, FALSE);
    }
    unmark(&c->pn[0]);
    job_free(j);
    ui_draw(c);
}

/* The job's files and drawers from the host into the Amiga's drawer:
 * a drawer copied again goes into the one there, what it has already
 * (as long) left out. */
static void fetch_job(struct RConn *c)
{
    struct Job *j = &c->job;
    if (!c->pn[0].path[0]) {
        ui_note(c, "Choose a drawer on the Amiga's side first", TRUE);
        job_free(j);
        return;
    }
    work(c, TRUE);
    if (job_walk(c, j))
        xfer_batch(c, FALSE, j, c->pn[0].path);
    work(c, FALSE);
    unmark(&c->pn[1]);
    job_free(j);
    loc_list(c);
    ui_draw(c);
}

static void ask(struct RConn *c, int what, const char *q)
{
    c->mode = U_ASK;
    c->akind = what;
    strncpy(c->line, q, sizeof c->line - 1);
    c->line[sizeof c->line - 1] = 0;
    redraw_msg(c);
}

/* An upload: asked first if names are there already (but the one that
 * broke off: it goes on by itself). */
static void up_job(struct RConn *c)
{
    struct Job *j = &c->job;
    int i, k = 0;
    char h[256], q[200];
    j->nexist = 0;
    for (i = 0; i < j->n; i++) {
        to_utf8(JREL(j, i), h, sizeof h);
        j->it[i].exist = pan_find(&c->pn[1], h) >= 0;
        if (j->it[i].exist && strcmp(h, c->upart)) {
            j->nexist++;
            k = i;
        }
    }
    if (!j->nexist) {
        send_job(c, 1);
        return;
    }
    /* (short: the answers must fit 80 columns with the name) */
    q[0] = 0;
    if (j->nexist == 1) {
        const char *n = JREL(j, k);
        int l = strlen(n);
        if (l > 16) {                   /* its start and end, ~ between */
            strncat(q, n, 7);
            strcat(q, "~");
            strcat(q, n + l - 8);
        } else
            strcat(q, n);
        strcat(q, j->it[k].dir == 1 ? "/" : "");
        strcat(q, " is on the host: y replace, n skip, g only what is "
                  "missing");
    } else {
        numstr_cat(q, j->nexist);
        strcat(q, " are on the host: y replace, n skip, g only what is "
                  "missing");
    }
    ask(c, A_REPLACE, q);
}

static void copy(struct RConn *c)
{
    if (!job_of(c, c->side))
        return;
    if (c->side)
        fetch_job(c);
    else
        up_job(c);
}

static void del_ask(struct RConn *c)
{
    char q[200];
    struct Job *j = &c->job;
    int i, d = 0;
    for (i = 0; i < j->n; i++)
        d += j->it[i].dir == 1;
    strcpy(q, "Delete ");
    if (j->n == 1) {
        char n[120];
        if (j->side)
            to_latin1(JNAME(j, 0), strlen(JNAME(j, 0)), n, 80, '?');
        else
            strncpy(n, (char *)FilePart((STRPTR)JNAME(j, 0)), 80), n[80] = 0;
        if (d)
            strcat(q, "the drawer ");
        strcat(q, n);
        if (d)
            strcat(q, " and all in it,");
    } else {
        numstr_cat(q, j->n);
        strcat(q, d ? " files and drawers (all in them too)" : " files");
    }
    strcat(q, j->side ? " on the host? (y/n)" : " on the Amiga? (y/n)");
    ask(c, A_DELETE, q);
}

/* Deleted: what is in the drawers first (the walk's order, backwards) */
static void del_job(struct RConn *c)
{
    struct Job *j = &c->job;
    int i;
    work(c, TRUE);
    if (!job_walk(c, j))
        tsay(c, "\x1b[1;31m[nothing deleted: the drawers could not be "
             "read]\x1b[0m\r\n", NULL, NULL);
    else
        for (i = j->n - 1; i >= 0 && !given_up(c); i--) {
            struct XItem *it = &j->it[i];
            char nm[120];
            BOOL ok;
            if (j->side) {
                ok = rem_do(c, it->dir == 1 ? FR_RMDIR : FR_DELETE,
                            JNAME(j, i), NULL);
                to_latin1(JNAME(j, i), strlen(JNAME(j, i)), nm, 100, '?');
            } else {
                ok = DeleteFile((STRPTR)JNAME(j, i));
                strncpy(nm, (char *)FilePart((STRPTR)JNAME(j, i)), 100);
                nm[100] = 0;
                if (!ok) {
                    char why[80];
                    Fault(IoErr(), (STRPTR)"", (STRPTR)why, sizeof why);
                    tsay(c, "\x1b[1;31m[", nm, why);
                    tsay(c, "]\x1b[0m\r\n", NULL, NULL);
                }
            }
            if (ok && !it->deep)
                tsay(c, "\x1b[2m[", nm, " deleted]\x1b[0m\r\n");
        }
    if (j->side)
        rem_list(c);
    else
        loc_list(c);
    work(c, FALSE);
    unmark(&c->pn[j->side]);
    job_free(j);
    ui_draw(c);
}

/* ---- a line typed ---- */

static void line_start(struct RConn *c, int kind, const char *text)
{
    c->mode = U_LINE;
    c->lkind = kind;
    c->line[0] = 0;
    if (text)
        strncat(c->line, text, sizeof c->line - 1);
    c->llen = c->lpos = strlen(c->line);
    c->hpos = c->hn;
    c->qn = 0;
    redraw_msg(c);
}

static void exec_line(struct RConn *c, char *l);

/* Rename or a new drawer, on the active side. */
static void name_done(struct RConn *c, const char *nm)
{
    int s = c->side;
    char h[256], old[300], nw[300];
    BOOL ok;
    if (!nm[0])
        return;
    if (c->lkind == L_RENAME) {
        if (!c->job.n)
            return;
        if (s) {
            to_utf8(nm, h, sizeof h);
            work(c, TRUE);
            ok = strcmp(h, JNAME(&c->job, 0)) &&
                 rem_do(c, FR_RENAME, JNAME(&c->job, 0), h);
            if (ok)
                rem_list(c);
            work(c, FALSE);
        } else {
            strcpy(old, c->pn[0].path);
            AddPart((STRPTR)old, (STRPTR)JNAME(&c->job, 0), sizeof old);
            strcpy(nw, c->pn[0].path);
            AddPart((STRPTR)nw, (STRPTR)nm, sizeof nw);
            ok = Rename((STRPTR)old, (STRPTR)nw);
            if (!ok)
                tsay(c, "\x1b[1;31m[", JNAME(&c->job, 0),
                     ": could not rename it]\x1b[0m\r\n");
            loc_list(c);
        }
        job_free(&c->job);
        if (ok)
            pan_seek(&c->pn[s], s ? h : nm);
    } else {
        if (s) {
            to_utf8(nm, h, sizeof h);
            work(c, TRUE);
            ok = rem_do(c, FR_MKDIR, h, NULL);
            if (ok)
                rem_list(c);
            work(c, FALSE);
        } else {
            BPTR l;
            strcpy(nw, c->pn[0].path);
            AddPart((STRPTR)nw, (STRPTR)nm, sizeof nw);
            if ((ok = (l = CreateDir((STRPTR)nw)) != 0))
                UnLock(l);
            else
                tsay(c, "\x1b[1;31m[", nm, ": could not make it]\x1b[0m\r\n");
            loc_list(c);
        }
        if (ok)
            pan_seek(&c->pn[s], s ? h : nm);
    }
}

static int lower(UBYTE u)
{
    return (u >= 'A' && u <= 'Z') || (u >= 0xC0 && u <= 0xDE && u != 0xD7)
           ? u + 32 : u;
}

/* Tab: the word before the cursor completed - a command, or a name in
 * the panel the command is about (put, lcd, lls: the Amiga's). */
static void complete(struct RConn *c)
{
    static const char *const cmds[] = { "ls", "lls", "cd", "lcd", "pwd",
        "lpwd", "get", "mget", "put", "mput", "mkdir", "rm", "rmdir",
        "rename", "passive", "bye", "help", NULL };
    char word[256], best[256], n[256], first[16];
    int ws = c->lpos, i, nb = 0, s = 1, wl;
    BOOL quoted = FALSE, isfirst;
    struct Panel *p;
    while (ws > 0 && c->line[ws - 1] != ' ' && c->line[ws - 1] != '"')
        ws--;
    if (ws > 0 && c->line[ws - 1] == '"')
        quoted = TRUE;
    wl = c->lpos - ws;
    memcpy(word, c->line + ws, wl);
    word[wl] = 0;
    for (i = 0; i < c->llen && c->line[i] == ' '; i++)
        ;
    isfirst = i >= ws - (quoted ? 1 : 0);
    for (wl = 0; wl < 15 && c->line[i + wl] && c->line[i + wl] != ' ';
         wl++)
        first[wl] = c->line[i + wl];
    first[wl] = 0;
    wl = c->lpos - ws;
    if (!stricmp(first, "put") || !stricmp(first, "mput") ||
        !stricmp(first, "lcd") || !stricmp(first, "lls"))
        s = 0;
    p = &c->pn[s];
    best[0] = 0;
    if (isfirst) {
        for (i = 0; cmds[i]; i++)
            if (!strnicmp(cmds[i], word, wl)) {
                if (!nb++)
                    strcpy(best, cmds[i]);
                else {
                    int k = 0;
                    while (best[k] && best[k] == cmds[i][k])
                        k++;
                    best[k] = 0;
                }
            }
    } else
        for (i = 0; i < p->nent; i++) {
            shown_name(c, s, i, n, sizeof n);
            if (!strnicmp(n, word, wl)) {
                if (!nb++)
                    strcpy(best, n);
                else {
                    int k = 0;
                    while (best[k] && lower(best[k]) == lower(n[k]))
                        k++;
                    best[k] = 0;
                }
            }
        }
    if (!nb || (int)strlen(best) < wl) {
        beep();
        return;
    }
    {
        char rest[256], ins[260];
        strcpy(rest, c->line + c->lpos);
        ins[0] = 0;
        if (!quoted && strchr(best, ' ') && !isfirst)
            strcat(ins, "\"");
        strcat(ins, best);
        if (nb == 1)
            strcat(ins, (!quoted && strchr(best, ' ') && !isfirst) ? "\" "
                        : quoted ? "\" " : " ");
        if (ws + strlen(ins) + strlen(rest) + (ins[0] == '"') >=
            sizeof c->line - 1)
            return;
        c->line[ws] = 0;
        strcat(c->line, ins);
        c->lpos = strlen(c->line);
        strcat(c->line, rest);
        c->llen = strlen(c->line);
        if (nb > 1 && (int)strlen(best) == wl)
            beep();                     /* several: nothing more in common */
    }
}

static void line_key(struct RConn *c, int k)
{
    int a = find(2, k);
    switch (a) {
    case P_LEFT: if (c->lpos > 0) c->lpos--; break;
    case P_RIGHT: if (c->lpos < c->llen) c->lpos++; break;
    case P_START: c->lpos = 0; break;
    case P_END: c->lpos = c->llen; break;
    case P_BACK:
        if (c->lpos > 0) {
            memmove(c->line + c->lpos - 1, c->line + c->lpos,
                    c->llen - c->lpos + 1);
            c->lpos--;
            c->llen--;
        }
        break;
    case P_DEL:
        if (c->lpos < c->llen) {
            memmove(c->line + c->lpos, c->line + c->lpos + 1,
                    c->llen - c->lpos);
            c->llen--;
        }
        break;
    case P_KILL:
        memmove(c->line, c->line + c->lpos, c->llen - c->lpos + 1);
        c->llen -= c->lpos;
        c->lpos = 0;
        break;
    case P_COMPLETE:
        if (c->lkind == L_CMD)
            complete(c);
        break;
    case P_OLDER: case P_NEWER:
        if (c->lkind != L_CMD || !c->hn)
            break;
        c->hpos += a == P_OLDER ? -1 : 1;
        if (c->hpos < 0)
            c->hpos = 0;
        if (c->hpos >= c->hn) {
            c->hpos = c->hn;
            c->line[0] = 0;
        } else
            strcpy(c->line, c->hist[c->hpos]);
        c->llen = c->lpos = strlen(c->line);
        break;
    case P_CANCEL:
        c->mode = U_PANELS;
        job_free(&c->job);
        redraw_msg(c);
        return;
    case P_ACCEPT: {
        char l[256];
        strcpy(l, c->line);
        c->mode = U_PANELS;
        c->msg[0] = 0;
        redraw_msg(c);
        if (c->lkind == L_CMD) {
            if (l[0] && (!c->hn || strcmp(c->hist[c->hn - 1], l))) {
                if (c->hn == NHIST) {
                    memmove(c->hist[0], c->hist[1], (NHIST - 1) * 128);
                    c->hn--;
                }
                strncpy(c->hist[c->hn], l, 127);
                c->hist[c->hn++][127] = 0;
            }
            exec_line(c, l);
        } else {
            name_done(c, l);
            ui_draw(c);
        }
        return;
    }
    default:
        if (k >= 32 && k < 256 && k != 127 && c->llen < 250) {
            memmove(c->line + c->lpos + 1, c->line + c->lpos,
                    c->llen - c->lpos + 1);
            c->line[c->lpos++] = k;
            c->llen++;
        } else
            return;
    }
    redraw_msg(c);
}

static void ask_key(struct RConn *c, int k)
{
    BOOL yes = k == 'y' || k == 'Y', no = k == 'n' || k == 'N';
    BOOL go = (k == 'g' || k == 'G') && c->akind == A_REPLACE;
    if (!yes && !no && !go && find(1, k) != K_STOP &&
        find(2, k) != P_CANCEL)
        return;
    c->mode = U_PANELS;
    c->line[0] = 0;
    redraw_msg(c);
    switch (c->akind) {
    case A_QUIT:
        if (yes)
            hang_up(c);
        break;
    case A_DELETE:
        if (yes)
            del_job(c);
        else
            job_free(&c->job);
        break;
    case A_REPLACE:
        if (yes || no || go)
            send_job(c, yes ? 1 : no ? 0 : 2);
        else
            job_free(&c->job);
        break;
    }
}

/* ---- the command line ---- */

/* The words of a line: spaces between, "quotes" around one with spaces. */
static int words(char *l, char **w, int max)
{
    int n = 0;
    while (*l && n < max) {
        while (*l == ' ')
            l++;
        if (!*l)
            break;
        if (*l == '"') {
            w[n++] = ++l;
            while (*l && *l != '"')
                l++;
        } else {
            w[n++] = l;
            while (*l && *l != ' ')
                l++;
        }
        if (*l)
            *l++ = 0;
    }
    return n;
}

/* A pattern (#? ? * and AmigaDOS's others) parsed into tok: TRUE when it
 * has wild letters. */
static BOOL wild(const char *pat, char *tok, int max)
{
    char p[260];
    int k = 0;
    for (; *pat && k < (int)sizeof p - 3; pat++)
        if (*pat == '*') {
            p[k++] = '#';
            p[k++] = '?';
        } else
            p[k++] = *pat;
    p[k] = 0;
    return ParsePatternNoCase((STRPTR)p, (STRPTR)tok, max) == 1;
}

/* The names on side s the words match (patterns or names), as the job.
 * A name of a drawer is the drawer and all in it; a pattern matches
 * drawers too only when all ("-r", rm). FALSE: none. */
static BOOL job_words(struct RConn *c, int s, char **w, int n, BOOL all)
{
    struct Panel *p = &c->pn[s];
    char tok[600], nm[256];
    int i, k;
    BOOL ok = TRUE;
    job_free(&c->job);
    c->job.side = s;
    for (k = 0; k < n && ok; k++) {
        if (wild(w[k], tok, sizeof tok)) {
            for (i = 0; i < p->nent && ok; i++) {
                if (!all && p->ent[i].dir == 1)
                    continue;
                shown_name(c, s, i, nm, sizeof nm);
                if (MatchPatternNoCase((STRPTR)tok, (STRPTR)nm))
                    ok = job_ent(c, s, i);
            }
            continue;
        }
        /* a name: as the host has it, or an Amiga path */
        if (s) {
            host_name(c, w[k], nm, sizeof nm);
            if ((i = pan_find(p, nm)) >= 0)
                ok = job_ent(c, s, i);
            else
                ok = job_add(&c->job, NULL, nm, 0, 0);
        } else if (!strchr(w[k], ':') && (i = pan_find(p, w[k])) >= 0)
            ok = job_ent(c, s, i);
        else {
            BPTR l = Lock((STRPTR)w[k], ACCESS_READ);   /* Work:path */
            BOOL d = FALSE;
            if (l) {
                struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
                if (fib) {
                    d = Examine(l, fib) && fib->fib_DirEntryType > 0;
                    FreeDosObject(DOS_FIB, fib);
                }
                UnLock(l);
            }
            ok = job_add(&c->job, strchr(w[k], ':') ? NULL : p->path, w[k],
                         0, d);
        }
    }
    if (!c->job.n) {
        job_free(&c->job);
        ui_note(c, "No names match", TRUE);
        return FALSE;
    }
    return TRUE;
}

/* ls: a listing as text over the panels */
static void list_view(struct RConn *c, int s, const char *pat)
{
    struct Panel *p = &c->pn[s];
    struct Text x;
    char tok[600], nm[256], l[400], z[16];
    int i, w = 8;
    BOOL w_ = pat && wild(pat, tok, sizeof tok);
    memset(&x, 0, sizeof x);
    for (i = 0; i < p->nent; i++) {
        shown_name(c, s, i, nm, sizeof nm);
        if ((int)strlen(nm) > w)
            w = strlen(nm) < 40 ? strlen(nm) : 40;
    }
    for (i = 0; i < p->nent; i++) {
        int k;
        shown_name(c, s, i, nm, sizeof nm);
        if (pat && (w_ ? !MatchPatternNoCase((STRPTR)tok, (STRPTR)nm)
                       : stricmp(pat, nm)))
            continue;
        strcpy(l, nm);
        for (k = strlen(l); k < w; k++)
            l[k] = ' ';
        l[k] = 0;
        if (p->ent[i].dir == 1)
            strcpy(z, "Drawer");
        else
            size_text(p->ent[i].size, z);
        for (k = strlen(z); k < 9; k++)
            strcat(l, " ");
        strcat(l, z);
        strcat(l, "  ");
        strcat(l, p->ent[i].date);
        strcat(l, "\n");
        tx_add(&x, l);
    }
    if (!x.t)
        tx_add(&x, "(nothing)\n");
    if (x.t) {
        if (s)
            to_latin1(c->cwd, strlen(c->cwd), l, 60, '?');
        else
            strncpy(l, p->path[0] ? p->path : "Volumes", 60), l[60] = 0;
        view_show(c, l, x.t, x.n);
    }
}

static void exec_line(struct RConn *c, char *l)
{
    char *w[17], *cmd, h[256];
    int n = words(l, w, 17);
    BOOL r = FALSE;                     /* -r: patterns take drawers too */
    if (!n)
        return;
    cmd = w[0];
    if (n > 2 && (!strcmp(w[1], "-r") || !strcmp(w[1], "-R"))) {
        r = TRUE;
        memmove(w + 1, w + 2, (n - 2) * sizeof w[0]);
        n--;
    }
    if (!stricmp(cmd, "ls") || !stricmp(cmd, "dir"))
        list_view(c, 1, n > 1 ? w[1] : NULL);
    else if (!stricmp(cmd, "lls") || !stricmp(cmd, "ldir"))
        list_view(c, 0, n > 1 ? w[1] : NULL);
    else if ((!stricmp(cmd, "cd") || !stricmp(cmd, "lcd")) && n > 1) {
        BOOL lo = !stricmp(cmd, "lcd"), ok;
        if (!strcmp(w[1], ".."))
            ok = lo ? loc_go(c, NULL) : rem_go(c, NULL);
        else if (lo)
            ok = loc_go(c, w[1]);
        else {
            host_name(c, w[1], h, sizeof h);
            ok = rem_go(c, h);
        }
        if (ok)
            ui_draw(c);
    } else if (!stricmp(cmd, "pwd")) {
        to_latin1(c->cwd, strlen(c->cwd), h, 150, '?');
        ui_note(c, h, FALSE);
    } else if (!stricmp(cmd, "lpwd"))
        ui_note(c, c->pn[0].path[0] ? c->pn[0].path : "Volumes", FALSE);
    else if ((!stricmp(cmd, "get") || !stricmp(cmd, "mget")) && n > 1) {
        if (job_words(c, 1, w + 1, n - 1, r))
            fetch_job(c);
    } else if ((!stricmp(cmd, "put") || !stricmp(cmd, "mput")) && n > 1) {
        if (job_words(c, 0, w + 1, n - 1, r))
            up_job(c);
    } else if (!stricmp(cmd, "passive")) {
        if (c->kind != FK_FTP)
            ui_note(c, "SFTP has no passive or active: that is FTP's", TRUE);
        else {
            c->active = !c->active;
            ui_note(c, c->active ? "Active FTP from now on (PORT: the host "
                                   "calls the Amiga)"
                                 : "Passive FTP from now on (the Amiga "
                                   "calls the host)", FALSE);
        }
    } else if ((!stricmp(cmd, "mkdir") || !stricmp(cmd, "md")) && n > 1) {
        to_utf8(w[1], h, sizeof h);
        work(c, TRUE);
        if (rem_do(c, FR_MKDIR, h, NULL))
            rem_list(c);
        work(c, FALSE);
        ui_draw(c);
    } else if ((!stricmp(cmd, "rm") || !stricmp(cmd, "rmdir") ||
                !stricmp(cmd, "delete") || !stricmp(cmd, "del")) && n > 1) {
        if (job_words(c, 1, w + 1, n - 1, TRUE))
            del_ask(c);
    } else if ((!stricmp(cmd, "rename") || !stricmp(cmd, "mv") ||
                !stricmp(cmd, "ren")) && n > 2) {
        char h2[256];
        host_name(c, w[1], h, sizeof h);
        to_utf8(w[2], h2, sizeof h2);
        work(c, TRUE);
        if (rem_do(c, FR_RENAME, h, h2))
            rem_list(c);
        work(c, FALSE);
        ui_draw(c);
    } else if (!stricmp(cmd, "bye") || !stricmp(cmd, "quit") ||
               !stricmp(cmd, "exit") || !stricmp(cmd, "close"))
        hang_up(c);
    else if (!stricmp(cmd, "help") || !strcmp(cmd, "?"))
        help(c);
    else {
        char m[100];
        strcpy(m, "Not a command (or a word missing): ");
        strncat(m, cmd, 40);
        strcat(m, " - help lists them");
        ui_note(c, m, TRUE);
    }
}

/* ---- keys and the mouse ---- */

static void act(struct RConn *c, int a)
{
    struct Panel *p = &c->pn[c->side];
    int e;
    if (a != K_STOP && c->qn) {
        c->qn = 0;                      /* a search ends with any key */
        redraw_msg(c);
    }
    switch (a) {
    case K_UP: move(c, c->side, p->cur - 1); break;
    case K_DOWN: move(c, c->side, p->cur + 1); break;
    case K_PGUP: move(c, c->side, p->cur - (LH > 1 ? LH - 1 : 1)); break;
    case K_PGDN: move(c, c->side, p->cur + (LH > 1 ? LH - 1 : 1)); break;
    case K_TOP: move(c, c->side, 0); break;
    case K_BOTTOM: move(c, c->side, nrows(p) - 1); break;
    case K_LEFT: to_side(c, 0); break;
    case K_RIGHT: to_side(c, 1); break;
    case K_SWITCH: to_side(c, !c->side); break;
    case K_OPEN: open_it(c); break;
    case K_PARENT: parent(c); break;
    case K_MARK: mark(c); break;
    case K_INVERT: invert(c); break;
    case K_HELP: help(c); break;
    case K_REFRESH:
        loc_list(c);
        work(c, TRUE);
        rem_list(c);
        work(c, FALSE);
        ui_draw(c);
        break;
    case K_COPY: copy(c); break;
    case K_RENAME: {
        char n[256];
        e = under(p);
        if (e < 0 || (!c->side && !p->path[0]))
            break;
        job_free(&c->job);
        c->job.side = c->side;
        if (!job_add(&c->job, NULL, pname(p, e), 0, 0))
            break;
        shown_name(c, c->side, e, n, sizeof n);
        line_start(c, L_RENAME, n);
        break;
    }
    case K_MKDIR:
        if (c->side || p->path[0])
            line_start(c, L_MKDIR, NULL);
        break;
    case K_DELETE:
        if ((c->side || p->path[0]) && job_of(c, c->side))
            del_ask(c);
        break;
    case K_LOG:
        to_main(c);
        c->mode = U_LOG;
        {
            struct Tab *keep = T;
            T = c->tab;
            refresh();
            T = keep;
        }
        break;
    case K_QUIT:
        ask(c, A_QUIT, "Hang up and close this tab? (y/n)");
        break;
    case K_STOP:
        c->qn = 0;
        c->msg[0] = 0;
        redraw_msg(c);
        break;
    case K_CMD: line_start(c, L_CMD, NULL); break;
    case K_REDRAW: ui_draw(c); break;
    }
}

void ui_key(struct RConn *c, int k)
{
    int a;
    BOOL ok = geom(c);
    if (k == KM_SHIFTDN || k == KM_SHIFTUP) {
        BOOL s = k == KM_SHIFTDN;
        if (s != c->shifted) {
            c->shifted = s;
            if (c->ui && ok && c->mode != U_LOG && c->dc == W &&
                c->dr == H) {
                begin(c);
                draw_keys(c);
                end(c);
            }
        }
        return;
    }
    if (!c->ui) {                       /* calling: it may be given up */
        if (c->busy && find(1, k) == K_STOP)
            c->stop = TRUE;
        return;
    }
    if (c->mode == U_LOG) {             /* any key: the panels again */
        begin(c);
        o_str("\x1b[?1049h");
        c->mode = U_PANELS;
        ui_draw(c);
        return;
    }
    a = find(1, k);
    if (c->busy) {                      /* at work: it may be stopped */
        if (a == K_STOP) {
            c->stop = TRUE;
            if (c->x)
                c->x->stop = 1;
        } else if (a == K_REDRAW)
            ui_draw(c);
        return;
    }
    if (!ok) {
        if (a == K_QUIT || a == K_REDRAW)
            act(c, a);
        return;
    }
    switch (c->mode) {
    case U_ASK: ask_key(c, k); return;
    case U_LINE: line_key(c, k); return;
    case U_VIEW: view_key(c, a, TRUE); return;
    }
    if (c->qn && k == 8) {              /* a search: a letter back */
        c->qs[--c->qn] = 0;
        redraw_msg(c);
        return;
    }
    if (a) {
        act(c, a);
        return;
    }
    if (k >= 32 && k < 256 && k != 127)
        quick(c, k);
}

/* what: 0 a click, 1 the wheel up, 2 down; r, col: the cell */
void ui_mouse(struct RConn *c, int what, int r, int col, ULONG s, ULONG us)
{
    struct Panel *p;
    int side, row;
    if (c->mode == U_LOG) {
        if (!what)
            ui_key(c, 0);
        return;
    }
    if (!geom(c) || c->busy || c->mode == U_LINE || c->mode == U_ASK)
        return;
    if (c->mode == U_VIEW) {
        if (what) {
            int i;
            for (i = 0; i < 3; i++)
                view_key(c, what == 1 ? K_UP : K_DOWN, FALSE);
        }
        else
            view_key(c, K_NONE, TRUE);
        return;
    }
    if (!what && r == H - 1) {          /* the key bar (as it shows) */
        int a = find(1, (c->shifted ? KM_SF1 : KM_F1) + col * 10 / W);
        if (a)
            act(c, a);
        return;
    }
    if (r >= PH)
        return;
    side = col < LW ? 0 : 1;
    p = &c->pn[side];
    if (what) {                         /* the wheel: the panel under it */
        int n = nrows(p), top = p->top + (what == 1 ? -3 : 3);
        if (top > n - LH)
            top = n - LH;
        if (top < 0)
            top = 0;
        if (top == p->top)
            return;
        p->top = top;
        if (p->cur < top)
            p->cur = top;
        if (p->cur >= top + LH)
            p->cur = top + LH - 1;
        if (!same_size(c))
            return;
        begin(c);
        draw_list(c, side);
        end(c);
        return;
    }
    if (c->qn) {
        c->qn = 0;
        redraw_msg(c);
    }
    to_side(c, side);
    if (r < 1 || r > LH)
        return;
    row = p->top + r - 1;
    if (row >= nrows(p))
        return;
    move(c, side, row);
    if (row == c->clickrow && DoubleClick(c->clicks, c->clickm, s, us)) {
        c->clickrow = -1;
        open_it(c);
        return;
    }
    c->clickrow = row;
    c->clicks = s;
    c->clickm = us;
}
