/*
 * parts - CTerm's side of the parts (part.h): what each part may use of
 * CTerm (penv), loading a part the first time it is wanted, and the
 * calls the rest of CTerm makes, which go to the part when it is there.
 * A part leaves memory as soon as its window is closed and none of its
 * code is running (a count around every call into it: its waits serve
 * the window and the Shells, so a close from in there must not pull the
 * code away). ARexx's part, once loaded, stays.
 */
#include <rexx/storage.h>
#include <rexx/errors.h>
#include "cterm.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

/* ssh here (CTermSSH) or not: the parts are the same files for both */
#ifdef NO_SSH
BOOL has_ssh = FALSE;
const struct SftpOps *sftp_ops = NULL;  /* SFTP is CTermSSH's (ssh.c) */
#else
BOOL has_ssh = TRUE;
#endif

/* What the parts keep in CTerm between loads */
BOOL call_newtab = TRUE;                /* the Call window's "new tab" */
WORD xwleft = -1, xwtop;                /* where the Transfers window was */
WORD bwleft = -1, bwtop;                /* ... the phone book's */
int set_page;                           /* the settings' tab last shown */

static const struct PartEnv penv = {
#define V(x) &x,
#define A(x) x,
#define F(x) x,
#include "partlist.h"
#undef V
#undef A
#undef F
};

struct Slot {
    const char *file;                   /* in PROGDIR:Parts/ */
    const char *what;                   /* for the message */
    BPTR seg;
    APTR fns;                           /* its table (part.h) */
    int depth;                          /* calls into it under way */
};

/* The part in memory, loaded now if need be. NULL: missing or of
 * another version, said in the tab. */
APTR part_load(struct Slot *s)
{
    char path[40];
    BOOL found;
    PartStart start;
    if (s->fns)
        return s->fns;
    strcpy(path, "PROGDIR:Parts/");
    strcat(path, s->file);
    if (!(s->seg = LoadSeg((STRPTR)path))) {
        say("\r\n\x1b[1;31m[");
        say(s->what);
        say(" missing: ");
        say(path);
        say(" not found]\x1b[0m\r\n");
        refresh();
        return NULL;
    }
    start = (PartStart)mod_entry(s->seg, PART_HEAD, PART_ABI, &found);
    if (!start || !(s->fns = start(&penv))) {
        UnLoadSeg(s->seg);
        s->seg = 0;
        say("\r\n\x1b[1;31m[");
        say(path);
        say(" is not CTerm's part of this version]\x1b[0m\r\n");
        refresh();
        return NULL;
    }
    return s->fns;
}

/* Done with (its window closed): gone, when none of its code runs. */
void part_unload(struct Slot *s)
{
    if (s->seg && !s->depth) {
        s->fns = NULL;
        UnLoadSeg(s->seg);
        s->seg = 0;
        reaction_drop();                /* its classes too, maybe */
    }
}

/* ---- the settings window ---- */

static struct Slot ps = { "Settings", "the settings window" };
#define SP ((struct SetPart *)ps.fns)

static void set_done(void)
{
    if (!--ps.depth && !setwin)
        part_unload(&ps);
}

void open_settings(void)
{
    if (!part_load(&ps))
        return;
    ps.depth++;
    SP->open();
    set_done();
}

void close_settings(void)
{
    if (!ps.fns)
        return;
    ps.depth++;
    SP->close();
    set_done();
}

void settings_events(void)
{
    if (!ps.fns)
        return;
    ps.depth++;
    SP->events();
    set_done();
}

void settings_cancel(void)
{
    if (!ps.fns)
        return;
    ps.depth++;
    SP->cancel();
    set_done();
}

/* ---- the phone book's window ---- */

static struct Slot pb = { "PhoneBook", "the phone book window" };
#define BP ((struct BookPart *)pb.fns)

ULONG bookwin_sigmask(void)
{
    return pb.fns ? BP->sigmask() : 0;
}

static void book_done(void)
{
    if (!--pb.depth && !BP->sigmask())
        part_unload(&pb);
}

void bookwin_open(void)
{
    if (!part_load(&pb))
        return;
    pb.depth++;
    BP->open();
    book_done();
}

void bookwin_events(void)
{
    if (!pb.fns)
        return;
    pb.depth++;
    BP->events();
    book_done();
}

void bookwin_close(void)
{
    if (!pb.fns)
        return;
    pb.depth++;
    BP->close();
    book_done();
}

/* ---- the Transfers window ---- */

static struct Slot px = { "Transfers", "the Transfers window" };
#define XP ((struct XwinPart *)px.fns)

ULONG xwin_sigmask(void)
{
    return px.fns ? XP->sigmask() : 0;
}

static void xwin_done(void)
{
    if (!--px.depth && !XP->sigmask()) {
        XP->close();                    /* its gauge class back */
        part_unload(&px);
    }
}

void xwin_open(void)
{
    if (!part_load(&px))
        return;
    px.depth++;
    XP->open();
    xwin_done();
}

void xwin_update(void)
{
    if (!px.fns)
        return;
    px.depth++;
    XP->update();
    xwin_done();
}

void xwin_events(void)
{
    if (!px.fns)
        return;
    px.depth++;
    XP->events();
    xwin_done();
}

void xwin_shut(void)
{
    if (!px.fns)
        return;
    px.depth++;
    XP->close();
    xwin_done();
}

/* ---- the Call window, the speed test: gone when they return ---- */

static struct Slot pc_ = { "Call", "the Call window" };

void call_dialog(void)
{
    if (!part_load(&pc_))
        return;
    pc_.depth++;
    ((struct CallPart *)pc_.fns)->dialog();
    pc_.depth--;
    part_unload(&pc_);
}

static struct Slot pt = { "SpeedTest", "the speed test" };

void speed_test(void)
{
    if (!part_load(&pt))
        return;
    pt.depth++;
    ((struct SpeedPart *)pt.fns)->test();
    pt.depth--;
    part_unload(&pt);
}

/* ---- the file browser: there while a place is connected ---- */

static struct Slot pf = { "Files", "the file browser" };
#define FP ((struct FilesPart *)pf.fns)

static void files_done(void)
{
    if (!--pf.depth && FP->idle())
        part_unload(&pf);
}

BOOL files_call(void)
{
    BOOL ok;
    if (!part_load(&pf))
        return FALSE;
    pf.depth++;
    ok = FP->call();
    files_done();
    return ok;
}

void files_menu(void)
{
    if (!part_load(&pf))
        return;
    pf.depth++;
    FP->menu();
    files_done();
}

void files_key(int k)
{
    if (!pf.fns)
        return;
    pf.depth++;
    FP->key(k);
    files_done();
}

void files_mouse(int what, int r, int c, ULONG s, ULONG us)
{
    if (!pf.fns)
        return;
    pf.depth++;
    FP->mouse(what, r, c, s, us);
    files_done();
}

void files_resized(struct Tab *t)
{
    if (!pf.fns)
        return;
    pf.depth++;
    FP->resized(t);
    files_done();
}

void files_tab_gone(struct Tab *t)
{
    if (!pf.fns)
        return;
    pf.depth++;
    FP->tab_gone(t);
    files_done();
}


int files_rexx(int what, const char *const *names, int n, const char *to,
               char **res)
{
    int rc;
    *res = NULL;
    if (!pf.fns)
        return 10;                      /* no place connected */
    pf.depth++;
    rc = FP->rexx(what, names, n, to, res);
    files_done();
    return rc;
}

/* ---- ARexx: the port is CTerm's, the commands the part's ---- */

/* The port is made at the start (cheap: a port and a name), so scripts
 * find CTerm; Parts/ARexx is loaded when the first message comes or a
 * script is started, and stays until the end (scripts may be waiting in
 * it, a WAITFOR, a queued command). rexxsyslib.library is the part's to
 * open. */
struct MsgPort *rxport;
char rxpname[12];                       /* "CTERM", "CTERM.2"... */
struct RxsLib *RexxSysBase = NULL;

static struct Slot pr = { "ARexx", "ARexx support" };
#define RP ((struct RexxPart *)pr.fns)

const char *rexx_port(void)
{
    return rxport ? rxpname : "";
}

ULONG rexx_sigmask(void)
{
    return rxport ? 1UL << rxport->mp_SigBit : 0;
}

/* The port, at the start: the first free name. */
void rexx_open(void)
{
    int n;
    if (!(rxport = CreateMsgPort()))
        return;
    Forbid();
    for (n = 1; n < 100; n++) {
        strcpy(rxpname, "CTERM");
        if (n > 1) {
            strcat(rxpname, ".");
            numstr_cat(rxpname, n);
        }
        if (!FindPort((STRPTR)rxpname))
            break;
    }
    rxport->mp_Node.ln_Name = rxpname;
    rxport->mp_Node.ln_Pri = 0;
    if (n < 100)
        AddPort(rxport);
    Permit();
    if (n >= 100) {
        DeleteMsgPort(rxport);
        rxport = NULL;
    }
}

/* Every message on the port answered: RC 20 for ARexx's (the part is
 * missing, or CTerm ends without it). */
static void rx_refuse(void)
{
    struct Message *m;
    while ((m = GetMsg(rxport))) {
        if (m->mn_Node.ln_Name && !strcmp(m->mn_Node.ln_Name, "REXX")) {
            ((struct RexxMsg *)m)->rm_Result1 = RC_FATAL;
            ((struct RexxMsg *)m)->rm_Result2 = 0;
        }
        ReplyMsg(m);
    }
}

static BOOL rx_load(void)
{
    if (pr.fns)
        return TRUE;
    if (!part_load(&pr))
        return FALSE;
    return TRUE;
}

void rexx_serve(void)
{
    if (!rxport)
        return;
    if (!pr.fns) {
        if (IsListEmpty(&rxport->mp_MsgList))
            return;
        if (!rx_load()) {
            rx_refuse();
            return;
        }
    }
    pr.depth++;
    RP->serve();
    pr.depth--;
}

void rexx_run(void)
{
    if (pr.fns)
        RP->run();
}

void rexx_feed(struct Tab *t, const UBYTE *b, LONG n)
{
    if (pr.fns)
        RP->feed(t, b, n);
}

void rexx_watch(struct Tab *t)
{
    if (pr.fns)
        RP->watch(t);
}

void rexx_tab_gone(struct Tab *t)
{
    if (pr.fns)
        RP->tab_gone(t);
}

void rexx_tab_over(struct Tab *t)
{
    if (pr.fns)
        RP->tab_over(t);
}

void rexx_xfer_done(struct Tab *t, int got)
{
    if (pr.fns)
        RP->xfer_done(t, got);
}

int rexx_uploads(struct Tab *t, char (*store)[256], const char **paths,
                 int max)
{
    return pr.fns ? RP->uploads(t, store, paths, max) : 0;
}

BOOL rexx_start(const char *path, struct Tab *t)
{
    return rx_load() && RP->start(path, t);
}

/* A call is up: its place's login script, if it has one. */
void rexx_after_call(void)
{
    if (T->t_script[0] && rx_load())
        RP->after_call();
}

void rexx_menu(void)
{
    if (rx_load())
        RP->menu();
}

/* At the start, before the port: a hidden CTerm asked to show itself.
 * No CTerm port at all: nobody to ask, and nothing loaded. */
BOOL rexx_wake(BOOL any)
{
    struct MsgPort *p = NULL;
    char name[12];
    int n;
    BOOL done;
    Forbid();
    for (n = 1; n < 100 && !p; n++) {
        strcpy(name, "CTERM");
        if (n > 1) {
            strcat(name, ".");
            numstr_cat(name, n);
        }
        p = FindPort((STRPTR)name);
    }
    Permit();
    if (!p || !rx_load())
        return FALSE;
    done = RP->wake(any);
    if (!done)
        part_unload(&pr);               /* ARexx may never be used */
    return done;
}

void rexx_close(void)
{
    if (pr.fns)
        RP->close();
    else if (rxport) {
        Forbid();
        RemPort(rxport);
        rx_refuse();
        Permit();
        DeleteMsgPort(rxport);
        rxport = NULL;
    }
    if (!pr.fns && RexxSysBase) {       /* opened by a start's rexx_wake */
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
}

/* At the end: every part is closed by then; what is left goes. */
void parts_free(void)
{
    part_unload(&ps);
    part_unload(&pb);
    part_unload(&px);
    part_unload(&pr);
}
