/*
 * clip - copy and paste through clipboard.device, and the log.
 */
#include "cterm.h"

static BOOL clip_open(void);
static void put_be(UBYTE *p, ULONG v);
static ULONG get_be(const UBYTE *p);

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

void clip_close(void)
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
void sel_redraw(int ra, int rb)
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

/* Double-click: the word at r,c (letters, digits and what names and
 * paths are made of); triple-click: the whole line. FALSE: nothing
 * there to mark. */
BOOL sel_word(int r, int c, BOOL line)
{
    int w, a, b;
    const VtCell *l = vt_line(vt, r, back, &w);
#define WORDY(x) (l[x].cp == VT_WIDE_TAIL || (l[x].cp > ' ' && \
    (l[x].cp >= 128 || !strchr("\"'`()[]{}<>,;|", (int)l[x].cp))))
    if (line) {
        sa_c = 0;
        sb_c = cols - 1;
        return TRUE;
    }
    if (!l || c >= w || !WORDY(c))
        return FALSE;
    for (a = c; a > 0 && WORDY(a - 1); a--)
        ;
    for (b = c; b + 1 < w && b + 1 < cols && WORDY(b + 1); b++)
        ;
#undef WORDY
    sa_c = a;
    sb_c = b;
    return TRUE;
}

void sel_clear(void)
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
void sel_copy(void)
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
    clip_put(buf, n);
    FreeVec(buf);
}

/* buf+20 holds n bytes of Latin-1: an FTXT around them, onto the
 * clipboard (buf has room for the 20-byte head and a pad byte). */
void clip_put(UBYTE *buf, ULONG n)
{
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
}

/* What a program on the host copied (OSC 52, vim/tmux): onto the
 * Amiga's clipboard, UTF-8 made Latin-1. */
void host_clip(void)
{
    int len, i;
    ULONG n = 0;
    UBYTE *buf;
    char *t = vt_take_clip(vt, &len);
    if (!t)
        return;
    if (len > 0 && clip_open() && (buf = AllocVec(len + 22, MEMF_ANY))) {
        for (i = 0; i < len; i++) {
            UBYTE c = (UBYTE)t[i];
            if (c < 0x80 || !utf8)
                buf[20 + n++] = c;
            else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
                ULONG cp = ((c & 0x1F) << 6) | (t[++i] & 0x3F);
                buf[20 + n++] = cp < 256 ? (UBYTE)cp : '?';
            } else if (c >= 0xC0) {
                while (i + 1 < len && ((UBYTE)t[i + 1] & 0xC0) == 0x80)
                    i++;
                buf[20 + n++] = '?';
            }
        }
        clip_put(buf, n);
        FreeVec(buf);
    }
    free(t);
}

/* ---- the log: what came in, into a file ---------------------------- */

void log_stop(void)
{
    if (!T->t_log)
        return;
    Close(T->t_log);
    T->t_log = 0;
    if (T == shown)
        title_update_now();
}

/* Start log...: a file for this tab, added to when it is there. */
void log_start(void)
{
    struct FileRequester *req;
    char path[256];
    BPTR fh;
    if (!asl_open())
        return;
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: log this tab to...",
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialDrawer, (ULONG)"RAM:",
        ASLFR_InitialFile, (ULONG)"CTerm.log", ASLFR_RejectIcons, TRUE,
        TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL) && req->fr_File[0]) {
        strncpy(path, (char *)req->fr_Drawer, sizeof path - 1);
        path[sizeof path - 1] = 0;
        AddPart((STRPTR)path, req->fr_File, sizeof path);
        fh = Open((STRPTR)path, MODE_READWRITE);    /* made if missing */
        if (fh) {
            Seek(fh, 0, OFFSET_END);
            log_stop();
            T->t_log = fh;
            title_update_now();
        } else
            DisplayBeep(scr);
    }
    FreeAslRequest(req);
}

/* The clipboard's text, typed in: to the Shell's line, or to the host
 * (bracketed when the program asked); a new line is a Return. */
static void paste_(UBYTE *buf, ULONG bufsize)
{
    static UBYTE scratch[256];
    ULONG got, o;
    if (!clip_open())
        return;
    clipreq->io_Command = CMD_READ;
    clipreq->io_Data = (STRPTR)buf;
    clipreq->io_Length = bufsize;
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
        if (sz > got - o - 8)           /* past the end (or wraps): done */
            break;
        o += 8 + sz + (sz & 1);
    }
}

/* 16 KB for the clipboard's text only while pasting: a stock A1200
 * keeps it otherwise. */
void paste(void)
{
    UBYTE *buf = AllocVec(16384, MEMF_ANY);
    if (!buf)
        return;
    paste_(buf, 16384);
    FreeVec(buf);
}

/* Mouse position to a cell, held inside the grid. */
void mouse_cell(int mx, int my, int *r, int *c)
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

