/*
 * sermod - calls over a serial line, the part loaded on demand
 * (PROGDIR:Protocols/Serial, sermod.h): a null modem (the line is the
 * connection) or a modem dialled with its AT commands. Any device that
 * works as serial.device does (a multi-serial card's own), any unit.
 *
 * Reading never holds CTerm: one CMD_READ of a byte is always out, and
 * when it comes back, what else waits (SDCMD_QUERY) is read at once.
 * The bytes wait in the line's buffer until loop_round (or a transfer)
 * takes them. Writes wait with a way out (Esc) and a time limit, as
 * telnet's do: with RTS/CTS a line that is never cleared would hold a
 * write for ever.
 *
 * Every line's requests and the two timers reply to one port, so one
 * signal wakes loop_round for all of them. Built without the C start-up
 * (as the protocols): no stdio, SysBase and DOSBase set at the start.
 */
#pragma GCC optimize("Os")              /* called rarely: small before quick */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <devices/serial.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "sermod.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
static const struct SerEnv *env;

#define SBUF     4096                   /* a line's own buffer */
#define CD_BIT   (1 << 5)               /* io_Status: carrier, low active */
#define OK_MS    5000                   /* a modem answers a command */

enum { R_NONE, R_OK, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NODIAL,
       R_BUSY, R_NOANSWER, R_TIME, R_GIVEUP };

struct Ser {
    struct Ser *next;                   /* every open line */
    struct IOExtSer *rd, *wr, *q;       /* reading, writing, asking */
    UBYTE rbyte;                        /* the byte the read waits for */
    BOOL rpend;                         /* a read is out */
    int errs;                           /* reads failed one after another */
    UBYTE buf[SBUF];                    /* what came, not yet taken */
    LONG r, w;
    LONG baud;                          /* the speed now */
    BOOL online;                        /* a modem's call is up */
    BOOL cdseen, cdon;                  /* the carrier: ever seen, now */
    BOOL lost;                          /* the call ended (carrier, words) */
    BOOL gone;                          /* the device failed */
    BOOL held;                          /* the last write was held back */
    BOOL told;                          /* held back: said once */
    LONG overran;                       /* reads that lost bytes (overrun,
                                           parity, break): not yet said */
    BOOL stuck;                         /* a write the device kept */
    SerLine line;                       /* as opened */
    char ln[48];                        /* the modem's words: the line */
    int ll;
};

static struct Ser *all;                 /* the open lines */
static struct MsgPort *sport;           /* the lines' and timers' replies */
static struct timerequest *alarm_io, *tick_io;
static BOOL alarm_on, tick_on, timer_ok;
static int nlines;

#define say(s) env->say(s)
#define modem (*env->modem)

static LONG num(const char *s)
{
    LONG n = 0;
    while (*s == ' ')
        s++;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + *s++ - '0';
    return n;
}

static void numcat(char *out, LONG n)
{
    env->num(out, n);
}

/* ---- the port and the timers: while any line is open ---- */

static BOOL lines_up(void)
{
    if (sport)
        return TRUE;
    if (!(sport = CreateMsgPort()))
        return FALSE;
    alarm_io = (struct timerequest *)CreateIORequest(sport, sizeof *alarm_io);
    tick_io = (struct timerequest *)CreateIORequest(sport, sizeof *tick_io);
    if (alarm_io && tick_io &&
        !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                    (struct IORequest *)alarm_io, 0)) {
        timer_ok = TRUE;
        tick_io->tr_node.io_Device = alarm_io->tr_node.io_Device;
        tick_io->tr_node.io_Unit = alarm_io->tr_node.io_Unit;
    }
    return TRUE;
}

static void timer_stop(struct timerequest *t, BOOL *on)
{
    if (*on) {
        AbortIO((struct IORequest *)t);
        WaitIO((struct IORequest *)t);
        *on = FALSE;
    }
}

static void timer_go(struct timerequest *t, BOOL *on, LONG ms)
{
    timer_stop(t, on);
    if (!timer_ok)
        return;
    t->tr_node.io_Command = TR_ADDREQUEST;
    t->tr_time.tv_secs = ms / 1000;
    t->tr_time.tv_micro = (ms % 1000) * 1000;
    SendIO((struct IORequest *)t);
    *on = TRUE;
}

/* The timer is up (and taken). */
static BOOL timer_up(struct timerequest *t, BOOL *on)
{
    if (*on && CheckIO((struct IORequest *)t)) {
        WaitIO((struct IORequest *)t);
        *on = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void lines_down(void)
{
    if (!sport || nlines > 0)
        return;
    timer_stop(alarm_io, &alarm_on);
    timer_stop(tick_io, &tick_on);
    if (timer_ok)
        CloseDevice((struct IORequest *)alarm_io);
    timer_ok = FALSE;
    DeleteIORequest((struct IORequest *)alarm_io);
    DeleteIORequest((struct IORequest *)tick_io);
    alarm_io = tick_io = NULL;
    DeleteMsgPort(sport);
    sport = NULL;
}

static ULONG sm_sigmask(void)
{
    return sport ? 1UL << sport->mp_SigBit : 0;
}

/* loop_round's timed wait when there is no TCP/IP stack: ms (-1: off). */
static void sm_alarm(LONG ms)
{
    if (!sport)
        return;
    if (ms < 0)
        timer_stop(alarm_io, &alarm_on);
    else
        timer_go(alarm_io, &alarm_on, ms);
}

static int sm_lines(void)
{
    return nlines;
}

/* ---- one line ---- */

static void read_go(struct Ser *s)
{
    s->rd->IOSer.io_Command = CMD_READ;
    s->rd->IOSer.io_Length = 1;
    s->rd->IOSer.io_Data = &s->rbyte;
    SendIO((struct IORequest *)s->rd);
    s->rpend = TRUE;
}

static void read_stop(struct Ser *s)
{
    if (s->rpend) {
        AbortIO((struct IORequest *)s->rd);
        WaitIO((struct IORequest *)s->rd);
        s->rpend = FALSE;
        if (!s->rd->IOSer.io_Error && s->rd->IOSer.io_Actual == 1 &&
            s->w < SBUF)
            s->buf[s->w++] = s->rbyte;
    }
}

/* The line as l says, at baud; the read must not be out. 0: taken. */
static BYTE set_line(struct Ser *s, const SerLine *l, LONG baud)
{
    struct IOExtSer *io = s->rd;
    UBYTE f = io->io_SerFlags & ~(SERF_PARTY_ON | SERF_PARTY_ODD |
                                  SERF_XDISABLED | SERF_RAD_BOOGIE |
                                  SERF_7WIRE);
    /* the open filled the request with the device's own flags, without
     * the 7WIRE asked for: asked again, or SETPARAMS would turn it off */
    if (l->flow == SF_RTSCTS)
        f |= SERF_7WIRE;
    io->io_Baud = baud;
    io->io_ReadLen = io->io_WriteLen = l->bits == 7 ? 7 : 8;
    io->io_StopBits = l->stop == 2 ? 2 : 1;
    io->io_RBufLen = l->bufkb > 0 ? l->bufkb * 1024 : 16384;
    io->io_ExtFlags = 0;
    if (l->parity != SP_NONE) {
        f |= SERF_PARTY_ON;
        if (l->parity == SP_ODD)
            f |= SERF_PARTY_ODD;
        else if (l->parity == SP_MARK)
            io->io_ExtFlags = SEXTF_MSPON | SEXTF_MARK;
        else if (l->parity == SP_SPACE)
            io->io_ExtFlags = SEXTF_MSPON;
    }
    if (l->flow == SF_XONXOFF)
        io->io_CtlChar = 0x11130000;    /* XON ^Q, XOFF ^S */
    else
        f |= SERF_XDISABLED;            /* XON/XOFF is on by default */
    io->io_SerFlags = f;
    io->IOSer.io_Command = SDCMD_SETPARAMS;
    if (DoIO((struct IORequest *)io))
        return io->IOSer.io_Error;
    s->baud = baud;
    return 0;
}

/* What the device took (its request after SETPARAMS), as a line's text:
 * "serial.device 0, 38400 8N1, RTS/CTS". */
static void line_now(struct Ser *s, const SerLine *l, char *out, int max)
{
    struct IOExtSer *io = s->rd;
    SerLine now = *l;
    UBYTE f = io->io_SerFlags;
    now.baud = io->io_Baud;
    now.bits = io->io_ReadLen;
    now.stop = io->io_StopBits;
    now.parity = !(f & SERF_PARTY_ON) ? SP_NONE
                 : (io->io_ExtFlags & SEXTF_MSPON)
                   ? (io->io_ExtFlags & SEXTF_MARK ? SP_MARK : SP_SPACE)
                 : f & SERF_PARTY_ODD ? SP_ODD : SP_EVEN;
    now.flow = f & SERF_7WIRE ? SF_RTSCTS
               : f & SERF_XDISABLED ? SF_NONE : SF_XONXOFF;
    env->linetext(&now, out, max);
}

/* What waits in the device, into the line's buffer; the next read out.
 * A line whose reads keep failing is gone. */
static void poll(struct Ser *s)
{
    LONG room;
    if (s->rpend && CheckIO((struct IORequest *)s->rd)) {
        WaitIO((struct IORequest *)s->rd);
        s->rpend = FALSE;
        BYTE e = s->rd->IOSer.io_Error;
        if (!e && s->rd->IOSer.io_Actual == 1) {
            s->buf[s->w++] = s->rbyte;
            s->errs = 0;
        } else if (e == SerErr_LineErr || e == SerErr_BufOverflow ||
                   e == SerErr_ParityErr || e == SerErr_DetectedBreak)
            s->overran++;               /* bytes lost; the line goes on */
        else if (++s->errs > 50)
            s->gone = TRUE;             /* the device keeps failing */
    }
    if (s->rpend || s->gone)
        return;
    if (s->r == s->w)
        s->r = s->w = 0;
    else if (s->r > SBUF / 2) {
        memmove(s->buf, s->buf + s->r, s->w - s->r);
        s->w -= s->r;
        s->r = 0;
    }
    room = SBUF - s->w;
    if (room <= 0)
        return;                         /* full: taking reads on */
    s->q->IOSer.io_Command = SDCMD_QUERY;
    if (!DoIO((struct IORequest *)s->q)) {
        LONG n = s->q->IOSer.io_Actual;
        s->cdon = !(s->q->io_Status & CD_BIT);
        if (n > 0) {
            s->rd->IOSer.io_Command = CMD_READ;
            s->rd->IOSer.io_Length = n < room ? n : room;
            s->rd->IOSer.io_Data = s->buf + s->w;
            if (!DoIO((struct IORequest *)s->rd))
                s->w += s->rd->IOSer.io_Actual;
            room = SBUF - s->w;
        }
    }
    if (room > 0)
        read_go(s);
}

/* What came on t's line: up to max bytes, 0 none, -1 the line is gone. */
static LONG sm_take(SerTab *t, UBYTE *b, LONG max)
{
    struct Ser *s = t->ser;
    LONG n;
    if (!s)
        return -1;
    poll(s);
    n = s->w - s->r;
    if (n > max)
        n = max;
    if (n > 0) {
        CopyMem(s->buf + s->r, b, n);
        s->r += n;
        poll(s);                        /* room again: the next read */
        return n;
    }
    return s->gone ? -1 : 0;
}

/* Wait for io (NULL: for bytes on the line, if data), at most ms: the
 * Shells go on, Esc and the close gadget give up. 1: done or bytes came,
 * 0: the time is up, -1: given up. */
static int ser_wait(struct Ser *s, struct IORequest *io, BOOL data, LONG ms)
{
    ULONG ssig = sm_sigmask();
    int r;
    if (!timer_ok && !io && !data) {
        Delay(ms / 20 + 1);             /* no timer: a plain pause */
        return 0;
    }
    timer_go(alarm_io, &alarm_on, ms);
    for (;;) {
        ULONG sigs;
        if (io && CheckIO(io)) { r = 1; break; }
        if (data) {
            poll(s);
            if (s->w > s->r || s->gone) { r = 1; break; }
        }
        if (timer_up(alarm_io, &alarm_on)) { r = 0; break; }
        sigs = Wait(ssig | env->sigs() | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) {
            SetSignal(SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_C);  /* for quitting */
            r = -1;
            break;
        }
        if ((sigs & ~ssig) && env->serve(sigs & ~ssig)) {
            r = -1;                     /* the other tabs went on; Esc */
            break;
        }
    }
    timer_stop(alarm_io, &alarm_on);
    SetSignal(ssig, ssig);              /* loop_round looks at every line */
    return r;
}

/* io back within ms (no window, no Shells: a short wait). */
static BOOL back_soon(struct IORequest *io, LONG ms)
{
    for (; ms > 0; ms -= 40) {
        if (CheckIO(io))
            return TRUE;
        Delay(2);
    }
    return CheckIO(io) != NULL;
}

/* A write given up on. serial.device does not let go of a write that
 * CTS holds back, aborted or not (WaitIO would wait for ever - seen in
 * FS-UAE): then the unit is reset, which does, and set up again. A write
 * kept even so is never waited on: the line is dead until a reboot. */
static void write_abort(struct Ser *s)
{
    AbortIO((struct IORequest *)s->wr);
    if (back_soon((struct IORequest *)s->wr, 1000)) {
        WaitIO((struct IORequest *)s->wr);
        return;
    }
    read_stop(s);
    s->q->IOSer.io_Command = CMD_RESET;
    DoIO((struct IORequest *)s->q);
    if (back_soon((struct IORequest *)s->wr, 1000)) {
        WaitIO((struct IORequest *)s->wr);
        if (!set_line(s, &s->line, s->baud))
            read_go(s);
        else
            s->gone = TRUE;
        return;
    }
    s->stuck = s->gone = TRUE;
}

/* Bytes out, all of them or none further: FALSE when the line would
 * not take them in time (CTS never came) or the person gave up. */
static BOOL put_bytes(struct Ser *s, const UBYTE *b, LONG n)
{
    int r;
    if (n <= 0 || s->gone)
        return n <= 0;
    s->wr->IOSer.io_Command = CMD_WRITE;
    s->wr->IOSer.io_Length = n;
    s->wr->IOSer.io_Data = (APTR)b;
    SendIO((struct IORequest *)s->wr);
    /* the time the bytes take, and 20 s more for a line held back (3 s
     * once it was: else every key typed would wait 20 s) */
    r = ser_wait(s, (struct IORequest *)s->wr, FALSE,
                 (s->held ? 3000 : 20000) +
                 n * 10000 / (s->baud > 0 ? s->baud : 300));
    if (r == 1) {
        WaitIO((struct IORequest *)s->wr);
        s->held = FALSE;
        return TRUE;
    }
    s->held = r == 0;
    write_abort(s);
    return FALSE;
}

/* Why a write did not go: the line's CTS, if it says. */
static void held_say(struct Ser *s)
{
    BOOL cts = TRUE;
    if (s->stuck) {
        say("\r\n\x1b[1;31m[the serial device keeps a write it will not give "
            "back: the line stays busy until the Amiga restarts]\x1b[0m\r\n");
        return;
    }
    s->q->IOSer.io_Command = SDCMD_QUERY;
    if (!DoIO((struct IORequest *)s->q))
        cts = !(s->q->io_Status & (1 << 4));
    say(cts ? "\r\n\x1b[1;31m[the line did not take what was sent in time]"
              "\x1b[0m\r\n"
            : "\r\n\x1b[1;31m[the line holds back what is sent: no CTS. Is "
              "the modem on and its cable in? Or flow control off (Line...)]"
              "\x1b[0m\r\n");
}

/* Data to t's line, as it is (no telnet here). FALSE: it did not go. */
static BOOL sm_write(SerTab *t, const UBYTE *b, LONG n)
{
    struct Ser *s = t->ser;
    if (!s)
        return FALSE;
    if (!put_bytes(s, b, n)) {
        if (!s->told && s->held) {
            s->told = TRUE;
            held_say(s);
        }
        return FALSE;
    }
    s->told = FALSE;
    return TRUE;
}

static struct Ser *ser_open(const SerLine *l)
{
    struct Ser *s;
    BYTE err;
    if (!lines_up())
        return NULL;
    s = AllocVec(sizeof *s, MEMF_ANY | MEMF_CLEAR);
    if (!s)
        return NULL;
    s->rd = (struct IOExtSer *)CreateIORequest(sport, sizeof *s->rd);
    s->wr = (struct IOExtSer *)CreateIORequest(sport, sizeof *s->wr);
    s->q = (struct IOExtSer *)CreateIORequest(sport, sizeof *s->q);
    if (s->rd && s->wr && s->q) {
        /* RTS/CTS only counts when it is asked for at the open */
        s->rd->io_SerFlags = (l->flow == SF_RTSCTS ? SERF_7WIRE : 0) |
                             (l->flow != SF_XONXOFF ? SERF_XDISABLED : 0);
        if (!OpenDevice((STRPTR)l->device, l->unit,
                        (struct IORequest *)s->rd, 0)) {
            nlines++;
            s->line = *l;
            CopyMem(s->rd, s->wr, sizeof *s->wr);
            CopyMem(s->rd, s->q, sizeof *s->q);
            if (!(err = set_line(s, l, l->baud))) {
                read_go(s);
                s->next = all;
                all = s;
                return s;
            }
            say("\x1b[1;31m[");
            say(l->device);
            say(" would not take these settings - another speed, framing "
                "or buffer?]\x1b[0m\r\n");
            CloseDevice((struct IORequest *)s->rd);
            nlines--;
        } else {
            char n[12] = "";
            say("\x1b[1;31m[could not open ");
            say(l->device);
            say(" unit ");
            numcat(n, l->unit);
            say(n);
            say(" - missing, or another program has it]\x1b[0m\r\n");
        }
    }
    DeleteIORequest((struct IORequest *)s->rd);
    DeleteIORequest((struct IORequest *)s->wr);
    DeleteIORequest((struct IORequest *)s->q);
    FreeVec(s);
    lines_down();
    return NULL;
}

/* The device closed: the line's DTR drops, which hangs up a modem set
 * to (AT&D2, the usual). */
static void ser_close(struct Ser *s)
{
    struct Ser **p;
    if (s->stuck)
        return;                         /* never closed: its write is out
                                         * (nor its port: the line counts,
                                         * and the file stays loaded) */
    for (p = &all; *p; p = &(*p)->next)
        if (*p == s) {
            *p = s->next;
            break;
        }
    read_stop(s);
    CloseDevice((struct IORequest *)s->rd);
    DeleteIORequest((struct IORequest *)s->rd);
    DeleteIORequest((struct IORequest *)s->wr);
    DeleteIORequest((struct IORequest *)s->q);
    FreeVec(s);
    nlines--;
    lines_down();
}

/* ---- the modem ---- */

/* A command with ^M-style letters for control bytes ("^M" is Return)
 * and ~ for half a second's pause; cr: Return after it. */
static BOOL send_cmd(struct Ser *s, const char *c, BOOL cr)
{
    UBYTE out[96];
    int k = 0;
    for (; *c; c++) {
        if (*c == '~') {
            if (!put_bytes(s, out, k) || ser_wait(s, NULL, FALSE, 500) < 0)
                return FALSE;
            k = 0;
            continue;
        }
        if (*c == '^' && c[1] >= '@' && c[1] <= '_')
            out[k++] = *++c & 31;
        else if (*c == '^' && c[1] >= 'a' && c[1] <= 'z')
            out[k++] = *++c & 31;
        else
            out[k++] = *c;
        if (k >= (int)sizeof out - 2) {
            if (!put_bytes(s, out, k))
                return FALSE;
            k = 0;
        }
    }
    if (cr)
        out[k++] = '\r';
    return put_bytes(s, out, k);
}

/* A line the modem said: which result it is (numeric too, when asked). */
static int result_of(const char *w, BOOL numeric)
{
    static const struct { const char *t; int r; } words[] = {
        { "OK", R_OK }, { "CONNECT", R_CONNECT }, { "RING", R_RING },
        { "NO CARRIER", R_NOCARRIER }, { "ERROR", R_ERROR },
        { "NO DIALTONE", R_NODIAL }, { "NO DIAL TONE", R_NODIAL },
        { "BUSY", R_BUSY }, { "NO ANSWER", R_NOANSWER } };
    int i, n;
    for (i = 0; i < (int)(sizeof words / sizeof words[0]); i++) {
        n = strlen(words[i].t);
        if (!strncmp(w, words[i].t, n) &&
            (!w[n] || (words[i].r == R_CONNECT && (w[n] == ' ' ||
                                                   w[n] == '/'))))
            return words[i].r;
    }
    if (numeric && w[0] >= '0' && w[0] <= '9' &&
        (!w[1] || (w[1] >= '0' && w[1] <= '9' && !w[2]))) {
        static const UBYTE code[] = { R_OK, R_CONNECT, R_RING, R_NOCARRIER,
            R_ERROR, R_CONNECT, R_NODIAL, R_BUSY, R_NOANSWER };
        n = num(w);
        return n < 9 ? code[n] : R_CONNECT;     /* 10 up: CONNECT speeds */
    }
    return R_NONE;
}

/* One byte of the modem's words: a whole line's result, or R_NONE. */
static int scan(struct Ser *s, UBYTE c, BOOL numeric)
{
    int r = R_NONE;
    if (c == '\r' || c == '\n') {
        if (s->ll > 0 && s->ll < (int)sizeof s->ln) {
            while (s->ll && s->ln[s->ll - 1] == ' ')
                s->ll--;
            s->ln[s->ll] = 0;
            r = result_of(s->ln, numeric);
        }
        s->ll = 0;
    } else if (s->ll < (int)sizeof s->ln) {
        if (c >= ' ' && c < 127)
            s->ln[s->ll++] = c;
        else
            s->ll = sizeof s->ln;       /* not a modem's line */
    }
    return r;
}

static void status(const char *a, const char *b);

/* CONNECT 38400/ARQ: the line follows the speed, unless locked. */
static void follow_speed(SerTab *t, struct Ser *s)
{
    LONG b;
    if (t->line.locked || strncmp(s->ln, "CONNECT ", 8))
        return;
    b = num(s->ln + 8);
    if (b < 110 || b == s->baud)
        return;
    read_stop(s);
    if (set_line(s, &t->line, b)) {
        status("the line would not follow the modem's speed", NULL);
        set_line(s, &t->line, s->baud);
    } else {
        char m[40] = "the line follows: ";
        numcat(m, s->rd->io_Baud);      /* as the device took it */
        status(m, NULL);
    }
    read_go(s);
}

static UBYTE lastcr;                    /* the words shown ended in CR */

/* The modem's words in the tab, as they come. */
static void show(const UBYTE *b, LONG n)
{
    if (n > 0)
        lastcr = b[n - 1] == '\r';
    env->text(b, n);
    env->refresh();
}

/* Wait for the modem's answer (shown), at most ms. */
static int answer(SerTab *t, struct Ser *s, LONG ms)
{
    struct timeval t0, now;
    env->now(&t0);
    for (;;) {
        LONG st, left;
        int r = R_NONE, w;
        /* up to the result's line only: what follows (a board's first
         * words) comes after CTerm's own [connected] */
        poll(s);
        st = s->r;
        while (s->r < s->w && r == R_NONE) {
            r = scan(s, s->buf[s->r++], TRUE);
            if (r == R_RING)
                r = R_NONE;
        }
        if (r != R_NONE && s->r < s->w && s->buf[s->r] == '\n')
            s->r++;                     /* the result's line, whole */
        if (s->r > st)
            show(s->buf + st, s->r - st);
        if (r == R_CONNECT)
            follow_speed(t, s);
        if (r != R_NONE)
            return r;
        if (s->gone)
            return R_ERROR;
        env->now(&now);
        left = now.tv_secs ? ms - (LONG)(now.tv_secs - t0.tv_secs) * 1000 -
                             ((LONG)now.tv_micro - (LONG)t0.tv_micro) / 1000
                           : ms;
        if (left <= 0)
            return R_TIME;
        w = ser_wait(s, NULL, TRUE, left);
        if (w < 0)
            return R_GIVEUP;
        if (w == 0 && !now.tv_secs)
            return R_TIME;              /* no clock: the alarm said */
    }
}

static void status(const char *a, const char *b)
{
    if (env->column() || lastcr)        /* after the modem's words (their
                                         * line ends in CR alone) */
        say("\r\n");
    lastcr = FALSE;
    say("\x1b[2m[");
    say(a);
    if (b)
        say(b);
    say("]\x1b[0m\r\n");
    env->refresh();
}

/* +++ (a second's quiet each side), then the hang-up command. */
static void modem_hangup(SerTab *t, struct Ser *s)
{
    if (ser_wait(s, NULL, FALSE, 1100) < 0 || !send_cmd(s, "+++", FALSE) ||
        ser_wait(s, NULL, FALSE, 1100) < 0)
        return;
    if (modem.hangcmd[0] && send_cmd(s, modem.hangcmd, TRUE))
        answer(t, s, 2000);
}

/* Call t's line: open it, and with a phone number dial it. TRUE: the
 * line (or the call) is up. */
static BOOL sm_call(SerTab *t)
{
    struct Ser *s;
    const char *inits[3];
    char what[100];
    int i, tries;
    s = ser_open(&t->line);
    if (!s)
        return FALSE;
    t->ser = s;
    line_now(s, &t->line, what, sizeof what);
    status(what, NULL);
    if (!t->phone[0]) {
        status(t->named ? "a direct line: type away"
               : "a serial terminal: type AT commands to a modem, or talk "
                 "to the other side", NULL);
        return TRUE;
    }
    inits[0] = modem.init;
    inits[1] = modem.init2;
    inits[2] = t->init;
    for (i = 0; i < 3; i++) {
        int r;
        if (!inits[i][0])
            continue;
        if (!send_cmd(s, inits[i], TRUE)) {
            if (s->held) {
                held_say(s);
                goto fail;
            }
            r = R_GIVEUP;
        }
        else
            r = answer(t, s, OK_MS);
        if (r == R_OK)
            continue;
        status(r == R_TIME ? "no answer from the modem to " :
               r == R_GIVEUP ? "given up" : "the modem said no to ",
               r == R_GIVEUP ? NULL : inits[i]);
        if (r == R_TIME)
            status("is it on, and is the speed right?", NULL);
        goto fail;
    }
    for (tries = 0;; tries++) {
        int r;
        char cmd[100];
        strcpy(cmd, t->dialpre[0] ? t->dialpre : modem.dial);
        strncat(cmd, t->phone, 47);
        strncat(cmd, modem.suffix, 15);
        if (!send_cmd(s, cmd, FALSE)) {
            if (s->held) {
                held_say(s);
                goto fail;
            }
            r = R_GIVEUP;
        }
        else
            r = answer(t, s, (modem.wait > 0 ? modem.wait : 60) * 1000);
        if (r == R_CONNECT) {
            s->online = TRUE;
            s->cdseen = s->cdon;
            status("connected", NULL);
            return TRUE;
        }
        if ((r == R_BUSY || r == R_NOANSWER) && tries < modem.redial) {
            char m[96];
            strcpy(m, r == R_BUSY ? "busy" : "no answer");
            strcat(m, " - calling again in ");
            numcat(m, modem.pause);
            strcat(m, " s (");
            numcat(m, tries + 2);
            strcat(m, " of ");
            numcat(m, modem.redial + 1);
            strcat(m, "), ");
            strncat(m, env->keyword(0), 23);
            strcat(m, " stops");
            status(m, NULL);
            if (ser_wait(s, NULL, FALSE, modem.pause * 1000) < 0) {
                status("given up", NULL);
                goto fail;
            }
            continue;
        }
        if (r == R_TIME || r == R_GIVEUP) {
            /* a key stops a modem's dialling; then hang up */
            send_cmd(s, "", TRUE);
            answer(t, s, 1500);
            if (modem.hangcmd[0] && send_cmd(s, modem.hangcmd, TRUE))
                answer(t, s, 1500);
            status(r == R_TIME ? "no answer in time" : "given up", NULL);
            if (s->held)
                held_say(s);
        } else
            status(r == R_BUSY ? "busy" : r == R_NOANSWER ? "no answer" :
                   r == R_NODIAL ? "no dial tone" :
                   r == R_ERROR ? "the modem said ERROR" : "no carrier",
                   NULL);
        break;
    }
fail:
    t->ser = NULL;
    ser_close(s);
    return FALSE;
}

/* t's line goes: a modem's call hung up first (+++ and ATH0, unless
 * dropping DTR is the way: the close does that). */
static void sm_close(SerTab *t)
{
    struct Ser *s = t->ser;
    if (!s)
        return;
    if (s->online && !s->gone && modem.hangup != SH_DTR)
        modem_hangup(t, s);
    t->ser = NULL;
    ser_close(s);
}

/* Project/Answer: the modem picks up (ATA). */
static void sm_answer(SerTab *t)
{
    if (t->ser)
        send_cmd(t->ser, "ATA", TRUE);
}

/* A call that ended (the carrier, the modem's words): a phone book's
 * dial-up closes its line, Return calls again; a serial terminal stays
 * at the modem. */
static void call_over(SerTab *t, struct Ser *s)
{
    s->online = s->lost = FALSE;
    if (t->phone[0]) {
        t->ser = NULL;
        ser_close(s);
        say("\r\n\x1b[0;2m[the carrier is gone - ");
        say(env->keyword(1));
        say(" calls again]\x1b[0m\r\n");
    } else
        say("\r\n\x1b[0;2m[the call ended]\x1b[0m\r\n");
}

/* Every line: what came, into its buffer; once a second, the carriers. */
static void sm_poll(void)
{
    BOOL tick, any = FALSE;
    struct Ser *s;
    if (!sport)
        return;
    tick = timer_up(tick_io, &tick_on);
    for (s = all; s; s = s->next) {
        poll(s);
        if (tick && s->online) {
            s->q->IOSer.io_Command = SDCMD_QUERY;
            if (!DoIO((struct IORequest *)s->q))
                s->cdon = !(s->q->io_Status & CD_BIT);
        }
        if (s->online) {
            if (s->cdon)
                s->cdseen = TRUE;
            else if (s->cdseen)
                s->lost = TRUE;         /* it was there, it is gone */
            any = TRUE;
        }
    }
    if (any && !tick_on)
        timer_go(tick_io, &tick_on, 1000);
}

/* t's line has something for loop_round: bytes, or its end. */
static BOOL sm_due(SerTab *t)
{
    struct Ser *s = t->ser;
    return s && (s->w > s->r || s->lost || s->gone);
}

/* t's bytes, shown (loop_round): a transfer's start is noticed, the
 * modem's RING, CONNECT and NO CARRIER heeded. */
static void sm_input(SerTab *t, UBYTE *buf, LONG size)
{
    struct Ser *s = t->ser;
    int rounds = 0;
    LONG k, i;
    while (s && (k = sm_take(t, buf, size)) > 0) {
        UBYTE *p = buf;
        if (env->xfer_take(buf, k))
            break;                      /* a file transfer took the rest */
        for (i = 0; i < k; i++)
            switch (scan(s, p[i], FALSE)) {
            case R_RING:
                if (modem.answer && !s->online) {
                    show(p, i + 1);     /* RING, then what we do */
                    p += i + 1;
                    k -= i + 1;
                    i = -1;
                    status("answering", NULL);
                    send_cmd(s, "ATA", TRUE);
                }
                break;
            case R_CONNECT:
                if (!s->online) {
                    show(p, i + 1);     /* CONNECT, then what we do */
                    p += i + 1;
                    k -= i + 1;
                    i = -1;
                    follow_speed(t, s);
                    s->online = TRUE;
                    s->cdseen = s->cdon;
                }
                break;
            case R_NOCARRIER:
                if (s->online)
                    s->lost = TRUE;
                break;
            }
        env->text(p, k);
        if (s->lost || ++rounds > 16)
            break;
    }
    s = t->ser;
    if (s && s->overran && !env->xfer_on() && !env->column()) {
        s->overran = 0;
        status("bytes were lost: the line ran over (too fast for the "
               "Amiga without flow control - RTS/CTS, or a lower speed)",
               NULL);
    }
    if (s && s->gone) {
        t->ser = NULL;
        ser_close(s);
        say("\r\n\x1b[1;31m[the serial device stopped working - Return "
            "opens it again]\x1b[0m\r\n");
    } else if (s && s->lost)
        call_over(t, s);
    env->replies();
}

/* ---- the way in ---- */

static struct SerMod *sermod_start(const struct SerEnv *e)
{
    static struct SerMod m;
    SysBase = *(struct ExecBase **)4;
    env = e;
    DOSBase = (struct DosLibrary *)e->dosbase;
    m.magic = SERMOD_MAGIC;
    m.call = sm_call;
    m.close = sm_close;
    m.answer = sm_answer;
    m.take = sm_take;
    m.write = sm_write;
    m.sigmask = sm_sigmask;
    m.alarm = sm_alarm;
    m.poll = sm_poll;
    m.due = sm_due;
    m.input = sm_input;
    m.lines = sm_lines;
    return &m;
}

/* What CTerm looks for after LoadSeg: the mark, the version, then the
 * way in. */
const struct { ULONG mark, version; SerModStart start; }
    sermod_head __attribute__((used)) = { SERMOD_HEAD, SERMOD_VERSION,
                                          sermod_start };
