/*
 * net - telnet over TCP, calling out, the Call window and the phone book
 * calls (a serial line's own are serial.c's).
 */
#include "cterm.h"

static void send_raw(const UBYTE *b, LONG n);
static void telnet_option(int cmd, int opt);
static BOOL dial(void);
static void window_resized(void);
static void call_start_book(const BookEntry *e);

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define O_BINARY 0
#define O_ECHO   1
#define O_SGA    3
#define O_NAWS   31
#define O_TTYPE  24

LONG port = DEFAULT_PORT;
BOOL saved_utf8 = TRUE;          /* the setting; utf8 may be LATIN1's */

#define NB_FIONBIO 0x8004667EUL         /* _IOW('f', 126, long): not in
                                         * this toolchain's headers */
#define NET_SECS   30                   /* a host silent this long is gone */

/* A window message while CTerm waits (a call, a typed answer), answered
 * here: a damaged window is drawn again, a new size laid out. Others are
 * not acted on. m is replied to. */
static void window_chore(struct IntuiMessage *m)
{
    ULONG cl = m->Class;
    if (cl == IDCMP_REFRESHWINDOW) {
        BeginRefresh(win);
        EndRefresh(win, TRUE);
    }
    ReplyMsg((struct Message *)m);
    if (cl == IDCMP_REFRESHWINDOW)
        redraw_all();
    else if (cl == IDCMP_NEWSIZE)
        window_resized();
}

/* Esc or the close gadget, while a call waits: give up. */
static BOOL user_gives_up(void)
{
    struct IntuiMessage *m;
    BOOL out = FALSE;
    while (win && (m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cl = m->Class;
        UWORD code = m->Code;
        if ((cl == IDCMP_CLOSEWINDOW && code != 1) ||   /* 1: iconify */
            (cl == IDCMP_RAWKEY && code == 0x45)) {
            ReplyMsg((struct Message *)m);
            out = TRUE;
        } else
            window_chore(m);
    }
    return out;
}

BOOL net_gives_up(void)
{
    return user_gives_up();
}

int net_wait(LONG fd, int rd, int wr, LONG secs)
{
    struct timeval t0, now;
    LONG idle = 0;
    term_now(&t0);
    for (;;) {
        fd_set r, w;
        struct timeval tv;
        ULONG wsig = win ? 1UL << win->UserPort->mp_SigBit : 0;
        ULONG sigs = wsig | shells_sigmask() | SIGBREAKF_CTRL_C;
        LONG n;
        FD_ZERO(&r);
        FD_ZERO(&w);
        if (rd)
            FD_SET(fd, &r);
        if (wr)
            FD_SET(fd, &w);
        tv.tv_sec = 1;                  /* look at the clock each second */
        tv.tv_usec = 0;
        n = WaitSelect(fd + 1, rd ? &r : NULL, wr ? &w : NULL, NULL, &tv,
                       &sigs);
        if (n > 0) {
            if (sigs)                   /* not ours: back for the main loop */
                SetSignal(sigs, sigs);
            return 1;
        }
        if (n < 0 || (sigs & SIGBREAKF_CTRL_C))
            return -1;
        if (sigs & ~(wsig | SIGBREAKF_CTRL_C))
            shells_handle(sigs);        /* the other tabs go on */
        if ((sigs & wsig) && user_gives_up())
            return -1;
        if (n == 0 && !sigs)
            idle++;
        term_now(&now);
        /* no timer (secs 0): count the quiet seconds instead */
        if ((now.tv_secs ? (LONG)(now.tv_secs - t0.tv_secs) : idle) >= secs)
            return 0;
    }
}

/* A host name to an address, in a process of its own: gethostbyname
 * waits for the name server as long as the TCP/IP stack likes, and the
 * window must go on (Esc gives up). The process has its own
 * bsdsocket.library base (a base is per task). Given up on, it goes on
 * alone and frees what it was given; CTerm waits for any still running
 * before it quits (their code is CTerm's). */
struct Lookup {
    char host[256];
    ULONG addr;
    BYTE done, found, abandoned, sig;
    struct Task *parent;
};
static volatile int lookups_alive;

static void lookup_entry(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    struct Lookup *l = (struct Lookup *)me->pr_ExitData;
    struct Library *SocketBase = OpenLibrary("bsdsocket.library", 4);
    ULONG addr = 0;
    BOOL found = FALSE;
    if (SocketBase) {
        struct hostent *he = gethostbyname((STRPTR)l->host);
        if (he && he->h_length == 4) {
            memcpy(&addr, he->h_addr, 4);
            found = TRUE;
        }
        CloseLibrary(SocketBase);
    }
    Forbid();
    if (l->abandoned)
        FreeVec(l);
    else {
        l->addr = addr;
        l->found = found;
        l->done = TRUE;
        Signal(l->parent, 1UL << l->sig);
    }
    lookups_alive--;
    /* still in Forbid: back to DOS, which ends the process - CTerm cannot
     * be unloaded under the last instructions */
}

/* Wait for every lookup given up on (at quit: their code is ours). */
void net_lookups_wait(void)
{
    while (lookups_alive > 0)
        Delay(10);
}

/* 1: found (*addr), 0: no such host, -1: given up (Esc, the close gadget,
 * Ctrl-C, NET_SECS), -2: could not start the lookup. */
static int lookup(const char *host, ULONG *addr)
{
    struct Lookup *l = AllocVec(sizeof *l, MEMF_PUBLIC | MEMF_CLEAR);
    struct timeval t0, now;
    LONG idle = 0;
    int r = -1;
    if (!l)
        return -2;
    strncpy(l->host, host, sizeof l->host - 1);
    l->parent = FindTask(NULL);
    l->sig = AllocSignal(-1);
    if (l->sig < 0) {
        FreeVec(l);
        return -2;
    }
    SetSignal(0, 1UL << l->sig);
    Forbid();
    lookups_alive++;
    if (!CreateNewProcTags(NP_Entry, (ULONG)lookup_entry,
                           NP_Name, (ULONG)"CTerm name lookup",
                           NP_StackSize, 16384,
                           NP_ExitData, (ULONG)l, TAG_END)) {
        lookups_alive--;
        Permit();
        FreeSignal(l->sig);
        FreeVec(l);
        return -2;
    }
    Permit();
    term_now(&t0);
    for (;;) {
        struct timeval tv;
        ULONG lsig = 1UL << l->sig;
        ULONG wsig = win ? 1UL << win->UserPort->mp_SigBit : 0;
        ULONG sigs = lsig | wsig | shells_sigmask() | SIGBREAKF_CTRL_C;
        tv.tv_sec = 1;                  /* look at the clock each second */
        tv.tv_usec = 0;
        WaitSelect(0, NULL, NULL, NULL, &tv, &sigs);
        if (l->done) {
            *addr = l->addr;
            r = l->found ? 1 : 0;
            break;
        }
        if (sigs & SIGBREAKF_CTRL_C)
            break;
        if (sigs & ~(lsig | wsig))
            shells_handle(sigs);        /* the other tabs go on */
        if ((sigs & wsig) && user_gives_up())
            break;
        if (!sigs)
            idle++;
        term_now(&now);
        if ((now.tv_secs ? (LONG)(now.tv_secs - t0.tv_secs) : idle) >=
            NET_SECS)
            break;
    }
    Forbid();
    if (l->done)
        FreeVec(l);                     /* answered (perhaps just now) */
    else
        l->abandoned = TRUE;            /* it frees l itself */
    Permit();
    FreeSignal(l->sig);
    return r;
}

LONG net_connect(const char *host, LONG p, ssh_say_fn sayf)
{
    struct sockaddr_in sa;
    LONG s, on = 1, off = 0;
    if (p <= 0 || p > 65535) {
        sayf("\x1b[1;31m[no such port]\x1b[0m\r\n");
        return -1;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(p);
    sa.sin_addr.s_addr = inet_addr((STRPTR)host);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        ULONG a;
        int r;
        sayf("\x1b[2m[looking up the name...]\x1b[0m\r\n");
        r = lookup(host, &a);
        if (r <= 0) {
            sayf(r == 0 ? "\x1b[1;31m[no such host]\x1b[0m\r\n"
                 : r == -1 ? "\x1b[1;31m[given up]\x1b[0m\r\n"
                 : "\x1b[1;31m[could not look up the name]\x1b[0m\r\n");
            return -1;
        }
        memcpy(&sa.sin_addr, &a, 4);
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    /* connect without blocking, then wait for it with a way out (Esc) */
    IoctlSocket(s, NB_FIONBIO, (APTR)&on);
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) < 0) {
        struct sockaddr_in peer;
        socklen_t len = sizeof peer;
        int r = net_wait(s, 0, 1, NET_SECS);
        if (r <= 0 || getpeername(s, (struct sockaddr *)&peer, &len) < 0) {
            CloseSocket(s);
            sayf(r < 0 ? "\x1b[1;31m[given up]\x1b[0m\r\n"
                       : "\x1b[1;31m[the call was not answered]\x1b[0m\r\n");
            return -1;
        }
    }
    IoctlSocket(s, NB_FIONBIO, (APTR)&off);
    setsockopt(s, IPPROTO_TCP, 1 /* TCP_NODELAY */, &on, sizeof on);
    return s;
}


static struct Tab *sending;              /* send_raw is on this tab's bytes */

static void send_raw(const UBYTE *b, LONG n)
{
    LONG done = 0;
    struct Tab *keep = sending;
    if (sock < 0 || sshmode)            /* telnet's own bytes only */
        return;
    sending = T;
    while (done < n) {
        /* a blocking send to a host that stopped reading would hold all
         * of CTerm: wait for room (a way out, a time limit), then send
         * no more than room surely is for (the low-water mark is 2 KB) */
        LONG k = n - done < 512 ? n - done : 512;
        if (net_wait(sock, 0, 1, NET_SECS) <= 0)
            break;                      /* the rest is dropped */
        k = send(sock, (APTR)(b + done), k, 0);
        if (k <= 0)
            break;
        done += k;
    }
    sending = keep;
}

/* Data to the host: 0xFF doubled (telnet), or through ssh. */
void send_data(const UBYTE *b, LONG n)
{
    static UBYTE buf[1024];
    LONG i, k = 0;
    if (T->t_serial) {                  /* a serial line: as it is */
        serial_write(b, n);
        return;
    }
    if (sshmode) {
        if (sock >= 0)
            ssh_write(T->ssh, b, n);
        return;
    }
    for (i = 0; i < n; i++) {
        if (k >= (LONG)sizeof buf - 2) {
            send_raw(buf, k);
            k = 0;
        }
        buf[k++] = b[i];
        if (b[i] == IAC)
            buf[k++] = IAC;
    }
    send_raw(buf, k);
}

/* Telnet options as RFC 1143 keeps them (its "Q method", without the
 * queue): for each option CTerm takes part in, whether it is on at our end
 * (us) and at the host's (him), or asked for and not yet answered. A
 * request for what already is gets no answer, so two programs cannot
 * bounce one back and forth for ever. Per tab, in a table of our own:
 * set afresh when a connection begins (dial, connected). */
enum { Q_NO, Q_YES, Q_WANTNO, Q_WANTYES };
static const UBYTE qopt[] = { O_BINARY, O_ECHO, O_SGA, O_TTYPE, O_NAWS };
#define NQ ((int)sizeof qopt)
#define Q_NAWS 4                        /* O_NAWS's place in qopt */
static struct TelOpts {
    struct Tab *t;
    UBYTE us[NQ], him[NQ];
} topts[MAXTABS];

/* T's options (fresh: all off, a new connection). NULL only if the table
 * is full of live tabs, which MAXTABS rules out. */
static struct TelOpts *tel_opts(BOOL fresh)
{
    struct TelOpts *o = NULL;
    int i, j;
    for (i = 0; i < MAXTABS && !o; i++)
        if (topts[i].t == T)
            o = &topts[i];
    for (i = 0; i < MAXTABS && !o; i++) {
        /* a place no live tab holds: never used, or its tab closed */
        for (j = 0; j < ntabs && tabs[j] != topts[i].t; j++)
            ;
        if (!topts[i].t || j == ntabs) {
            o = &topts[i];
            fresh = TRUE;
        }
    }
    if (o && fresh) {
        memset(o, 0, sizeof *o);
        o->t = T;
    }
    return o;
}

static void tel_send(int cmd, int opt)
{
    UBYTE r[3];
    r[0] = IAC;
    r[1] = cmd;
    r[2] = opt;
    send_raw(r, 3);
}

/* The window's size to the host, if it asked for it (DO NAWS): IAC SB
 * NAWS width height IAC SE, each a 16-bit number, a 255 byte doubled. */
void send_naws(void)
{
    UBYTE m[13], v[4];
    struct TelOpts *o;
    int i, k = 0;
    int c = vt_cols(vt), r = vt_rows(vt);  /* the tab's: its pane's */
    if (sshmode) {                      /* the pty's size, ssh's way */
        ssh_resize(T->ssh, c, r);
        return;
    }
    o = tel_opts(FALSE);
    if (!o || o->us[Q_NAWS] != Q_YES)
        return;
    v[0] = c >> 8; v[1] = c & 255;
    v[2] = r >> 8; v[3] = r & 255;
    m[k++] = IAC; m[k++] = SB; m[k++] = O_NAWS;
    for (i = 0; i < 4; i++) {
        m[k++] = v[i];
        if (v[i] == IAC)
            m[k++] = IAC;
    }
    m[k++] = IAC; m[k++] = SE;
    send_raw(m, k);
}

/* We ask for an option at our end (WILL opt), unless it is on or asked. */
static void tel_will(int opt)
{
    struct TelOpts *o = tel_opts(FALSE);
    int i;
    for (i = 0; i < NQ && qopt[i] != opt; i++)
        ;
    if (!o || i == NQ || o->us[i] == Q_YES || o->us[i] == Q_WANTYES)
        return;
    o->us[i] = Q_WANTYES;
    tel_send(WILL, opt);
}

static void telnet_option(int cmd, int opt)
{
    struct TelOpts *o = tel_opts(FALSE);
    BOOL ours = cmd == DO || cmd == DONT;       /* about our end */
    BOOL on = cmd == DO || cmd == WILL;
    BOOL ok;
    UBYTE *q = NULL, was;
    int i;
    for (i = 0; i < NQ && qopt[i] != opt; i++)
        ;
    if (o && i < NQ)
        q = ours ? &o->us[i] : &o->him[i];
    /* what CTerm agrees to: it tells the size, binary, its terminal's
     * name; the host may echo, drop go-ahead, send binary */
    ok = q && (ours ? (opt == O_NAWS || opt == O_BINARY || opt == O_TTYPE)
                    : (opt == O_ECHO || opt == O_SGA || opt == O_BINARY));
    if (!q) {                           /* an option CTerm keeps off */
        if (on)
            tel_send(ours ? WONT : DONT, opt);
        return;
    }
    was = *q;
    if (on) {
        if (was == Q_NO) {
            *q = ok ? Q_YES : Q_NO;
            tel_send(ours ? (ok ? WILL : WONT) : (ok ? DO : DONT), opt);
        } else if (was == Q_WANTYES)
            *q = Q_YES;                 /* the answer to our asking */
        else if (was == Q_WANTNO)
            *q = Q_NO;                  /* the peer erred: it stays off */
    } else {
        if (was == Q_YES)
            tel_send(ours ? WONT : DONT, opt);
        *q = Q_NO;
    }
    if (ours && i == Q_NAWS && was != Q_YES && *q == Q_YES)
        send_naws();                    /* agreed now: the size at once */
}

/* Bytes from the host: telnet commands out, data to the emulator. */
void from_host(UBYTE *b, LONG n)
{
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = b[i];
        switch (tstate) {
        case 0:
            if (c == IAC) tstate = 1;
            else b[k++] = c;
            break;
        case 1:
            if (c == IAC) { b[k++] = IAC; tstate = 0; }
            else if (c >= WILL && c <= DONT) { tcmd = c; tstate = 2; }
            else if (c == SB) { tstate = 3; sblen = 0; }
            else tstate = 0;
            break;
        case 2:
            if (k) { log_bytes(b, k); vt_write(vt, b, k); k = 0; }
            telnet_option(tcmd, c);
            tstate = 0;
            break;
        case 3:                         /* subnegotiation */
            if (c == IAC) tstate = 4;
            else if (sblen < (int)sizeof sbuf) sbuf[sblen++] = c;
            break;
        case 4:
            if (c == SE) {
                /* "which terminal are you?": a BBS picks ANSI or not by it */
                if (sblen >= 2 && sbuf[0] == O_TTYPE && sbuf[1] == 1) {
                    static const UBYTE head[4] = { IAC, SB, O_TTYPE, 0 };
                    static const UBYTE tail[2] = { IAC, SE };
                    const char *name = pcmode ? "ANSI" : "XTERM-256COLOR";
                    send_raw(head, 4);
                    send_raw((const UBYTE *)name, strlen(name));
                    send_raw(tail, 2);
                }
                sblen = 0;
                tstate = 0;
            } else {
                if (c == IAC && sblen < (int)sizeof sbuf) sbuf[sblen++] = c;
                tstate = 3;
            }
            break;
        }
    }
    if (k) {
        log_bytes(b, k);
        vt_write(vt, b, k);
    }
}

void numstr_cat(char *out, LONG n)
{
    char tmp[12], *p = tmp + sizeof tmp - 1;
    *p = 0;
    if (n < 0) n = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    strcat(out, p);
}

void say(const char *s)
{
    vt_write(vt, (const uint8_t *)s, strlen(s));
}

LONG listen_on(void)
{
    struct sockaddr_in sa;
    LONG one = 1, fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 ||
        listen(fd, 1) < 0) {
        CloseSocket(fd);
        return -1;
    }
    return fd;
}

/* HOST: call out ("name" or "name:port", telnet's 23 by default). */
static BOOL dial(void)
{
    char name[128], *colon;
    LONG p = 23;
    strncpy(name, dialhost, sizeof name - 1);
    name[sizeof name - 1] = 0;
    colon = strchr(name, ':');
    if (colon) {
        *colon = 0;
        p = atol(colon + 1);
    }
    sock = net_connect(name, p, say);
    if (sock < 0)
        return FALSE;
    tstate = 0;
    tel_opts(TRUE);                     /* every option off, as it begins */
    return TRUE;
}

/* The window got a new size while CTerm waits: laid out, and the hosts
 * told, as the main loop does. Not the tab whose bytes are being sent
 * just now (its stream may stand between the two bytes of a doubled
 * 255); ssh leaves alone a connection waiting for its socket. */
static void window_resized(void)
{
    struct Tab *keep = T;
    int i;
    layout_and_resize();
    painter_setup();
    redraw_all();
    RefreshWindowFrame(win);
    for (i = 0; i < ntabs; i++) {
        T = tabs[i];
        if (sock >= 0 && T != sending)
            send_naws();
        if (T->t_fui)
            files_resized(T);
    }
    T = keep;
}

/* A line typed in the window while ssh sets up (a password: echo off),
 * or an answer to a question. FALSE: the window was closed. */
__attribute__((noinline)) BOOL term_ask(const char *prompt, char *buf, int max,
                                        BOOL echo)
{
    int n = 0;
    if (!echo && T->t_pass[0] && !T->t_passed) {
        /* the phone book's password, once: asked again, the person types */
        T->t_passed = TRUE;
        strncpy(buf, T->t_pass, max - 1);
        buf[max - 1] = 0;
        say(prompt);
        say("(from the phone book)\r\n");
        refresh();
        return TRUE;
    }
    if (!win)
        return FALSE;                   /* hidden: nobody to type it */
    say(prompt);
    refresh();
    for (;;) {
        struct IntuiMessage *m;
        ULONG wsig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(wsig | shells_sigmask());
        if (got & ~wsig)
            shells_handle(got);         /* the other tabs go on */
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cl = m->Class;
            if (cl == IDCMP_RAWKEY && !(m->Code & IECODE_UP_PREFIX)) {
                struct InputEvent ie;
                UBYTE k[8];
                int got, i;
                memset(&ie, 0, sizeof ie);
                ie.ie_Class = IECLASS_RAWKEY;
                ie.ie_Code = m->Code;
                ie.ie_Qualifier = m->Qualifier;
                ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
                got = MapRawKey(&ie, (STRPTR)k, sizeof k, NULL);
                ReplyMsg((struct Message *)m);
                for (i = 0; i < got; i++) {
                    if (k[i] == '\r' || k[i] == '\n') {
                        buf[n] = 0;
                        say("\r\n");
                        refresh();
                        return TRUE;
                    }
                    if (k[i] == 8 && n > 0) {
                        n--;
                        if (echo) say("\b \b");
                    } else if (k[i] >= 32 && n < max - 1) {
                        buf[n++] = k[i];
                        if (echo) {
                            char e[2] = { (char)k[i], 0 };
                            say(e);
                        }
                    }
                    refresh();
                }
                continue;
            }
            if (cl == IDCMP_CLOSEWINDOW && m->Code != 1) {
                ReplyMsg((struct Message *)m);
                buf[0] = 0;
                return FALSE;
            }
            window_chore(m);            /* drawn again, a new size */
        }
    }
}

/* Call the host: telnet, or ssh (which asks what it must in the window).
 * The TCP/IP stack is opened the first time a call needs it: a CTerm
 * started as a Shell has none open (and a call must not use a NULL base). */
BOOL connect_out(void)
{
    if (T->t_serial) {
        refresh();
        return serial_call();
    }
    if (!SocketBase)
        SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        say("\x1b[1;31m[a call needs a TCP/IP stack (bsdsocket.library)]"
            "\x1b[0m\r\n");
        return FALSE;
    }
    refresh();
    if (T->t_fkind)                     /* SFTP, FTP: the file browser */
        return files_call();
    if (!sshmode)
        return dial();
    {
        int c0 = vt_cols(vt), r0 = vt_rows(vt);
        T->ssh = ssh_open(dialhost, c0, r0, term_ask, say);
        if (!T->ssh)
            return FALSE;
        sock = ssh_socket(T->ssh);
        if (vt_cols(vt) != c0 || vt_rows(vt) != r0)
            send_naws();                /* resized while it was set up */
    }
    return TRUE;
}

/* Who may call a listening CTerm: the one address in RELAY_FROM, the
 * PC's relay. With no such file the person is asked, once, and the
 * answer kept; so no one else on the network can show things in this
 * window or read what is typed into it. TRUE: let the call in. */
#define RELAY_DIR  "ENVARC:CTerm"
#define RELAY_FROM "ENVARC:CTerm/relay_from"

static BOOL relay_allowed(ULONG addr)
{
    static ULONG told;                  /* refused, and said so: once */
    char line[40], ip[20], a[8];
    BPTR f = Open((STRPTR)RELAY_FROM, MODE_OLDFILE);
    int i;
    strncpy(ip, (char *)Inet_NtoA(addr), sizeof ip - 1);
    ip[sizeof ip - 1] = 0;
    if (f) {
        LONG n = Read(f, line, sizeof line - 1);
        Close(f);
        line[n > 0 ? n : 0] = 0;
        for (i = 0; line[i] && line[i] != '\n' && line[i] != '\r' &&
                    line[i] != ' '; i++)
            ;
        line[i] = 0;
        if (inet_addr((STRPTR)line) == addr)
            return TRUE;
        if (told != addr) {
            told = addr;
            say("\x1b[1;31m[a call from ");
            say(ip);
            say(" was refused: only ");
            say(line);
            say(" may call (delete " RELAY_FROM " to choose again)]"
                "\x1b[0m\r\n");
            refresh();
        }
        return FALSE;
    }
    if (!pane_of(T))                    /* the question where it is seen */
        for (i = 0; i < ntabs; i++)
            if (tabs[i] == T)
                tab_show(i);
    say("\x1b[1m[a call from ");
    say(ip);
    say(" - the PC's relay?]\x1b[0m\r\n");
    if (!term_ask("Let it in, now and from now on? (y/n) ", a, sizeof a,
                  TRUE) || (a[0] != 'y' && a[0] != 'Y')) {
        say("\x1b[2m[refused - waiting for the PC (cterm-relay)]"
            "\x1b[0m\r\n");
        refresh();
        return FALSE;
    }
    {
        BPTR d = Lock((STRPTR)RELAY_DIR, ACCESS_READ);
        if (!d)
            d = CreateDir((STRPTR)RELAY_DIR);
        if (d)
            UnLock(d);
    }
    if ((f = Open((STRPTR)RELAY_FROM, MODE_NEWFILE))) {
        Write(f, ip, strlen(ip));
        Write(f, "\n", 1);
        Close(f);
    }
    return TRUE;
}

void connected(void)
{
    LONG one = 1, s;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;
    s = accept(lfd, (struct sockaddr *)&peer, &len);
    if (s < 0)
        return;                         /* gone before it was taken */
    if (peer.sin_family != AF_INET || !relay_allowed(peer.sin_addr.s_addr)) {
        CloseSocket(s);                 /* still listening for the PC */
        return;
    }
    sock = s;
    CloseSocket(lfd);
    lfd = -1;
    setsockopt(sock, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
    tstate = 0;
    tel_opts(TRUE);
    /* the size, once the relay says DO (it always asks for it) */
    tel_will(O_NAWS);
}

void hung_up(void)
{
    rexx_tab_over(T);                   /* a WAITFOR's text will not come */
    if (sshmode) {
        ssh_close(T->ssh);              /* it owns the socket */
        T->ssh = NULL;
    }
    else
        CloseSocket(sock);
    sock = -1;
    if (dialhost[0]) {
        say("\r\n\x1b[0;2m[the connection closed - Return calls again]"
            "\x1b[0m\r\n");
        return;
    }
    say("\r\n\x1b[0;2m[the connection closed - waiting for it again]"
        "\x1b[0m\r\n");
    lfd = listen_on();
}

/* A program beside ours (NULL: ours), to start another CTerm with. */
void own_path(char *buf, int max, const char *sibling)
{
    char name[108];
    BPTR dir = GetProgramDir();
    buf[0] = 0;
    if (!GetProgramName((STRPTR)name, sizeof name) || !name[0])
        strcpy(name, "CTerm");
    if (sibling)
        strcpy(name, sibling);
    if (dir && NameFromLock(dir, (STRPTR)buf, max)) {
        if (!AddPart((STRPTR)buf, FilePart((STRPTR)name), max))
            strcpy(buf, name);
    } else
        strncpy(buf, name, max - 1);
}


/* A tab as a phone book entry says: where, how, who, which letters. The
 * emulator may not be there yet (CTerm's start): then only the flags. */
void tab_from_book(const BookEntry *e)
{
    local = FALSE;
    if (e->serial)
        serial_tab_book(e);
    book_target(e, dialhost, sizeof dialhost);
    strcpy(dialtitle, "CTerm: ");
    strncat(dialtitle, e->name[0] ? e->name : dialhost,
            sizeof dialtitle - 10);
    sshmode = e->ssh && !e->serial;
    pcmode = e->chars == BC_PC;
    utf8 = e->chars == BC_UTF8;
    if (vt) {
        if (pcmode)
            vt_set_cp437(vt, 1);
        else
            vt_set_utf8(vt, utf8);
    }
    strcpy(T->t_user, e->user);
    strcpy(T->t_pass, e->pass);
    strcpy(T->t_bookname, e->name);
    T->t_passed = FALSE;
    T->t_letters = e->letters;
    strcpy(T->t_down, e->down);
    strcpy(T->t_up, e->up);
    strcpy(T->t_script, e->script);
    T->t_fkind = e->serial ? FK_NONE : e->fkind;
    strcpy(T->t_fdir, e->dir);
}

/* Another CTerm (or CTermSSH) for a place: BOOK "name", or for a file
 * browser's place that is not in the book (a URL typed) SFTP or FTP and
 * its address. */
static void call_start_book(const BookEntry *e)
{
    char cmd[640], path[256];
    BPTR nil;
#ifdef NO_SSH
    own_path(path, sizeof path, e->ssh ? "CTermSSH" : NULL);
#else
    own_path(path, sizeof path, NULL);
#endif
    strcpy(cmd, "Run >NIL: \"");
    strcat(cmd, path);
    if (e->name[0] || !e->fkind) {
        strcat(cmd, "\" BOOK \"");
        strcat(cmd, e->name);
    } else {
        strcat(cmd, e->fkind == FK_SFTP ? "\" SFTP \"" : "\" FTP \"");
        if (e->fkind == FK_FTP && e->user[0]) {
            strcat(cmd, e->user);
            strcat(cmd, "@");
        }
        book_target(e, cmd + strlen(cmd), 200);
        strcat(cmd, e->dir);
    }
    strcat(cmd, "\"");
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                   TAG_END) != 0 && nil)
        Close(nil);
}

/* Call a place: in a new tab of this window when it can be. */
void call_place(const BookEntry *e, BOOL newtab)
{
    struct Tab *t;
#ifdef NO_SSH
    if (e->ssh)
        newtab = FALSE;                 /* ssh is CTermSSH's: a window */
#endif
    if (!newtab || !(t = tab_new())) {
        call_start_book(e);
        return;
    }
    T = t;
    tab_from_book(e);
    tab_show(ntabs - 1);
    say("\x1b[2m[calling ");
    say(e->name[0] ? e->name : dialhost);
    say("... Esc gives up]\x1b[0m\r\n");
    refresh();
    if (!connect_out())
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    else
        rexx_after_call();              /* its login script */
    refresh();
}

/* Call phone book entry i. */
void call_book(int i, BOOL newtab)
{
    call_place(&book[i], newtab);
}

/* ARexx's CALL: a phone book place, host[:port] or SERIAL, in a new tab
 * (a place on ssh needs CTermSSH: not in CTerm's tabs). The RC: 0 up,
 * 10 not; *made: the tab (NULL: none was made). */
int call_rexx(const char *what, struct Tab **made)
{
    int i = book_find(what);
    struct Tab *t;
    BookEntry *u = NULL;
    BOOL ok;
    *made = NULL;
    if (i < 0 && strstr(what, "://") &&
        (u = AllocVec(sizeof *u, MEMF_ANY)))
        book_url(u, what, FK_NONE);     /* ftp://, sftp://, ssh://... */
#ifdef NO_SSH
    if ((i >= 0 && book[i].ssh && !book[i].serial) || (u && u->ssh)) {
        if (u)
            FreeVec(u);
        return 10;
    }
#endif
    if (!what[0] || !(t = tab_new())) {
        if (u)
            FreeVec(u);
        return 10;
    }
    T = t;
    *made = t;
    rexx_watch(t);                      /* the board's first words count */
    if (u) {
        tab_from_book(u);
        FreeVec(u);
    } else if (i >= 0)
        tab_from_book(&book[i]);
    else if (!stricmp((char *)what, "SERIAL"))
        serial_tab_setup(NULL, NULL, NULL);
    else {
        local = FALSE;
        strncpy(dialhost, what, sizeof dialhost - 1);
        strcpy(dialtitle, "CTerm: ");
        strncat(dialtitle, dialhost, sizeof dialtitle - 10);
    }
    tab_show(ntabs - 1);
    say(T->t_serial && !T->t_st.phone[0] ? "\x1b[2m[opening "
                                         : "\x1b[2m[calling ");
    say(i >= 0 ? book[i].name : dialhost);
    say("... Esc gives up]\x1b[0m\r\n");
    refresh();
    ok = connect_out();
    if (!ok)
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    refresh();
    return ok ? 0 : 10;
}

/* The phone book's login, typed to the host: the name or the password,
 * and Return. */
void send_login(BOOL pass)
{
    const char *s = pass ? T->t_pass : T->t_user;
    if (local || !linked || !s[0]) {
        beep();
        return;
    }
    send_data((UBYTE *)s, strlen(s));
    send_data((UBYTE *)"\r", 1);
}

