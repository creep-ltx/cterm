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
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <string.h>

#include "console.h"

struct Device *TimerBase;

#define INMAX   4096
#define LINEMAX 1024
#define HISTMAX 50
#define READMAX 16
#define ID_CTRM 0x4354524DUL            /* 'CTRM': "not CON:, edit lines" */

static struct MsgPort *conport;
static struct timerequest *tio;
static BOOL timer_open;
static struct Task *maintask;
static LONG endbit = -1;
static struct FileHandle *infh;
static char startcmd[256];
static BOOL child_running;
static int opens;                       /* handles open on this console */

static UBYTE inq[INMAX];                /* ready for READs */
static LONG inlen;
static BOOL eof_pending;
static BOOL rawmode;
static struct DosPacket *reads[READMAX];
static int nreads;
static struct DosPacket *waitpkt;
static struct Task *breaktask;

/* the line being typed (cooked mode) */
static UBYTE line[LINEMAX];
static int llen, lcur;
static BOOL editing;
static int x0, y0;                      /* where it starts on screen */
static UBYTE *hist[HISTMAX];
static int nhist, hpos = -1;

/* ---- the helper that runs the Shell ---------------------------------- */

static void child_entry(void)
{
    /* an interactive Shell on our handle: returns at EndShell */
    Execute((STRPTR)startcmd, MKBADDR(infh), 0);
    Close(MKBADDR(infh));
    /* tell CTerm, and leave without returning to its code first */
    Forbid();
    Signal(maintask, 1UL << endbit);
}

/* ---- small things ------------------------------------------------------- */

static struct MsgPort *timerport;
static struct timerequest *rtio, *ctio;  /* the frame; WaitForChar */

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
        ctio = (struct timerequest *)CreateIORequest(timerport, sizeof *ctio);
        if (rtio) {
            rtio->tr_node.io_Device = tio->tr_node.io_Device;
            rtio->tr_node.io_Unit = tio->tr_node.io_Unit;
        }
        if (ctio) {
            ctio->tr_node.io_Device = tio->tr_node.io_Device;
            ctio->tr_node.io_Unit = tio->tr_node.io_Unit;
        }
    }
}

/* Two timer requests on the timer port, whose bit con_sigmask() adds:
 * the frame (draw what is owed) and a WaitForChar's timeout. Neither
 * reads the clock - that is costly on the A1200 under Emu68. */
static BOOL rarmed, carmed;

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

void con_timer_free(void)
{
    if (rtio) {
        disarm(rtio, &rarmed);
        DeleteIORequest((struct IORequest *)rtio);
        rtio = NULL;
    }
    if (ctio) {
        disarm(ctio, &carmed);
        DeleteIORequest((struct IORequest *)ctio);
        ctio = NULL;
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

static void wr(const char *s)
{
    term_write((const UBYTE *)s, strlen(s));
}

static void numcat(char *out, LONG n)
{
    char t[12], *p = t + sizeof t - 1;
    *p = 0;
    if (n < 0) n = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    strcat(out, p);
}

static void goto_rc(int row, int col)
{
    char s[24];
    s[0] = (char)0x9B;
    s[1] = 0;
    numcat(s, row + 1);
    strcat(s, ";");
    numcat(s, col + 1);
    strcat(s, "H");
    wr(s);
}

static void push_input(const UBYTE *b, LONG n)
{
    if (n > INMAX - inlen)
        n = INMAX - inlen;
    if (n > 0) {
        CopyMem((APTR)b, inq + inlen, n);
        inlen += n;
    }
}

/* Answer waiting READs from what is ready; and a waiting WAIT_CHAR. */
static void serve(void)
{
    while (nreads > 0 && (inlen > 0 || eof_pending)) {
        struct DosPacket *p = reads[0];
        LONG want = p->dp_Arg3, n = 0;
        if (inlen > 0) {
            n = want < inlen ? want : inlen;
            if (n < 0)
                n = 0;
            CopyMem(inq, (APTR)p->dp_Arg2, n);
            memmove(inq, inq + n, inlen - n);
            inlen -= n;
        } else
            eof_pending = FALSE;        /* an end of input: read 0 */
        memmove(reads, reads + 1, (nreads - 1) * sizeof reads[0]);
        nreads--;
        ReplyPkt(p, n, 0);
    }
    if (waitpkt && (inlen > 0 || eof_pending)) {
        ReplyPkt(waitpkt, DOSTRUE, 0);
        waitpkt = NULL;
        if (ctio)
            disarm(ctio, &carmed);
    }
}

/* ---- the line editor (cooked mode) --------------------------------- */

static void begin_line(void)
{
    if (editing)
        return;
    editing = TRUE;
    llen = lcur = 0;
    hpos = -1;
    term_flush();
    x0 = term_cursor_x();
    y0 = term_cursor_y();
}

/* Draw the line from where it starts, and put the cursor at lcur. */
static void redraw_line(void)
{
    int cols = term_cols(), end, want, got, p;
    goto_rc(y0, x0);
    term_write(line, llen);
    wr("\x9b" "J");
    /* if the line pushed the screen up, it starts higher now */
    end = x0 + llen;
    want = y0 + (llen > 0 && end % cols == 0 ? end / cols - 1 : end / cols);
    got = term_cursor_y();
    if (got < want)
        y0 -= want - got;
    p = x0 + lcur;
    goto_rc(y0 + p / cols, p % cols);
    term_render_soon();
}

static void set_line(const UBYTE *s)
{
    llen = lcur = strlen((const char *)s);
    if (llen > LINEMAX)
        llen = lcur = LINEMAX;
    CopyMem((APTR)s, line, llen);
    redraw_line();
}

static void remember(void)
{
    UBYTE *h;
    if (llen == 0)
        return;
    if (nhist && strlen((char *)hist[nhist - 1]) == (size_t)llen &&
        !memcmp(hist[nhist - 1], line, llen))
        return;
    if (nhist == HISTMAX) {
        FreeVec(hist[0]);
        memmove(hist, hist + 1, (HISTMAX - 1) * sizeof hist[0]);
        nhist--;
    }
    h = AllocVec(llen + 1, MEMF_ANY);
    if (h) {
        CopyMem(line, h, llen);
        h[llen] = 0;
        hist[nhist++] = h;
    }
}

static void finish_line(void)
{
    int cols = term_cols(), p = x0 + llen;
    goto_rc(y0 + p / cols, p % cols);
    wr("\n");
    remember();
    push_input(line, llen);
    push_input((const UBYTE *)"\n", 1);
    editing = FALSE;
    llen = lcur = 0;
    term_render_soon();
    serve();
}

/* One key's bytes, cooked. */
static void edit_key(const UBYTE *b, LONG n)
{
    UBYTE c = b[0];
    if (c == 0x9B && n >= 2) {          /* cursor keys */
        UBYTE k = b[1];
        begin_line();
        if (k == 'A' && nhist) {                    /* up: history */
            hpos = hpos < 0 ? nhist - 1 : (hpos > 0 ? hpos - 1 : 0);
            set_line(hist[hpos]);
        } else if (k == 'B' && hpos >= 0) {         /* down */
            if (++hpos >= nhist) {
                hpos = -1;
                set_line((const UBYTE *)"");
            } else
                set_line(hist[hpos]);
        } else if (k == 'C' && lcur < llen) {
            lcur++;
            redraw_line();
        } else if (k == 'D' && lcur > 0) {
            lcur--;
            redraw_line();
        } else if (k == ' ' && n >= 3) {            /* shift: ends */
            lcur = b[2] == 'A' ? 0 : llen;
            redraw_line();
        }
        return;
    }
    while (n-- > 0) {
        c = *b++;
        if (c == '\r' || c == '\n') {
            begin_line();
            finish_line();
        } else if (c == 8) {                        /* backspace */
            begin_line();
            if (lcur > 0) {
                memmove(line + lcur - 1, line + lcur, llen - lcur);
                lcur--;
                llen--;
                redraw_line();
            }
        } else if (c == 0x7F) {                     /* del */
            begin_line();
            if (lcur < llen) {
                memmove(line + lcur, line + lcur + 1, llen - lcur - 1);
                llen--;
                redraw_line();
            }
        } else if (c == 0x18) {                     /* ctrl-x: clear */
            begin_line();
            set_line((const UBYTE *)"");
        } else if (c == 0x1C) {                     /* ctrl-\: end */
            if (!editing || llen == 0) {
                eof_pending = TRUE;
                serve();
            }
        } else if (c >= 0x20 && !(c >= 0x80 && c < 0xA0) &&
                   llen < LINEMAX - 1) {
            begin_line();
            memmove(line + lcur + 1, line + lcur, llen - lcur);
            line[lcur++] = c;
            llen++;
            if (lcur == llen) {                     /* at the end: just add */
                term_write(&c, 1);
                term_render_soon();
            } else
                redraw_line();
        }
    }
}

/* ---- what CTerm calls ----------------------------------------------- */

void con_keys(const UBYTE *b, LONG n)
{
    LONG i;
    /* Ctrl-C..F: break signals, never input */
    for (i = 0; i < n; i++)
        if (b[i] >= 3 && b[i] <= 6) {
            if (breaktask)
                Signal(breaktask, SIGBREAKF_CTRL_C << (b[i] - 3));
            return;
        }
    if (rawmode) {
        push_input(b, n);
        serve();
    } else
        edit_key(b, n);
}

void con_reply(const UBYTE *b, LONG n)
{
    push_input(b, n);
    serve();
}

void con_close_gadget(void)
{
    /* a cooked reader (the Shell) is told the input ended; a raw one
     * gets nothing unless it asked for close reports (class 11) */
    if (!rawmode) {
        eof_pending = TRUE;
        serve();
    }
}

BOOL con_alive(void)
{
    return child_running || opens > 0;
}

ULONG con_sigmask(void)
{
    ULONG m = 0;
    if (conport)
        m |= 1UL << conport->mp_SigBit;
    if (endbit >= 0)
        m |= 1UL << endbit;
    if (timerport)
        m |= 1UL << timerport->mp_SigBit;
    return m;
}

void con_handle(ULONG sigs)
{
    struct Message *m;
    if (!conport)
        return;
    if (endbit >= 0 && (sigs & (1UL << endbit)))
        child_running = FALSE;
    while ((m = GetMsg(conport))) {
        struct DosPacket *p = (struct DosPacket *)m->mn_Node.ln_Name;
        struct MsgPort *from = p->dp_Port;
        switch (p->dp_Type) {
        case ACTION_FINDINPUT:
        case ACTION_FINDOUTPUT:
        case ACTION_FINDUPDATE: {
            struct FileHandle *fh = BADDR(p->dp_Arg1);
            fh->fh_Port = (struct MsgPort *)DOSTRUE;    /* interactive */
            fh->fh_Args = 1;
            opens++;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        }
        case ACTION_END:
            if (opens > 0)
                opens--;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        case ACTION_WRITE: {
            LONG n = p->dp_Arg3;
            if (from)
                breaktask = from->mp_SigTask;
            if (n > 0)
                term_write((const UBYTE *)p->dp_Arg2, n);
            ReplyPkt(p, n > 0 ? n : 0, 0);
            term_render_soon();
            break;
        }
        case ACTION_READ:
            if (from)
                breaktask = from->mp_SigTask;
            if (nreads < READMAX) {
                reads[nreads++] = p;
                serve();
                if (nreads) {
                    /* about to wait for the person: show everything */
                    if (!rawmode)
                        begin_line();
                    term_flush();
                }
            } else
                ReplyPkt(p, -1, ERROR_NO_FREE_STORE);
            break;
        case ACTION_WAIT_CHAR:
            if (inlen > 0 || eof_pending) {
                ReplyPkt(p, DOSTRUE, 0);
            } else if (p->dp_Arg1 <= 0) {
                /* "is a key there?" without waiting: nobody is looking at
                 * the screen yet, so it is drawn within the frame (jump
                 * scroll) - conbench's sync-line is a stream of these */
                term_render_soon();
                ReplyPkt(p, DOSFALSE, 0);
            } else {
                term_flush();           /* it will really wait: show all */
                if (waitpkt)
                    ReplyPkt(waitpkt, DOSFALSE, 0);
                waitpkt = p;
                if (ctio) {
                    disarm(ctio, &carmed);
                    arm(ctio, p->dp_Arg1);
                    carmed = TRUE;
                }
            }
            break;
        case ACTION_SCREEN_MODE:
            if (p->dp_Arg1 && !rawmode && editing && llen) {
                /* typed ahead, then raw: hand over what was typed */
                push_input(line, llen);
                llen = lcur = 0;
            }
            rawmode = p->dp_Arg1 != 0;
            if (rawmode)
                editing = FALSE;
            term_flush();
            ReplyPkt(p, DOSTRUE, 0);
            break;
        case ACTION_DISK_INFO: {
            struct InfoData *id = BADDR(p->dp_Arg1);
            memset(id, 0, sizeof *id);
            id->id_DiskType = ID_CTRM;  /* not 'CON\0': we edit the lines */
            id->id_VolumeNode = (BPTR)win;  /* More and Ed find the window */
            id->id_InUse = 0;
            ReplyPkt(p, DOSTRUE, 0);
            break;
        }
        case ACTION_CHANGE_SIGNAL: {
            struct Task *old = breaktask;
            if (p->dp_Arg2)
                breaktask = (struct Task *)p->dp_Arg2;
            ReplyPkt(p, DOSTRUE, (LONG)old);
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
    if (carmed && CheckIO((struct IORequest *)ctio)) {
        WaitIO((struct IORequest *)ctio);
        carmed = FALSE;
        if (waitpkt) {
            ReplyPkt(waitpkt, DOSFALSE, 0);
            waitpkt = NULL;
        }
    }
}

BOOL con_start(const char *startup)
{
    struct Process *pr;
    maintask = FindTask(NULL);
    conport = CreateMsgPort();
    endbit = AllocSignal(-1);
    if (!conport || endbit < 0)
        return FALSE;
    infh = AllocDosObject(DOS_FILEHANDLE, NULL);
    if (!infh)
        return FALSE;
    infh->fh_Type = conport;
    infh->fh_Port = (struct MsgPort *)DOSTRUE;  /* interactive */
    infh->fh_Args = 1;
    opens = 1;
    startcmd[0] = 0;
    if (startup && startup[0]) {
        strcpy(startcmd, "Execute ");
        strncat(startcmd, startup, sizeof startcmd - 10);
    }
    child_running = TRUE;
    pr = CreateNewProcTags(NP_Entry, (ULONG)child_entry,
                           NP_Name, (ULONG)"CTerm Shell",
                           NP_StackSize, 16384,
                           NP_ConsoleTask, (ULONG)conport,
                           NP_WindowPtr, (ULONG)win,
                           TAG_END);
    if (!pr) {
        child_running = FALSE;
        FreeDosObject(DOS_FILEHANDLE, infh);
        infh = NULL;
        opens = 0;
        return FALSE;
    }
    return TRUE;
}

/* The person wants out: end the input so the Shell ends by itself. The
 * caller keeps answering packets until con_alive() is FALSE. */
void con_stop(void)
{
    eof_pending = TRUE;
    if (breaktask)
        Signal(breaktask, SIGBREAKF_CTRL_C);
    serve();
}

/* Only when nothing is open on the port any more. */
void con_free(void)
{
    int i;
    if (conport) {
        DeleteMsgPort(conport);
        conport = NULL;
    }
    if (endbit >= 0) {
        FreeSignal(endbit);
        endbit = -1;
    }
    for (i = 0; i < nhist; i++)
        FreeVec(hist[i]);
    nhist = 0;
}
