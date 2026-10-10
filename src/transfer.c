/*
 * transfer - file transfers in a host's tab, CTerm's side. It notices a
 * Zmodem or Hydra start in what the host sends (Ymodem and Xmodem the
 * person starts from the Transfer menu), then gives the protocol engine
 * (zmodem.c, xymodem.c, hydra.c) the connection - telnet or ssh - and
 * AmigaDOS files. Downloads go to the tab's phone book drawer, else the
 * Files tab's, else PROGDIR:Downloads; uploads are chosen in a requester.
 *
 * While it waits for the host, a transfer runs CTerm's own round
 * (loop_round), so the window, the other tabs and their hosts go on. How
 * it goes shows in a panel over the tab's last rows, in the tab's name,
 * and in the Transfers window; Esc or Stop ends it.
 */
#include "cterm.h"
#include "xfer.h"

#define NUP 32

/* One transfer: its tab, what the panel shows, its telnet parser. */
struct XCtx {
    struct Tab *tab;
    struct Xfer x;
    int tel;                            /* telnet's state in its bytes */
    BOOL damaged;                       /* a requester was over the window */
    char dir[256];                      /* where downloads go */
    char (*paths)[256];                 /* the files to send */
};

static char (*hq)[256];                 /* Hydra's queue: files waiting */
static int nhq;

ULONG ticks50(void)                     /* 1/50 s */
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 4320000UL + (ULONG)ds.ds_Minute * 3000UL +
           (ULONG)ds.ds_Tick;
}

/* Telnet off the bytes: IAC IAC is 0xff, commands and SB...SE go. */
static LONG tel_decode(int *st, UBYTE *b, LONG n)
{
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = b[i];
        switch (*st) {
        case 0:
            if (c == 255) *st = 1;
            else b[k++] = c;
            break;
        case 1:
            if (c == 255) { b[k++] = 255; *st = 0; }
            else if (c >= 251 && c <= 254) *st = 2;
            else if (c == 250) *st = 3;
            else *st = 0;
            break;
        case 2:                         /* WILL/WONT/DO/DONT x: not now */
            *st = 0;
            break;
        case 3:                         /* SB ... IAC SE */
            if (c == 255) *st = 4;
            break;
        case 4:
            *st = c == 240 ? 0 : 3;
            break;
        }
    }
    return k;
}

/* ---- what the person sees ---- */

/* The panel, the tab bar, the Transfers window: a few times a second. */
static void x_redraw(struct XCtx *c, BOOL now)
{
    ULONG t = ticks50();
    if (!now && t - c->x.tlast < 12)
        return;
    c->x.tlast = t;
    if (win) {
        if (shown == c->tab)
            xfer_panel();
        if (tabbar_h())
            draw_tabbar();
    }
    xwin_update();
}

static long x_get(void *u, unsigned char *b, long max, long ms)
{
    struct XCtx *c = u;
    ULONG until = ticks50() + (ULONG)(ms / 20);
    for (;;) {
        ULONG now;
        LONG k;
        BOOL ready;
        T = c->tab;
        if (sshmode) {
            k = ssh_read(T->ssh, b, max);
            if (k != 0)
                return k < 0 ? -1 : k;
        }
        if (c->x.stop || T->ended || quitting)
            return 0;                   /* x_stop says why */
        now = ticks50();
        if (ms > 0 && now >= until)
            return 0;
        /* wait the way CTerm always does: the window and the other tabs
         * are served meanwhile */
        ready = loop_round(sock, ms > 0 ? (LONG)(until - now) * 20 : 0);
        T = c->tab;
        if (c->x.stop || T->ended || quitting)
            return 0;
        if (!ready) {
            if (ms == 0)
                return 0;
            continue;
        }
        if (sshmode)
            continue;                   /* ssh_read above */
        k = recv(sock, b, max, 0);
        if (k <= 0)
            return -1;
        k = tel_decode(&c->tel, b, k);
        if (k > 0)
            return k;
    }
}

static void x_put(void *u, const unsigned char *b, long n)
{
    struct XCtx *c = u;
    T = c->tab;
    send_data(b, n);                    /* doubles IAC; ssh writes it */
}

static int x_stop(void *u)
{
    struct XCtx *c = u;
    return c->x.stop || c->tab->ended || quitting;
}

static void x_show(void *u, const char *name, unsigned long done,
                   unsigned long size, int what)
{
    struct XCtx *c = u;
    struct Xfer *x = &c->x;
    if (strcmp(x->name, name)) {        /* a new file */
        strncpy(x->name, name, sizeof x->name - 1);
        x->name[sizeof x->name - 1] = 0;
        x->fbase = x->bdone;
    }
    x->done = done;
    x->size = size;
    x->bdone = x->fbase + done;
    if (what) {
        T = c->tab;
        if (what == 1)
            x->files++;
        say(what == 1 ? "\x1b[2m[" : what == 2 ? "\x1b[2m[skipped "
                                                : "\x1b[1;31m[");
        say(name);
        say(what == 1 ? " done]\x1b[0m\r\n" : what == 2 ? "]\x1b[0m\r\n"
                                              : " failed]\x1b[0m\r\n");
        x->fbase = x->bdone;
        x->name[0] = 0;
        if (shown == c->tab)
            refresh();
    }
    x_redraw(c, what != 0);
}

static void x_batch(void *u, long files, unsigned long bytes)
{
    struct XCtx *c = u;
    if (files > 0)
        c->x.nfiles = c->x.files + (int)files;
    if (bytes)
        c->x.bsize = c->x.bdone + bytes;
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
    struct XCtx *c = u;
    static char path[400];
    char nm[112];
    int n = 0;
    BPTR f;
    (void)size;
    strncpy(nm, name[0] ? name : "file", 100);  /* nameless: still one */
    nm[100] = 0;
    for (;;) {                          /* never over a file that is there */
        strcpy(path, c->dir);
        AddPart((STRPTR)path, (STRPTR)nm, sizeof path);
        if (!exists(path))
            break;
        if (++n > 99)
            return NULL;
        strncpy(nm, name[0] ? name : "file", 96);
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

/* ---- starting and ending one ---- */

/* The drawer downloads go to: the tab's own, the Files tab's, or
 * PROGDIR:Downloads (made when missing). */
static void down_dir(struct XCtx *c)
{
    BPTR l;
    strcpy(c->dir, T->t_down[0] ? T->t_down : dldir[0] ? dldir
                                            : "PROGDIR:Downloads");
    l = Lock((STRPTR)c->dir, ACCESS_READ);
    if (!l)
        l = CreateDir((STRPTR)c->dir);
    if (l)
        UnLock(l);
    strncpy(c->x.where, c->dir, sizeof c->x.where - 1);
}

/* Xmodem brings no name: the person gives one (in the download drawer). */
static BOOL ask_name(struct XCtx *c, char *name, int max)
{
    struct FileRequester *req;
    BOOL ok = FALSE;
    if (!asl_open())
        return FALSE;
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: Xmodem - the file's name",
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialDrawer, (ULONG)c->dir,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return FALSE;
    c->damaged = TRUE;
    if (AslRequest(req, NULL) && req->fr_File[0]) {
        strncpy(c->dir, (char *)req->fr_Drawer, sizeof c->dir - 1);
        c->dir[sizeof c->dir - 1] = 0;
        strncpy(c->x.where, c->dir, sizeof c->x.where - 1);
        strncpy(name, (char *)req->fr_File, max - 1);
        name[max - 1] = 0;
        ok = TRUE;
    }
    FreeAslRequest(req);
    return ok;
}

/* Files for an upload, chosen in a requester (several at once), into
 * store (NUP paths of 256). */
static int ask_uploads(char (*store)[256], const char **paths,
                       const char *title)
{
    struct FileRequester *req;
    int n = 0, i;
    const char *dir = T->t_up[0] ? T->t_up : uldir[0] ? uldir : "";
    if (!asl_open() || !store)
        return 0;
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

/* The total of the files to send, for the batch bar. */
static void send_totals(struct XCtx *c, const char **paths, int n)
{
    int i;
    c->x.nfiles = n;
    c->x.bsize = 0;
    for (i = 0; i < n; i++) {
        unsigned long sz = 0;
        void *f = x_ropen(c, paths[i], &sz);
        if (f) {
            x_rclose(c, f);
            c->x.bsize += sz;
        }
    }
}

void transfer_free(void)
{
    if (hq)
        FreeVec(hq);
    hq = NULL;
    nhq = 0;
    xwin_shut();
}

/* A transfer begins in this tab (T): its context. */
static struct XCtx *begin(XferIO *io, const char *proto, int dir)
{
    struct XCtx *c = AllocVec(sizeof *c, MEMF_ANY | MEMF_CLEAR);
    if (!c)
        return NULL;
    c->paths = AllocVec(NUP * 256, MEMF_ANY);
    if (!c->paths) {
        FreeVec(c);
        return NULL;
    }
    c->tab = T;
    c->x.proto = proto;
    c->x.dir = dir;
    c->x.t0 = c->x.tlast = ticks50();
    memset(io, 0, sizeof *io);
    io->u = c;
    io->get = x_get; io->put = x_put; io->stop = x_stop; io->show = x_show;
    io->batch = x_batch;
    io->wopen = x_wopen; io->wwrite = x_wwrite; io->wclose = x_wclose;
    io->ropen = x_ropen; io->rread = x_rread; io->rseek = x_rseek;
    io->rclose = x_rclose;
    return c;
}

static void ctx_free(struct XCtx *c)
{
    FreeVec(c->paths);
    FreeVec(c);
}

/* The transfer shows: the panel, the name, the window. */
static void shows(struct XCtx *c)
{
    T = c->tab;
    T->t_xfer = &c->x;
    x_redraw(c, TRUE);
}

/* How it ended, in the tab; the panel goes. */
static void finish(struct XCtx *c, int got)
{
    T = c->tab;
    T->t_xfer = NULL;
    if (got < 0)
        say(c->x.stop ? "\x1b[1;31m[transfer stopped]\x1b[0m\r\n"
                      : "\x1b[1;31m[transfer failed]\x1b[0m\r\n");
    else if (got > 0) {
        char s[80];
        strcpy(s, "\x1b[2m[");
        numstr_cat(s, got);
        strcat(s, got == 1 ? " file, " : " files, ");
        numstr_cat(s, (LONG)(c->x.bdone / 1024));
        strcat(s, " KB]\x1b[0m\r\n");
        say(s);
    }
    xwin_update();
    if (win) {
        /* the rows under the panel, and whatever a requester covered */
        if (shown == c->tab || c->damaged)
            redraw_all();
        if (tabbar_h())
            draw_tabbar();
    }
    title_update_now();
    refresh();
    ctx_free(c);
}

/* Transfer/Receive and Send with Ymodem or Xmodem: the person starts them
 * (after starting the other side on the host). Send with Hydra queues. */
void transfer_menu(int what)
{
    XferIO io;
    struct XCtx *c;
    const char *paths[NUP];
    int got = -1, np;
    BOOL ymodem = what == M_YRECV || what == M_YSEND;
    if (what == M_XWIN) {
        xwin_open();
        return;
    }
    if (what == M_HSEND) {
        /* Hydra starts from the host; these wait for its next session */
        if (!hq && !(hq = AllocVec(NUP * 256, MEMF_ANY)))
            return;
        nhq = ask_uploads(hq, paths, "CTerm: files to send with Hydra");
        say(nhq ? "\r\n\x1b[2m[Hydra: the files go at the host's next Hydra start]\x1b[0m\r\n"
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
    if (T->t_xfer) {
        DisplayBeep(scr);               /* one at a time in a tab */
        return;
    }
    c = begin(&io, ymodem ? "Ymodem" : "Xmodem",
              what == M_YRECV || what == M_XRECV ? 1 : 2);
    if (!c)
        return;
    c->damaged = TRUE;
    if (what == M_YRECV || what == M_XRECV) {
        char name[108];
        down_dir(c);
        name[0] = 0;
        if (!ymodem && !ask_name(c, name, sizeof name)) {
            redraw_all();
            refresh();
            ctx_free(c);
            return;
        }
        shows(c);
        got = xy_receive(&io, ymodem, name);
    } else {
        np = ask_uploads(c->paths, paths,
                         ymodem ? "CTerm: files to send (Ymodem)"
                                : "CTerm: a file to send (Xmodem)");
        if (np == 0) {
            redraw_all();
            ctx_free(c);
            return;
        }
        if (!ymodem)
            np = 1;                     /* Xmodem has no names: one file */
        send_totals(c, paths, np);
        shows(c);
        got = xy_send(&io, ymodem, 1, paths, np);
    }
    finish(c, got);
}

/* What came from the host: does a transfer begin in it? If so, what came
 * before goes to the screen as usual (raw: telnet still on it for telnet
 * tabs), the transfer runs, and TRUE says the rest was taken. */
BOOL transfer_take(UBYTE *buf, LONG n)
{
    static int hydra_busy;              /* HydraCom's code runs one at once */
    XferIO io;
    struct XCtx *c;
    long at, hat;
    int kind = zm_detect(buf, n, &at), got;
    UBYTE *pre;
    LONG pn;
    if (hy_detect(buf, n, &hat) && (!kind || hat < at) && !hydra_busy) {
        kind = 3;                       /* Hydra: its START packet */
        at = hat;
    }
    if (!kind || !win || T->t_xfer)
        return FALSE;
    if (at > 0) {
        if (sshmode) {
            log_bytes(buf, at);
            vt_write(vt, buf, at);
        } else
            from_host(buf, at);
    }
    c = begin(&io, kind == 3 ? "Hydra" : "Zmodem",
              kind == 3 ? (nhq ? 3 : 1) : kind);
    if (!c)
        return FALSE;
    /* the start header and what followed it, telnet taken off */
    pre = buf + at;                     /* decoded where it lies */
    pn = n - at;
    if (!sshmode)
        pn = tel_decode(&c->tel, pre, pn);
    if (kind == 3) {
        const char *paths[NUP];
        int i;
        for (i = 0; i < nhq; i++)
            paths[i] = hq[i];
        down_dir(c);
        if (nhq)
            send_totals(c, paths, nhq);
        c->x.nfiles = 0;                /* what comes is not known */
        c->x.bsize = 0;
        shows(c);
        hydra_busy = 1;
        got = hy_session(&io, paths, nhq, pre, pn, 1);
        hydra_busy = 0;
        nhq = 0;                        /* sent, or given up */
    } else if (kind == 1) {
        down_dir(c);
        shows(c);
        got = zm_receive(&io, pre, pn);
    } else {
        const char *paths[NUP];
        int np;
        say("\r\n\x1b[2m[Zmodem: the host waits for files]\x1b[0m\r\n");
        refresh();
        c->damaged = TRUE;
        np = ask_uploads(c->paths, paths, "CTerm: files to send (Zmodem)");
        if (np == 0) {
            static const UBYTE can[] = { 24, 24, 24, 24, 24, 24, 24, 24,
                                         8, 8, 8, 8, 8, 8, 8, 8 };
            send_data(can, sizeof can); /* none: the host stops waiting */
            got = 0;
        } else {
            send_totals(c, paths, np);
            shows(c);
            got = zm_send(&io, paths, np, pre, pn);
        }
    }
    finish(c, got);
    return TRUE;
}

/* ---- the Transfers window (Transfer/Show transfers...) ---- */

#define XW 4                            /* transfers it shows */
enum { XW_STOP = 1 };
static Object *xwobj, *xwlab[XW], *xwfile[XW], *xwall[XW], *xwstop[XW];
static struct Window *xwwin;
static int xwslots;                     /* the transfers it has room for */
static WORD xwleft = -1, xwtop;         /* where it was */
struct Library *FuelGaugeBase = NULL;

ULONG xwin_sigmask(void)
{
    return xwwin ? 1UL << xwwin->UserPort->mp_SigBit : 0;
}

static void xwin_close(void)
{
    if (xwwin) {
        xwleft = xwwin->LeftEdge;
        xwtop = xwwin->TopEdge;
    }
    if (xwobj)
        DisposeObject(xwobj);
    xwobj = NULL;
    xwwin = NULL;
}

/* The transfers, in order of their tabs: the ones the window shows. */
static int xlist(struct Tab **out)
{
    int i, n = 0;
    for (i = 0; i < ntabs && n < XW; i++)
        if (tabs[i]->t_xfer)
            out[n++] = tabs[i];
    return n;
}

static void xset(Object *o, ULONG tag, ULONG val)
{
    if (o && xwwin &&
        SetGadgetAttrs((struct Gadget *)o, xwwin, NULL, tag, val, TAG_END))
        RefreshGList((struct Gadget *)o, xwwin, NULL, 1);
}

static void xwin_open_(void);

void xwin_update(void)
{
    struct Tab *t[XW];
    int i, n;
    static char lab[XW][160];
    if (!xwwin)
        return;
    n = xlist(t);
    if ((n > 1 ? n : 1) != xwslots) {   /* a transfer came or went */
        xwin_close();
        deep_call(xwin_open_);
        if (!xwwin)
            return;
    }
    for (i = 0; i < xwslots; i++) {
        struct Xfer *x = i < n ? t[i]->t_xfer : NULL;
        if (x) {
            char nm[64];
            tab_label(t[i], nm);
            strcpy(lab[i], nm);
            strcat(lab[i], ": ");
            strcat(lab[i], x->dir == 2 ? "sending " : "receiving ");
            strncat(lab[i], x->name[0] ? x->name : "...", 60);
        } else
            strcpy(lab[i], i == 0 && !n ? "No transfers now." : "");
        xset(xwlab[i], GA_Text, (ULONG)lab[i]);
        xset(xwfile[i], FUELGAUGE_Level,
             x ? pct100(x->done, x->size) : 0);
        xset(xwall[i], FUELGAUGE_Level,
             x ? pct100(x->bdone, x->bsize) : 0);
        xset(xwstop[i], GA_Disabled, !x);
    }
}

static void xwin_open_(void)
{
    Object *root;
    int i;
    if (xwwin) {
        WindowToFront(xwwin);
        return;
    }
    if (!reaction_open())
        return;
    if (!FuelGaugeBase &&
        !(FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 45)))
        return;
    {
        struct Tab *t[XW];
        int n = xlist(t);
        xwslots = n > 1 ? n : 1;        /* one row per transfer */
    }
    memset(xwlab, 0, sizeof xwlab);
    memset(xwfile, 0, sizeof xwfile);
    memset(xwall, 0, sizeof xwall);
    memset(xwstop, 0, sizeof xwstop);
    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    for (i = 0; i < xwslots && root; i++) {
        Object *g = VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_SpaceOuter, TRUE, End, *h;
        xwlab[i] = ButtonObject, GA_ReadOnly, TRUE, GA_Text, (ULONG)"",
            BUTTON_BevelStyle, BVS_NONE, BUTTON_Justification, BCJ_LEFT, End;
        xwfile[i] = FuelGaugeObject, FUELGAUGE_Min, 0, FUELGAUGE_Max, 100,
            FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE, End;
        xwall[i] = FuelGaugeObject, FUELGAUGE_Min, 0, FUELGAUGE_Max, 100,
            FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE, End;
        xwstop[i] = ButtonObject, GA_ID, XW_STOP + i, GA_RelVerify, TRUE,
            GA_Text, (ULONG)"Stop", GA_Disabled, TRUE, End;
        if (!g)
            continue;
        SetAttrs(g, LAYOUT_AddChild, (ULONG)xwlab[i], TAG_END);
        h = HLayoutObject, End;
        if (h) {
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwfile[i], CHILD_Label,
                     (ULONG)(LabelObject, LABEL_Text, (ULONG)"File",
                             LabelEnd), TAG_END);
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwall[i], CHILD_Label,
                     (ULONG)(LabelObject, LABEL_Text, (ULONG)"All",
                             LabelEnd), TAG_END);
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwstop[i],
                     CHILD_WeightedWidth, 0, TAG_END);
            SetAttrs(g, LAYOUT_AddChild, (ULONG)h, TAG_END);
        }
        SetAttrs(root, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 TAG_END);
    }
    if (!root)
        return;
    xwobj = WindowObject, WA_Title, (ULONG)"CTerm Transfers",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, FALSE, WA_InnerWidth, 360,
        xwleft >= 0 ? WA_Left : TAG_IGNORE, xwleft,
        xwleft >= 0 ? WA_Top : TAG_IGNORE, xwtop,
        xwleft >= 0 ? TAG_IGNORE : WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, (ULONG)root, End;
    if (!xwobj) {
        DisposeObject(root);
        return;
    }
    xwwin = (struct Window *)DoMethod(xwobj, WM_OPEN);
    if (!xwwin)
        xwin_close();
    else {
        int keep = xwslots;
        xwin_update();                  /* (same count: no reopening) */
        (void)keep;
    }
}

void xwin_open(void) { deep_call(xwin_open_); }

static void xwin_events_(void)
{
    ULONG res;
    UWORD code;
    while (xwobj &&
           (res = DoMethod(xwobj, WM_HANDLEINPUT, (ULONG)&code)) !=
           WMHI_LASTMSG) {
        if ((res & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
            xwin_close();
            return;
        }
        if ((res & WMHI_CLASSMASK) == WMHI_GADGETUP) {
            int id = res & WMHI_GADGETMASK;
            struct Tab *t[XW];
            int n = xlist(t);
            if (id >= XW_STOP && id < XW_STOP + n)
                t[id - XW_STOP]->t_xfer->stop = 1;
        }
    }
}

void xwin_events(void) { deep_call(xwin_events_); }

/* Before the screen goes, and at the end. */
void xwin_shut(void)
{
    xwin_close();
    if (FuelGaugeBase) {
        CloseLibrary(FuelGaugeBase);
        FuelGaugeBase = NULL;
    }
}
