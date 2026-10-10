/*
 * net - telnet over TCP, calling out, the Call window and the phone book
 * calls.
 */
#include "cterm.h"

static void send_raw(const UBYTE *b, LONG n);
static void telnet_option(int cmd, int opt);
static BOOL dial(void);
static BOOL term_ask(const char *prompt, char *buf, int max, BOOL echo);
static void window_resized(void);
static int calls_split(void);
static void calls_add(const char *entry);
static void call_start(const char *host, BOOL pc, BOOL ssh);
static void call_parse(const char *entry, char *host, BOOL *pc, BOOL *ssh);
static void call_tab(const char *host, BOOL pc, BOOL ssh);
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
        if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_RAWKEY && code == 0x45)) {
            ReplyMsg((struct Message *)m);
            out = TRUE;
        } else
            window_chore(m);
    }
    return out;
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

LONG net_connect(const char *host, LONG p, ssh_say_fn sayf)
{
    struct hostent *he;
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
        /* the one wait left that cannot be cut short: the stack's own */
        he = gethostbyname((STRPTR)host);
        if (!he) {
            sayf("\x1b[1;31m[no such host]\x1b[0m\r\n");
            return -1;
        }
        memcpy(&sa.sin_addr, he->h_addr, 4);
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
    if (sshmode) {                      /* the pty's size, ssh's way */
        ssh_resize(T->ssh, cols, rows);
        return;
    }
    o = tel_opts(FALSE);
    if (!o || o->us[Q_NAWS] != Q_YES)
        return;
    v[0] = cols >> 8; v[1] = cols & 255;
    v[2] = rows >> 8; v[3] = rows & 255;
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
    }
    T = keep;
}

/* A line typed in the window while ssh sets up (a password: echo off),
 * or an answer to a question. FALSE: the window was closed. */
static BOOL term_ask(const char *prompt, char *buf, int max, BOOL echo)
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
            if (cl == IDCMP_CLOSEWINDOW) {
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
    if (!SocketBase)
        SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        say("\x1b[1;31m[a call needs a TCP/IP stack (bsdsocket.library)]"
            "\x1b[0m\r\n");
        return FALSE;
    }
    refresh();
    if (!sshmode)
        return dial();
    {
        int c0 = cols, r0 = rows;
        T->ssh = ssh_open(dialhost, cols, rows, term_ask, say);
        if (!T->ssh)
            return FALSE;
        sock = ssh_socket(T->ssh);
        if (cols != c0 || rows != r0)   /* resized while it was set up */
            send_naws();
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
    if (shown != T)                     /* the question where it is seen */
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
#define NCALLS 8

/* The recent calls as a list for the list view. */
static struct List calllist;
static struct Node callnode[NCALLS];
static char callname[NCALLS][64];

static int calls_split(void)
{
    const char *p = calls;
    int n = 0;
    calllist.lh_Head = (struct Node *)&calllist.lh_Tail;  /* NewList */
    calllist.lh_Tail = NULL;
    calllist.lh_TailPred = (struct Node *)&calllist.lh_Head;
    while (*p && n < NCALLS) {
        int k = 0;
        while (*p && *p != ';' && k < 63)
            callname[n][k++] = *p++;
        callname[n][k] = 0;
        while (*p && *p != ';')
            p++;
        if (*p == ';')
            p++;
        if (k) {
            callnode[n].ln_Name = callname[n];
            AddTail(&calllist, &callnode[n]);
            n++;
        }
    }
    return n;
}

/* host (with a ! when PC) to the front of the recent calls. */
static void calls_add(const char *entry)
{
    char out[sizeof calls];
    int i, n = calls_split();
    strcpy(out, entry);
    for (i = 0; i < n; i++) {
        if (!stricmp(callname[i], entry))
            continue;
        if (strlen(out) + strlen(callname[i]) + 2 >= sizeof out)
            break;
        strcat(out, ";");
        strcat(out, callname[i]);
    }
    strcpy(calls, out);
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

static void call_start(const char *host, BOOL pc, BOOL ssh)
{
    char cmd[400], path[256];
    BPTR nil;
#ifdef NO_SSH
    own_path(path, sizeof path, ssh ? "CTermSSH" : NULL);
#else
    own_path(path, sizeof path, NULL);
#endif
    strcpy(cmd, "Run >NIL: \"");
    strcat(cmd, path);
    strcat(cmd, ssh ? "\" SSH \"" : "\" HOST \"");
    strcat(cmd, host);
    strcat(cmd, pc ? "\" PC" : "\"");
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                   TAG_END) != 0 && nil)
        Close(nil);
}

/* A recent call's text: "ssh:user@host", or "host" with "!" for a PC BBS. */
static void call_parse(const char *entry, char *host, BOOL *pc, BOOL *ssh)
{
    *ssh = !strncmp(entry, "ssh:", 4);
    strcpy(host, *ssh ? entry + 4 : entry);
    *pc = FALSE;
    if (host[0] && host[strlen(host) - 1] == '!') {
        host[strlen(host) - 1] = 0;
        *pc = TRUE;
    }
}

/* A call in a new tab of this window. */
static void call_tab(const char *host, BOOL pc, BOOL ssh)
{
    struct Tab *t = tab_new();
    if (!t) {
        call_start(host, pc, ssh);      /* no room: a window of its own */
        return;
    }
    T = t;
    local = FALSE;
    strncpy(dialhost, host, sizeof dialhost - 1);
    strcpy(dialtitle, ssh ? "CTerm: ssh " : "CTerm: ");
    strncat(dialtitle, dialhost, sizeof dialtitle - 20);
    sshmode = ssh;
    pcmode = pc;
    if (pc)
        vt_set_cp437(vt, 1);
    tab_show(ntabs - 1);
    say("\x1b[2m[calling ");
    say(dialhost);
    say("... Esc gives up]\x1b[0m\r\n");
    refresh();
    if (!connect_out())
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    refresh();
}

/* A tab as a phone book entry says: where, how, who, which letters. The
 * emulator may not be there yet (CTerm's start): then only the flags. */
void tab_from_book(const BookEntry *e)
{
    local = FALSE;
    book_target(e, dialhost, sizeof dialhost);
    strcpy(dialtitle, "CTerm: ");
    strncat(dialtitle, e->name[0] ? e->name : dialhost,
            sizeof dialtitle - 10);
    sshmode = e->ssh;
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
}

/* Another CTerm (or CTermSSH) for a phone book entry: BOOK "name". */
static void call_start_book(const BookEntry *e)
{
    char cmd[400], path[256];
    BPTR nil;
#ifdef NO_SSH
    own_path(path, sizeof path, e->ssh ? "CTermSSH" : NULL);
#else
    own_path(path, sizeof path, NULL);
#endif
    strcpy(cmd, "Run >NIL: \"");
    strcat(cmd, path);
    strcat(cmd, "\" BOOK \"");
    strcat(cmd, e->name);
    strcat(cmd, "\"");
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                   TAG_END) != 0 && nil)
        Close(nil);
}

/* Call phone book entry i: in a new tab of this window when it can be. */
void call_book(int i, BOOL newtab)
{
    const BookEntry *e = &book[i];
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
    say(e->name);
    say("... Esc gives up]\x1b[0m\r\n");
    refresh();
    if (!connect_out())
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    refresh();
}

/* The phone book's login, typed to the host: the name or the password,
 * and Return. */
void send_login(BOOL pass)
{
    const char *s = pass ? T->t_pass : T->t_user;
    if (local || sock < 0 || !s[0]) {
        DisplayBeep(scr);
        return;
    }
    send_data((UBYTE *)s, strlen(s));
    send_data((UBYTE *)"\r", 1);
}

void call_dialog(void)
{
    enum { G_HOST, G_PC, G_SSH, G_NEWTAB, G_LIST, G_CALL, G_CANCEL };
    struct Gadget *glist = NULL, *g, *gad[7];
    struct NewGadget ng;
    struct Window *dw;
    struct TextAttr *ta = scr->Font;
    int th = ta->ta_YSize, n, W, H, x0, y, bw;
    BOOL pc = pcmode, ssh = FALSE, done = FALSE, go = FALSE;
    static BOOL newtab = TRUE;
    char host[128], booked[136];
    ULONG dsig;
    static struct Node booknode[BOOK_MAX];
    int nb, pick = -1, i;

    n = calls_split();
    host[0] = 0;
    if (n)                              /* the last place, ready to call */
        call_parse(callname[0], host, &pc, &ssh);
    /* the phone book first, then the recent calls */
    nb = nbook;
    for (i = nb - 1; i >= 0; i--) {
        booknode[i].ln_Name = book[i].name;
        AddHead(&calllist, &booknode[i]);
    }
    x0 = scr->WBorLeft + 8 + TextLength(&scr->RastPort, (STRPTR)"Recent", 6) + 8;
    W = x0 + 300 + scr->WBorRight + 8;
    bw = 90;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = ta;
    ng.ng_VisualInfo = visual;
    g = CreateContext(&glist);
    y = scr->WBorTop + th + 1 + 6;

    ng.ng_LeftEdge = x0; ng.ng_TopEdge = y; ng.ng_Width = 260;
    ng.ng_Height = th + 6; ng.ng_GadgetText = (STRPTR)"Host";
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = G_HOST;
    g = gad[G_HOST] = CreateGadget(STRING_KIND, g, &ng, GTST_String,
                                   (ULONG)host, GTST_MaxChars, 120, TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"PC BBS (CP437)"; ng.ng_Flags = PLACETEXT_RIGHT;
    ng.ng_GadgetID = G_PC;
    g = gad[G_PC] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, pc,
                                 GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0 + 150; ng.ng_GadgetText = (STRPTR)"ssh (user@host)";
    ng.ng_GadgetID = G_SSH;
    g = gad[G_SSH] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, ssh,
                                  GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0;
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"in a new tab of this window";
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_GadgetID = G_NEWTAB;
    g = gad[G_NEWTAB] = CreateGadget(CHECKBOX_KIND, g, &ng,
                                     GTCB_Checked, newtab, GTCB_Scaled, TRUE,
                                     TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 260; ng.ng_Height = (th + 2) * 5 + 4;
    ng.ng_GadgetText = (STRPTR)(nb ? "Book" : "Recent");
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = G_LIST;
    g = gad[G_LIST] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels,
                                   (ULONG)&calllist, TAG_END);
    y += ng.ng_Height + 8;
    ng.ng_TopEdge = y; ng.ng_Width = bw; ng.ng_Height = th + 6;
    ng.ng_GadgetText = (STRPTR)"Call"; ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = G_CALL; ng.ng_LeftEdge = scr->WBorLeft + 8;
    g = gad[G_CALL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    ng.ng_LeftEdge = W - scr->WBorRight - 8 - bw;
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = G_CANCEL;
    g = gad[G_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    H = y + th + 6 + 6 + scr->WBorBottom;
    if (!g) {
        FreeGadgets(glist);
        return;
    }
    dw = OpenWindowTags(NULL,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
        WA_Width, W, WA_Height, H, WA_Title, (ULONG)"CTerm: Call",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_IDCMP, STRINGIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP | BUTTONIDCMP |
                  IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_END);
    if (!dw) {
        FreeGadgets(glist);
        return;
    }
    GT_RefreshWindow(dw, NULL);
    ActivateGadget(gad[G_HOST], dw, NULL);
    dsig = 1UL << dw->UserPort->mp_SigBit;
    while (!done) {
        struct IntuiMessage *m;
        ULONG sigs = Wait(dsig | shells_sigmask());
        if (sigs & ~dsig) {
            /* the Shells go on meanwhile */
            shells_handle(sigs);
            if (con_render_due() && owed)
                draw_now();
        }
        while ((m = GT_GetIMsg(dw->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *src = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            switch (cl) {
            case IDCMP_CLOSEWINDOW: done = TRUE; break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(dw);
                GT_EndRefresh(dw, TRUE);
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27) done = TRUE;
                break;
            case IDCMP_GADGETUP:
                switch (src->GadgetID) {
                case G_HOST:            /* Return in the host field */
                    if (code != 9) go = done = TRUE;
                    break;
                case G_PC: pc = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_SSH: ssh = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_NEWTAB:
                    newtab = (src->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_LIST:
                    pick = -1;
                    if (code < nb) {        /* a phone book entry */
                        const BookEntry *e = &book[code];
                        pick = code;
                        book_target(e, host, sizeof host);
                        strcpy(booked, host);
                        pc = e->chars == BC_PC;
                        ssh = e->ssh;
                        GT_SetGadgetAttrs(gad[G_HOST], dw, NULL,
                                          GTST_String, (ULONG)host, TAG_END);
                        GT_SetGadgetAttrs(gad[G_PC], dw, NULL,
                                          GTCB_Checked, pc, TAG_END);
                        GT_SetGadgetAttrs(gad[G_SSH], dw, NULL,
                                          GTCB_Checked, ssh, TAG_END);
                    } else if ((code -= nb) < n) {
                        call_parse(callname[code], host, &pc, &ssh);
                        GT_SetGadgetAttrs(gad[G_HOST], dw, NULL,
                                          GTST_String, (ULONG)host, TAG_END);
                        GT_SetGadgetAttrs(gad[G_PC], dw, NULL,
                                          GTCB_Checked, pc, TAG_END);
                        GT_SetGadgetAttrs(gad[G_SSH], dw, NULL,
                                          GTCB_Checked, ssh, TAG_END);
                    }
                    break;
                case G_CALL: go = done = TRUE; break;
                case G_CANCEL: done = TRUE; break;
                }
                break;
            }
        }
    }
    if (go) {
        STRPTR s0 = ((struct StringInfo *)gad[G_HOST]->SpecialInfo)->Buffer;
        strncpy(host, (char *)s0, sizeof host - 2);
        host[sizeof host - 2] = 0;
    }
    CloseWindow(dw);
    FreeGadgets(glist);
    if (go && pick >= 0 && !strcmp(host, booked)) {
        call_book(pick, newtab);        /* as the entry says, all of it */
        return;
    }
    if (go && host[0]) {
        char entry[136];
        strcpy(entry, ssh ? "ssh:" : "");
        strcat(entry, host);
        if (pc)
            strcat(entry, "!");
        calls_add(entry);
        save_prefs();
#ifdef NO_SSH
        if (newtab && !ssh)             /* ssh is CTermSSH's: a window */
#else
        if (newtab)
#endif
            call_tab(host, pc, ssh);
        else
            call_start(host, pc, ssh);
    }
}
