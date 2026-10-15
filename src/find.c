/*
 * find - searching the history (Edit/Find in history), and links: the
 * address under a Ctrl+click or in the marked text, opened.
 */
#include "cterm.h"

/* ---- a line of the tab's text, any line ----------------------------
 * The history and the screen as one list: 0 the oldest kept line,
 * vt_history() the screen's top row. */

static int all_lines(void)
{
    return vt_history(vt) + rows;
}

/* Line L as Latin-1, one byte a cell (so a column is an index): a wide
 * letter's right half, a picture and what Latin-1 lacks are bytes no
 * one types. Returns the cells. */
static int line_text(int L, UBYTE *out, int max)
{
    int h = vt_history(vt), w, c;
    const VtCell *l = L < h ? vt_line(vt, 0, h - L, &w)
                            : vt_line(vt, L - h, 0, &w);
    if (!l)
        return 0;
    if (w > max)
        w = max;
    for (c = 0; c < w; c++) {
        ULONG cp = l[c].cp;
        out[c] = cp == 0 ? ' ' : cp == VT_WIDE_TAIL || VT_IS_PIC(cp) ? 1
               : cp < 256 ? (UBYTE)cp : 2;
    }
    return w;
}

static UBYTE fold(UBYTE c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 32;
    return c;
}

/* ---- find ----------------------------------------------------------
 * A mode as i3's resize: every key is its own while it lasts. Typing
 * searches back from the newest line for the text (any case); Return
 * or Up goes on to older, Shift+Return or Down to newer; Esc ends it,
 * the view staying where it is with the match marked (Copy takes it). */

static BOOL finding;
static struct Tab *ftab;                /* the tab it searches */
static UBYTE ftext[48], flast[48];    /* the text; the last one searched */
static int flen;
static int fline = -1, fcol;            /* the match shown, or -1 */
static UBYTE fbuf[512];

BOOL find_active(void)
{
    if (finding && (shown != ftab || !shown))
        finding = FALSE;                /* the tab went, or another shows */
    return finding;
}

static void find_title(BOOL missed)
{
    char t[100];
    if (!flen && flast[0])
        sprintf(t, "find: _  (the find key again: \"%.20s\")", (char *)flast);
    else
        sprintf(t, "find: %s_%s  (Return older, Shift+Return newer, Esc)",
                (char *)ftext, missed ? " - not found" : "");
    title_prompt(t);
}

/* Is the text at line L, column c? */
static int find_in(int L, int from, int dir)
{
    int n = line_text(L, fbuf, sizeof fbuf), c, i;
    if (from < 0 || from > n - flen)
        from = dir < 0 ? n - flen : 0;
    for (c = from; c >= 0 && c <= n - flen; c += dir) {
        for (i = 0; i < flen && fold(fbuf[c + i]) == fold(ftext[i]); i++)
            ;
        if (i == flen)
            return c;
    }
    return -1;
}

/* The match at line L, column c: brought into view (in the middle, when
 * it was not on the screen) and marked. */
static void find_show(int L, int c)
{
    int h = vt_history(vt), top = h - back, b;
    sel_clear();
    if (L < top || L >= top + rows) {
        b = h - (L - rows / 2);
        scroll_view(b);                 /* (kept within the history) */
        top = h - back;
    }
    sa_r = sb_r = L - top;
    sa_c = c;
    sb_c = c + flen - 1;
    if (sb_c >= cols)
        sb_c = cols - 1;
    sel_on = TRUE;
    sel_drag = FALSE;
    sel_redraw(sa_r, sa_r);
    fline = L;
    fcol = c;
}

/* Search from the match shown (or the newest line) in a direction:
 * -1 older, 1 newer; again: the match shown counts too (the text grew). */
static void find_go(int dir, BOOL again)
{
    int L, c, from;
    if (!flen) {
        find_title(FALSE);
        return;
    }
    if (fline < 0) {
        L = all_lines() - 1;
        from = -1;
    } else {
        L = fline;
        from = again ? fcol : fcol + dir;
    }
    for (; L >= 0 && L < all_lines(); L += dir, from = -1)
        if ((c = find_in(L, from, dir)) >= 0) {
            find_show(L, c);
            find_title(FALSE);
            return;
        }
    beep();
    find_title(TRUE);
}

static void find_end(void)
{
    finding = FALSE;
    if (flen)
        strcpy((char *)flast, (char *)ftext);
    title_prompt(NULL);
}

void find_start(void)
{
    if (!T || T->t_rf) {
        beep();                         /* a file browser has no history */
        return;
    }
    if (finding) {
        find_key(0);                    /* (as the key pressed in it) */
        return;
    }
    finding = TRUE;
    ftab = shown;
    fline = -1;
    flen = 0;                           /* a new text; the key again: the last */
    ftext[0] = 0;
    find_title(FALSE);
}

/* A key while finding (keys_code's). */
void find_key(UWORD k)
{
    UWORD key = k & KQ_KEY;
    BOOL shift = (k & KQ_S) != 0;
    if (!k || keys_find(KC_WINDOW, k) == KA_FIND) {
        if (!flen && flast[0]) {        /* nothing typed yet: the last text */
            strcpy((char *)ftext, (char *)flast);
            flen = strlen((char *)ftext);
            find_go(-1, TRUE);
        } else
            find_go(-1, FALSE);         /* the next older */
        return;
    }
    if (key == 27) {
        find_end();
        return;
    }
    if (key == 13) {
        find_go(shift ? 1 : -1, FALSE);
        return;
    }
    if (key == KM_UP || key == KM_DOWN) {
        find_go(key == KM_UP ? -1 : 1, FALSE);
        return;
    }
    if (key == 8) {
        if (flen) {
            ftext[--flen] = 0;
            fline = -1;                 /* from the newest again */
            sel_clear();
            find_go(-1, TRUE);
        }
        return;
    }
    if (k & (KQ_C | KQ_A | KQ_L) || key < ' ' || key > 255 ||
        flen >= (int)sizeof ftext - 1)
        return;
    ftext[flen++] = (UBYTE)key;
    ftext[flen] = 0;
    find_go(-1, TRUE);                  /* the match may stay where it is */
}

/* ---- links -----------------------------------------------------------
 * An address in the text: scheme://... (or www.), as far as letters an
 * address may hold go, the closing punctuation of a sentence left off. */

static const char *const schemes[] = {
    "http://", "https://", "ftp://", "sftp://", "ssh://", "telnet://",
    "gopher://", "gemini://", "www.", NULL
};

static BOOL urlish(UBYTE c)
{
    return c > ' ' && c < 127 && !strchr("\"'<>`{}|\\^", c);
}

/* The next address in t (n bytes) from byte `from` on: its start and
 * length in *s, *len; FALSE: none. */
static BOOL url_next(const UBYTE *t, int n, int from, int *s, int *len)
{
    int i, a, b, k;
    for (i = from; i < n; i++) {
        for (k = 0; schemes[k]; k++) {
            int m = strlen(schemes[k]), j;
            if (i + m > n)
                continue;
            for (j = 0; j < m && fold(t[i + j]) == schemes[k][j]; j++)
                ;
            if (j == m && (i == 0 || !isalnum(t[i - 1])))
                break;
        }
        if (!schemes[k])
            continue;
        a = i;
        for (b = i; b < n && urlish(t[b]); b++)
            ;
        /* a sentence's end or a bracket around it is not the address's */
        while (b > a && strchr(".,;:!?*", t[b - 1]))
            b--;
        if (b > a && t[b - 1] == ')') {
            int open = 0, x;
            for (x = a; x < b; x++)
                open += t[x] == '(' ? 1 : t[x] == ')' ? -1 : 0;
            if (open < 0)
                b--;
        }
        if (b - a > (int)strlen(schemes[k])) {
            *s = a;
            *len = b - a;
            return TRUE;
        }
        i = b;
    }
    return FALSE;
}

/* The address on line L that has a cell between columns c0 and c1:
 * into *s, *len (in fbuf, which then holds the line). */
static BOOL url_at(int L, int c0, int c1, int *s, int *len)
{
    int n = line_text(L, fbuf, sizeof fbuf), from = 0;
    while (url_next(fbuf, n, from, s, len)) {
        if (*s <= c1 && *s + *len - 1 >= c0)
            return TRUE;
        from = *s + *len;
    }
    return FALSE;
}

/* Settings: a program run with a web address (CTerm.prefs browser=):
 * "%s" is where the address goes, else it is put at the end. */
char browsercmd[160];

static struct Library *OpenURLBase;

/* openurl.library's URL_OpenA (-30): the user's browser, as set there */
static ULONG url_open_lib(STRPTR url)
{
    register ULONG d0 __asm("d0");
    register STRPTR a0 __asm("a0") = url;
    register APTR a1 __asm("a1") = NULL;
    register struct Library *a6 __asm("a6") = OpenURLBase;
    __asm volatile ("jsr a6@(-30:W)"
                    : "=r"(d0)
                    : "r"(a0), "r"(a1), "r"(a6)
                    : "d1", "memory", "cc");
    return d0;
}

static BOOL run_browser(const char *url)
{
    char cmd[400];
    const char *p = strstr(browsercmd, "%s");
    BPTR nil;
    LONG rc;
    if (!browsercmd[0])
        return FALSE;
    if (p)
        sprintf(cmd, "%.*s\"%s\"%s", (int)(p - browsercmd), browsercmd, url, p + 2);
    else
        sprintf(cmd, "%s \"%s\"", browsercmd, url);
    if (!(nil = Open((STRPTR)"NIL:", MODE_NEWFILE)))
        return FALSE;
    rc = SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                    SYS_Asynch, TRUE, NP_StackSize, 16384, TAG_DONE);
    if (rc == -1) {
        Close(nil);
        return FALSE;
    }
    return TRUE;
}

/* Open an address: a host's (telnet, ssh, ftp, sftp) as CTerm's own new
 * tab; the web's with openurl.library or the browser setting; else it
 * goes onto the clipboard, which says so. */
static void link_open(const UBYTE *t, int len)
{
    char url[300];
    UBYTE cb[20 + sizeof url + 2];
    if (len > (int)sizeof url - 8)
        len = sizeof url - 8;
    if (!strnicmp((const char *)t, "www.", 4))
        strcpy(url, "http://");
    else
        url[0] = 0;
    strncat(url, (const char *)t, len);
    if (!strnicmp(url, "telnet://", 9) || !strnicmp(url, "ssh://", 6) ||
        !strnicmp(url, "ftp://", 6) || !strnicmp(url, "sftp://", 7)) {
        struct Tab *made;
        title_hint("opening the link in a new tab");
        call_rexx(url, &made);
        T = shown;
        return;
    }
    if (!OpenURLBase)
        OpenURLBase = OpenLibrary((STRPTR)"openurl.library", 0);
    if (OpenURLBase && url_open_lib((STRPTR)url)) {
        title_hint("link opened");
        return;
    }
    if (run_browser(url)) {
        title_hint("link opened");
        return;
    }
    strcpy((char *)cb + 20, url);
    clip_put(cb, strlen(url));
    title_hint("no browser set: link copied");
}

/* Ctrl+click: the address under the pointer (screen row r, column c),
 * if any - TRUE when there was one. */
BOOL link_click(int r, int c)
{
    int s, len;
    if (!url_at(vt_history(vt) - back + r, c, c, &s, &len))
        return FALSE;
    link_open(fbuf + s, len);
    return TRUE;
}

/* Edit/Open link: the first address the marked text touches, else the
 * first on the cursor's line. */
void link_menu(void)
{
    int r0, c0, r1, c1, r, s, len, x, y, vis;
    if (sel_on) {
        sel_bounds(&r0, &c0, &r1, &c1);
    } else {
        vt_cursor(vt, &x, &y, &vis);
        r0 = r1 = y + back;             /* (screen rows: the view's) */
        c0 = 0;
        c1 = cols - 1;
        if (r0 >= rows) {
            beep();
            return;
        }
    }
    for (r = r0; r <= r1; r++)
        if (url_at(vt_history(vt) - back + r, r == r0 ? c0 : 0,
                   r == r1 ? c1 : cols - 1, &s, &len)) {
            link_open(fbuf + s, len);
            return;
        }
    beep();
}

void find_free(void)
{
    if (OpenURLBase) {
        CloseLibrary(OpenURLBase);
        OpenURLBase = NULL;
    }
}
