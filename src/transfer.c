/*
 * transfer - file transfers in a host's tab, CTerm's side. It notices a
 * Zmodem or Hydra start in what the host sends (Ymodem and Xmodem the
 * person starts from the Transfer menu), then gives the protocol engine
 * (zmodem.c, xymodem.c, hydra.c) the connection - telnet, ssh or a
 * serial line - and
 * AmigaDOS files. Downloads go to the tab's phone book drawer, else the
 * Files tab's, else PROGDIR:Downloads; uploads are chosen in a requester.
 *
 * The protocols themselves are files of their own (PROGDIR:Protocols/,
 * xfermod.h), loaded for a transfer and unloaded after it: what is not
 * used takes no memory.
 *
 * While it waits for the host, a transfer runs CTerm's own round
 * (loop_round), so the window, the other tabs and their hosts go on. How
 * it goes shows in a panel over the tab's last rows, in the tab's name,
 * and in the Transfers window; Esc or Stop ends it.
 */
#include "cterm.h"
#include "xfer.h"
#include "xfermod.h"

#define NUP 32

/* ---- the protocol files ---- */

static void *m_alloc(unsigned long n) { return AllocVec(n, MEMF_ANY); }
static void m_release(void *p) { FreeVec(p); }
static long m_now(void) { return (long)(ticks50() / 50); }
static struct XferEnv menv = { m_alloc, m_release, m_now };

/* A protocol file loaded: its segments and its table. Each transfer loads
 * its own copy, so two never share the protocol's memory. */
struct XMod {
    BPTR seg;
    struct XferMod *m;
};

/* A file's way in: the header (head, version, then the entry's address,
 * inside the file) found in its loaded segments. NULL: no header, or one
 * of another version (*found TRUE then) - such a file is not run at all.
 * The protocols' files and the serial line's (sermod.h) are found so. */
APTR mod_entry(BPTR seg, ULONG head, ULONG version, BOOL *found)
{
    BPTR s;
    *found = FALSE;
    for (s = seg; s; s = *(BPTR *)BADDR(s)) {
        /* in 2-byte steps: the 68k puts data on even addresses, not on
         * multiples of 4 (the 68020 reads a long from any even one).
         * The segment's words are mem[-2] (its length, in bytes, these
         * 8 included) to mem[n - 3]; the header is 6 words. */
        UWORD *mem = (UWORD *)BADDR(s);
        ULONG n = ((ULONG *)mem)[-1] / 2, i;   /* the length, in words */
        for (i = 2; i + 8 <= n; i++)
            if (*(ULONG *)(mem + i) == head) {
                *found = TRUE;
                return *(ULONG *)(mem + i + 2) == version
                       ? (APTR)*(ULONG *)(mem + i + 4) : NULL;
            }
    }
    return NULL;
}

static BOOL mod_load(struct XMod *x, const char *name)
{
    char path[64];
    BOOL found;
    XferModStart start;
    strcpy(path, "PROGDIR:Protocols/");
    strcat(path, name);
    x->m = NULL;
    x->seg = LoadSeg((STRPTR)path);
    if (!x->seg) {
        say("\r\n\x1b[1;31m[the ");
        say(name);
        say(" protocol is missing: ");
        say(path);
        say("]\x1b[0m\r\n");
        refresh();
        return FALSE;
    }
    start = (XferModStart)mod_entry(x->seg, XFERMOD_HEAD, XFERMOD_VERSION,
                                    &found);
    if (start) {
        struct XferMod *m = start(&menv);
        if (m && m->magic == XFERMOD_MAGIC)
            x->m = m;
    }
    if (!x->m) {
        UnLoadSeg(x->seg);
        x->seg = 0;
        say("\r\n\x1b[1;31m[");
        say(path);
        say(" is not a CTerm protocol of this version]\x1b[0m\r\n");
        refresh();
        return FALSE;
    }
    return TRUE;
}

static void mod_unload(struct XMod *x)
{
    if (x->seg)
        UnLoadSeg(x->seg);
    x->seg = 0;
    x->m = NULL;
}

/* A transfer the host began that cannot happen here: it stops waiting. */
static void refuse(void)
{
    static const UBYTE can[] = { 24, 24, 24, 24, 24, 24, 24, 24,
                                 8, 8, 8, 8, 8, 8, 8, 8 };
    send_data(can, sizeof can);
}

/* A transfer is running in some tab. The engines are not resumable: one
 * started while another runs would run inside it (x_get serves the other
 * tabs) and freeze it until it ended - so one at a time, in all tabs. */
static BOOL xfer_running(void)
{
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->t_xfer)
            return TRUE;
    return FALSE;
}

/* One transfer: its tab, what the panel shows, its telnet parser. */
struct XCtx {
    struct Tab *tab;
    struct Xfer x;
    int tel;                            /* telnet's state in its bytes */
    BOOL damaged;                       /* a requester was over the window */
    BOOL exact;                         /* the person named the download:
                                         * over a file there, if they said */
    BOOL resumes;                       /* its protocol goes on from parts */
    char dir[256];                      /* where downloads go */
    char (*paths)[256];                 /* the files to send */
    char wpath[400];                    /* the download: its name when */
    char ppath[408];                    /* whole, and while it comes */
    ULONG wlen;                         /* bytes in it */
    UBYTE *rest;                        /* what the host sent after it */
    LONG nrest;
};

#define MAXREST 16384                   /* of that, at most: for the screen */

/* Hydra's queue: files waiting for the next Hydra start in hqtab's host
 * (the tab it was made in, not any tab's). */
static char (*hq)[256];
static int nhq;
static struct Tab *hqtab;

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
        if (T->t_serial) {              /* the line's buffer, as it is */
            k = serial_take(b, max);
            if (k != 0)
                return k;
        }
        if (c->x.stop || T->ended || quitting)
            return 0;                   /* x_stop says why */
        now = ticks50();
        if (ms > 0 && now >= until)
            return 0;
        /* wait the way CTerm always does: the window and the other tabs
         * are served meanwhile */
        ready = loop_round(T->t_serial ? -1 : sock,
                           ms > 0 ? (LONG)(until - now) * 20 : 0);
        T = c->tab;
        if (c->x.stop || T->ended || quitting)
            return 0;
        if (T->t_serial) {              /* serial_take above */
            if (ms == 0)
                return serial_take(b, max);
            continue;
        }
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

/* The bytes in a file, -1: none there. */
static LONG fsize(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    struct FileInfoBlock *fib;
    LONG n = -1;
    if (!l)
        return -1;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(l, fib) && fib->fib_DirEntryType < 0)
            n = fib->fib_Size;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    return n;
}

/* Room for n more bytes in the download drawer? (Yes when it cannot
 * tell: RAM: always says it is full, and grows.) */
static BOOL room(struct XCtx *c, ULONG n)
{
    BPTR l;
    struct InfoData *id;
    BOOL ok = TRUE;
    if (!n || !(l = Lock((STRPTR)c->dir, ACCESS_READ)))
        return TRUE;
    if ((id = AllocVec(sizeof *id, MEMF_ANY)) && Info(l, id) &&
        id->id_BytesPerBlock > 0 && id->id_NumBlocksUsed < id->id_NumBlocks) {
        ULONG bpb = (ULONG)id->id_BytesPerBlock,
              need = n / bpb + 1,
              free_ = (ULONG)(id->id_NumBlocks - id->id_NumBlocksUsed);
        need += need / 64 + 2;          /* the file's own list blocks */
        ok = need <= free_;
    }
    if (id)
        FreeVec(id);
    UnLock(l);
    return ok;
}

/* A download: made as "name.part" in the drawer (renamed to name when it
 * is whole: x_wclose), never over a file that is there - unless the person
 * chose it (Xmodem) and said so. A protocol that can go on (have: Zmodem,
 * Hydra) does, from a part of the same name an earlier try left. */
static void *x_wopen(void *u, const char *name, unsigned long size,
                     char *made, int madelen, unsigned long *have)
{
    struct XCtx *c = u;
    char nm[112];
    int n = 0;
    LONG part;
    BPTR f = 0;
    /* never a drawer from the name, whoever sent it: AddPart lets a full
     * path (SYS:x) or a leading / replace or climb out of c->dir */
    {
        const char *q;
        for (q = name; *q; q++)
            if (*q == '/' || *q == ':' || *q == '\\')
                name = q + 1;
    }
    strncpy(nm, name[0] ? name : "file", 100);  /* nameless: still one */
    nm[100] = 0;
    for (;;) {                          /* never over a file that is there */
        strcpy(c->wpath, c->dir);
        AddPart((STRPTR)c->wpath, (STRPTR)nm, sizeof c->wpath);
        if (c->exact || !exists(c->wpath))
            break;
        if (++n > 99)
            return NULL;
        strncpy(nm, name[0] ? name : "file", 96);
        nm[96] = 0;
        strcat(nm, ".");
        numstr_cat(nm, n);
    }
    strncpy(made, nm, madelen - 1);
    made[madelen - 1] = 0;
    strcpy(c->ppath, c->wpath);
    strcat(c->ppath, ".part");
    c->resumes = have != NULL;
    c->wlen = 0;
    if (have) {
        /* a part from before, shorter than the file: on from its end */
        *have = 0;
        part = fsize(c->ppath);
        if (part > 0 && (ULONG)part < size &&
            (f = Open((STRPTR)c->ppath, MODE_OLDFILE))) {
            if (Seek(f, part, OFFSET_BEGINNING) < 0) {
                Close(f);
                f = 0;
            } else
                c->wlen = *have = (ULONG)part;
        }
    }
    if (size && !room(c, size - c->wlen)) {
        T = c->tab;
        say("\x1b[1;31m[no room for ");
        say(nm);
        say(" in ");
        say(c->dir);
        say("]\x1b[0m\r\n");
        if (f)
            Close(f);
        return NULL;
    }
    if (!f)
        f = Open((STRPTR)c->ppath, MODE_NEWFILE);
    return f ? (void *)f : NULL;
}

static long x_wwrite(void *u, void *f, const unsigned char *b, long n)
{
    struct XCtx *c = u;
    LONG k = Write((BPTR)f, (APTR)b, n);
    if (k > 0)
        c->wlen += (ULONG)k;
    return k;
}

/* Whole: name.part becomes name (over the file the person chose to
 * replace). Not: the part stays, for a later try to go on from - or goes,
 * when nothing came. */
static void x_wclose(void *u, void *f, int ok)
{
    struct XCtx *c = u;
    Close((BPTR)f);
    T = c->tab;
    if (ok) {
        if (c->exact)
            DeleteFile((STRPTR)c->wpath);
        if (!Rename((STRPTR)c->ppath, (STRPTR)c->wpath)) {
            say("\x1b[1;31m[it is in ");
            say(c->ppath);
            say(": the name was taken]\x1b[0m\r\n");
        }
    } else if (c->wlen == 0)
        DeleteFile((STRPTR)c->ppath);
    else {
        say("\x1b[2m[the part is kept as ");
        say(FilePart((STRPTR)c->ppath));
        say(c->resumes ? ": a new try goes on from it]\x1b[0m\r\n"
                       : "]\x1b[0m\r\n");
    }
}

/* An upload: only a file whose size is known (a wrong 0 would send it
 * empty), not a drawer. */
static void *x_ropen(void *u, const char *path, unsigned long *size)
{
    LONG n = fsize(path);
    BPTR f;
    (void)u;
    if (n < 0)
        return NULL;
    *size = (unsigned long)n;
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

/* What the host sent after the transfer (its prompt): kept for the
 * screen, shown when it has ended (finish). */
static void x_rest(void *u, const unsigned char *b, long n)
{
    struct XCtx *c = u;
    UBYTE *r;
    if (n <= 0 || c->nrest + n > MAXREST)
        return;
    if (!(r = AllocVec(c->nrest + n, MEMF_ANY)))
        return;
    if (c->nrest)
        CopyMem(c->rest, r, c->nrest);
    CopyMem((APTR)b, r + c->nrest, n);
    if (c->rest)
        FreeVec(c->rest);
    c->rest = r;
    c->nrest += n;
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
    while (!ok && AslRequest(req, NULL) && req->fr_File[0]) {
        char path[400];
        strncpy(c->dir, (char *)req->fr_Drawer, sizeof c->dir - 1);
        c->dir[sizeof c->dir - 1] = 0;
        strncpy(c->x.where, c->dir, sizeof c->x.where - 1);
        strncpy(name, (char *)req->fr_File, max - 1);
        name[max - 1] = 0;
        /* the name the person chose is the one it gets: over a file
         * there only when they say so */
        strcpy(path, c->dir);
        AddPart((STRPTR)path, (STRPTR)name, sizeof path);
        ok = TRUE;
        if (exists(path)) {
            struct EasyStruct es = {
                sizeof(struct EasyStruct), 0, (UBYTE *)"CTerm",
                (UBYTE *)"%s is there already.\nReplace it?",
                (UBYTE *)"Replace|Another name|Cancel" };
            ULONG arg = (ULONG)name;
            LONG a = EasyRequestArgs(win, &es, NULL, &arg);
            if (a == 0) {
                ok = FALSE;
                break;
            }
            ok = a == 1;
        }
    }
    c->exact = ok;
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
    hqtab = NULL;
    xwin_shut();
}

/* A tab closes: a Hydra queue made in it goes too (a new tab may get the
 * same memory and must not inherit it). */
void transfer_tab_gone(struct Tab *t)
{
    if (hqtab == t) {
        nhq = 0;
        hqtab = NULL;
    }
}

/* The Hydra queue's files, when it was made in this tab (T) and that tab
 * is still open: 0 for any other. */
static int hq_mine(void)
{
    int i;
    if (!nhq || hqtab != T)
        return 0;
    for (i = 0; i < ntabs; i++)
        if (tabs[i] == hqtab)
            return nhq;
    return 0;
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
    io->rclose = x_rclose; io->rest = x_rest;
    return c;
}

static void ctx_free(struct XCtx *c)
{
    if (c->rest)
        FreeVec(c->rest);
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

/* After a stop the sender's data already on its way still comes (500 KB
 * a second over the relay): it is thrown away until the line has been
 * quiet half a second, at most 3 s - not shown as screens of noise. */
static void drain(struct XCtx *c)
{
    ULONG until = ticks50() + 150;
    UBYTE b[512];
    T = c->tab;
    if (T->t_serial) {
        ULONG quiet = ticks50() + 25;
        while (ticks50() < until && ticks50() < quiet) {
            LONG k = serial_take(b, sizeof b);
            if (k < 0)
                break;
            if (k > 0)
                quiet = ticks50() + 25;
            else
                Delay(2);
        }
        return;
    }
    while (sock >= 0 && ticks50() < until) {
        fd_set rd;
        struct timeval tv;
        if (sshmode) {
            LONG k = ssh_read(T->ssh, b, sizeof b);
            if (k < 0)
                break;
            if (k > 0)
                continue;
        }
        FD_ZERO(&rd);
        FD_SET(sock, &rd);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        if (WaitSelect(sock + 1, &rd, NULL, NULL, &tv, NULL) <= 0)
            break;
        if (!sshmode && recv(sock, b, sizeof b, 0) <= 0)
            break;
    }
}

/* How it ended, in the tab; the panel goes. */
static void finish(struct XCtx *c, int got)
{
    T = c->tab;
    if (got < 0 && c->x.stop)
        drain(c);
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
    if (c->nrest && got >= 0) {         /* the host's prompt, say (not a
                                         * stopped one's protocol leftovers) */
        log_bytes(c->rest, c->nrest);
        vt_write(vt, c->rest, c->nrest);
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
    rexx_xfer_done(c->tab, got);        /* an ARexx DOWNLOAD/UPLOAD waits */
    ctx_free(c);
}

/* Ymodem or Xmodem (or a Zmodem send, ARexx's), started from this side:
 * the files asked for in requesters (paths NULL, xname NULL), or given
 * (ARexx). The files done; -1 failed or could not start. */
static int start_xfer(int what, const char *const *given, int ngiven,
                      const char *xname)
{
    XferIO io;
    struct XCtx *c;
    const char *paths[NUP];
    int got = -1, np, i;
    BOOL zm = what == M_ZSEND;
    BOOL ymodem = what == M_YRECV || what == M_YSEND;
    BOOL rexx = given || xname;
    struct XMod xm;
    if (local || !linked) {
        say("\r\n\x1b[2m[transfers are with a host: call one first]\x1b[0m\r\n");
        refresh();
        return -1;
    }
    if (xfer_running()) {
        say("\r\n\x1b[2m[one transfer at a time: wait for the other one]\x1b[0m\r\n");
        refresh();
        return -1;
    }
    if (!mod_load(&xm, zm ? "Zmodem" : "XYmodem"))
        return -1;
    c = begin(&io, zm ? "Zmodem" : ymodem ? "Ymodem" : "Xmodem",
              what == M_YRECV || what == M_XRECV ? 1 : 2);
    if (!c) {
        mod_unload(&xm);
        return -1;
    }
    c->damaged = !rexx;
    if (what == M_YRECV || what == M_XRECV) {
        char name[108];
        down_dir(c);
        name[0] = 0;
        if (!ymodem && xname) {
            /* ARexx's name: in the download drawer, or a path of its own */
            const char *fp = (const char *)FilePart((STRPTR)xname);
            if (fp != xname) {
                LONG k = fp - xname;
                if (k > (LONG)sizeof c->dir - 1)
                    k = sizeof c->dir - 1;
                strncpy(c->dir, xname, k);
                c->dir[k] = 0;
                strncpy(c->x.where, c->dir, sizeof c->x.where - 1);
            }
            strncpy(name, fp, sizeof name - 1);
            name[sizeof name - 1] = 0;
            c->exact = TRUE;
        } else if (!ymodem && !ask_name(c, name, sizeof name)) {
            redraw_all();
            refresh();
            ctx_free(c);
            mod_unload(&xm);
            return -1;
        }
        shows(c);
        got = xm.m->xy_receive(&io, ymodem, name);
    } else {
        if (given) {
            for (np = i = 0; i < ngiven && np < NUP; i++) {
                strncpy(c->paths[np], given[i], 255);
                c->paths[np][255] = 0;
                paths[np] = c->paths[np];
                np++;
            }
        } else
            np = ask_uploads(c->paths, paths,
                             ymodem ? "CTerm: files to send (Ymodem)"
                                    : "CTerm: a file to send (Xmodem)");
        if (np == 0) {
            redraw_all();
            ctx_free(c);
            mod_unload(&xm);
            return -1;
        }
        if (!ymodem && !zm)
            np = 1;                     /* Xmodem has no names: one file */
        send_totals(c, paths, np);
        shows(c);
        /* 1: Xmodem in 1 KB blocks when the receiver asks with 'C' and
         * takes them, else 128 (xymodem.c finds out) */
        got = zm ? xm.m->zm_send(&io, paths, np, NULL, 0)
                 : xm.m->xy_send(&io, ymodem, 1, paths, np);
    }
    finish(c, got);
    mod_unload(&xm);
    return got;
}

/* ARexx's DOWNLOAD and UPLOAD: as the menu's, with the files given. */
int transfer_rexx(int what, const char *const *paths, int n,
                  const char *xname)
{
    return start_xfer(what, paths, n, xname);
}

/* ARexx's UPLOAD with Hydra: the files wait for this tab's next Hydra
 * session, as Send with Hydra... does. */
BOOL transfer_hydra_queue(const char *const *paths, int n)
{
    int i;
    if (xfer_running() || n <= 0 ||
        (!hq && !(hq = AllocVec(NUP * 256, MEMF_ANY))))
        return FALSE;
    for (i = 0; i < n && i < NUP; i++) {
        strncpy(hq[i], paths[i], 255);
        hq[i][255] = 0;
    }
    nhq = i;
    hqtab = T;
    return TRUE;
}

/* Transfer/Receive and Send with Ymodem or Xmodem: the person starts them
 * (after starting the other side on the host). Send with Hydra queues. */
void transfer_menu(int what)
{
    const char *paths[NUP];
    if (what == M_XWIN) {
        xwin_open();
        return;
    }
    if (what == M_HSEND) {
        /* Hydra starts from the host; these wait for its next session
         * (not while one runs: it is sending from this very list) */
        if (xfer_running()) {
            say("\r\n\x1b[2m[one transfer at a time: wait for the other one]\x1b[0m\r\n");
            refresh();
            return;
        }
        if (!hq && !(hq = AllocVec(NUP * 256, MEMF_ANY)))
            return;
        nhq = ask_uploads(hq, paths, "CTerm: files to send with Hydra");
        hqtab = T;                      /* this tab's host's, no other's */
        say(nhq ? "\r\n\x1b[2m[Hydra: the files go at this tab's next Hydra start]\x1b[0m\r\n"
                : "\r\n\x1b[2m[Hydra: nothing waits to be sent]\x1b[0m\r\n");
        redraw_all();
        refresh();
        return;
    }
    start_xfer(what, NULL, 0, NULL);
}

/* Each tab's last bytes, for a start split across two reads. */
static struct { struct Tab *t; UBYTE b[SPLIT_TAIL + 1]; } tails[MAXTABS];

static UBYTE *tail_of(struct Tab *t)
{
    int i, k, free_ = -1;
    for (i = 0; i < MAXTABS; i++) {
        if (tails[i].t == t)
            return tails[i].b;
        if (free_ < 0) {                /* empty, or its tab is closed */
            for (k = 0; k < ntabs && tabs[k] != tails[i].t; k++)
                ;
            if (!tails[i].t || k == ntabs)
                free_ = i;
        }
    }
    if (free_ < 0)
        free_ = 0;
    tails[free_].t = t;
    tails[free_].b[0] = 0;
    return tails[free_].b;
}

/* What came from the host: does a transfer begin in it? If so, what came
 * before goes to the screen as usual (raw: telnet still on it for telnet
 * tabs), the transfer runs, and TRUE says the rest was taken. A start
 * split across this read and the one before is found too (its first
 * bytes are on the screen already; the protocol does without them). */
BOOL transfer_take(UBYTE *buf, LONG n)
{
    XferIO io;
    struct XMod xm;
    struct XCtx *c;
    long at, hat;
    int kind = zm_detect(buf, n, &at), got;
    UBYTE *pre;
    LONG pn;
    int nq;
    if (hy_detect(buf, n, &hat) && (!kind || hat < at)) {
        kind = 3;                       /* Hydra: its START packet */
        at = hat;
    }
    if (kind)
        tail_of(T)[0] = 0;              /* a new start after this one */
    else if ((kind = split_detect(tail_of(T), buf, n, NULL)))
        at = 0;
    if (!kind || !win || T->t_xfer)
        return FALSE;
    if (xfer_running()) {               /* another tab's: say no, cleanly */
        refuse();
        say("\r\n\x1b[2m[a transfer was refused: another one is running]\x1b[0m\r\n");
        return TRUE;
    }
    if (at > 0) {
        if (sshmode || T->t_serial) {
            log_bytes(buf, at);
            vt_write(vt, buf, at);
        } else
            from_host(buf, at);
    }
    if (!mod_load(&xm, kind == 3 ? "Hydra" : "Zmodem")) {
        refuse();                       /* the host would wait for nothing */
        return TRUE;
    }
    nq = kind == 3 ? hq_mine() : 0;
    c = begin(&io, kind == 3 ? "Hydra" : "Zmodem",
              kind == 3 ? (nq ? 3 : 1) : kind);
    if (!c) {
        mod_unload(&xm);
        refuse();
        return TRUE;
    }
    /* the start header and what followed it, telnet taken off */
    pre = buf + at;                     /* decoded where it lies */
    pn = n - at;
    if (!sshmode && !T->t_serial)
        pn = tel_decode(&c->tel, pre, pn);
    if (kind == 3) {
        const char *paths[NUP];
        int i;
        for (i = 0; i < nq; i++)
            paths[i] = hq[i];
        down_dir(c);
        if (nq)
            send_totals(c, paths, nq);
        c->x.nfiles = 0;                /* what comes is not known */
        c->x.bsize = 0;
        shows(c);
        got = xm.m->hy_session(&io, paths, nq, pre, pn, 1);
        if (nq)
            nhq = 0;                    /* sent, or given up */
    } else if (kind == 1) {
        down_dir(c);
        shows(c);
        got = xm.m->zm_receive(&io, pre, pn);
    } else {
        const char *paths[NUP];
        int np;
        say("\r\n\x1b[2m[Zmodem: the host waits for files]\x1b[0m\r\n");
        refresh();
        c->damaged = TRUE;
        /* an ARexx UPLOAD queued for this tab: its files, no requester.
         * A tab a script drives gets 2 s for its UPLOAD to come: the
         * script typed the host's upload command, the host was quicker */
        np = rexx_uploads(T, c->paths, paths, NUP);
        if (!np && T->t_watch) {
            int i;
            for (i = 0; i < 20 && !np; i++) {
                Delay(5);
                rexx_serve();           /* read only: it is queued */
                np = rexx_uploads(T, c->paths, paths, NUP);
            }
        }
        if (!np)
            np = ask_uploads(c->paths, paths,
                             "CTerm: files to send (Zmodem)");
        if (np == 0) {
            refuse();                   /* none: the host stops waiting */
            got = 0;
        } else {
            send_totals(c, paths, np);
            shows(c);
            got = xm.m->zm_send(&io, paths, np, pre, pn);
        }
    }
    finish(c, got);
    mod_unload(&xm);
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
