/*
 * transfer - file transfers in a host's tab, CTerm's side: it notices a
 * Zmodem start in what the host sends, then gives the protocol engine
 * (zmodem.c) the connection (telnet or ssh) and AmigaDOS files. Downloads
 * go to the tab's phone book drawer, else the Files tab's, else
 * PROGDIR:Downloads; uploads are chosen in a file requester. The window
 * title shows how it goes; Esc stops it. The other tabs' Shells go on.
 */
#include "cterm.h"
#include "xfer.h"

static int xstop;                       /* Esc or the close gadget */
static int xdamaged;                    /* the window wants drawing again */
static int xtel;                      /* telnet's parser, for the transfer */
static ULONG t0, tlast;                 /* when it began, last shown (ticks) */
static char xdir[256];
static ULONG xfiles, xbytes;

static ULONG ticks_now(void)            /* 1/50 s */
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 4320000UL + (ULONG)ds.ds_Minute * 3000UL +
           (ULONG)ds.ds_Tick;
}

/* The window's messages while a transfer runs: Esc and the close gadget
 * stop it, the rest waits for later (dropped). */
static void x_events(void)
{
    struct IntuiMessage *m;
    while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        if ((m->Class == IDCMP_RAWKEY && m->Code == 0x45) ||
            m->Class == IDCMP_CLOSEWINDOW)
            xstop = 1;
        if (m->Class == IDCMP_REFRESHWINDOW || m->Class == IDCMP_NEWSIZE)
            xdamaged = 1;               /* drawn whole when it is over */
        if (m->Class == IDCMP_REFRESHWINDOW) {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
        }
        ReplyMsg((struct Message *)m);
    }
}

/* Telnet off the bytes: IAC IAC is 0xff, commands and SB...SE go. */
static LONG tel_decode(UBYTE *b, LONG n)
{
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = b[i];
        switch (xtel) {
        case 0:
            if (c == 255) xtel = 1;
            else b[k++] = c;
            break;
        case 1:
            if (c == 255) { b[k++] = 255; xtel = 0; }
            else if (c >= 251 && c <= 254) xtel = 2;
            else if (c == 250) xtel = 3;
            else xtel = 0;
            break;
        case 2:                         /* WILL/WONT/DO/DONT x: not now */
            xtel = 0;
            break;
        case 3:                         /* SB ... IAC SE */
            if (c == 255) xtel = 4;
            break;
        case 4:
            xtel = c == 240 ? 0 : 3;
            break;
        }
    }
    return k;
}

static long x_get(void *u, unsigned char *b, long max, long ms)
{
    ULONG winsig = 1UL << win->UserPort->mp_SigBit;
    ULONG until = ticks_now() + (ULONG)(ms / 20);
    (void)u;
    for (;;) {
        fd_set rd;
        struct timeval tv;
        ULONG sigs, now;
        LONG n, k;
        if (sshmode) {
            k = ssh_read(T->ssh, b, max);
            if (k != 0)
                return k < 0 ? -1 : k;
        }
        now = ticks_now();
        if (now >= until && ms > 0)
            return 0;
        tv.tv_sec = ms > 0 ? (until - now) / 50 : 0;
        tv.tv_usec = ms > 0 ? ((until - now) % 50) * 20000 : 0;
        FD_ZERO(&rd);
        FD_SET(sock, &rd);
        sigs = winsig | shells_sigmask();
        n = WaitSelect(sock + 1, &rd, NULL, NULL, &tv, &sigs);
        if (sigs & ~winsig)
            shells_handle(sigs);        /* the other tabs go on */
        if (sigs & winsig)
            x_events();
        if (xstop)
            return 0;                   /* x_stop says why */
        if (n < 0)
            return -1;
        if (n == 0) {
            if (ms == 0 || ticks_now() >= until)
                return 0;
            continue;                   /* a signal, not data */
        }
        if (sshmode)
            continue;                   /* ssh_read above */
        k = recv(sock, b, max, 0);
        if (k <= 0)
            return -1;
        k = tel_decode(b, k);
        if (k > 0)
            return k;
    }
}

static void x_put(void *u, const unsigned char *b, long n)
{
    (void)u;
    send_data(b, n);                    /* doubles IAC; ssh writes it */
}

static int x_stop(void *u)
{
    (void)u;
    return xstop;
}

static void x_show(void *u, const char *name, unsigned long done,
                   unsigned long size, int what)
{
    static char t[200];
    ULONG now = ticks_now(), secs;
    (void)u;
    if (what == 0 && now - tlast < 10)  /* a few times a second */
        return;
    tlast = now;
    secs = (now - t0) / 50;
    strcpy(t, "CTerm: ");
    strncat(t, name, 60);
    strcat(t, what == 1 ? " done, " : what == 2 ? " skipped, "
              : what == 3 ? " FAILED at " : " ");
    numstr_cat(t, done / 1024);
    strcat(t, " KB");
    if (size) {
        strcat(t, " of ");
        numstr_cat(t, size / 1024);
        strcat(t, " KB (");
        numstr_cat(t, size ? (LONG)((done / 1024) * 100 / (size / 1024 + 1))
                           : 0);
        strcat(t, "%)");
    }
    if (secs) {
        strcat(t, ", ");
        numstr_cat(t, (LONG)(done / 1024 / secs));
        strcat(t, " KB/s");
    }
    if (what == 0)
        strcat(t, " - Esc stops");
    SetWindowTitles(win, (UBYTE *)t, (UBYTE *)-1);
    if (what == 1) {
        xfiles++;
        xbytes += done;
        say("\x1b[2m[");
        say(name);
        say(" done]\x1b[0m\r\n");
    } else if (what == 3) {
        say("\x1b[1;31m[");
        say(name);
        say(" failed]\x1b[0m\r\n");
    }
}

/* ---- AmigaDOS files ---- */

static BOOL exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l)
        UnLock(l);
    return l != 0;
}

static void *x_wopen(void *u, const char *name, unsigned long size,
                     char *made, int madelen)
{
    static char path[400];
    char nm[112];
    int n = 0;
    BPTR f;
    (void)u; (void)size;
    strncpy(nm, name, 100);
    nm[100] = 0;
    for (;;) {                          /* never over a file that is there */
        strcpy(path, xdir);
        AddPart((STRPTR)path, (STRPTR)nm, sizeof path);
        if (!exists(path))
            break;
        if (++n > 99)
            return NULL;
        strncpy(nm, name, 96);
        nm[96] = 0;
        strcat(nm, ".");
        numstr_cat(nm, n);
    }
    f = Open((STRPTR)path, MODE_NEWFILE);
    strncpy(made, nm, madelen - 1);
    made[madelen - 1] = 0;
    return f ? (void *)f : NULL;
}

static long x_wwrite(void *u, void *f, const unsigned char *b, long n)
{
    (void)u;
    return Write((BPTR)f, (APTR)b, n);
}

static void x_wclose(void *u, void *f, int ok)
{
    (void)u; (void)ok;                  /* a part is kept: it may resume */
    Close((BPTR)f);
}

static void *x_ropen(void *u, const char *path, unsigned long *size)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ), f;
    struct FileInfoBlock *fib;
    (void)u;
    if (!l)
        return NULL;
    fib = AllocDosObject(DOS_FIB, NULL);
    *size = fib && Examine(l, fib) ? (unsigned long)fib->fib_Size : 0;
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    UnLock(l);
    f = Open((STRPTR)path, MODE_OLDFILE);
    return f ? (void *)f : NULL;
}

static long x_rread(void *u, void *f, unsigned char *b, long n)
{
    (void)u;
    return Read((BPTR)f, b, n);
}

static int x_rseek(void *u, void *f, unsigned long pos)
{
    (void)u;
    return Seek((BPTR)f, (LONG)pos, OFFSET_BEGINNING) < 0;
}

static void x_rclose(void *u, void *f)
{
    (void)u;
    Close((BPTR)f);
}

/* ---- starting one ---- */

/* The drawer downloads go to: the tab's own, the Files tab's, or
 * PROGDIR:Downloads (made when missing). */
static void down_dir(void)
{
    BPTR l;
    strcpy(xdir, T->t_down[0] ? T->t_down : dldir[0] ? dldir
                                          : "PROGDIR:Downloads");
    l = Lock((STRPTR)xdir, ACCESS_READ);
    if (!l)
        l = CreateDir((STRPTR)xdir);
    if (l)
        UnLock(l);
}

/* Xmodem brings no name: the person gives one (in the download drawer). */
static BOOL ask_name(char *name, int max)
{
    struct FileRequester *req;
    BOOL ok = FALSE;
    if (!asl_open())
        return FALSE;
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: Xmodem - the file's name",
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialDrawer, (ULONG)xdir,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return FALSE;
    if (AslRequest(req, NULL) && req->fr_File[0]) {
        strncpy(xdir, (char *)req->fr_Drawer, sizeof xdir - 1);
        xdir[sizeof xdir - 1] = 0;
        strncpy(name, (char *)req->fr_File, max - 1);
        name[max - 1] = 0;
        ok = TRUE;
    }
    FreeAslRequest(req);
    return ok;
}

/* Files for an upload, chosen in a requester (several at once). */
#define NUP 32
static char (*store)[256];              /* the chosen paths */
static int nhq;                         /* Hydra's queue: store[0..nhq-1] */

void transfer_free(void)
{
    if (store)
        FreeVec(store);
    store = NULL;
}

static int ask_uploads(const char **paths, const char *title)
{
    struct FileRequester *req;
    int n = 0, i;
    const char *dir = T->t_up[0] ? T->t_up : uldir[0] ? uldir : "";
    if (!asl_open())
        return 0;
    if (!store && !(store = AllocVec(NUP * 256, MEMF_ANY)))
        return 0;                       /* kept for the next time */
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)title,
        dir[0] ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)dir,
        ASLFR_DoMultiSelect, TRUE, ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return 0;
    if (AslRequest(req, NULL)) {
        if (req->fr_NumArgs > 0) {
            for (i = 0; i < req->fr_NumArgs && n < NUP; i++) {
                strncpy(store[n], (char *)req->fr_Drawer, 255);
                store[n][255] = 0;
                AddPart((STRPTR)store[n], req->fr_ArgList[i].wa_Name, 256);
                paths[n] = store[n];
                n++;
            }
        } else if (req->fr_File[0]) {
            strncpy(store[0], (char *)req->fr_Drawer, 255);
            store[0][255] = 0;
            AddPart((STRPTR)store[0], req->fr_File, 256);
            paths[n++] = store[0];
        }
    }
    FreeAslRequest(req);
    return n;
}

static void begin(XferIO *io)
{
    memset(io, 0, sizeof *io);
    io->get = x_get; io->put = x_put; io->stop = x_stop; io->show = x_show;
    io->wopen = x_wopen; io->wwrite = x_wwrite; io->wclose = x_wclose;
    io->ropen = x_ropen; io->rread = x_rread; io->rseek = x_rseek;
    io->rclose = x_rclose;
    xstop = xdamaged = 0;
    xfiles = xbytes = 0;
    xtel = 0;
    t0 = tlast = ticks_now();
}

/* How it ended, in the tab; the window again. */
static void finish(int got, BOOL damaged)
{
    if (got < 0)
        say(xstop ? "\x1b[1;31m[transfer stopped]\x1b[0m\r\n"
                  : "\x1b[1;31m[transfer failed]\x1b[0m\r\n");
    else if (got > 0) {
        char s[80];
        strcpy(s, "\x1b[2m[");
        numstr_cat(s, got);
        strcat(s, got == 1 ? " file, " : " files, ");
        numstr_cat(s, (LONG)(xbytes / 1024));
        strcat(s, " KB]\x1b[0m\r\n");
        say(s);
    }
    /* a requester was over the window, or it was hidden: all of it again
     * - SIMPLE_REFRESH keeps nothing */
    if (damaged || xdamaged)
        redraw_all();
    title_update_now();
    refresh();
}

/* Transfer/Receive and Send with Ymodem or Xmodem: the person starts them
 * (after starting the other side on the host). */
void transfer_menu(int what)
{
    static XferIO io;
    const char *paths[NUP];
    int got = -1, np;
    BOOL ymodem = what == M_YRECV || what == M_YSEND;
    if (what == M_HSEND) {
        /* Hydra starts from the host; these wait for its next session */
        nhq = 0;
        np = ask_uploads(paths, "CTerm: files to send with Hydra");
        nhq = np;
        say(np ? "\r\n\x1b[2m[Hydra: the files go at the host's next Hydra start]\x1b[0m\r\n"
               : "\r\n\x1b[2m[Hydra: nothing waits to be sent]\x1b[0m\r\n");
        redraw_all();
        refresh();
        return;
    }
    if (local || sock < 0) {
        say("\r\n\x1b[2m[transfers are with a host: call one first]\x1b[0m\r\n");
        refresh();
        return;
    }
    begin(&io);
    if (what == M_YRECV || what == M_XRECV) {
        char name[108];
        down_dir();
        name[0] = 0;
        if (!ymodem && !ask_name(name, sizeof name)) {
            refresh();
            redraw_all();
            return;
        }
        say(ymodem ? "\r\n\x1b[2m[Ymodem: receiving into "
                   : "\r\n\x1b[2m[Xmodem: receiving into ");
        say(xdir);
        say(" - Esc stops]\x1b[0m\r\n");
        refresh();
        got = xy_receive(&io, ymodem, name);
    } else {
        np = ask_uploads(paths, ymodem ? "CTerm: files to send (Ymodem)"
                                       : "CTerm: a file to send (Xmodem)");
        if (np == 0) {
            redraw_all();
            return;
        }
        if (!ymodem)
            np = 1;                     /* Xmodem has no names: one file */
        say(ymodem ? "\r\n\x1b[2m[Ymodem: sending - Esc stops]\x1b[0m\r\n"
                   : "\r\n\x1b[2m[Xmodem: sending - Esc stops]\x1b[0m\r\n");
        refresh();
        got = xy_send(&io, ymodem, 1, paths, np);
    }
    finish(got, TRUE);
}

/* What came from the host: does a transfer begin in it? If so, what came
 * before goes to the screen as usual (raw: telnet still on it for telnet
 * tabs), the transfer runs, and TRUE says the rest was taken. */
BOOL transfer_take(UBYTE *buf, LONG n)
{
    static XferIO io;
    long at;
    int kind = zm_detect(buf, n, &at), got;
    long hat;
    if (hy_detect(buf, n, &hat) && (!kind || hat < at)) {
        kind = 3;                       /* Hydra: its START packet */
        at = hat;
    }
    UBYTE *pre;
    LONG pn;
    if (!kind || !win)
        return FALSE;
    if (at > 0) {
        if (sshmode) {
            log_bytes(buf, at);
            vt_write(vt, buf, at);
        } else
            from_host(buf, at);
    }
    /* the start header and what followed it, telnet taken off */
    pre = buf + at;                     /* decoded where it lies */
    pn = n - at;
    if (!sshmode)
        pn = tel_decode(pre, pn);
    begin(&io);
    refresh();
    if (kind == 3) {
        const char *paths[NUP];
        int i;
        for (i = 0; i < nhq; i++)
            paths[i] = store[i];
        down_dir();
        say("\r\n\x1b[2m[Hydra: receiving into ");
        say(xdir);
        if (nhq)
            say(", sending the queued files");
        say(" - Esc stops]\x1b[0m\r\n");
        refresh();
        got = hy_session(&io, paths, nhq, pre, pn, 1);
        nhq = 0;                        /* sent, or given up */
    } else if (kind == 1) {
        down_dir();
        say("\r\n\x1b[2m[Zmodem: receiving into ");
        say(xdir);
        say(" - Esc stops]\x1b[0m\r\n");
        refresh();
        got = zm_receive(&io, pre, pn);
    } else {
        const char *paths[NUP];
        int np;
        say("\r\n\x1b[2m[Zmodem: the host waits for files]\x1b[0m\r\n");
        refresh();
        np = ask_uploads(paths, "CTerm: files to send (Zmodem)");
        if (np == 0) {
            static const UBYTE can[] = { 24, 24, 24, 24, 24, 24, 24, 24,
                                         8, 8, 8, 8, 8, 8, 8, 8 };
            send_data(can, sizeof can); /* none: the host stops waiting */
            got = 0;
        } else
            got = zm_send(&io, paths, np, pre, pn);
    }
    finish(got, kind == 2);
    return TRUE;
}
