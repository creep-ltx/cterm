/*
 * console - CTerm as the local Shell's console.
 *
 * No mounted handler: CTerm makes a FileHandle whose port is its own,
 * and a helper process Execute()s an interactive Shell on it (the form
 * the old CTerm proved at boot - a SystemTags/SYS_UserShell start crashed
 * there). The Shell's ConsoleTask is then this port too, so "*" and
 * CONSOLE: opens land here. The packets are answered in CTerm's main
 * loop. What CCON learned decides how (Knowledge/amiga/console-protocol.md,
 * amigados-handlers.md, graphics-and-performance.md):
 *
 * - WRITE: the bytes go into the emulator and the packet is answered at
 *   once; the screen catches up within a frame, so a flood is drawn
 *   once, not per write (accept-then-render).
 * - Before anything waits for the person (a READ with nothing typed, a
 *   WAIT_CHAR), the screen is brought up to date.
 * - DISK_INFO answers 'CTRM', not 'CON\0', so the 3.2 Shell hands line
 *   editing to the console - CTerm edits the line here, with history.
 * - DISK_INFO's id_VolumeNode is the window: More retitles it, Ed hangs
 *   its menus on it.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/timer.h>

#include <string.h>

#include "console.h"
#include "dbg.h"

struct Device *TimerBase = NULL;

#define INMAX   4096
#define LINEMAX 1024
#define HISTMAX 50
#define READMAX 16
#define ID_CTRM 0x4354524DUL            /* 'CTRM': "not CON:, edit lines" */

/* One console: a Shell's port, its typed input, its line editor. CTerm
 * keeps one per tab. */
struct Con {
    void *user;                         /* the tab: given to the term_ calls */
    struct MsgPort *conport;
    struct Task *maintask;
    LONG endbit;                        /* (shared by all, consig) */
    volatile BOOL child_done;           /* its helper has ended */
    struct FileHandle *infh;
    char startcmd[256];
    BOOL child_running, started;        /* started: the Shell closes infh */
    int opens;                          /* handles open on this console */

    UBYTE inq[INMAX];                   /* ready for READs */
    LONG inlen;
    BOOL eof_pending;
    BOOL closing;                       /* the tab closes: every read ends (con_stop) */
    BOOL rawmode;
    struct DosPacket *reads[READMAX];
    int nreads;
    struct DosPacket *waitpkt;
    struct Task *breaktask;             /* whom Ctrl-C..F go to */
    struct Task *shelltask;             /* the first reader: the Shell */
    LONG breakarg;                      /* CHANGE_SIGNAL's last arg2 */
    struct timerequest *ctio;           /* a WaitForChar's timeout */
    BOOL carmed;

    /* the line being typed (cooked mode) */
    UBYTE line[LINEMAX];
    int llen, lcur;
    BOOL editing;
    int x0, y0;                         /* where it starts on screen */
    UBYTE *hist[HISTMAX];
    int nhist, hpos;
    UBYTE spat[64];                     /* a history search's text, or "" */

    /* keys typed while nobody reads, kept as keys (a shell that edits its
     * own line asked, CSI ?7727 h): as bash's terminal keeps them */
    UBYTE ahead[INMAX];
    LONG aheadlen;

    /* a paste not yet taken: fed in as the reader asks for it */
    UBYTE *paste;
    LONG plen, ppos;
    BOOL pfull;                         /* this line was too long: beeped */
};

/* ---- the helper that runs the Shell ---------------------------------- */

static void child_entry(void)
{
    /* which console: set at creation (NP_ExitData), before this runs */
    struct Process *me = (struct Process *)FindTask(NULL);
    struct Con *c = (struct Con *)me->pr_ExitData;
    /* an interactive Shell on our handle: returns at EndShell */
    Execute((STRPTR)c->startcmd, MKBADDR(c->infh), 0);
    Close(MKBADDR(c->infh));
    /* tell CTerm, and leave without returning to its code first */
    Forbid();
    c->child_done = TRUE;               /* (the signal is everyone's) */
    Signal(c->maintask, 1UL << c->endbit);
}

/* ---- the timer: one port, the frame shared, a WaitForChar per console - */

static struct MsgPort *timerport;
static struct timerequest *tio, *rtio;  /* the device; the frame */
static BOOL timer_open, rarmed;

void term_now(struct timeval *tv)
{
    if (TimerBase)
        GetSysTime(tv);
    else
        tv->tv_secs = tv->tv_micro = 0;
}

void con_timer_init(void)
{
    timerport = CreateMsgPort();
    if (!timerport)
        return;
    tio = (struct timerequest *)CreateIORequest(timerport, sizeof *tio);
    if (tio && !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                           (struct IORequest *)tio, 0)) {
        timer_open = TRUE;
        TimerBase = tio->tr_node.io_Device;
        rtio = (struct timerequest *)CreateIORequest(timerport, sizeof *rtio);
        if (rtio) {
            rtio->tr_node.io_Device = tio->tr_node.io_Device;
            rtio->tr_node.io_Unit = tio->tr_node.io_Unit;
        }
    }
}

/* Neither timer reads the clock - that is costly on the A1200 (Emu68). */
static void arm(struct timerequest *t, LONG us)
{
    t->tr_node.io_Command = TR_ADDREQUEST;
    t->tr_time.tv_secs = us / 1000000;
    t->tr_time.tv_micro = us % 1000000;
    SendIO((struct IORequest *)t);
}

static void disarm(struct timerequest *t, BOOL *armedp)
{
    if (*armedp) {
        AbortIO((struct IORequest *)t);
        WaitIO((struct IORequest *)t);
        *armedp = FALSE;
    }
}

void con_arm_render(LONG us)
{
    if (rtio && !rarmed) {
        arm(rtio, us);
        rarmed = TRUE;
    }
}

BOOL con_render_due(void)
{
    if (rarmed && CheckIO((struct IORequest *)rtio)) {
        WaitIO((struct IORequest *)rtio);
        rarmed = FALSE;
        return TRUE;
    }
    return FALSE;
}

/* The frame is up, but not taken (con_render_due takes it). */
BOOL con_render_ready(void)
{
    return rarmed && CheckIO((struct IORequest *)rtio);
}

ULONG con_timer_sig(void)
{
    return timerport ? 1UL << timerport->mp_SigBit : 0;
}

void con_timer_free(void)
{
    if (rtio) {
        disarm(rtio, &rarmed);
        DeleteIORequest((struct IORequest *)rtio);
        rtio = NULL;
    }
    if (tio) {
        if (timer_open)
            CloseDevice((struct IORequest *)tio);
        DeleteIORequest((struct IORequest *)tio);
        tio = NULL;
        TimerBase = NULL;
    }
    if (timerport) {
        DeleteMsgPort(timerport);
        timerport = NULL;
    }
}

/* ---- small things ------------------------------------------------------- */

static void wr(struct Con *c, const char *s)
{
    term_write(c->user, (const UBYTE *)s, strlen(s));
}

static void numcat(char *out, LONG n)
{
    char t[12], *p = t + sizeof t - 1;
    *p = 0;
    if (n < 0) n = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    strcat(out, p);
}

static void goto_rc(struct Con *c, int row, int col)
{
    char s[24];
    s[0] = (char)0x9B;
    s[1] = 0;
    numcat(s, row + 1);
    strcat(s, ";");
    numcat(s, col + 1);
    strcat(s, "H");
    wr(c, s);
}

/* Is t a task that still runs? Under Forbid(): the answer holds until
 * Permit(). A task that has ended is on neither list. */
static BOOL task_alive(struct Task *t)
{
    struct Node *n;
    if (!t)
        return FALSE;
    if (t == SysBase->ThisTask)
        return TRUE;
    for (n = SysBase->TaskReady.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if ((struct Task *)n == t)
            return TRUE;
    for (n = SysBase->TaskWait.lh_Head; n->ln_Succ; n = n->ln_Succ)
        if ((struct Task *)n == t)
            return TRUE;
    return FALSE;
}

/* Ctrl-C..F: to the break task as console.handler keeps it (the one
 * CHANGE_SIGNAL named, else the last reader), never to a task that has
 * ended - a background writer may be long gone; then to the Shell. */
static void send_break(struct Con *c, ULONG sig)
{
    Forbid();
    if (task_alive(c->breaktask))
        Signal(c->breaktask, sig);
    else if (task_alive(c->shelltask))
        Signal(c->shelltask, sig);
    Permit();
}

static void push_input(struct Con *c, const UBYTE *b, LONG n)
{
    if (n > INMAX - c->inlen)
        n = INMAX - c->inlen;
    if (n > 0) {
        CopyMem((APTR)b, c->inq + c->inlen, n);
        c->inlen += n;
    }
}

/* Answer waiting READs from what is ready; and a waiting WAIT_CHAR. */
static void serve(struct Con *c)
{
    while (c->nreads > 0 && (c->inlen > 0 || c->eof_pending || c->closing)) {
        struct DosPacket *p = c->reads[0];
        LONG want = p->dp_Arg3, n = 0;
        if (c->inlen > 0) {
            n = want < c->inlen ? want : c->inlen;
            if (n < 0)
                n = 0;
            CopyMem(c->inq, (APTR)p->dp_Arg2, n);
            memmove(c->inq, c->inq + n, c->inlen - n);
            c->inlen -= n;
        } else {
            c->eof_pending = FALSE;     /* an end of input: read 0 */
        }
        memmove(c->reads, c->reads + 1, (c->nreads - 1) * sizeof c->reads[0]);
        c->nreads--;
        ReplyPkt(p, n, 0);
    }
    if (c->waitpkt && (c->inlen > 0 || c->eof_pending || c->closing)) {
        ReplyPkt(c->waitpkt, DOSTRUE, 0);
        c->waitpkt = NULL;
        if (c->ctio)
            disarm(c->ctio, &c->carmed);
    }
}

/* ---- the line editor (cooked mode) --------------------------------- */

static void begin_line(struct Con *c)
{
    if (c->editing)
        return;
    c->editing = TRUE;
    c->llen = c->lcur = 0;
    c->hpos = -1;
    term_flush(c->user);
    c->x0 = term_cursor_x(c->user);
    c->y0 = term_cursor_y(c->user);
}

/* The window changed size: the emulator moved the text (and the line
 * being typed with it), so where the line starts is found again from the
 * cursor, which sits lcur letters into it - not kept from before, or the
 * Shell's answer lands rows away from the line. */
void con_resized(struct Con *c)
{
    int cols, p;
    if (!c || !c->editing)
        return;
    cols = term_cols(c->user);
    p = term_cursor_y(c->user) * cols + term_cursor_x(c->user) - c->lcur;
    if (p < 0)
        p = 0;
    c->y0 = p / cols;
    c->x0 = p % cols;
}

/* Draw the line from where it starts, and put the cursor at lcur. */
static void redraw_line(struct Con *c)
{
    int cols = term_cols(c->user), end, want, got, p;
    goto_rc(c, c->y0, c->x0);
    term_write(c->user, c->line, c->llen);
    wr(c, "\x9b" "J");
    /* if the line pushed the screen up, it starts higher now */
    end = c->x0 + c->llen;
    want = c->y0 + (c->llen > 0 && end % cols == 0 ? end / cols - 1
                                                   : end / cols);
    got = term_cursor_y(c->user);
    if (got < want)
        c->y0 -= want - got;
    p = c->x0 + c->lcur;
    goto_rc(c, c->y0 + p / cols, p % cols);
    term_render_soon(c->user);
}

static void set_line(struct Con *c, const UBYTE *s)
{
    c->llen = c->lcur = strlen((const char *)s);
    if (c->llen > LINEMAX)
        c->llen = c->lcur = LINEMAX;
    CopyMem((APTR)s, c->line, c->llen);
    redraw_line(c);
}

static void remember(struct Con *c)
{
    UBYTE *h;
    if (c->llen == 0)
        return;
    if (c->nhist && strlen((char *)c->hist[c->nhist - 1]) ==
                    (size_t)c->llen &&
        !memcmp(c->hist[c->nhist - 1], c->line, c->llen))
        return;
    if (c->nhist == HISTMAX) {
        FreeVec(c->hist[0]);
        memmove(c->hist, c->hist + 1, (HISTMAX - 1) * sizeof c->hist[0]);
        c->nhist--;
    }
    h = AllocVec(c->llen + 1, MEMF_ANY);
    if (h) {
        CopyMem(c->line, h, c->llen);
        h[c->llen] = 0;
        c->hist[c->nhist++] = h;
    }
}

static void finish_line(struct Con *c)
{
    int cols = term_cols(c->user), p = c->x0 + c->llen;
    goto_rc(c, c->y0 + p / cols, p % cols);
    wr(c, "\n");
    remember(c);
    push_input(c, c->line, c->llen);
    push_input(c, (const UBYTE *)"\n", 1);
    c->editing = FALSE;
    c->llen = c->lcur = 0;
    term_render_soon(c->user);
    serve(c);
}

/* One character into the line at the cursor. */
static void insert_char(struct Con *c, UBYTE ch)
{
    begin_line(c);
    memmove(c->line + c->lcur + 1, c->line + c->lcur, c->llen - c->lcur);
    c->line[c->lcur++] = ch;
    c->llen++;
    if (c->lcur == c->llen) {                       /* at the end: just add */
        term_write(c->user, &ch, 1);
        term_render_soon(c->user);
    } else
        redraw_line(c);
}

static void beep(void)
{
    if (win)                            /* none while hidden: NULL would
                                         * flash every screen */
        DisplayBeep(win->WScreen);
}

/* A pasted byte that is text: no control codes, no CSI - a paste is
 * never taken as keys (Ctrl-C, cursor keys). */
static BOOL pasteable(UBYTE ch)
{
    return ch >= 0x20 && ch != 0x7F && !(ch >= 0x80 && ch < 0xA0);
}

static void paste_drop(struct Con *c)
{
    if (c->paste)
        FreeVec(c->paste);
    c->paste = NULL;
    c->plen = c->ppos = 0;
}

/* Feed the paste in as far as the reader takes it. Raw: as much as the
 * input queue holds. Cooked: a line at a time, each into the editor when
 * the reader asks for a line, so the prompts come between the lines and
 * nothing is lost past the queue; a line too long for the editor is cut
 * with a beep. */
static void paste_feed(struct Con *c)
{
    BOOL fed = FALSE;
    while (c->paste && c->ppos < c->plen) {
        UBYTE ch = c->paste[c->ppos];
        if (c->rawmode) {
            if (c->inlen >= INMAX)
                break;
            /* the bracketed-paste marks clip.c put around it, whole (the
             * text itself has no Esc left: clip.c took them out) */
            if (ch == 0x1b && c->plen - c->ppos >= 6 &&
                (!memcmp(c->paste + c->ppos, "\x1b[200~", 6) || !memcmp(c->paste + c->ppos, "\x1b[201~", 6))) {
                if (c->inlen + 6 > INMAX)
                    break;
                CopyMem(c->paste + c->ppos, c->inq + c->inlen, 6);
                c->inlen += 6;
                c->ppos += 6;
                fed = TRUE;
                continue;
            }
            c->ppos++;
            if (pasteable(ch) || ch == '\t' || ch == '\r')
                c->inq[c->inlen++] = ch;
            fed = TRUE;
            continue;
        }
        if (!c->editing && (c->nreads == 0 || c->inlen > 0))
            break;                      /* the next line when it is asked */
        c->ppos++;
        if (ch == '\r') {
            begin_line(c);
            c->pfull = FALSE;
            finish_line(c);
        } else if (pasteable(ch) || ch == '\t') {
            if (c->llen < LINEMAX - 1)
                insert_char(c, ch == '\t' ? ' ' : ch);
            else if (!c->pfull) {
                c->pfull = TRUE;
                beep();
            }
        }
    }
    if (c->paste && c->ppos >= c->plen)
        paste_drop(c);
    if (fed)
        serve(c);
}

/* ---- Tab: the name at the cursor completed ------------------------
 * CTerm edits the Shell's lines (DISK_INFO 'CTRM'), so the 3.2 console's
 * completion is CTerm's to do. Against the reader's current directory:
 * the reader waits in its READ, so its pr_CurrentDir holds still. */

static UBYTE lower(UBYTE ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7)
           ? ch + 32 : ch;
}

/* The directory of the process waiting to read (0 is the boot volume's
 * root); FALSE: no process waits. */
static BOOL reader_dir(struct Con *c, BPTR *dir)
{
    struct MsgPort *port;
    struct Task *t;
    if (c->nreads == 0 || !(port = c->reads[0]->dp_Port) ||
        (port->mp_Flags & PF_ACTION) != PA_SIGNAL ||
        !(t = port->mp_SigTask) || t->tc_Node.ln_Type != NT_PROCESS)
        return FALSE;
    *dir = ((struct Process *)t)->pr_CurrentDir;
    return TRUE;
}

static void insert_text(struct Con *c, const UBYTE *s, int n)
{
    while (n-- > 0 && c->llen < LINEMAX - 1)
        insert_char(c, *s++);
}

static void complete(struct Con *c)
{
    static UBYTE dir[LINEMAX], best[108];
    struct FileInfoBlock *fib;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR keepwin;
    BPTR cd = 0, l, keepcd;
    int i, start = 0, sep, plen, nmatch = 0, common = 0;
    BOOL quoted = FALSE, isdir = FALSE;
    begin_line(c);
    /* the word at the cursor: after the last space outside quotes */
    for (i = 0; i < c->lcur; i++)
        if (c->line[i] == '"') {
            quoted = !quoted;
            start = i + 1;
        } else if (c->line[i] == ' ' && !quoted)
            start = i + 1;
    for (sep = c->lcur; sep > start && c->line[sep - 1] != '/' &&
                        c->line[sep - 1] != ':'; sep--)
        ;
    plen = c->lcur - sep;               /* the name's start, typed */
    if (!reader_dir(c, &cd) || !(fib = AllocDosObject(DOS_FIB, NULL))) {
        beep();
        return;
    }
    CopyMem(c->line + start, dir, sep - start);
    dir[sep - start] = 0;
    keepwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;        /* no "insert volume" requester */
    keepcd = CurrentDir(cd);
    l = Lock((STRPTR)dir, ACCESS_READ);
    CurrentDir(keepcd);
    if (l && Examine(l, fib) && fib->fib_DirEntryType > 0)
        while (ExNext(l, fib)) {
            UBYTE *nm = (UBYTE *)fib->fib_FileName;
            for (i = 0; i < plen && nm[i] &&
                        lower(nm[i]) == lower(c->line[sep + i]); i++)
                ;
            if (i < plen)
                continue;
            if (nmatch++ == 0) {
                strcpy((char *)best, (char *)nm);
                common = strlen((char *)nm);
                isdir = fib->fib_DirEntryType > 0;
            } else {
                for (i = plen; i < common && lower(nm[i]) == lower(best[i]);
                     i++)
                    ;
                common = i;
            }
        }
    if (l)
        UnLock(l);
    me->pr_WindowPtr = keepwin;
    FreeDosObject(DOS_FIB, fib);
    if (nmatch == 0) {
        beep();
        return;
    }
    if (!quoted)                        /* a space in it: quote the word */
        for (i = plen; i < common; i++)
            if (best[i] == ' ') {
                if (c->llen < LINEMAX - 1) {
                    int keep = c->lcur;
                    c->lcur = start;
                    insert_char(c, '"');
                    c->lcur = keep + 1;
                    quoted = TRUE;
                }
                break;
            }
    insert_text(c, best + plen, common - plen);
    if (nmatch > 1)
        beep();                         /* more than one: as far as they agree */
    else if (isdir)
        insert_text(c, (const UBYTE *)"/", 1);
    else {
        if (quoted)
            insert_text(c, (const UBYTE *)"\"", 1);
        insert_text(c, (const UBYTE *)" ", 1);
    }
    redraw_line(c);
}

/* A typed key's bytes, cooked: its letters into the line. The editing
 * keys come as actions (con_edit); Ctrl-J still ends the line. */
static void edit_key(struct Con *c, const UBYTE *b, LONG n)
{
    if (b[0] == 0x9B)
        return;                         /* a key no action has */
    while (n-- > 0) {
        UBYTE ch = *b++;
        if (ch == '\n') {
            begin_line(c);
            finish_line(c);
        } else if (ch >= 0x20 && !(ch >= 0x80 && ch < 0xA0) &&
                   c->llen < LINEMAX - 1) {
            c->spat[0] = 0;             /* a search starts afresh */
            insert_char(c, ch);
        }
    }
}

static BOOL word_ch(UBYTE ch)
{
    return ch != ' ' && ch != '/' && ch != ':' && ch != '"';
}

/* Does history line h hold the text s (any case)? */
static BOOL holds(const UBYTE *h, const UBYTE *s)
{
    int i, n = strlen((const char *)s);
    for (; *h; h++) {
        for (i = 0; i < n; i++)         /* (h's 0 never matches s) */
            if (lower(h[i]) != lower(s[i]))
                break;
        if (i == n)
            return TRUE;
    }
    return n == 0;
}

/* Delete the letters a..b-1 of the line; the cursor to a. */
static void cut(struct Con *c, int a, int b)
{
    if (a >= b)
        return;
    memmove(c->line + a, c->line + b, c->llen - b);
    c->llen -= b - a;
    c->lcur = a;
    redraw_line(c);
}

/* One of the line editor's actions (CE_*: CTerm.keys' [shell]); a key
 * at a time: small before quick. */
__attribute__((optimize("Os"))) void con_edit(struct Con *c, int what)
{
    int i;
    if (!c)
        return;
    if (c->rawmode) {
        if (what == CE_ACCEPT)
            con_keys(c, (const UBYTE *)"\r", 1);
        return;
    }
    begin_line(c);
    if (what != CE_SEARCH)
        c->spat[0] = 0;
    switch (what) {
    case CE_LEFT:
        if (c->lcur > 0) {
            c->lcur--;
            redraw_line(c);
        }
        break;
    case CE_RIGHT:
        if (c->lcur < c->llen) {
            c->lcur++;
            redraw_line(c);
        }
        break;
    case CE_START: c->lcur = 0; redraw_line(c); break;
    case CE_END: c->lcur = c->llen; redraw_line(c); break;
    case CE_WORDLEFT:
        for (i = c->lcur; i > 0 && !word_ch(c->line[i - 1]); i--)
            ;
        for (; i > 0 && word_ch(c->line[i - 1]); i--)
            ;
        c->lcur = i;
        redraw_line(c);
        break;
    case CE_WORDRIGHT:
        for (i = c->lcur; i < c->llen && !word_ch(c->line[i]); i++)
            ;
        for (; i < c->llen && word_ch(c->line[i]); i++)
            ;
        c->lcur = i;
        redraw_line(c);
        break;
    case CE_BACK:
        if (c->lcur > 0)
            cut(c, c->lcur - 1, c->lcur);
        break;
    case CE_DELETE:
        if (c->lcur < c->llen)
            cut(c, c->lcur, c->lcur + 1);
        break;
    case CE_WORDBACK:
        for (i = c->lcur; i > 0 && !word_ch(c->line[i - 1]); i--)
            ;
        for (; i > 0 && word_ch(c->line[i - 1]); i--)
            ;
        cut(c, i, c->lcur);
        break;
    case CE_KILLSTART: cut(c, 0, c->lcur); break;
    case CE_KILLEND:
        c->llen = c->lcur;
        redraw_line(c);
        break;
    case CE_CLEAR: set_line(c, (const UBYTE *)""); break;
    case CE_OLDER:
        if (c->nhist) {
            c->hpos = c->hpos < 0 ? c->nhist - 1
                    : (c->hpos > 0 ? c->hpos - 1 : 0);
            set_line(c, c->hist[c->hpos]);
        }
        break;
    case CE_NEWER:
        if (c->hpos >= 0) {
            if (++c->hpos >= c->nhist) {
                c->hpos = -1;
                set_line(c, (const UBYTE *)"");
            } else
                set_line(c, c->hist[c->hpos]);
        }
        break;
    case CE_SEARCH:
        /* the next older line holding what was typed when the search
         * began (pressed again: the one before that) */
        if (!c->spat[0]) {
            i = c->llen < (int)sizeof c->spat - 1 ? c->llen
                                                  : (int)sizeof c->spat - 1;
            CopyMem(c->line, c->spat, i);
            c->spat[i] = 0;
            c->hpos = -1;
            if (!i)
                break;                  /* nothing typed: nothing to find */
        }
        for (i = (c->hpos < 0 ? c->nhist : c->hpos) - 1; i >= 0; i--)
            if (holds(c->hist[i], c->spat))
                break;
        if (i < 0)
            beep();
        else {
            c->hpos = i;
            set_line(c, c->hist[i]);
        }
        break;
    case CE_COMPLETE: complete(c); break;
    case CE_ACCEPT: finish_line(c); break;
    case CE_EOF:
        if (c->llen == 0) {
            c->editing = FALSE;
            c->eof_pending = TRUE;
            serve(c);
        }
        break;
    }
}

/* ---- what CTerm calls ----------------------------------------------- */

void con_keys(struct Con *c, const UBYTE *b, LONG n)
{
    LONG i;
    if (!c)                             /* a tab whose Shell never started */
        return;
    /* Ctrl-C..F: break signals, never input */
    for (i = 0; i < n; i++)
        if (b[i] >= 3 && b[i] <= 6) {
            paste_drop(c);              /* a paste still going in stops */
            send_break(c, SIGBREAKF_CTRL_C << (b[i] - 3));
            return;
        }
    if (c->rawmode) {
        push_input(c, b, n);
        serve(c);
    } else if (c->nreads == 0 && !c->editing && term_keys_ahead(c->user)) {
        /* nobody reads and the shell edits its own line (csh): the keys
         * wait as keys - Tab and all - for whoever reads next, as on a
         * Unix terminal; the letters are shown as typed (echo) */
        for (i = 0; i < n && c->aheadlen < INMAX; i++) {
            UBYTE k = b[i];
            c->ahead[c->aheadlen++] = k;
            if ((k >= 32 && k < 127) || k >= 160) term_take(c->user, &k, 1);
            else if (k == 13) term_take(c->user, (const UBYTE *)"\n", 1);
        }
        term_render_soon(c->user);
    } else
        edit_key(c, b, n);
}

/* What was typed ahead goes to the reader that came: a raw one (the shell's
 * own editor) gets the keys as they were, a cooked one (a program, the ROM
 * Shell) gets them through the line editor now, as if typed now. */
static void ahead_give(struct Con *c)
{
    LONG n = c->aheadlen;
    if (!n) return;
    c->aheadlen = 0;
    if (c->rawmode) {
        push_input(c, c->ahead, n);
        serve(c);
    } else {
        /* as if typed now: Return ends the line, Tab completes, Backspace
         * and Del delete, letters go in; other sequences (arrows) are
         * dropped - a program reading a line has no use for them */
        UBYTE *t = AllocVec(n, MEMF_ANY);
        LONG i;
        if (!t) return;
        CopyMem(c->ahead, t, n);
        for (i = 0; i < n; i++) {
            UBYTE k = t[i];
            if (k == 13 || k == 10) {
                con_edit(c, CE_ACCEPT);
                if (con_keeping(c)) {               /* the reader is served: the rest waits again */
                    LONG left = n - i - 1;
                    CopyMem(t + i + 1, c->ahead, left);
                    c->aheadlen = left;
                    break;
                }
                continue;
            }
            else if (k == 9) con_edit(c, CE_COMPLETE);
            else if (k == 8) con_edit(c, CE_BACK);
            else if (k == 0x7F) con_edit(c, CE_DELETE);
            else if (k == 0x9B || k == 0x1B) {      /* a sequence: to its final byte */
                for (i++; i < n && !(t[i] >= 0x40 && t[i] <= 0x7E && !(k == 0x1B && t[i] == '[')); i++) ;
            } else edit_key(c, &k, 1);
            if (c->rawmode) {                       /* the line's end gave it to a raw reader */
                push_input(c, t + i + 1, n - i - 1);
                serve(c);
                break;
            }
        }
        FreeVec(t);
    }
}

/* Pasted text, typed in as text: added to what is still going in. Newlines
 * are Returns (\r). TRUE: taken. */
BOOL con_paste(struct Con *c, const UBYTE *b, LONG n)
{
    UBYTE *p;
    LONG left;
    if (!c || n <= 0)
        return c != NULL;
    left = c->paste ? c->plen - c->ppos : 0;
    if (!(p = AllocVec(left + n, MEMF_ANY)))
        return FALSE;
    if (left)
        CopyMem(c->paste + c->ppos, p, left);
    CopyMem((APTR)b, p + left, n);
    paste_drop(c);
    c->paste = p;
    c->plen = left + n;
    c->pfull = FALSE;
    paste_feed(c);
    return TRUE;
}

void con_reply(struct Con *c, const UBYTE *b, LONG n)
{
    if (!c)                             /* a tab whose Shell never started */
        return;
    push_input(c, b, n);
    serve(c);
}

void con_close_gadget(struct Con *c)
{
    if (!c)                             /* a tab whose Shell never started */
        return;
    /* a cooked reader (the Shell) is told the input ended; a raw one
     * gets nothing unless it asked for close reports (class 11) */
    if (!c->rawmode) {
        c->eof_pending = TRUE;
        serve(c);
    }
}

/* Keys typed now are kept as keys (nobody reads, the shell asked, ?7727):
 * the key table gives them as the keymap's bytes, not as editing actions. */
BOOL con_keeping(struct Con *c)
{
    return c && !c->rawmode && c->nreads == 0 && !c->editing && term_keys_ahead(c->user);
}

/* A program has the console raw (Ed, More): it may hold the window. */
BOOL con_raw(struct Con *c)
{
    return c && c->rawmode;
}

/* The window was opened again (another screen): every process whose
 * requesters went to the old one - the Shells and what they run, given
 * it by NP_WindowPtr - uses the new one, not a closed window. */
void con_window_moved(struct Window *old, struct Window *now)
{
    struct List *l[2];
    struct Node *n;
    int i;
    if (!old || old == now)
        return;
    l[0] = &SysBase->TaskReady;
    l[1] = &SysBase->TaskWait;
    Forbid();
    for (i = 0; i < 2; i++)
        for (n = l[i]->lh_Head; n->ln_Succ; n = n->ln_Succ)
            if (n->ln_Type == NT_PROCESS &&
                ((struct Process *)n)->pr_WindowPtr == (APTR)old)
                ((struct Process *)n)->pr_WindowPtr = (APTR)now;
    Permit();
}

/* The window back after hiding: the processes of these consoles (their
 * console is one of ours) that were given no window meanwhile - moved
 * off the hidden one, or started while it was away - get it. */
void con_window_back(struct Window *now, struct Con **cs, int n)
{
    struct List *l[2];
    struct Node *nd;
    int i, k;
    if (!now)
        return;
    l[0] = &SysBase->TaskReady;
    l[1] = &SysBase->TaskWait;
    Forbid();
    for (i = 0; i < 2; i++)
        for (nd = l[i]->lh_Head; nd->ln_Succ; nd = nd->ln_Succ) {
            struct Process *pr = (struct Process *)nd;
            if (nd->ln_Type != NT_PROCESS || pr->pr_WindowPtr)
                continue;
            for (k = 0; k < n; k++)
                if (cs[k] && cs[k]->conport &&
                    pr->pr_ConsoleTask == (APTR)cs[k]->conport) {
                    pr->pr_WindowPtr = (APTR)now;
                    break;
                }
        }
    Permit();
}

BOOL con_alive(struct Con *c)
{
    return c->child_running || c->opens > 0;
}

ULONG con_sigmask(struct Con *c)
{
    ULONG m = con_timer_sig();
    if (c->conport)
        m |= 1UL << c->conport->mp_SigBit;
    if (c->endbit >= 0)
        m |= 1UL << c->endbit;
    return m;
}

void con_handle(struct Con *c, ULONG sigs)
{
    struct Message *m;
    if (!c->conport)
        return;
    if (c->child_done)
        c->child_running = FALSE;
    while ((m = GetMsg(c->conport))) {
        struct DosPacket *p = (struct DosPacket *)m->mn_Node.ln_Name;
        struct MsgPort *from = p->dp_Port;
        /* writes are parsed in batches (term_take): anything else may
         * look at the text, so what was taken is parsed first */
        if (p->dp_Type != ACTION_WRITE &&
            !(p->dp_Type == ACTION_WAIT_CHAR && p->dp_Arg1 <= 0))
            term_sync(c->user);
        switch (p->dp_Type) {
        case ACTION_FINDINPUT:
        case ACTION_FINDOUTPUT:
        case ACTION_FINDUPDATE: {
            struct FileHandle *fh = BADDR(p->dp_Arg1);
            fh->fh_Port = (struct MsgPort *)DOSTRUE;    /* interactive */
            fh->fh_Args = 1;
            c->opens++;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        }
        case ACTION_END:
            if (c->opens > 0)
                c->opens--;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        case ACTION_WRITE: {
            LONG n = p->dp_Arg3;
            /* a writer does not take the break keys (Run List's would,
             * then end): only when no one has them yet */
            if (from && !c->breaktask)
                c->breaktask = from->mp_SigTask;
            if (n > 0)
                term_take(c->user, (const UBYTE *)p->dp_Arg2, n);
            ReplyPkt(p, n > 0 ? n : 0, 0);
            term_render_soon(c->user);
            break;
        }
        case ACTION_READ:
            if (from) {
                c->breaktask = from->mp_SigTask;
                if (!c->shelltask)
                    c->shelltask = from->mp_SigTask;
            }
            if (c->nreads < READMAX) {
                c->reads[c->nreads++] = p;
                if (!c->rawmode) ahead_give(c);     /* typed ahead: through the editor now */
                serve(c);
                paste_feed(c);          /* the next of a paste, if any */
                if (c->nreads) {
                    /* about to wait for the person: show everything */
                    if (!c->rawmode)
                        begin_line(c);
                    term_flush(c->user);
                }
            } else
                ReplyPkt(p, -1, ERROR_NO_FREE_STORE);
            break;
        case ACTION_WAIT_CHAR:
            if (c->rawmode)
                paste_feed(c);
            if (c->rawmode && c->aheadlen) ahead_give(c);
            if (c->inlen > 0 || c->eof_pending || c->closing) {
                ReplyPkt(p, DOSTRUE, 0);
            } else if (p->dp_Arg1 <= 0) {
                /* "is a key there?" without waiting: nobody is looking at
                 * the screen yet, so it is drawn within the frame (jump
                 * scroll) - conbench's sync-line is a stream of these */
                term_render_soon(c->user);
                ReplyPkt(p, DOSFALSE, 0);
            } else {
                term_flush(c->user);    /* it will really wait: show all */
                if (c->waitpkt)
                    ReplyPkt(c->waitpkt, DOSFALSE, 0);
                c->waitpkt = p;
                if (c->ctio) {
                    disarm(c->ctio, &c->carmed);
                    arm(c->ctio, p->dp_Arg1);
                    c->carmed = TRUE;
                }
            }
            break;
        case ACTION_SCREEN_MODE:
            if (p->dp_Arg1 && !c->rawmode && c->editing && c->llen) {
                /* typed ahead, then raw: hand over what was typed */
                push_input(c, c->line, c->llen);
                c->llen = c->lcur = 0;
            }
            if (c->rawmode != (p->dp_Arg1 != 0))
                term_raw(c->user, p->dp_Arg1 != 0);
            c->rawmode = p->dp_Arg1 != 0;
            if (c->rawmode) {
                c->editing = FALSE;
                ahead_give(c);              /* the shell's editor: the keys as typed */
            } else if (c->inlen && term_keys_ahead(c->user)) {
                /* back to cooked with raw keys still queued (typed after the
                 * shell's Return): they wait as kept keys, for the editor of
                 * whoever reads next (Ask) or the shell's next prompt */
                LONG n = c->inlen < INMAX - c->aheadlen ? c->inlen : INMAX - c->aheadlen;
                memmove(c->ahead + n, c->ahead, c->aheadlen);
                CopyMem(c->inq, c->ahead, n);
                c->aheadlen += n;
                c->inlen = 0;
            }
            term_flush(c->user);
            ReplyPkt(p, DOSTRUE, 0);
            break;
        case ACTION_DISK_INFO: {
            struct InfoData *id = BADDR(p->dp_Arg1);
            memset(id, 0, sizeof *id);
            id->id_DiskType = ID_CTRM;  /* not 'CON\0': we edit the lines */
            /* More and Ed find the window. They keep the pointer, so the
             * window must not change under them: CTerm switches screens
             * only while no console is raw (Ed, More), see main.c */
            id->id_VolumeNode = (BPTR)win;
            id->id_InUse = 0;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        }
        case ACTION_CHANGE_SIGNAL: {
            /* arg2 is the message port to signal (a process's pr_MsgPort,
             * seen on 3.2.3: 0x5C past the process), not a task: Signal()
             * on a port writes into its message list. Take its task;
             * answer with what was given before. */
            struct Node *n = (struct Node *)p->dp_Arg2;
            LONG old = c->breakarg;
            if (n) {
                if (n->ln_Type == NT_MSGPORT &&
                    (((struct MsgPort *)n)->mp_Flags & PF_ACTION) == PA_SIGNAL)
                    c->breaktask = ((struct MsgPort *)n)->mp_SigTask;
                else if (n->ln_Type == NT_TASK || n->ln_Type == NT_PROCESS)
                    c->breaktask = (struct Task *)n;
                c->breakarg = p->dp_Arg2;
            }
            ReplyPkt(p, DOSTRUE, old);
            break;
        }
        case ACTION_SEEK:
            ReplyPkt(p, DOSTRUE, ERROR_ACTION_NOT_KNOWN);
            break;
        case ACTION_IS_FILESYSTEM:
            ReplyPkt(p, DOSFALSE, 0);
            break;
        default:
            ReplyPkt(p, DOSFALSE, ERROR_ACTION_NOT_KNOWN);
            break;
        }
    }
    /* a WAIT_CHAR whose time is up */
    if (c->carmed && CheckIO((struct IORequest *)c->ctio)) {
        WaitIO((struct IORequest *)c->ctio);
        c->carmed = FALSE;
        if (c->waitpkt) {
            ReplyPkt(c->waitpkt, DOSFALSE, 0);
            c->waitpkt = NULL;
        }
    }
}

/* A port made by hand, not CreateMsgPort: when its tab closes the port
 * stays (a process left behind may still open "*" on it) but its signal
 * goes back, and only a port of our own can be freed without one. */
/* The consoles' signals: one for all their ports and one for all their
 * helpers' ends (two each ran out at seven Shells: a task has 16), each
 * console looks at its own port and flag when it is set. */
static BYTE sigport = -1, sigend = -1;
static int nport, nend;

static BYTE sig_get(BYTE *sb, int *n)
{
    if (*sb < 0 && (*sb = AllocSignal(-1)) < 0)
        return -1;
    (*n)++;
    return *sb;
}

static void sig_put(BYTE *sb, int *n)
{
    if (*n > 0 && !--*n && *sb >= 0) {
        FreeSignal(*sb);
        *sb = -1;
    }
}

static struct MsgPort *port_new(void)
{
    struct MsgPort *p;
    BYTE sb = sig_get(&sigport, &nport);
    if (sb < 0)
        return NULL;
    if (!(p = AllocVec(sizeof *p, MEMF_PUBLIC | MEMF_CLEAR))) {
        sig_put(&sigport, &nport);
        return NULL;
    }
    p->mp_Node.ln_Type = NT_MSGPORT;
    p->mp_Flags = PA_SIGNAL;
    p->mp_SigBit = sb;
    p->mp_SigTask = FindTask(NULL);
    p->mp_MsgList.lh_Head = (struct Node *)&p->mp_MsgList.lh_Tail;
    p->mp_MsgList.lh_TailPred = (struct Node *)&p->mp_MsgList.lh_Head;
    return p;
}

/* Ports of closed tabs, linked through their nodes (never on a list of
 * the system's): no signal, polled, every packet refused */
static struct MsgPort *spent;

static void refuse(struct MsgPort *port)
{
    struct Message *m;
    while ((m = GetMsg(port))) {
        struct DosPacket *p = (struct DosPacket *)m->mn_Node.ln_Name;
        ReplyPkt(p, DOSFALSE, ERROR_OBJECT_NOT_FOUND);
    }
}

void con_spent_poll(void)
{
    struct MsgPort *p;
    for (p = spent; p; p = (struct MsgPort *)p->mp_Node.ln_Succ)
        refuse(p);
}

void con_spent_free(void)
{
    while (spent) {
        struct MsgPort *p = spent;
        spent = (struct MsgPort *)p->mp_Node.ln_Succ;
        refuse(p);
        FreeVec(p);
    }
}

/* The tab closes (con_alive() is FALSE): everything goes back but the
 * port, which is kept without its signal until CTerm ends. A packet
 * still waiting is answered as the end of the input. */
void con_retire(struct Con *c)
{
    struct MsgPort *port;
    int i;
    if (!c)
        return;
    for (i = 0; i < c->nreads; i++)
        ReplyPkt(c->reads[i], 0, 0);
    c->nreads = 0;
    if (c->waitpkt) {
        ReplyPkt(c->waitpkt, DOSFALSE, 0);
        c->waitpkt = NULL;
    }
    port = c->conport;
    c->conport = NULL;
    if (port) {
        Disable();                      /* no PutMsg between the two */
        port->mp_Flags = PA_IGNORE;
        Enable();
        sig_put(&sigport, &nport);
        refuse(port);
        port->mp_Node.ln_Succ = (struct Node *)spent;
        spent = port;
    }
    con_free(c);
}

struct Con *con_start(const char *startup, const char *then, void *user)
{
    struct Process *pr;
    struct Con *c = AllocVec(sizeof *c, MEMF_ANY | MEMF_CLEAR);
    if (!c)
        return NULL;
    c->user = user;
    c->endbit = -1;
    c->hpos = -1;
    c->maintask = FindTask(NULL);
    c->conport = port_new();
    c->endbit = sig_get(&sigend, &nend);
    c->infh = AllocDosObject(DOS_FILEHANDLE, NULL);
    if (!c->conport || c->endbit < 0 || !c->infh) {
        con_free(c);
        return NULL;
    }
    if (tio) {                          /* its own WaitForChar timer */
        c->ctio = (struct timerequest *)CreateIORequest(timerport,
                                                        sizeof *c->ctio);
        if (c->ctio) {
            c->ctio->tr_node.io_Device = tio->tr_node.io_Device;
            c->ctio->tr_node.io_Unit = tio->tr_node.io_Unit;
        }
    }
    c->infh->fh_Type = c->conport;
    c->infh->fh_Port = (struct MsgPort *)DOSTRUE;   /* interactive */
    c->infh->fh_Args = 1;
    c->opens = 1;
    if (startup && startup[0]) {
        strcpy(c->startcmd, "Execute ");
        strncat(c->startcmd, startup, sizeof c->startcmd - 10);
    }
    /* then a shell of the person's (csh): the next line of the same
     * Execute() string (3.2 runs each line, Knowledge writing-a-shell.md D);
     * the ROM Shell stays under it, so its exit gives the ROM Shell back */
    if (then && then[0] && strlen(c->startcmd) + strlen(then) + 2 < sizeof c->startcmd) {
        if (c->startcmd[0]) strcat(c->startcmd, "\n");
        strcat(c->startcmd, then);
    }
    c->child_running = TRUE;
    /* 4 KB of stack (was 16): the helper only waits in Execute, ~0.5 KB
     * used (seen in FS-UAE); the Shell is a process of its own, with
     * the Shell's usual stack */
    pr = CreateNewProcTags(NP_Entry, (ULONG)child_entry,
                           NP_Name, (ULONG)"CTerm Shell",
                           NP_StackSize, 4096,
                           NP_ConsoleTask, (ULONG)c->conport,
                           NP_WindowPtr, (ULONG)win,
                           NP_ExitData, (ULONG)c,   /* the child's console */
                           TAG_END);
    if (!pr) {
        c->child_running = FALSE;
        c->opens = 0;
        con_free(c);
        return NULL;
    }
    c->started = TRUE;
    return c;
}

/* The person wants out: end the input so the Shell ends by itself. The
 * caller keeps answering packets until con_alive() is FALSE. */
/* The tab closes: its input ends, for every reader from now on - a shell
 * started inside the tab's Shell (csh) ends, then the Shell under it reads
 * and ends too (one end of input only ended the first: the tab stayed). */
void con_stop(struct Con *c)
{
    paste_drop(c);
    c->eof_pending = TRUE;
    c->closing = TRUE;
    send_break(c, SIGBREAKF_CTRL_C);
    serve(c);
}

/* The tab's Shell: its current drawer and the program running in it
 * (none while the Shell itself waits: its own name, csh, cshell). */
void con_shell_info(struct Con *c, char *dir, int dsz, char *prog, int psz)
{
    struct Process *pr;
    struct CommandLineInterface *cli;
    dir[0] = prog[0] = 0;
    if (!c)
        return;
    Forbid();
    if (task_alive(c->shelltask) && c->shelltask->tc_Node.ln_Type == NT_PROCESS &&
        (pr = (struct Process *)c->shelltask)->pr_CLI &&
        (cli = BADDR(pr->pr_CLI))) {
        UBYTE *b = BADDR(cli->cli_SetName);
        int n;
        if (b && (n = b[0]) > 0) {
            if (n >= dsz) n = dsz - 1;
            memcpy(dir, b + 1, n); dir[n] = 0;
        }
        b = BADDR(cli->cli_CommandName);
        if (b && (n = b[0]) > 0) {
            if (n >= psz) n = psz - 1;
            memcpy(prog, b + 1, n); prog[n] = 0;
        }
    }
    Permit();
    {   /* the Shell itself waiting: no program */
        const char *f = FilePart((STRPTR)prog);
        if (!stricmp(f, "csh") || !stricmp(f, "cshell") || !stricmp(f, "Shell") ||
            !stricmp(f, "Execute") || !stricmp(f, "NewShell"))
            prog[0] = 0;
    }
}

/* Only when nothing is open on the port any more. */
void con_free(struct Con *c)
{
    int i;
    if (!c)
        return;
    if (c->ctio) {
        disarm(c->ctio, &c->carmed);
        DeleteIORequest((struct IORequest *)c->ctio);
    }
    if (c->infh && !c->started)
        FreeDosObject(DOS_FILEHANDLE, c->infh);
    if (c->conport) {
        sig_put(&sigport, &nport);
        FreeVec(c->conport);
    }
    if (c->endbit >= 0)
        sig_put(&sigend, &nend);
    for (i = 0; i < c->nhist; i++)
        FreeVec(c->hist[i]);
    paste_drop(c);
    FreeVec(c);
}
