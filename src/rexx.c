/*
 * rexx - CTerm's ARexx port: scripts and other programs drive it
 * (CALL, SEND, WAITFOR, GETSCREEN...), and it starts scripts (the
 * Project menu, a phone book place's login script).
 *
 * The port is "CTERM" for the first CTerm, "CTERM.2", "CTERM.3"... for
 * more; it is made at the start (cheap: a port and a name).
 * rexxsyslib.library is opened only when the first ARexx message comes
 * or a script is started.
 *
 * When commands run (re-entrancy): CTerm waits in many places - its main
 * round, a call being made, a transfer, a modem dialling - and each of
 * those waits serves the port (shells_handle does it: every nested wait
 * calls it). But a command that changes things (CALL, SEND, HANGUP, TAB
 * NEW, DOWNLOAD...) may itself wait, and run inside a wait that is in
 * the middle of its own work (a dial's timer, a transfer's line) it
 * would pull the ground from under it. So the port is only *read* in
 * those waits: what is safe anywhere (reading the screen, VERSION, TAB
 * COUNT, registering a WAITFOR) is answered at once; the rest is queued
 * and run by run() between rounds (rexx_run), where nothing else is half
 * done. A WAITFOR is answered when its text arrives - from wherever the
 * text came in - by ReplyMsg alone, which is safe anywhere: the script's
 * next command comes through the port again, and is queued if it must.
 */
#include "cterm.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <rexx/rxslib.h>
#include <proto/rexxsyslib.h>
#include <devices/timer.h>

struct RxsLib *RexxSysBase;

#define WATCH    2048                   /* a tab's text kept for WAITFOR */
#define MAXTEXT  8                      /* WAITFOR's texts at most */
#define HELD     -1                     /* kept: answered later */
#define LATER    -2                     /* runs between rounds: queued */

/* What came from a tab's host, since the last SEND or match: WAITFOR
 * looks in it, so text that came before the WAITFOR counts too. Colour
 * codes are left out, UTF-8 is read as Latin-1. */
struct Watch {
    LONG n;
    UBYTE st;                           /* in an escape code: how far */
    UBYTE u8;                           /* UTF-8: bytes still to come */
    ULONG cp;                           /* ... and what it is so far */
    UBYTE b[WATCH];
};

enum { P_RUN, P_WAIT, P_XFER };

/* A message kept: queued to run, or waiting (text, a transfer). */
struct Pend {
    struct Pend *next;
    struct RexxMsg *rm;
    UBYTE kind;                         /* P_* */
    BOOL began;                         /* P_XFER: a transfer started */
    struct Tab *tab;
    ULONG due;                          /* ticks50() it gives up; 0 never */
    int ntext, maxlen;
    char *text[MAXTEXT];                /* WAITFOR's, unescaped */
    char *said[MAXTEXT];                /* ... as given: the RESULT */
};

/* A script CTerm started: back on the port when it ends. */
struct Script {
    struct Script *next;
    struct RexxMsg *rm;
    struct Tab *tab;
    char name[32];
};

static struct MsgPort *rxport;
static char pname[12];                  /* "CTERM", "CTERM.2"... */
static struct Pend *pend;               /* in order */
static struct Script *scripts;
static struct timerequest *tio;
static BOOL topen, ton;
static struct Tab *rxtab;               /* the tab commands go to */
static char *rbuf;                      /* a command's RESULT */
static LONG rcap, rlen;
static BOOL running;                    /* rexx_run is at work */
static BOOL closing;                    /* rexx_close: nothing said */

/* ---- the port ---- */

const char *rexx_port(void)
{
    return rxport ? pname : "";
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
        strcpy(pname, "CTERM");
        if (n > 1) {
            strcat(pname, ".");
            numstr_cat(pname, n);
        }
        if (!FindPort((STRPTR)pname))
            break;
    }
    rxport->mp_Node.ln_Name = pname;
    rxport->mp_Node.ln_Pri = 0;
    if (n < 100)
        AddPort(rxport);
    Permit();
    if (n >= 100) {
        DeleteMsgPort(rxport);
        rxport = NULL;
    }
}

static BOOL lib_open(void)
{
    if (!RexxSysBase)
        RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)RXSNAME, 0);
    return RexxSysBase != NULL;
}

/* ---- answers ---- */

static BOOL res_room(LONG n)
{
    if (n <= rcap)
        return TRUE;
    if (rbuf)
        FreeVec(rbuf);
    rcap = (n + 1023) & ~1023;
    if (!(rbuf = AllocVec(rcap, MEMF_ANY)))
        rcap = 0;
    return rbuf != NULL;
}

static void res_set(const char *s, LONG n)
{
    if (n < 0)
        n = strlen(s);
    rlen = 0;
    if (res_room(n + 1)) {
        CopyMem((APTR)s, rbuf, n);
        rlen = n;
        rbuf[n] = 0;
    }
}

static void res_num(LONG v)
{
    char s[16] = "";
    numstr_cat(s, v);
    res_set(s, -1);
}

/* rc, and with OPTIONS RESULTS and rc 0 the text in res (rlen bytes). */
static void reply(struct RexxMsg *rm, LONG rc, BOOL res)
{
    rm->rm_Result1 = rc;
    rm->rm_Result2 = 0;
    if (!rc && res && (rm->rm_Action & RXFF_RESULT) && lib_open())
        rm->rm_Result2 = (LONG)CreateArgstring((STRPTR)(rbuf ? rbuf : ""),
                                               rbuf ? rlen : 0);
    ReplyMsg((struct Message *)rm);
}

static void pend_free(struct Pend *p)
{
    struct Pend **q;
    for (q = &pend; *q; q = &(*q)->next)
        if (*q == p) {
            *q = p->next;
            break;
        }
    FreeVec(p);
}

static struct Pend *pend_add(struct RexxMsg *rm, int kind, LONG extra)
{
    struct Pend *p = AllocVec(sizeof *p + extra, MEMF_ANY | MEMF_CLEAR), **q;
    if (!p)
        return NULL;
    p->rm = rm;
    p->kind = kind;
    for (q = &pend; *q; q = &(*q)->next)
        ;
    *q = p;
    return p;
}

/* ---- the clock: WAITFOR's and DOWNLOAD's time limits ---- */

static void tick_go(void)
{
    if (ton || !rxport)
        return;
    if (!tio) {
        tio = (struct timerequest *)CreateIORequest(rxport, sizeof *tio);
        if (!tio)
            return;
        topen = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                            (struct IORequest *)tio, 0);
    }
    if (!topen)
        return;
    tio->tr_node.io_Command = TR_ADDREQUEST;
    tio->tr_time.tv_secs = 0;
    tio->tr_time.tv_micro = 500000;
    SendIO((struct IORequest *)tio);
    ton = TRUE;
}

/* Waits whose time is up: RC 5. The clock again while one has a limit. */
static void tick(void)
{
    struct Pend *p, *nx;
    ULONG now = ticks50();
    BOOL more = FALSE;
    for (p = pend; p; p = nx) {
        nx = p->next;
        if (p->kind == P_RUN || !p->due)
            continue;
        if (p->kind == P_XFER && p->tab->t_xfer)
            continue;                   /* it runs: no limit now */
        if (now >= p->due) {
            reply(p->rm, RC_WARN, FALSE);
            pend_free(p);
        } else
            more = TRUE;
    }
    if (more)
        tick_go();
}

/* ---- the tab commands go to ---- */

static BOOL tab_open(struct Tab *t)
{
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i] == t)
            return TRUE;
    return FALSE;
}

static struct Tab *target(void)
{
    if (rxtab && tab_open(rxtab))
        return rxtab;
    return shown ? shown : tabs[0];
}

static int tab_no(struct Tab *t)
{
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i] == t)
            return i + 1;
    return 0;
}

/* Text from t's host is kept for WAITFOR from now on. */
void rexx_watch(struct Tab *t)
{
    if (t && !t->t_watch)
        t->t_watch = AllocVec(sizeof(struct Watch), MEMF_ANY | MEMF_CLEAR);
}

/* ---- WAITFOR ---- */

static UBYTE fold(UBYTE c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return c - 32;
    return c;
}

/* s in w (any case) from byte from on: the end of the first match, or
 * -1. */
static LONG find(const struct Watch *w, const char *s, LONG from)
{
    LONG i, k, n = strlen(s);
    if (!n)
        return from <= w->n ? from : -1;
    for (i = from < 0 ? 0 : from; i + n <= w->n; i++) {
        for (k = 0; k < n && fold(w->b[i + k]) == fold((UBYTE)s[k]); k++)
            ;
        if (k == n)
            return i + n;
    }
    return -1;
}

/* A waiting WAITFOR's texts in t's text, from byte from: answered (and
 * what came up to its text taken off). TRUE: one was. */
static BOOL try_wait(struct Pend *p, LONG from)
{
    struct Watch *w = p->tab->t_watch;
    LONG at = -1, e;
    int i, which = 0;
    if (!w)
        return FALSE;
    for (i = 0; i < p->ntext; i++) {
        e = find(w, p->text[i], from - p->maxlen);
        if (e >= 0 && (at < 0 || e < at)) {
            at = e;
            which = i;
        }
    }
    if (at < 0)
        return FALSE;
    memmove(w->b, w->b + at, w->n - at);
    w->n -= at;
    res_set(p->said[which], -1);
    reply(p->rm, RC_OK, TRUE);
    pend_free(p);
    return TRUE;
}

/* Bytes from t's host (as they are logged): kept for WAITFOR. */
void rexx_feed(struct Tab *t, const UBYTE *b, LONG n)
{
    struct Watch *w = t->t_watch;
    struct Pend *p, *nx;
    LONG from = w->n, i;
    for (i = 0; i < n; i++) {
        UBYTE c = b[i];
        switch (w->st) {
        case 1:                         /* after ESC */
            w->st = c == '[' ? 2 : c == ']' || c == 'P' || c == '_' ||
                    c == '^' ? 3 : 0;
            continue;
        case 2:                         /* CSI: to its last letter */
            if (c >= 0x40 && c <= 0x7E)
                w->st = 0;
            continue;
        case 3:                         /* OSC, DCS: to BEL or ESC \ */
            if (c == 7)
                w->st = 0;
            else if (c == 27)
                w->st = 4;
            continue;
        case 4:
            w->st = c == '\\' ? 0 : 3;
            continue;
        }
        if (c == 27) {
            w->st = 1;
            continue;
        }
        if (c == 0x9B && !t->t_utf8 && !t->t_pcmode) {
            w->st = 2;                  /* the Amiga's CSI */
            continue;
        }
        if (!c)
            continue;
        if (t->t_utf8 && !t->t_pcmode && c >= 0x80) {
            if (c >= 0xC0) {            /* a letter begins */
                w->u8 = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
                w->cp = c & (c >= 0xF0 ? 0x07 : c >= 0xE0 ? 0x0F : 0x1F);
                continue;
            }
            if (!w->u8)
                continue;
            w->cp = (w->cp << 6) | (c & 63);
            if (--w->u8)
                continue;
            c = w->cp < 256 ? (UBYTE)w->cp : '?';
        }
        if (w->n >= WATCH) {            /* full: the older half goes */
            memmove(w->b, w->b + WATCH / 2, WATCH - WATCH / 2);
            w->n -= WATCH / 2;
            from = from > WATCH / 2 ? from - WATCH / 2 : 0;
        }
        w->b[w->n++] = c;
    }
    if (w->n == from)
        return;
    for (p = pend; p; p = nx) {
        nx = p->next;
        if (p->kind == P_WAIT && p->tab == t && try_wait(p, from))
            from = 0;                   /* the text moved */
    }
}

/* \r \n \t \e \\ \xHH in s, in place; the new length. */
static LONG unescape(char *s)
{
    char *o = s, *i = s;
    while (*i) {
        if (*i != '\\' || !i[1]) {
            *o++ = *i++;
            continue;
        }
        i++;
        switch (*i) {
        case 'r': *o++ = '\r'; i++; break;
        case 'n': *o++ = '\n'; i++; break;
        case 't': *o++ = '\t'; i++; break;
        case 'e': *o++ = 27; i++; break;
        case 'a': *o++ = 7; i++; break;
        case 'b': *o++ = 8; i++; break;
        case 'x': {
            int v = 0, k;
            i++;
            for (k = 0; k < 2 && isxdigit((UBYTE)*i); k++, i++)
                v = v * 16 + (*i <= '9' ? *i - '0' : (*i | 32) - 'a' + 10);
            *o++ = (char)v;
            break;
        }
        default: *o++ = *i++; break;
        }
    }
    *o = 0;
    return o - s;
}

/* ---- the commands ---- */

typedef LONG (*CmdFn)(struct RexxMsg *rm, LONG *a, BOOL top);

/* SEND and WAITFOR read their own words, not through ReadArgs: it would
 * take a star in quotes for its escape ("6*7" came as "67"). The words
 * of s, in place: a word in double quotes runs to the next quote not
 * after a backslash; quoted[i]: it was. */
static int words(char *s, char **w, BOOL *quoted, int max)
{
    int n = 0;
    while (n < max) {
        while (*s == ' ' || *s == '\t' || *s == '\n')
            s++;
        if (!*s)
            break;
        quoted[n] = *s == '"';
        if (quoted[n]) {
            w[n++] = ++s;
            while (*s && *s != '"')
                s += s[0] == '\\' && s[1] ? 2 : 1;
        } else {
            w[n++] = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n')
                s++;
        }
        if (*s)
            *s++ = 0;
    }
    return n;
}

/* WAITFOR "text" ["text"...] [TIMEOUT secs]: any of the texts, in any
 * case; RESULT the one that came. */
static LONG c_waitfor(struct RexxMsg *rm, LONG *a, BOOL top)
{
    char *w[MAXTEXT + 3];
    BOOL q[MAXTEXT + 3];
    char *t[MAXTEXT];
    struct Pend *p;
    LONG extra = 0, i, n = 0, secs = -1;
    int nw = words((char *)a[0], w, q, MAXTEXT + 3);
    char *s;
    struct Tab *tab = target();
    (void)top;
    for (i = 0; i < nw; i++)
        if (!q[i] && !stricmp(w[i], "TIMEOUT") && i + 1 < nw)
            secs = atol(w[++i]);
        else if (!q[i] && !stricmp(w[i], "TEXT"))
            ;                           /* the keyword: may be given */
        else if (n < MAXTEXT)
            t[n++] = w[i];
    if (!n)
        return RC_ERROR;
    for (i = 0; i < n; i++)
        extra += 2 * (strlen(t[i]) + 1);
    if (!(p = pend_add(rm, P_WAIT, extra)))
        return RC_FATAL;
    p->tab = tab;
    s = (char *)(p + 1);
    for (i = 0; i < n; i++) {
        LONG k;
        p->said[i] = s;
        strcpy(s, t[i]);
        s += strlen(s) + 1;
        p->text[i] = s;
        strcpy(s, t[i]);
        k = unescape(s);
        s += strlen(t[i]) + 1;
        if (k > p->maxlen)
            p->maxlen = k;
    }
    p->ntext = n;
    rexx_watch(tab);
    if (try_wait(p, 0))
        return HELD;                    /* there already: answered */
    if (!tab->t_local && tab->t_sock < 0 && !tab->t_st.ser &&
        tab->t_lfd < 0) {
        pend_free(p);                   /* no connection: it cannot come */
        return RC_ERROR;
    }
    if (secs >= 0) {
        if (secs == 0) {
            pend_free(p);
            return RC_WARN;
        }
        p->due = ticks50() + secs * 50;
        tick_go();
    }
    return HELD;
}

/* SEND [RAW] text: typed to the tab's host or Shell; the text as it is
 * (in quotes or not), with \r \n \e \t \xHH unless RAW. */
static LONG c_send(struct RexxMsg *rm, LONG *a, BOOL top)
{
    char *s = (char *)a[0], *e;
    struct Tab *keep = T;
    BOOL raw = FALSE;
    LONG n;
    (void)rm;
    if (!top)
        return LATER;
    for (;;) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!strnicmp(s, "RAW", 3) && (s[3] == ' ' || s[3] == '\t')) {
            raw = TRUE;
            s += 3;
        } else if (!strnicmp(s, "TEXT", 4) && (s[4] == ' ' || s[4] == '\t'))
            s += 4;
        else
            break;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    if (e - s >= 2 && s[0] == '"' && e[-1] == '"') {
        e[-1] = 0;                      /* the quotes */
        s++;
    }
    n = raw ? (LONG)strlen(s) : unescape(s);
    T = target();
    rexx_watch(T);
    if (T->t_watch)
        ((struct Watch *)T->t_watch)->n = 0;    /* what came before is old */
    T->t_rxdone = FALSE;
    if (local) {
        if (!con_paste(T->con, (UBYTE *)s, n)) {
            T = keep;
            return RC_ERROR;
        }
    } else if (!linked) {
        T = keep;
        return RC_ERROR;                /* no host: call one first */
    } else if (raw)
        send_data((UBYTE *)s, n);
    else
        send_typed((UBYTE *)s, n);
    T = keep;
    return RC_OK;
}

static LONG c_call(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *t = NULL;
    int rc;
    (void)rm;
    if (!top)
        return LATER;
    rc = call_rexx((char *)a[0], &t);
    if (t) {
        rxtab = t;
        if (!a[1] && rc == 0)
            rexx_after_call();          /* its login script */
    }
    T = shown;
    res_num(t ? tab_no(t) : 0);
    return rc;
}

static LONG c_hangup(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *keep = T;
    (void)rm; (void)a;
    if (!top)
        return LATER;
    T = target();
    if (local || !linked) {
        T = keep;
        return RC_WARN;
    }
    tab_hangup();
    T = keep;
    refresh();
    return RC_OK;
}

/* A row of t's screen as Latin-1, its spaces at the end left off. */
static LONG row_text(struct Tab *t, int r, char *out)
{
    int w, c, k = 0, end = 0;
    const VtCell *l = vt_line(t->t_vt, r, 0, &w);
    for (c = 0; c < w; c++) {
        ULONG cp = l[c].cp;
        if (cp == VT_WIDE_TAIL)
            continue;
        out[k++] = !cp ? ' ' : cp < 256 ? (char)cp : '?';
        if (cp && cp != ' ')
            end = k;
    }
    return end;
}

static LONG c_getline(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *t = target();
    int x, y, v, r;
    (void)rm; (void)top;
    term_sync(t);
    vt_cursor(t->t_vt, &x, &y, &v);
    r = a[0] ? *(LONG *)a[0] - 1 : y;
    if (r < 0 || r >= vt_rows(t->t_vt))
        return RC_ERROR;
    if (!res_room(vt_cols(t->t_vt) + 1))
        return RC_FATAL;
    rlen = row_text(t, r, rbuf);
    return RC_OK;
}

static LONG c_getscreen(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *t = target();
    int r, rs;
    LONG k = 0, last = 0;
    (void)rm; (void)a; (void)top;
    term_sync(t);
    rs = vt_rows(t->t_vt);
    if (!res_room((LONG)rs * (vt_cols(t->t_vt) + 1) + 1))
        return RC_FATAL;
    for (r = 0; r < rs; r++) {
        LONG m = row_text(t, r, rbuf + k);
        k += m;
        if (m)
            last = k;                   /* empty rows at the end go */
        rbuf[k++] = '\n';
    }
    rlen = last;
    return RC_OK;
}

static LONG c_cursor(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *t = target();
    int x, y, v;
    char s[24] = "";
    (void)rm; (void)a; (void)top;
    term_sync(t);
    vt_cursor(t->t_vt, &x, &y, &v);
    numstr_cat(s, y + 1);
    strcat(s, " ");
    numstr_cat(s, x + 1);
    res_set(s, -1);
    return RC_OK;
}

static LONG c_tab(struct RexxMsg *rm, LONG *a, BOOL top)
{
    char *w = (char *)a[0];
    int n = a[1] ? (int)*(LONG *)a[1] : 0;
    struct Tab *t = n ? (n >= 1 && n <= ntabs ? tabs[n - 1] : NULL)
                      : target();
    (void)rm;
    if (!stricmp(w, "COUNT")) {
        res_num(ntabs);
        return RC_OK;
    }
    if (!stricmp(w, "CURRENT")) {
        res_num(tab_no(target()));
        return RC_OK;
    }
    if (!t)
        return RC_ERROR;                /* no such tab */
    if (!stricmp(w, "NAME")) {
        char s[200];
        tab_label(t, s);
        res_set(s, -1);
        return RC_OK;
    }
    if (stricmp(w, "NEW") && stricmp(w, "SELECT") && stricmp(w, "CLOSE"))
        return RC_ERROR;
    if (!top)
        return LATER;
    if (!stricmp(w, "NEW")) {
        int had = ntabs;
        tab_new_shell();
        if (ntabs == had)
            return RC_ERROR;            /* nine already */
        rxtab = tabs[ntabs - 1];
        res_num(ntabs);
        return RC_OK;
    }
    if (!stricmp(w, "SELECT")) {
        if (!a[1])
            return RC_ERROR;
        tab_show(n - 1);
        rxtab = t;
        res_num(n);
        return RC_OK;
    }
    T = t;                              /* CLOSE */
    tab_close_current();
    T = shown;
    return RC_OK;
}

/* PROTOCOL's word: 0 Zmodem, 1 Ymodem, 2 Xmodem, 3 Hydra, -1 none such. */
static int proto(LONG a)
{
    static const char *const names[] = { "ZMODEM", "YMODEM", "XMODEM",
                                         "HYDRA" };
    int i;
    if (!a)
        return 0;
    for (i = 0; i < 4; i++)
        if (!stricmp((char *)a, (char *)names[i]))
            return i;
    return -1;
}

static LONG xfer_rc(int got)
{
    if (got < 0)
        return RC_ERROR;
    res_num(got);
    return got > 0 ? RC_OK : RC_WARN;
}

/* DOWNLOAD [FILE] [PROTOCOL] [TIMEOUT]: Zmodem and Hydra wait for the
 * host to begin (the reply comes when the transfer has ended); Ymodem
 * and Xmodem start now (Xmodem's FILE: the name to save it as). */
static LONG c_download(struct RexxMsg *rm, LONG *a, BOOL top)
{
    int p = proto(a[1]);
    struct Tab *t = target();
    struct Tab *keep = T;
    int got;
    if (p < 0)
        return RC_ERROR;
    if (p == 0 || p == 3) {
        struct Pend *w;
        if (t->t_rxdone && !t->t_xfer) {
            t->t_rxdone = FALSE;        /* one ended since the last SEND */
            return xfer_rc(t->t_rxgot);
        }
        if (!(w = pend_add(rm, P_XFER, 0)))
            return RC_FATAL;
        w->tab = t;
        w->due = ticks50() + (a[2] ? *(LONG *)a[2] : 60) * 50;
        tick_go();
        return HELD;
    }
    if (!top)
        return LATER;
    if (p == 2 && !a[0])
        return RC_ERROR;                /* Xmodem brings no name */
    T = t;
    got = transfer_rexx(p == 1 ? M_YRECV : M_XRECV, NULL, 0,
                        (char *)a[0]);
    T = keep;
    return xfer_rc(got);
}

/* UPLOAD FILE... [PROTOCOL]: sent now (Hydra: with the host's next
 * Hydra session, answered when it has ended). */
static LONG c_upload(struct RexxMsg *rm, LONG *a, BOOL top)
{
    int p = proto(a[1]), n;
    const char **f = (const char **)a[0];
    struct Tab *keep = T;
    int got;
    if (p < 0)
        return RC_ERROR;
    if (!top)
        return LATER;
    for (n = 0; f[n]; n++)
        ;
    T = target();
    if (p == 3) {
        struct Pend *w;
        if (!transfer_hydra_queue(f, n) || !(w = pend_add(rm, P_XFER, 0))) {
            T = keep;
            return RC_ERROR;
        }
        w->tab = T;
        T = keep;
        return HELD;
    }
    got = transfer_rexx(p == 0 ? M_ZSEND : p == 1 ? M_YSEND : M_XSEND,
                        f, n, NULL);
    T = keep;
    return xfer_rc(got);
}

/* A Zmodem upload the host asked for (its ZRINIT came) while an UPLOAD
 * for the tab waits in the queue: that UPLOAD's files, and it is
 * answered when the transfer ends. 0: none waits. */
int rexx_uploads(struct Tab *t, char (*store)[256], const char **paths,
                 int max)
{
    struct Pend *p;
    for (p = pend; p; p = p->next) {
        struct RDArgs *rda;
        LONG a[2] = { 0, 0 }, len;
        char *cmd = p->kind == P_RUN ? (char *)p->rm->rm_Args[0] : NULL;
        char *buf;
        int n = 0;
        if (!cmd || target() != t)
            continue;
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        if (strnicmp(cmd, "UPLOAD", 6) || (cmd[6] != ' ' && cmd[6] != '\t'))
            continue;
        len = strlen(cmd + 6);
        if (!(buf = AllocVec(len + 2, MEMF_ANY)))
            return 0;
        strcpy(buf, cmd + 6);
        strcat(buf, "\n");
        if ((rda = AllocDosObject(DOS_RDARGS, NULL))) {
            rda->RDA_Source.CS_Buffer = (UBYTE *)buf;
            rda->RDA_Source.CS_Length = len + 1;
            rda->RDA_Flags = RDAF_NOPROMPT;
            if (ReadArgs((STRPTR)"FILE/A/M,PROTOCOL/K", a, rda)) {
                char **f = (char **)a[0];
                if (proto(a[1]) == 0)
                    for (; f[n] && n < max; n++) {
                        strncpy(store[n], f[n], 255);
                        store[n][255] = 0;
                        paths[n] = store[n];
                    }
                FreeArgs(rda);
            }
            FreeDosObject(DOS_RDARGS, rda);
        }
        FreeVec(buf);
        if (n) {
            p->kind = P_XFER;           /* answered at the transfer's end */
            p->tab = t;
            p->began = TRUE;
            return n;
        }
    }
    return 0;
}

/* A transfer in t has ended (got: files, -1 failed): a DOWNLOAD or
 * UPLOAD waiting for it is answered; else it is kept for the next. */
void rexx_xfer_done(struct Tab *t, int got)
{
    struct Pend *p;
    for (p = pend; p; p = p->next)
        if (p->kind == P_XFER && p->tab == t) {
            LONG rc = xfer_rc(got);
            reply(p->rm, rc, rc == RC_OK || rc == RC_WARN);
            pend_free(p);
            return;
        }
    t->t_rxdone = TRUE;
    t->t_rxgot = got;
}

static LONG c_log(struct RexxMsg *rm, LONG *a, BOOL top)
{
    struct Tab *keep = T;
    BOOL ok = TRUE;
    (void)rm;
    if (!top)
        return LATER;
    T = target();
    if (!stricmp((char *)a[0], "OFF"))
        log_stop();
    else
        ok = log_to((char *)a[0]);
    T = keep;
    return ok ? RC_OK : RC_ERROR;
}

static LONG c_screen(struct RexxMsg *rm, LONG *a, BOOL top)
{
    (void)rm; (void)top;
    if (!scr)
        return RC_ERROR;
    if (!stricmp((char *)a[0], "TOFRONT"))
        ScreenToFront(scr);
    else if (!stricmp((char *)a[0], "TOBACK"))
        ScreenToBack(scr);
    else
        return RC_ERROR;
    return RC_OK;
}

static LONG c_window(struct RexxMsg *rm, LONG *a, BOOL top)
{
    char *w = (char *)a[0];
    (void)rm; (void)top;
    if (!win)
        return RC_ERROR;
    if (!stricmp(w, "TOFRONT") || !stricmp(w, "SHOW")) {
        ScreenToFront(scr);
        WindowToFront(win);
    } else if (!stricmp(w, "ACTIVATE")) {
        ScreenToFront(scr);
        WindowToFront(win);
        ActivateWindow(win);
    } else if (!stricmp(w, "TOBACK"))
        WindowToBack(win);
    else
        return RC_ERROR;                /* HIDE: not yet */
    return RC_OK;
}

static LONG c_version(struct RexxMsg *rm, LONG *a, BOOL top)
{
    (void)rm; (void)a; (void)top;
    res_set(version + 6, -1);           /* after "$VER: " */
    return RC_OK;
}

static LONG c_quit(struct RexxMsg *rm, LONG *a, BOOL top)
{
    (void)rm; (void)a;
    if (!top)
        return LATER;
    quit_all();
    return RC_OK;
}

/* "name" "name"...: the places, each in quotes (the Style Guide's way). */
static LONG c_phonebook(struct RexxMsg *rm, LONG *a, BOOL top)
{
    int i;
    (void)rm; (void)a; (void)top;
    rlen = 0;
    if (!res_room(nbook * 52 + 1))
        return RC_FATAL;
    for (i = 0; i < nbook; i++) {
        if (i)
            rbuf[rlen++] = ' ';
        rbuf[rlen++] = '"';
        strcpy(rbuf + rlen, book[i].name);
        rlen += strlen(book[i].name);
        rbuf[rlen++] = '"';
    }
    rbuf[rlen] = 0;
    return RC_OK;
}

static LONG c_help(struct RexxMsg *rm, LONG *a, BOOL top);

/* tmpl NULL: the command reads its own words (a[0]: the line); use:
 * what HELP shows for it. */
static const struct {
    const char *name, *tmpl, *use;
    CmdFn fn;
} cmds[] = {
    { "CALL", "NAME/A,NOSCRIPT/S", NULL, c_call },
    { "HANGUP", "", NULL, c_hangup },
    { "SEND", NULL, "RAW/S,TEXT/A/F", c_send },
    { "WAITFOR", NULL, "TEXT/A/M,TIMEOUT/K/N", c_waitfor },
    { "GETLINE", "ROW/N", NULL, c_getline },
    { "GETSCREEN", "", NULL, c_getscreen },
    { "CURSOR", "", NULL, c_cursor },
    { "TAB", "WHAT/A,N/N", NULL, c_tab },
    { "DOWNLOAD", "FILE,PROTOCOL/K,TIMEOUT/K/N", NULL,
      c_download },
    { "UPLOAD", "FILE/A/M,PROTOCOL/K", NULL, c_upload },
    { "LOG", "FILE/A", NULL, c_log },
    { "SCREEN", "WHAT/A", NULL, c_screen },
    { "WINDOW", "WHAT/A", NULL, c_window },
    { "PHONEBOOK", "", NULL, c_phonebook },
    { "VERSION", "", NULL, c_version },
    { "HELP", "COMMAND", NULL, c_help },
    { "QUIT", "", NULL, c_quit },
};
#define NCMDS ((int)(sizeof cmds / sizeof cmds[0]))

/* HELP: the commands; HELP name: its template. */
static LONG c_help(struct RexxMsg *rm, LONG *a, BOOL top)
{
    int i;
    (void)rm; (void)top;
    if (a[0]) {
        for (i = 0; i < NCMDS; i++)
            if (!stricmp((char *)a[0], (char *)cmds[i].name)) {
                res_set(cmds[i].use ? cmds[i].use : cmds[i].tmpl, -1);
                return RC_OK;
            }
        return RC_ERROR;
    }
    rlen = 0;
    if (!res_room(NCMDS * 12))
        return RC_FATAL;
    for (i = 0; i < NCMDS; i++) {
        if (i)
            rbuf[rlen++] = ' ';
        strcpy(rbuf + rlen, cmds[i].name);
        rlen += strlen(cmds[i].name);
    }
    rbuf[rlen] = 0;
    return RC_OK;
}

/* One command: parsed (ReadArgs), run or queued, answered or kept. */
static void command(struct RexxMsg *rm, BOOL top)
{
    char *s = (char *)rm->rm_Args[0], *buf;
    struct RDArgs *rda;
    LONG a[4] = { 0, 0, 0, 0 }, rc = RC_ERROR, len;
    int i, k;
    if (!s) {
        reply(rm, RC_ERROR, FALSE);
        return;
    }
    while (*s == ' ' || *s == '\t')
        s++;
    for (k = 0; s[k] && s[k] != ' ' && s[k] != '\t' && s[k] != '\n'; k++)
        ;
    for (i = 0; i < NCMDS; i++)
        if ((int)strlen(cmds[i].name) == k &&
            !strnicmp(s, (char *)cmds[i].name, k))
            break;
    if (i == NCMDS) {
        reply(rm, RC_ERROR, FALSE);     /* no such command */
        return;
    }
    s += k;
    len = strlen(s);
    if (!(buf = AllocVec(len + 2, MEMF_ANY))) {
        reply(rm, RC_FATAL, FALSE);
        return;
    }
    strcpy(buf, s);
    strcat(buf, "\n");                  /* ReadArgs wants the line's end */
    if (!cmds[i].tmpl) {
        a[0] = (LONG)buf;
        rlen = 0;
        rc = cmds[i].fn(rm, a, top);
    } else if ((rda = AllocDosObject(DOS_RDARGS, NULL))) {
        rda->RDA_Source.CS_Buffer = (UBYTE *)buf;
        rda->RDA_Source.CS_Length = len + 1;
        rda->RDA_Flags = RDAF_NOPROMPT;
        if (ReadArgs((STRPTR)cmds[i].tmpl, a, rda)) {
            rlen = 0;
            if (rbuf)
                rbuf[0] = 0;
            rc = cmds[i].fn(rm, a, top);
            FreeArgs(rda);
        }
        FreeDosObject(DOS_RDARGS, rda);
    }
    FreeVec(buf);
    if (rc == LATER) {
        if (!pend_add(rm, P_RUN, 0))
            reply(rm, RC_FATAL, FALSE);
    } else if (rc != HELD)
        reply(rm, rc, rc == RC_OK);
}

/* A script CTerm started has ended: an error is said in its tab. */
static void script_back(struct RexxMsg *rm)
{
    struct Script **q, *s;
    for (q = &scripts; (s = *q); q = &s->next)
        if (s->rm == rm)
            break;
    if (s && rm->rm_Result1 && !closing && tab_open(s->tab)) {
        char m[120];
        struct Tab *keep = T;
        T = s->tab;
        strcpy(m, "\r\n\x1b[1;31m[ARexx script ");
        strcat(m, s->name);
        strcat(m, ": error ");
        numstr_cat(m, rm->rm_Result1);
        if (rm->rm_Result2) {
            strcat(m, ", ARexx error ");
            numstr_cat(m, rm->rm_Result2);
        }
        strcat(m, "]\x1b[0m\r\n");
        say(m);
        T = keep;
        refresh();
    }
    if (s) {
        *q = s->next;
        FreeVec(s);
    }
    DeleteArgstring((UBYTE *)rm->rm_Args[0]);
    DeleteRexxMsg(rm);
}

/* What came to the port: answered, queued or kept. Safe anywhere: what
 * would change things waits for rexx_run (see the top). */
void rexx_serve(void)
{
    struct Message *m;
    if (!rxport)
        return;
    while ((m = GetMsg(rxport))) {
        if (tio && m == &tio->tr_node.io_Message) {
            ton = FALSE;
            tick();
        } else if (m->mn_Node.ln_Type == NT_REPLYMSG)
            script_back((struct RexxMsg *)m);
        else if (!lib_open() || !IsRexxMsg((struct RexxMsg *)m))
            ReplyMsg(m);                /* not ARexx's: back as it came */
        else
            command((struct RexxMsg *)m, FALSE);
    }
}

/* The queued commands, run where nothing is half done (run(), between
 * rounds). Commands that come meanwhile (their waits read the port)
 * join the queue and run here too. */
void rexx_run(void)
{
    struct Pend *p;
    if (running)
        return;
    running = TRUE;
    for (;;) {
        for (p = pend; p && p->kind != P_RUN; p = p->next)
            ;
        if (!p || quitting)
            break;
        {
            struct RexxMsg *rm = p->rm;
            pend_free(p);
            T = shown;
            command(rm, TRUE);
            T = shown;
        }
    }
    running = FALSE;
}

/* ---- starting scripts ---- */

/* A script, with ADDRESS set to CTerm's port and the port's name as its
 * argument; t: the tab it works in (its commands go there). */
BOOL rexx_start(const char *path, struct Tab *t)
{
    struct RexxMsg *rm;
    struct MsgPort *rp;
    struct Script *s;
    char cmd[300];
    BOOL spaced;
    struct Tab *keep = T;
    T = t ? t : shown;
    if (!rxport || !lib_open()) {
        say("\r\n\x1b[1;31m[ARexx is not there (rexxsyslib.library)]"
            "\x1b[0m\r\n");
        T = keep;
        return FALSE;
    }
    /* ARexx takes the command's first word as the script's name, and a
     * name in quotes as the program itself: a name with a space in it
     * ("Ram Disk:x.rexx") is called from a one-line program instead */
    if ((spaced = strchr(path, ' ') != NULL)) {
        const char *p;
        int k;
        strcpy(cmd, "CALL '");
        for (p = path, k = 6; *p && k < (int)sizeof cmd - 24; p++) {
            if (*p == '\'')
                cmd[k++] = '\'';      /* ' in it: doubled */
            cmd[k++] = *p;
        }
        cmd[k] = 0;
        strcat(cmd, "' '");
        strcat(cmd, pname);
        strcat(cmd, "'");
    } else {
        strncpy(cmd, path, sizeof cmd - 20);
        cmd[sizeof cmd - 20] = 0;
        strcat(cmd, " ");
        strcat(cmd, pname);
    }
    s = AllocVec(sizeof *s, MEMF_ANY | MEMF_CLEAR);
    rm = CreateRexxMsg(rxport, (STRPTR)"rexx", (STRPTR)pname);
    if (!s || !rm ||
        !(rm->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)cmd,
                                                    strlen(cmd)))) {
        if (rm)
            DeleteRexxMsg(rm);
        if (s)
            FreeVec(s);
        T = keep;
        return FALSE;
    }
    rm->rm_Action = RXCOMM | (spaced ? RXFF_STRING : 0);
    Forbid();
    if ((rp = FindPort((STRPTR)RXSDIR)))
        PutMsg(rp, (struct Message *)rm);
    Permit();
    if (!rp) {
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
        FreeVec(s);
        say("\r\n\x1b[1;31m[ARexx is not running: start SYS:System/RexxMast]"
            "\x1b[0m\r\n");
        refresh();
        T = keep;
        return FALSE;
    }
    s->rm = rm;
    s->tab = T;
    strncpy(s->name, (char *)FilePart((STRPTR)path), sizeof s->name - 1);
    s->next = scripts;
    scripts = s;
    rxtab = T;
    rexx_watch(T);
    T = keep;
    return TRUE;
}

/* A call has connected in T: its phone book place's login script. */
void rexx_after_call(void)
{
    if (T->t_script[0])
        rexx_start(T->t_script, T);
}

/* Project/Run ARexx script...: chosen in REXX:. */
void rexx_menu(void)
{
    struct FileRequester *req;
    char path[256];
    if (!asl_open())
        return;
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: run an ARexx script",
        ASLFR_InitialDrawer, (ULONG)"REXX:",
        ASLFR_InitialPattern, (ULONG)"#?.rexx", ASLFR_DoPatterns, TRUE,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL) && req->fr_File[0]) {
        strncpy(path, (char *)req->fr_Drawer, sizeof path - 1);
        path[sizeof path - 1] = 0;
        AddPart((STRPTR)path, req->fr_File, sizeof path);
        rexx_start(path, T);
    }
    FreeAslRequest(req);
    redraw_all();                       /* where the requester was */
}

/* ---- tabs going, the end ---- */

/* t goes: what waits for it is answered (RC 10), its text freed. */
void rexx_tab_gone(struct Tab *t)
{
    struct Pend *p, *nx;
    struct Script *s;
    for (p = pend; p; p = nx) {
        nx = p->next;
        if (p->kind != P_RUN && p->tab == t) {
            reply(p->rm, RC_ERROR, FALSE);
            pend_free(p);
        }
    }
    for (s = scripts; s; s = s->next)
        if (s->tab == t)
            s->tab = NULL;
    if (rxtab == t)
        rxtab = NULL;
    if (t->t_watch) {
        FreeVec(t->t_watch);
        t->t_watch = NULL;
    }
}

/* t's connection has ended: a WAITFOR for its text will not see it. */
void rexx_tab_over(struct Tab *t)
{
    struct Pend *p, *nx;
    for (p = pend; p; p = nx) {
        nx = p->next;
        if (p->kind == P_WAIT && p->tab == t) {
            reply(p->rm, RC_ERROR, FALSE);
            pend_free(p);
        }
    }
}

/* Every message on the port answered with RC 20 (scripts' replies
 * taken). */
static void drain(void)
{
    struct Message *m;
    while ((m = GetMsg(rxport))) {
        if (tio && m == &tio->tr_node.io_Message)
            ton = FALSE;
        else if (m->mn_Node.ln_Type == NT_REPLYMSG)
            script_back((struct RexxMsg *)m);
        else {
            ((struct RexxMsg *)m)->rm_Result1 = RC_FATAL;
            ((struct RexxMsg *)m)->rm_Result2 = 0;
            ReplyMsg(m);
        }
    }
}

/* At the end: the port out of the list first (no new messages), then
 * everything on it and everything kept answered with RC 20. Scripts CTerm
 * started are waited for a little (their end comes back to the port);
 * one still running after that gets a port that ignores it, left in
 * memory, rather than a reply into freed memory. */
void rexx_close(void)
{
    int i;
    if (!rxport)
        return;
    closing = TRUE;
    Forbid();
    RemPort(rxport);
    drain();
    Permit();
    while (pend) {
        reply(pend->rm, RC_FATAL, FALSE);
        pend_free(pend);
    }
    if (ton) {
        AbortIO((struct IORequest *)tio);
        WaitIO((struct IORequest *)tio);
        ton = FALSE;
    }
    if (tio) {
        if (topen)
            CloseDevice((struct IORequest *)tio);
        DeleteIORequest((struct IORequest *)tio);
        tio = NULL;
    }
    for (i = 0; i < 60 && scripts; i++) {   /* 3 s at most */
        Delay(2);
        drain();
    }
    for (i = 0; i < ntabs; i++)
        rexx_tab_gone(tabs[i]);
    if (scripts) {
        Forbid();
        rxport->mp_Flags = PA_IGNORE;     /* its replies just lie there */
        drain();
        FreeSignal(rxport->mp_SigBit);
        Permit();
    } else
        DeleteMsgPort(rxport);
    rxport = NULL;
    if (rbuf)
        FreeVec(rbuf);
    rbuf = NULL;
    rcap = 0;
    if (RexxSysBase && !scripts)
        CloseLibrary((struct Library *)RexxSysBase);
}
