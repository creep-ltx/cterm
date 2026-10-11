/*
 * files - the file browser for SFTP and FTP places (Parts/Files, with
 * ftp.c and askwin.c): a window listing the drawer the host is in
 * (directories first), with Parent, a path to type, Refresh, Download,
 * Upload..., New drawer..., Rename..., Delete and Stop.
 *
 * Each place has a tab of its own, where its messages go (the host key
 * question, a password asked, the host's words); the window and the tab
 * are one thing: closing the window hangs up and closes the tab, closing
 * the tab closes the window. The part stays loaded while a place is
 * connected and leaves memory with the last one.
 *
 * Every wait (await) is CTerm's own round, loop_round: the other tabs,
 * the window and this one go on while a listing or a file comes. A
 * transfer shows as Zmodem's does (the tab's panel and name, the
 * Transfers window), Esc and Stop end it, one at a time in all of CTerm.
 * A download is made as name.part and renamed when whole; a later
 * download of the same file goes on from the part (FTP's REST, a seek
 * in SFTP); a file there is never written over (a number is added); a
 * name from the host is made safe first (no ':' or '/' to climb out of
 * the drawer). SFTP is CTermSSH's (sftp_ops); FTP is ftp.c.
 */
#include "cterm.h"
#include "files.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

#define BUFSZ  32768                    /* a transfer's bytes at a time */
#define OPS    sftp_ops

struct Library *FuelGaugeBase = NULL;   /* the part's own */
static struct RConn *conns;             /* every place connected */
static int tidy;                        /* ending what was begun: a Stop
                                           does not stop it */
static struct RConn *asking;            /* its password, for f_ask */
static BOOL asked;

/* ---- small things ---- */

static struct RConn *conn_of(struct Tab *t)
{
    struct RConn *c;
    for (c = conns; c && c->tab != t; c = c->next)
        ;
    return c;
}

/* Text in the place's tab (a, b, d: any may be NULL). */
static void status(struct RConn *c, const char *a, const char *b);
static void to_utf8(const char *in, char *out, int max);

/* Text in the place's tab (a, b, d: any may be NULL; Latin-1, as UTF-8
 * for a UTF-8 tab); a failure (red) is in the window's status line too,
 * its colours and brackets off. */
void tsay(struct RConn *c, const char *a, const char *b, const char *d)
{
    struct Tab *keep = T;
    const char *p[3];
    char u[512];
    int i;
    T = c->tab;
    p[0] = a; p[1] = b; p[2] = d;
    for (i = 0; i < 3; i++)
        if (p[i]) {
            if (T->t_utf8 && !T->t_pcmode) {
                to_utf8(p[i], u, sizeof u);
                say(u);
            } else
                say(p[i]);
        }
    refresh();
    T = keep;
    if (a && !strncmp(a, "\x1b[1;31m", 7)) {
        char m[160], *q = m;
        const char *p[3], *s;
        int i;
        p[0] = a; p[1] = b; p[2] = d;
        for (i = 0; i < 3; i++)
            for (s = p[i]; s && *s && q < m + sizeof m - 1; s++) {
                if (*s == 0x1b) {       /* ESC [ ... m */
                    while (s[1] && *s != 'm')
                        s++;
                    continue;
                }
                if (*s >= ' ' && *s != '[' && *s != ']')
                    *q++ = *s;
            }
        *q = 0;
        c->failed = TRUE;
        status(c, m, NULL);
    }
}

BOOL given_up(struct RConn *c)
{
    return (!tidy && (c->stop || (c->x && c->x->stop))) ||
           c->tab->ended || quitting;
}

/* Wait until fd can be read (SF_IN) or written (SF_OUT), CTerm going on
 * meanwhile; at most secs (then timedout). FALSE: given up. */
BOOL await(struct RConn *c, LONG fd, int dirs, LONG secs)
{
    ULONG until = ticks50() + (ULONG)secs * 50;
    c->timedout = FALSE;
    for (;;) {
        BOOL ready;
        if (given_up(c))
            return FALSE;
        loop_wfd = dirs & SF_OUT ? fd : -1;
        ready = loop_round(dirs & SF_IN ? fd : -1, 1000);
        loop_wfd = -1;
        if (given_up(c))
            return FALSE;
        if (ready)
            return TRUE;
        if (ticks50() >= until) {
            c->timedout = TRUE;
            tsay(c, "\x1b[1;31m[the host stopped answering]\x1b[0m\r\n",
                 NULL, NULL);
            return FALSE;
        }
    }
}

/* Days since 1970 to a date (H. Hinnant's civil_from_days). */
void ymd(LONG z, int *y, int *m, int *d)
{
    LONG era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

LONG today(void)
{
    struct DateStamp ds;
    int y, m, d;
    DateStamp(&ds);
    ymd(ds.ds_Days + 2922, &y, &m, &d);         /* 1978 -> 1970 */
    return y * 10000L + m * 100 + d;
}

/* The host's UTF-8 as Latin-1, the Amiga's: what Latin-1 has not
 * becomes bad; bytes that are not UTF-8 are taken as Latin-1 already. */
static void to_latin1(const char *in, int len, char *out, int max, char bad)
{
    const UBYTE *p = (const UBYTE *)in;
    int k = 0;
    while (len > 0 && k < max - 1) {
        UBYTE u = *p;
        int n = 1;
        if (u >= 0xC0 && len >= 2 && (p[1] & 0xC0) == 0x80) {
            n = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : 2;
            if (n > len)
                n = len;
            u = n == 2 && u < 0xC4 ? (UBYTE)(((u & 0x1F) << 6) |
                                               (p[1] & 0x3F)) : bad;
        }
        out[k++] = u < 32 || u == 127 ? bad : u;
        p += n;
        len -= n;
    }
    out[k] = 0;
}

/* A line from the host (UTF-8), dim in the place's tab. */
void tsay_host(struct RConn *c, const char *line)
{
    char l[256];
    to_latin1(line, strlen(line), l, sizeof l, '?');
    tsay(c, "\x1b[2m", l, "\x1b[0m\r\n");
}

/* A message with a host's name (UTF-8) in it, in the place's tab. */
static void tsay_name(struct RConn *c, const char *a, const char *name,
                      const char *d)
{
    char l[256];
    to_latin1(name, strlen(name), l, sizeof l, '?');
    tsay(c, a, l, d);
}

/* The Amiga's Latin-1 as UTF-8, for the host. */
static void to_utf8(const char *in, char *out, int max)
{
    int k = 0;
    for (; *in && k < max - 2; in++) {
        UBYTE u = *in;
        if (u >= 0x80) {
            out[k++] = 0xC0 | (u >> 6);
            out[k++] = 0x80 | (u & 0x3F);
        } else
            out[k++] = u;
    }
    out[k] = 0;
}

/* A name from the host, safe as a file's name in a drawer: its last
 * part, no ':' '/' '\' or controls (AddPart would let "SYS:x" or "/x"
 * replace or climb out of the drawer), at most 100 letters. */
static void local_name(const char *rname, char *out)
{
    const char *p;
    char *q;
    for (p = rname; *rname; rname++)
        if (*rname == '/')
            p = rname + 1;
    to_latin1(p, strlen(p), out, 101, '_');
    for (q = out; *q; q++)
        if (*q == ':' || *q == '/' || *q == '\\')
            *q = '_';
    if (!out[0] || !strcmp(out, ".") || !strcmp(out, ".."))
        strcpy(out, "file");
}

static void size_text(ULONG n, char *o)
{
    o[0] = 0;
    if (n < 10000)
        numstr_cat(o, n);
    else if (n < 10000UL * 1024) {
        numstr_cat(o, (n + 512) / 1024);
        strcat(o, " KB");
    } else {
        numstr_cat(o, (n + 524288) / 1048576);
        strcat(o, " MB");
    }
}

static const char *ename(struct RConn *c, int i)
{
    return c->pool + c->ent[i].noff;
}

/* A name in the drawer the host is in: its whole path (SFTP). */
static void rpath(struct RConn *c, const char *name, char *out, int max)
{
    out[0] = 0;
    if (name[0] != '/' && c->cwd[0]) {  /* none yet: as the host takes it
                                         * ("." is the home drawer) */
        strncat(out, c->cwd, max - 2);
        if (!out[0] || out[strlen(out) - 1] != '/')
            strcat(out, "/");
    }
    strncat(out, name, max - 1 - strlen(out));
}

static LONG fsize(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    struct FileInfoBlock *fib;
    LONG n = -1;
    if (!l)
        return -1;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(l, fib))
            n = fib->fib_DirEntryType < 0 ? fib->fib_Size : -2;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    return n;                           /* -2: a drawer */
}

/* The drawer downloads go to: the place's own, the Files tab's, or
 * PROGDIR:Downloads (made when missing). */
static void down_dir(struct RConn *c, char *dir)
{
    BPTR l;
    strcpy(dir, c->tab->t_down[0] ? c->tab->t_down : dldir[0] ? dldir
                                                   : "PROGDIR:Downloads");
    if (!(l = Lock((STRPTR)dir, ACCESS_READ)))
        l = CreateDir((STRPTR)dir);
    if (l)
        UnLock(l);
}

/* ---- the listing ---- */

void ent_clear(struct RConn *c)
{
    c->nent = 0;
    c->plen = 0;
}

BOOL ent_add(struct RConn *c, const char *name, int len, ULONG size,
             const char *date, int dir)
{
    struct REnt *e;
    if (c->nent == c->capent) {
        int cap = c->capent ? c->capent * 2 : 64;
        struct REnt *n = AllocVec(cap * sizeof *n, MEMF_ANY);
        if (!n)
            return FALSE;
        if (c->ent) {
            CopyMem(c->ent, n, c->nent * sizeof *n);
            FreeVec(c->ent);
        }
        c->ent = n;
        c->capent = cap;
    }
    if (c->plen + len + 1 > c->pcap) {
        LONG cap = c->pcap ? c->pcap * 2 : 4096;
        char *n;
        while (cap < c->plen + len + 1)
            cap *= 2;
        if (!(n = AllocVec(cap, MEMF_ANY)))
            return FALSE;
        if (c->pool) {
            CopyMem(c->pool, n, c->plen);
            FreeVec(c->pool);
        }
        c->pool = n;
        c->pcap = cap;
    }
    e = &c->ent[c->nent++];
    e->noff = c->plen;
    memcpy(c->pool + c->plen, name, len);
    c->pool[c->plen + len] = 0;
    c->plen += len + 1;
    e->size = size;
    strncpy(e->date, date, sizeof e->date - 1);
    e->date[sizeof e->date - 1] = 0;
    e->dir = dir;
    return TRUE;
}

static void ent_free(struct RConn *c)
{
    if (c->ent)
        FreeVec(c->ent);
    if (c->pool)
        FreeVec(c->pool);
    c->ent = NULL;
    c->pool = NULL;
    c->nent = c->capent = 0;
    c->plen = c->pcap = 0;
}

/* Drawers first, then by name (any case): a shell sort. */
static int ent_cmp(struct RConn *c, const struct REnt *a,
                   const struct REnt *b)
{
    if ((a->dir == 1) != (b->dir == 1))
        return a->dir == 1 ? -1 : 1;
    return stricmp(c->pool + a->noff, c->pool + b->noff);
}

static void ent_sort(struct RConn *c)
{
    int gap, i, j;
    for (gap = c->nent / 2; gap > 0; gap /= 2)
        for (i = gap; i < c->nent; i++) {
            struct REnt t = c->ent[i];
            for (j = i; j >= gap && ent_cmp(c, &c->ent[j - gap], &t) > 0;
                 j -= gap)
                c->ent[j] = c->ent[j - gap];
            c->ent[j] = t;
        }
}

/* The entry of this name (as the host has it, or as the Amiga shows
 * it), -1 none. */
static int ent_find(struct RConn *c, const char *name)
{
    int i;
    char seen[256];
    for (i = 0; i < c->nent; i++)
        if (!strcmp(ename(c, i), name))
            return i;
    for (i = 0; i < c->nent; i++) {
        to_latin1(ename(c, i), strlen(ename(c, i)), seen, sizeof seen,
                  '?');
        if (!strcmp(seen, name))
            return i;
    }
    return -1;
}

/* A name the Amiga gave (a script, a requester), as the host has it. */
static void host_name(struct RConn *c, const char *name, char *out, int max)
{
    int i = ent_find(c, name);
    if (i >= 0) {
        strncpy(out, ename(c, i), max - 1);
        out[max - 1] = 0;
    } else
        to_utf8(name, out, max);
}

/* ---- SFTP's waits and failures ---- */

static BOOL sf_wait(struct RConn *c)
{
    int d = OPS->sf_dirs(c->sf);
    return await(c, OPS->sf_socket(c->sf), d ? d : SF_IN, 60);
}

static void sf_drop(struct RConn *c)
{
    if (c->sf)
        OPS->sf_close(c->sf);
    c->sf = c->sfh = NULL;
}

/* What went wrong, said; a connection left in the middle of a call (or
 * gone) is dropped: the next command calls again. FALSE always. */
static BOOL sf_fail(struct RConn *c, LONG rc, const char *what)
{
    static const char *const why[] = { "", "the end", "no such file",
        "not allowed", "it failed", "", "", "", "the host cannot",
        "", "no such drawer", "", "it is write-protected", "",
        "the host's disk is full", "", "", "", "the drawer is not empty",
        "not a drawer", "a name the host does not take" };
    if (rc == SF_AGAIN || SF_LOST(rc) || !c->sf) {
        if (SF_LOST(rc))
            c->lost = TRUE;
        sf_drop(c);
        return FALSE;
    }
    if (rc < 0)
        rc = OPS->sf_error(c->sf);
    tsay_name(c, "\x1b[1;31m[", what, ": ");
    tsay(c, rc > 0 && rc <= 20 && why[rc][0] ? why[rc]
                                              : "the host said no",
         "]\x1b[0m\r\n", NULL);
    return FALSE;
}

/* realpath: where a path leads, into the cwd */
static BOOL sf_cd(struct RConn *c, const char *path)
{
    char out[400], full[400];
    LONG rc;
    SfAttr a;
    rpath(c, path, full, sizeof full);
    while ((rc = OPS->sf_realpath(c->sf, full, out, sizeof out)) ==
           SF_AGAIN && sf_wait(c))
        ;
    if (rc < 0)
        return sf_fail(c, rc, full);
    while ((rc = OPS->sf_stat(c->sf, out, &a)) == SF_AGAIN && sf_wait(c))
        ;
    if (rc)
        return sf_fail(c, rc, out);
    if (!a.dir) {
        tsay_name(c, "\x1b[1;31m[", out, ": not a drawer]\x1b[0m\r\n");
        return FALSE;
    }
    strcpy(c->cwd, out);
    return TRUE;
}

static BOOL sf_list(struct RConn *c)
{
    void *dh;
    LONG rc, r2;
    char name[512], date[18];
    SfAttr a;
    int i, links = 0;
    while ((rc = OPS->sf_opendir(c->sf, c->cwd, &dh)) == SF_AGAIN &&
           sf_wait(c))
        ;
    if (rc)
        return sf_fail(c, rc, c->cwd);
    ent_clear(c);
    for (;;) {
        while ((rc = OPS->sf_readdir(dh, name, sizeof name, &a)) ==
               SF_AGAIN && sf_wait(c))
            ;
        if (rc <= 0)
            break;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        date[0] = 0;
        if (a.mtime) {
            int y, m, d, mi = a.mtime % 86400 / 60;
            ymd(a.mtime / 86400, &y, &m, &d);
            /* as ftp.c writes them */
            date[0] = 0;
            numstr_cat(date, y);
            strcat(date, m < 10 ? "-0" : "-");
            numstr_cat(date, m);
            strcat(date, d < 10 ? "-0" : "-");
            numstr_cat(date, d);
            strcat(date, mi / 60 < 10 ? " 0" : " ");
            numstr_cat(date, mi / 60);
            strcat(date, mi % 60 < 10 ? ":0" : ":");
            numstr_cat(date, mi % 60);
        }
        ent_add(c, name, rc, a.dir ? 0 : a.size, date,
                a.dir ? 1 : a.link ? 2 : 0);
    }
    tidy++;
    while ((r2 = OPS->sf_closedir(dh)) == SF_AGAIN && sf_wait(c))
        ;
    tidy--;
    if (rc < 0)
        return sf_fail(c, rc, c->cwd);
    /* links followed, to tell drawers from files (the first 32) */
    for (i = 0; i < c->nent && links < 32; i++)
        if (c->ent[i].dir == 2) {
            links++;
            rpath(c, ename(c, i), name, sizeof name);
            while ((rc = OPS->sf_stat(c->sf, name, &a)) == SF_AGAIN &&
                   sf_wait(c))
                ;
            if (rc == SF_AGAIN)
                return sf_fail(c, rc, name);
            if (!rc) {
                c->ent[i].dir = a.dir ? 1 : 0;
                c->ent[i].size = a.dir ? 0 : a.size;
            }
        }
    return TRUE;
}

/* ---- the place's commands, either way ---- */

/* SFTP's password: the phone book's (or one typed before) once, then
 * asked in the tab as ssh asks. */
static BOOL f_ask(const char *prompt, char *buf, int max, BOOL echo)
{
    struct RConn *c = asking;
    if (!echo && c && c->pass[0] && !asked) {
        asked = TRUE;
        strncpy(buf, c->pass, max - 1);
        buf[max - 1] = 0;
        tsay(c, prompt, "(given)\r\n", NULL);
        return TRUE;
    }
    if (!term_ask(prompt, buf, max, echo))
        return FALSE;
    if (!echo && c) {
        strncpy(c->pass, buf, sizeof c->pass - 1);
        c->pass[sizeof c->pass - 1] = 0;
    }
    return TRUE;
}

static BOOL is_up(struct RConn *c)
{
    return c->kind == FK_FTP ? c->cfd >= 0 : c->sf != NULL;
}

static void r_drop(struct RConn *c)
{
    if (c->kind == FK_FTP)
        ftp_close(c);
    else
        sf_drop(c);
}

static BOOL r_connect(struct RConn *c)
{
    BOOL ok;
    c->lost = c->timedout = FALSE;
    T = c->tab;
    if (c->kind == FK_FTP)
        return ftp_open(c);
    if (!OPS) {
        tsay(c, "\x1b[1;31m[SFTP is CTermSSH's: call it from there]"
             "\x1b[0m\r\n", NULL, NULL);
        return FALSE;
    }
    asking = c;
    asked = FALSE;
    c->sf = OPS->sf_open(c->tab->t_dialhost, f_ask, say);
    asking = NULL;
    if (!c->sf)
        return FALSE;
    if (c->cwd[0] && sf_cd(c, c->cwd))  /* called again: where it was */
        return TRUE;
    c->cwd[0] = 0;
    if (c->tab->t_fdir[0] && sf_cd(c, c->tab->t_fdir))
        return TRUE;
    ok = sf_cd(c, ".");
    if (!ok)
        sf_drop(c);
    return ok;
}

/* Connected: called again if the line went meanwhile. */
static BOOL r_ready(struct RConn *c)
{
    if (is_up(c))
        return TRUE;
    tsay(c, "\x1b[2m[calling again...]\x1b[0m\r\n", NULL, NULL);
    return r_connect(c);
}

typedef BOOL (*OpFn)(struct RConn *c, const char *a, const char *b);

/* A command, once more after calling again when the line was lost (an
 * FTP host hangs up on a quiet line after a while). */
static BOOL run_op(struct RConn *c, OpFn fn, const char *a, const char *b)
{
    int i;
    for (i = 0; i < 2; i++) {
        BOOL ok;
        if (!r_ready(c))
            return FALSE;
        c->lost = FALSE;
        ok = fn(c, a, b);
        if (ok || !c->lost || given_up(c))
            return ok;
        r_drop(c);
        tsay(c, "\x1b[2m[the connection was lost]\x1b[0m\r\n", NULL, NULL);
    }
    return FALSE;
}

static BOOL op_list(struct RConn *c, const char *a, const char *b)
{
    BOOL ok = c->kind == FK_FTP ? ftp_list(c) : sf_list(c);
    (void)a; (void)b;
    if (ok)
        ent_sort(c);
    return ok;
}

static BOOL op_cd(struct RConn *c, const char *a, const char *b)
{
    (void)b;
    if (c->kind == FK_FTP) {
        if (ftp_cd(c, a))
            return TRUE;
        if (!c->lost)
            tsay_name(c, "\x1b[1;31m[no drawer ", a ? a : "above",
                      " there]\x1b[0m\r\n");
        return FALSE;
    }
    return sf_cd(c, a ? a : "..");
}

/* mkdir, rmdir, delete, rename: what is FR_* (FR_RMDIR) */
static int op_what;
static BOOL op_do(struct RConn *c, const char *a, const char *b)
{
    char pa[400], pb[400];
    LONG rc;
    if (c->kind == FK_FTP) {
        if (ftp_do(c, op_what, a, b))
            return TRUE;
        if (!c->lost && c->code < 400)
            tsay_name(c, "\x1b[1;31m[", a, ": the host said no]\x1b[0m\r\n");
        return FALSE;
    }
    rpath(c, a, pa, sizeof pa);
    if (b)
        rpath(c, b, pb, sizeof pb);
    for (;;) {
        switch (op_what) {
        case FR_MKDIR: rc = OPS->sf_mkdir(c->sf, pa); break;
        case FR_RMDIR: rc = OPS->sf_rmdir(c->sf, pa); break;
        case FR_DELETE: rc = OPS->sf_unlink(c->sf, pa); break;
        default: rc = OPS->sf_rename(c->sf, pa, pb); break;
        }
        if (rc != SF_AGAIN || !sf_wait(c))
            break;
    }
    return rc ? sf_fail(c, rc, a) : TRUE;
}

static BOOL r_do(struct RConn *c, int what, const char *a, const char *b)
{
    op_what = what;
    return run_op(c, op_do, a, b);
}

/* ---- a file's bytes, either way ---- */

static ULONG xfrom, xfrom0;             /* x_open: where a download goes
                                         * on from */
static int xhow;

static BOOL op_xopen(struct RConn *c, const char *name, const char *b)
{
    char p[400];
    LONG rc;
    (void)b;
    xfrom = xfrom0;                     /* each try from the part's end */
    if (c->kind == FK_FTP)
        return ftp_xopen(c, name, xhow, &xfrom) >= 0;
    rpath(c, name, p, sizeof p);
    while ((rc = OPS->sf_fopen(c->sf, p, xhow, &c->sfh)) == SF_AGAIN &&
           sf_wait(c))
        ;
    if (rc)
        return sf_fail(c, rc, name);
    if (xfrom)
        OPS->sf_seek(c->sfh, xfrom);
    return TRUE;
}

/* how: 0 a download (from *from on: 0 when it cannot), 1 an upload */
static BOOL x_open(struct RConn *c, const char *name, int how, ULONG *from)
{
    BOOL ok;
    xhow = how;
    xfrom0 = from ? *from : 0;
    ok = run_op(c, op_xopen, name, NULL);
    if (from)
        *from = xfrom;
    return ok;
}

/* The socket can be read (wr: written) now, without waiting. */
static BOOL ready_now(LONG fd, BOOL wr)
{
    fd_set s;
    struct timeval tv;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    tv.tv_sec = tv.tv_usec = 0;
    return WaitSelect(fd + 1, wr ? NULL : &s, wr ? &s : NULL, NULL, &tv,
                      NULL) > 0;
}

/* >0 bytes, 0 the end, -1 failed, AGAIN. FTP's data socket blocks: it
 * is read only when it has something (a write: room for 2 KB, the
 * stack's low-water mark). */
static LONG x_read(struct RConn *c, UBYTE *b, LONG n)
{
    LONG k;
    if (c->kind == FK_FTP) {
        if (!ready_now(c->dfd, FALSE))
            return AGAIN;
        k = recv(c->dfd, b, n, 0);
        if (WOULDBLOCK(k))
            return AGAIN;
        return k >= 0 ? k : -1;
    }
    k = OPS->sf_read(c->sfh, b, n);
    if (k == SF_AGAIN)
        return AGAIN;
    if (k < 0)
        sf_fail(c, k, "reading");
    return k < 0 ? -1 : k;
}

static LONG x_write(struct RConn *c, const UBYTE *b, LONG n)
{
    LONG k;
    if (c->kind == FK_FTP) {
        if (!ready_now(c->dfd, TRUE))
            return AGAIN;
        k = send(c->dfd, (APTR)b, n < 2048 ? n : 2048, 0);
        if (WOULDBLOCK(k))
            return AGAIN;
        return k > 0 ? k : -1;
    }
    k = OPS->sf_write(c->sfh, b, n);
    if (k == SF_AGAIN)
        return AGAIN;
    if (k < 0)
        sf_fail(c, k, "writing");
    return k < 0 ? -1 : k;
}

static BOOL x_wait(struct RConn *c, BOOL up)
{
    if (c->kind == FK_FTP)
        return await(c, c->dfd, up ? SF_OUT : SF_IN, 120);
    return sf_wait(c);
}

/* The file's end: TRUE the host has it all (or sent it all). */
static BOOL x_close(struct RConn *c, BOOL stopped)
{
    LONG rc = 0;
    BOOL ok;
    tidy++;
    if (c->kind == FK_FTP)
        ok = ftp_xend(c, stopped);
    else {
        if (c->sfh) {
            while ((rc = OPS->sf_fclose(c->sfh)) == SF_AGAIN &&
                   c->sf && sf_wait(c))
                ;
            if (rc == SF_AGAIN)
                sf_drop(c);             /* half closed: called again */
        }
        c->sfh = NULL;
        ok = !rc && !stopped;
    }
    tidy--;
    return ok;
}

/* ---- the window ---- */

static void gset(struct RConn *c, int id, ULONG tag, ULONG val)
{
    if (c->w && c->g[id] &&
        SetGadgetAttrs((struct Gadget *)c->g[id], c->w, NULL, tag, val,
                       TAG_END))
        RefreshGList((struct Gadget *)c->g[id], c->w, NULL, 1);
}

static void status(struct RConn *c, const char *a, const char *b)
{
    c->status[0] = 0;
    if (a)
        strncat(c->status, a, sizeof c->status - 1);
    if (b)
        strncat(c->status, b, sizeof c->status - 1 - strlen(c->status));
    gset(c, W_STATUS, GA_Text, (ULONG)c->status);
}

/* At rest: what the drawer holds, where downloads go. */
static void status_idle(struct RConn *c)
{
    char s[160], dir[160];
    int i, d = 0;
    for (i = 0; i < c->nent; i++)
        d += c->ent[i].dir == 1;
    s[0] = 0;
    numstr_cat(s, c->nent - d);
    strcat(s, c->nent - d == 1 ? " file, " : " files, ");
    numstr_cat(s, d);
    strcat(s, d == 1 ? " drawer. Downloads go to " : " drawers. Downloads go to ");
    down_dir(c, dir);
    status(c, s, dir);
    gset(c, W_GAUGE, FUELGAUGE_Level, 0);
}

static void list_free(struct RConn *c)
{
    struct Node *n;
    if (c->lbl.lh_Head)
        while ((n = RemHead(&c->lbl)))
            FreeListBrowserNode(n);
}

/* The listing into the window's list: ".." first, unless at the top
 * (only for a window, or one being made: opening). */
static BOOL opening;
static void list_fill(struct RConn *c)
{
    int i;
    char name[256], size[16], path[400];
    if (!c->w && !opening)
        return;
    if (c->w)
        gset(c, W_LIST, LISTBROWSER_Labels, ~0UL);  /* let go of it */
    list_free(c);
    NewList(&c->lbl);
    for (i = strcmp(c->cwd, "/") ? -1 : 0; i < c->nent; i++) {
        struct Node *n;
        const struct REnt *e = i >= 0 ? &c->ent[i] : NULL;
        if (e) {
            to_latin1(ename(c, i), strlen(ename(c, i)), name, sizeof name,
                      '?');
            if (e->dir == 1)
                strcpy(size, "(drawer)");
            else if (e->dir == 2)
                strcpy(size, "(link)");
            else
                size_text(e->size, size);
        } else {
            strcpy(name, "..");
            strcpy(size, "(parent)");
        }
        n = AllocListBrowserNode(3, LBNA_UserData, (ULONG)i,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)name,
            LBNA_Column, 1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)size,
            LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, 2, LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(e ? e->date : ""), TAG_END);
        if (n)
            AddTail(&c->lbl, n);
    }
    if (c->w) {
        gset(c, W_LIST, LISTBROWSER_Labels, (ULONG)&c->lbl);
        gset(c, W_LIST, LISTBROWSER_Top, 0);
        to_latin1(c->cwd, strlen(c->cwd), path, sizeof path, '?');
        gset(c, W_PATH, STRINGA_TextVal, (ULONG)path);
        status_idle(c);
    }
}

/* At work: everything asleep but Stop. */
static void busy(struct RConn *c, BOOL on)
{
    static const UBYTE ids[] = { W_UP, W_PATH, W_REFRESH, W_GET, W_PUT,
                                 W_MKDIR, W_REN, W_DEL };
    int i;
    c->busy = on;
    if (on)
        c->stop = FALSE;
    for (i = 0; i < (int)sizeof ids; i++)
        gset(c, ids[i], GA_Disabled, on);
    gset(c, W_STOP, GA_Disabled, !on);
}

static void close_gauge(void)
{
    struct RConn *c;
    for (c = conns; c; c = c->next)
        if (c->w)
            return;
    if (FuelGaugeBase)
        CloseLibrary(FuelGaugeBase);
    FuelGaugeBase = NULL;
}

static void b_close(struct RConn *c)
{
    if (!c->wobj)
        return;
    DisposeObject(c->wobj);             /* the window and every gadget */
    c->wobj = NULL;
    c->w = NULL;
    memset(c->g, 0, sizeof c->g);
    list_free(c);
    close_gauge();
}

static Object *button(struct RConn *c, int id, const char *t)
{
    return c->g[id] = ButtonObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)t, End;
}

static void add(Object *g, Object *child, ULONG weight)
{
    if (g && child)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)child,
                 weight != ~0UL ? CHILD_WeightedWidth : TAG_IGNORE, weight,
                 TAG_END);
}

static void b_open(struct RConn *c)
{
    static struct ColumnInfo ci[] = {
        { 60, (STRPTR)"Name", 0 }, { 16, (STRPTR)"Size", CIF_RIGHT },
        { 24, (STRPTR)"Date", 0 }, { -1, NULL, 0 } };
    Object *root, *h;
    char path[400];
    int iw, ih;
    if (c->w) {
        WindowToFront(c->w);
        ActivateWindow(c->w);
        return;
    }
    if (!win) {                         /* hidden: when it is back */
        c->reopen = TRUE;
        return;
    }
    c->reopen = FALSE;
    if (!reaction_open() || (!FuelGaugeBase && !(FuelGaugeBase =
        OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 45)))) {
        tsay(c, "\x1b[1;31m[the browser needs OS 3.2's window classes "
             "(LIBS:, SYS:Classes)]\x1b[0m\r\n", NULL, NULL);
        return;
    }
    memset(c->g, 0, sizeof c->g);
    NewList(&c->lbl);
    opening = TRUE;
    list_fill(c);
    opening = FALSE;
    to_latin1(c->cwd, strlen(c->cwd), path, sizeof path, '?');

    root = c->wlayout = VLayoutObject, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE, End;
    h = HLayoutObject, End;
    add(h, button(c, W_UP, "Parent"), 0);
    add(h, c->g[W_PATH] = StringObject, GA_ID, W_PATH, GA_RelVerify, TRUE,
        STRINGA_TextVal, (ULONG)path, STRINGA_MaxChars, 390, End, ~0UL);
    add(h, button(c, W_REFRESH, "Refresh"), 0);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    c->g[W_LIST] = ListBrowserObject, GA_ID, W_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&c->lbl, LISTBROWSER_ColumnInfo,
        (ULONG)ci, LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_MultiSelect, TRUE, LISTBROWSER_ShowSelected, TRUE, End;
    if (root && c->g[W_LIST])           /* 6 entries at least */
        SetAttrs(root, LAYOUT_AddChild, (ULONG)c->g[W_LIST],
                 CHILD_MinHeight, 7 * (scr->RastPort.TxHeight + 2) + 6,
                 TAG_END);
    h = HLayoutObject, End;
    add(h, c->g[W_STATUS] = ButtonObject, GA_ID, W_STATUS, GA_ReadOnly, TRUE,
        GA_Text, (ULONG)c->status, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Justification, BCJ_LEFT, End, 75);
    add(h, c->g[W_GAUGE] = FuelGaugeObject, FUELGAUGE_Min, 0,
        FUELGAUGE_Max, 100, FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE,
        FUELGAUGE_Ticks, 0, End, 25);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    h = HLayoutObject, End;
    add(h, button(c, W_GET, "Download"), ~0UL);
    add(h, button(c, W_PUT, "Upload..."), ~0UL);
    add(h, button(c, W_MKDIR, "New drawer..."), ~0UL);
    add(h, button(c, W_REN, "Rename..."), ~0UL);
    add(h, button(c, W_DEL, "Delete"), ~0UL);
    add(h, button(c, W_STOP, "Stop"), ~0UL);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (!root) {
        list_free(c);
        beep();
        return;
    }
    if (c->g[W_STOP])
        SetAttrs(c->g[W_STOP], GA_Disabled, !c->busy, TAG_END);
    /* most of a small screen, not all of a big one */
    iw = scr->Width - 40 < 720 ? scr->Width - 40 : 720;
    ih = scr->Height - scr->BarHeight - 32;
    if (ih > 440)
        ih = 440;
    strcpy(c->title, c->kind == FK_SFTP ? "CTerm SFTP: " : "CTerm FTP: ");
    tab_label(c->tab, c->title + strlen(c->title));
    c->wobj = WindowObject, WA_Title, (ULONG)c->title,
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE,
        WA_InnerWidth, iw, WA_InnerHeight, ih,
        WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_Layout, (ULONG)root, End;
    if (!c->wobj) {
        DisposeObject(root);
        memset(c->g, 0, sizeof c->g);
        list_free(c);
        beep();
        return;
    }
    if (!(c->w = (struct Window *)DoMethod(c->wobj, WM_OPEN)))
        b_close(c);
    else if (!c->busy)
        status_idle(c);
}

/* ---- transfers ---- */

/* How it goes: the tab's panel and the Transfers window (a few times a
 * second), the browser's status and bar; and CTerm's round now and then,
 * so a line that never waits still lets the window and Stop work. */
static void progress(struct RConn *c)
{
    struct Xfer *x = c->x;
    ULONG now = ticks50(), was = x->tlast;
    if (now - c->polled >= 5) {
        c->polled = now;
        loop_round(-1, 0);
    }
    xfer_show(c->tab, x, 0);
    if (x->tlast != was && c->w) {      /* drawn: the browser too */
        char s[160], n[16];
        ULONG secs = (now - x->t0) / 50;
        strcpy(s, x->dir == 2 ? "Sending " : "Receiving ");
        strncat(s, x->name, 60);
        strcat(s, ": ");
        size_text(x->done, n);
        strcat(s, n);
        if (x->size) {
            strcat(s, " of ");
            size_text(x->size, n);
            strcat(s, n);
        }
        if (secs) {
            strcat(s, ", ");
            numstr_cat(s, c->moved / secs / 1024);
            strcat(s, " KB/s");
        }
        status(c, s, NULL);
        gset(c, W_GAUGE, FUELGAUGE_Level, pct100(x->done, x->size));
    }
}

/* One file from the host into dir: 1 done, -1 failed, -2 stopped. */
static int get_one(struct RConn *c, const char *rname, ULONG size,
                   const char *dir, UBYTE *buf)
{
    struct Xfer *x = c->x;
    char nm[112], cand[112], path[400], part[408];
    ULONG have = 0, got;
    LONG k, ps;
    BPTR f;
    BOOL ok = FALSE, stopped;
    int n = 0;
    local_name(rname, nm);
    strcpy(cand, nm);
    for (;;) {                          /* never over a file that is there */
        strcpy(path, dir);
        AddPart((STRPTR)path, (STRPTR)cand, sizeof path);
        if (fsize(path) == -1)
            break;
        if (++n > 99) {
            tsay(c, "\x1b[1;31m[no free name for ", nm, "]\x1b[0m\r\n");
            return -1;
        }
        strncpy(cand, nm, 96);
        cand[96] = 0;
        strcat(cand, ".");
        numstr_cat(cand, n);
    }
    strcpy(part, path);
    strcat(part, ".part");
    /* a part from before, shorter than the file: on from its end */
    ps = fsize(part);
    if (ps > 0 && size && (ULONG)ps < size)
        have = ps;
    strncpy(x->name, cand, sizeof x->name - 1);
    x->size = size;
    x->done = 0;
    if (!x_open(c, rname, 0, &have))
        return given_up(c) ? -2 : -1;
    f = Open((STRPTR)part, have ? MODE_OLDFILE : MODE_NEWFILE);
    if (f && have && Seek(f, have, OFFSET_BEGINNING) < 0) {
        Close(f);
        f = 0;
    }
    if (!f) {
        tsay(c, "\x1b[1;31m[cannot write ", part, "]\x1b[0m\r\n");
        x_close(c, TRUE);
        return -1;
    }
    if (have) {
        char m[40];
        m[0] = 0;
        numstr_cat(m, have / 1024);
        strcat(m, " KB there]\x1b[0m\r\n");
        tsay(c, "\x1b[2m[going on from ", FilePart((STRPTR)part), ", ");
        tsay(c, m, NULL, NULL);
    }
    got = x->done = have;
    x->bdone += have;
    for (;;) {
        k = x_read(c, buf, BUFSZ);
        /* All the bytes the host said there are: the A1200's stack
         * (lwip) did not say "readable" for the end of FTP's data, so
         * the file hung at 100% and CTerm spun round the wait (wasabid
         * was starved). Two seconds for more (the file may have grown
         * since the listing), then it is the end. */
        if (k == AGAIN && c->kind == FK_FTP && size && got >= size) {
            ULONG until = ticks50() + 100;
            while (k == AGAIN && ticks50() < until && !given_up(c)) {
                loop_round(-1, 200);
                k = x_read(c, buf, BUFSZ);
            }
            if (k == AGAIN)
                k = 0;
        }
        if (k > 0) {
            if (Write(f, buf, k) != k) {
                tsay(c, "\x1b[1;31m[could not write ", part,
                     ": is the disk full?]\x1b[0m\r\n");
                break;
            }
            got += k;
            x->done = got;
            x->bdone += k;
            c->moved += k;
            progress(c);
            if (given_up(c))
                break;
            continue;
        }
        if (k == AGAIN && x_wait(c, FALSE)) {
            /* "ready" by the wait, "not yet" by the look: no spinning */
            if (c->kind == FK_FTP && !ready_now(c->dfd, FALSE))
                Delay(1);
            continue;
        }
        ok = k == 0;
        break;
    }
    stopped = given_up(c);
    ok = x_close(c, !ok) && ok;
    Close(f);
    if (ok) {
        if (!Rename((STRPTR)part, (STRPTR)path))
            tsay(c, "\x1b[1;31m[it is in ", part,
                 ": the name was taken]\x1b[0m\r\n");
        else
            tsay(c, "\x1b[2m[", cand, " received]\x1b[0m\r\n");
        x->files++;
        return 1;
    }
    if (got == 0)
        DeleteFile((STRPTR)part);
    else
        tsay(c, "\x1b[2m[the part is kept as ", FilePart((STRPTR)part),
             ": a new download goes on from it]\x1b[0m\r\n");
    if (!stopped)
        tsay(c, "\x1b[1;31m[", cand, " failed]\x1b[0m\r\n");
    return stopped ? -2 : -1;
}

/* One file to the host, into the drawer it is in: 1 done, 0 skipped,
 * -1 failed, -2 stopped. ask: a file of the name there - replace it? */
static int put_one(struct RConn *c, const char *lpath, BOOL ask, UBYTE *buf)
{
    struct Xfer *x = c->x;
    char rname[256];
    const char *fp = (const char *)FilePart((STRPTR)lpath);
    LONG size = fsize(lpath), k, sent = 0;
    BPTR f;
    BOOL ok = FALSE;
    if (size < 0) {
        tsay(c, "\x1b[2m[", lpath, ": not a file, not sent]\x1b[0m\r\n");
        return 0;
    }
    to_utf8(fp, rname, sizeof rname);
    if (ask && ent_find(c, rname) >= 0) {
        struct EasyStruct es = {
            sizeof(struct EasyStruct), 0, (UBYTE *)"CTerm",
            (UBYTE *)"%s is on the host already.\nReplace it?",
            (UBYTE *)"Replace|Skip|Stop" };
        ULONG arg = (ULONG)fp;
        LONG a = EasyRequestArgs(c->w, &es, NULL, &arg);
        if (a != 1)
            return a == 2 ? 0 : -2;
    }
    if (!(f = Open((STRPTR)lpath, MODE_OLDFILE))) {
        tsay(c, "\x1b[1;31m[cannot read ", lpath, "]\x1b[0m\r\n");
        return -1;
    }
    strncpy(x->name, fp, sizeof x->name - 1);
    x->size = size;
    x->done = 0;
    if (!x_open(c, rname, 1, NULL)) {
        Close(f);
        return given_up(c) ? -2 : -1;
    }
    for (;;) {
        LONG off = 0;
        k = Read(f, buf, BUFSZ);
        if (k <= 0) {
            ok = k == 0;
            break;
        }
        while (off < k) {
            LONG w = x_write(c, buf + off, k - off);
            if (w > 0) {
                off += w;
                sent += w;
                x->done = sent;
                x->bdone += w;
                c->moved += w;
                progress(c);
                if (given_up(c))
                    goto out;
            } else if (w != AGAIN || !x_wait(c, TRUE))
                goto out;
        }
    }
out:
    Close(f);
    if (given_up(c)) {
        x_close(c, TRUE);
        tsay(c, "\x1b[2m[", fp, ": stopped, a part of it may be on the "
             "host]\x1b[0m\r\n");
        return -2;
    }
    if (!x_close(c, !ok) || !ok) {
        tsay(c, "\x1b[1;31m[", fp, " failed]\x1b[0m\r\n");
        return -1;
    }
    tsay(c, "\x1b[2m[", fp, " sent]\x1b[0m\r\n");
    x->files++;
    return 1;
}

/* Files to (up: local paths) or from the host (names in its drawer,
 * into dir), one after another, shown as a transfer. The files done;
 * -1: none could start. */
static int xfer_batch(struct RConn *c, BOOL up, const char *const *names,
                      const ULONG *sizes, int n, const char *dir, BOOL ask)
{
    struct Xfer x;
    UBYTE *buf;
    int i, done = 0, r = 0;
    char m[80];
    if (xfer_busy()) {
        tsay(c, "\x1b[1;31m[one transfer at a time: wait for the other "
             "one]\x1b[0m\r\n", NULL, NULL);
        return -1;
    }
    if (!(buf = AllocVec(BUFSZ, MEMF_ANY)))
        return -1;
    memset(&x, 0, sizeof x);
    x.proto = c->kind == FK_SFTP ? "SFTP" : "FTP";
    x.dir = up ? 2 : 1;
    if (up)
        to_latin1(c->cwd, strlen(c->cwd), x.where, sizeof x.where, '?');
    else
        strncpy(x.where, dir, sizeof x.where - 1);
    x.nfiles = n;
    for (i = 0; i < n; i++) {
        LONG z = up ? fsize(names[i]) : (LONG)sizes[i];
        if (z > 0)
            x.bsize += z;
    }
    c->x = &x;
    c->moved = 0;
    c->polled = ticks50();
    xfer_show(c->tab, &x, 1);
    for (i = 0; i < n && r != -2; i++) {
        x.fbase = x.bdone;
        r = up ? put_one(c, names[i], ask, buf)
               : get_one(c, names[i], sizes[i], dir, buf);
        if (r > 0)
            done++;
        if (given_up(c))
            r = -2;
    }
    c->x = NULL;
    xfer_show(c->tab, &x, 2);
    FreeVec(buf);
    strcpy(m, r == -2 ? "\x1b[1;31m[transfer stopped: " : "\x1b[2m[");
    numstr_cat(m, done);
    strcat(m, done == 1 ? " file, " : " files, ");
    numstr_cat(m, c->moved / 1024);
    strcat(m, " KB]\x1b[0m\r\n");
    tsay(c, m, NULL, NULL);
    return done;
}

static ULONG r_size(struct RConn *c, const char *rname)
{
    int i = ent_find(c, rname);
    if (i >= 0)
        return c->ent[i].size;
    if (c->kind == FK_FTP) {
        LONG k = ftp_size(c, rname);
        return k > 0 ? (ULONG)k : 0;
    } else {
        char p[400];
        SfAttr a;
        LONG rc;
        rpath(c, rname, p, sizeof p);
        while ((rc = OPS->sf_stat(c->sf, p, &a)) == SF_AGAIN && sf_wait(c))
            ;
        return rc ? 0 : a.size;
    }
}

/* ---- what the window's buttons do ---- */

static void hang_up(struct RConn *c)
{
    b_close(c);
    r_drop(c);
    c->tab->ended = TRUE;               /* the tab goes, and with it c */
}

static BOOL refresh_list(struct RConn *c)
{
    BOOL ok = run_op(c, op_list, NULL, NULL);
    list_fill(c);
    return ok;
}

static void go(struct RConn *c, const char *path)
{
    if (run_op(c, op_cd, path, NULL))
        refresh_list(c);
    else
        list_fill(c);                   /* the path field as it was */
}

/* The entries chosen in the list (index), at most max; the first. */
static int chosen(struct RConn *c, int *out, int max)
{
    struct Node *n;
    int k = 0;
    for (n = c->lbl.lh_Head; n->ln_Succ && k < max; n = n->ln_Succ) {
        ULONG sel = 0, ud = 0;
        GetListBrowserNodeAttrs(n, LBNA_Selected, (ULONG)&sel,
                                LBNA_UserData, (ULONG)&ud, TAG_END);
        if (sel && (LONG)ud >= 0 && (LONG)ud < c->nent)
            out[k++] = ud;
    }
    return k;
}

static void act_get(struct RConn *c, int only)
{
    int *idx = AllocVec(c->nent * (sizeof(int) + sizeof(char *) +
                                   sizeof(ULONG)) + 4, MEMF_ANY);
    const char **names;
    ULONG *sizes;
    char dir[160];
    int n, i, k = 0;
    if (!idx)
        return;
    names = (const char **)(idx + c->nent);
    sizes = (ULONG *)(names + c->nent);
    if (only >= 0) {
        idx[0] = only;
        n = 1;
    } else
        n = chosen(c, idx, c->nent);
    for (i = 0; i < n; i++) {
        if (c->ent[idx[i]].dir == 1) {
            char nm[112];
            local_name(ename(c, idx[i]), nm);
            tsay(c, "\x1b[2m[", nm, ": a drawer, not downloaded]"
                 "\x1b[0m\r\n");
            continue;
        }
        names[k] = ename(c, idx[i]);
        sizes[k++] = c->ent[idx[i]].size;
    }
    if (!k)
        tsay(c, "\x1b[1;31m[choose files in the list first]\x1b[0m\r\n",
             NULL, NULL);
    else {
        down_dir(c, dir);
        xfer_batch(c, FALSE, names, sizes, k, dir, FALSE);
    }
    FreeVec(idx);
}

#define NUP 32

static void act_put(struct RConn *c)
{
    struct FileRequester *req;
    char (*store)[256];
    const char *paths[NUP];
    const char *dir = c->tab->t_up[0] ? c->tab->t_up : uldir;
    int n = 0, i;
    if (!asl_open() || !(store = AllocVec(NUP * 256, MEMF_ANY)))
        return;
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)c->w, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: files to upload",
        dir[0] ? ASLFR_InitialDrawer : TAG_IGNORE, (ULONG)dir,
        ASLFR_DoMultiSelect, TRUE, ASLFR_RejectIcons, TRUE, TAG_END);
    if (req && AslRequest(req, NULL)) {
        for (i = 0; i < req->fr_NumArgs && n < NUP; i++) {
            strncpy(store[n], (char *)req->fr_Drawer, 255);
            store[n][255] = 0;
            AddPart((STRPTR)store[n], req->fr_ArgList[i].wa_Name, 256);
            paths[n] = store[n];
            n++;
        }
        if (!req->fr_NumArgs && req->fr_File[0]) {
            strncpy(store[0], (char *)req->fr_Drawer, 255);
            store[0][255] = 0;
            AddPart((STRPTR)store[0], req->fr_File, 256);
            paths[n++] = store[0];
        }
    }
    if (req)
        FreeAslRequest(req);
    if (n) {
        xfer_batch(c, TRUE, paths, NULL, n, NULL, TRUE);
        refresh_list(c);
    }
    FreeVec(store);
}

static void act_mkdir(struct RConn *c)
{
    char nm[108], h[256];
    nm[0] = 0;
    if (!ask_line("CTerm: new drawer", "Name", nm, sizeof nm,
                  c->w) || !nm[0])
        return;
    to_utf8(nm, h, sizeof h);
    if (r_do(c, FR_MKDIR, h, NULL))
        refresh_list(c);
}

static void act_rename(struct RConn *c)
{
    int i;
    char nm[108], h[256];
    if (chosen(c, &i, 1) != 1) {
        tsay(c, "\x1b[1;31m[choose what to rename in the list]\x1b[0m\r\n",
             NULL, NULL);
        return;
    }
    to_latin1(ename(c, i), strlen(ename(c, i)), nm, sizeof nm, '?');
    if (!ask_line("CTerm: rename", "New name", nm, sizeof nm,
                  c->w) || !nm[0])
        return;
    to_utf8(nm, h, sizeof h);
    if (strcmp(h, ename(c, i)) && r_do(c, FR_RENAME, ename(c, i), h))
        refresh_list(c);
}

static void act_delete(struct RConn *c)
{
    int *idx = AllocVec(c->nent * sizeof(int) + 4, MEMF_ANY), n, i;
    char nm[112], what[140];
    if (!idx)
        return;
    n = chosen(c, idx, c->nent);
    if (n) {
        struct EasyStruct es = {
            sizeof(struct EasyStruct), 0, (UBYTE *)"CTerm",
            (UBYTE *)"Delete %s on the host?\n(A drawer must be empty.)",
            (UBYTE *)"Delete|Cancel" };
        ULONG arg = (ULONG)what;
        if (n == 1) {
            to_latin1(ename(c, idx[0]), strlen(ename(c, idx[0])), what,
                      sizeof what, '?');
        } else {
            what[0] = 0;
            numstr_cat(what, n);
            strcat(what, " files and drawers");
        }
        if (EasyRequestArgs(c->w, &es, NULL, &arg) == 1) {
            for (i = 0; i < n && !given_up(c); i++) {
                const struct REnt *e = &c->ent[idx[i]];
                if (r_do(c, e->dir == 1 ? FR_RMDIR : FR_DELETE,
                         ename(c, idx[i]), NULL)) {
                    local_name(ename(c, idx[i]), nm);
                    tsay(c, "\x1b[2m[", nm, " deleted]\x1b[0m\r\n");
                }
            }
            refresh_list(c);
        }
    } else
        tsay(c, "\x1b[1;31m[choose what to delete in the list]\x1b[0m\r\n",
             NULL, NULL);
    FreeVec(idx);
}

/* A double-click: a drawer is entered, a file downloaded; a link is
 * tried as a drawer first. */
static void act_open(struct RConn *c)
{
    struct Node *n = NULL;
    ULONG ud = 0;
    LONG i;
    GetAttr(LISTBROWSER_SelectedNode, c->g[W_LIST], (ULONG *)&n);
    if (!n)
        return;
    GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&ud, TAG_END);
    i = (LONG)ud;
    if (i < 0) {
        go(c, NULL);
        return;
    }
    if (i >= c->nent)
        return;
    if (c->ent[i].dir) {
        char nm[400];
        int was = c->ent[i].dir;
        strncpy(nm, ename(c, i), sizeof nm - 1);
        nm[sizeof nm - 1] = 0;
        if (run_op(c, op_cd, nm, NULL)) {
            refresh_list(c);
            return;
        }
        if (was == 1 || c->lost)
            return;
        c->failed = FALSE;              /* a link to a file: fetched */
    }
    act_get(c, i);
}

static void act(struct RConn *c, int id)
{
    char p[400];
    const char *t = NULL;
    busy(c, TRUE);
    c->failed = FALSE;
    switch (id) {
    case W_UP: go(c, NULL); break;
    case W_PATH:
        GetAttr(STRINGA_TextVal, c->g[W_PATH], (ULONG *)&t);
        if (t && t[0]) {
            to_utf8(t, p, sizeof p);
            go(c, p);
        }
        break;
    case W_REFRESH: refresh_list(c); break;
    case W_LIST: act_open(c); break;
    case W_GET: act_get(c, -1); break;
    case W_PUT: act_put(c); break;
    case W_MKDIR: act_mkdir(c); break;
    case W_REN: act_rename(c); break;
    case W_DEL: act_delete(c); break;
    }
    busy(c, FALSE);
    if (!c->failed && c->w)
        status_idle(c);
}

static void b_events(struct RConn *c)
{
    ULONG res, ev = 0;
    UWORD code;
    while (c->wobj && (res = DoMethod(c->wobj, WM_HANDLEINPUT,
                                      (ULONG)&code)) != WMHI_LASTMSG) {
        int id = res & WMHI_GADGETMASK;
        switch (res & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            if (!c->busy) {
                hang_up(c);
                return;
            }
            c->wantclose = TRUE;        /* when the work has stopped */
            /* fall through */
        case WMHI_GADGETUP:
            if ((res & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW || id == W_STOP) {
                c->stop = TRUE;
                if (c->x)
                    c->x->stop = 1;
                break;
            }
            if (id == W_LIST) {         /* a click chooses; two open */
                GetAttr(LISTBROWSER_RelEvent, c->g[W_LIST], &ev);
                if (ev != LBRE_DOUBLECLICK)
                    break;
            }
            if (c->busy) {
                beep();
                break;
            }
            act(c, id);
            if (c->wantclose) {
                hang_up(c);
                return;
            }
            break;
        }
    }
}

/* ---- the part's way in (part.h: FilesPart) ---- */

static void conn_free(struct RConn *c)
{
    struct RConn **q;
    b_close(c);
    r_drop(c);
    ent_free(c);
    for (q = &conns; *q && *q != c; q = &(*q)->next)
        ;
    if (*q)
        *q = c->next;
    c->tab->t_rf = NULL;
    memset(c->pass, 0, sizeof c->pass);
    FreeVec(c);
}

static BOOL callok;

/* T's place: connected (if it is not), its listing, its window. */
static void f_call_(void)
{
    struct RConn *c = conn_of(T);
    callok = FALSE;
    if (!c) {
        if (!(c = AllocVec(sizeof *c, MEMF_ANY | MEMF_CLEAR)))
            return;
        c->tab = T;
        c->kind = T->t_fkind;
        c->cfd = c->dfd = -1;
        strcpy(c->pass, T->t_pass);
        c->next = conns;
        conns = c;
        T->t_rf = c;
    }
    if (!c->busy && !is_up(c)) {
        BOOL ok;
        busy(c, TRUE);
        ok = r_connect(c);
        if (ok && !run_op(c, op_list, NULL, NULL) && !is_up(c))
            ok = FALSE;
        busy(c, FALSE);
        if (!ok) {
            if (!c->w)
                conn_free(c);           /* nothing open: the part may go */
            return;
        }
        tsay(c, "\x1b[2m[connected: the browser is open - Return in this "
             "tab opens it again]\x1b[0m\r\n", NULL, NULL);
    }
    callok = TRUE;
    if (c->w)
        list_fill(c);
    b_open(c);
}

static BOOL f_call(void)
{
    deep_call(f_call_);
    return callok;
}

/* Project/Open SFTP browser: from an ssh tab, its host and login in a
 * tab of its own (its own connection); from a browser's tab, its
 * window. */
static void f_menu_(void)
{
    struct Tab *t, *from = T;
    if (T->t_fkind) {
        f_call_();
        return;
    }
    if (local || !sshmode || !dialhost[0] || !OPS) {
        say("\r\n\x1b[2m[the SFTP browser opens from an ssh tab - or call "
            "a phone book place of the kind sftp or ftp]\x1b[0m\r\n");
        refresh();
        return;
    }
    if (!(t = tab_new())) {
        beep();
        return;
    }
    T = t;
    local = FALSE;
    sshmode = TRUE;
    t->t_fkind = FK_SFTP;
    strcpy(dialhost, from->t_dialhost);
    strcpy(dialtitle, "CTerm: sftp ");
    strncat(dialtitle, dialhost, sizeof dialtitle - 13);
    strcpy(t->t_user, from->t_user);
    strcpy(t->t_pass, from->t_pass);
    strcpy(t->t_down, from->t_down);
    strcpy(t->t_up, from->t_up);
    tab_show(ntabs - 1);
    say("\x1b[2m[calling ");
    say(dialhost);
    say(" for SFTP... Esc gives up]\x1b[0m\r\n");
    refresh();
    f_call_();
    if (!callok)
        say("\x1b[2m[Return calls again]\x1b[0m\r\n");
    refresh();
}

static void f_menu(void)
{
    deep_call(f_menu_);
}

static ULONG f_sigmask(void)
{
    struct RConn *c;
    ULONG m = 0;
    for (c = conns; c; c = c->next)
        if (c->w)
            m |= 1UL << c->w->UserPort->mp_SigBit;
    return m;
}

static void f_events_(void)
{
    struct RConn *c;
    for (c = conns; c; c = c->next)
        if (c->w)
            b_events(c);
}

static void f_events(void)
{
    deep_call(f_events_);
}

static struct Tab *gone_tab;

static void f_tab_gone_(void)
{
    struct RConn *c = conn_of(gone_tab);
    if (c)
        conn_free(c);
}

static void f_tab_gone(struct Tab *t)
{
    gone_tab = t;
    deep_call(f_tab_gone_);
}

static BOOL scr_again;

static void f_screen_(void)
{
    struct RConn *c;
    for (c = conns; c; c = c->next)
        if (!scr_again && c->w) {
            b_close(c);
            c->reopen = TRUE;
        } else if (scr_again && c->reopen)
            b_open(c);
}

static void f_screen(BOOL again)
{
    scr_again = again;
    deep_call(f_screen_);
}

/* ---- ARexx ---- */

static int rx_what, rx_n, rx_rc;
static const char *const *rx_names;
static const char *rx_to;
static char *rx_res;

/* RESULT: text (n: its length, -1: a string) */
static void rx_result(const char *t, LONG n)
{
    if (n < 0)
        n = strlen(t);
    if ((rx_res = AllocVec(n + 1, MEMF_ANY))) {
        CopyMem((APTR)t, rx_res, n);
        rx_res[n] = 0;
    }
}

static void f_rexx_(void)
{
    struct RConn *c = conn_of(T);
    char h[256], h2[256], dir[256];
    int i, got;
    rx_rc = 10;
    rx_res = NULL;
    if (!c || c->busy)
        return;                         /* no place here; at work */
    busy(c, TRUE);
    c->failed = FALSE;
    switch (rx_what) {
    case FR_LIST:
        if (refresh_list(c)) {
            LONG len = 1;
            char *r, *q;
            for (i = 0; i < c->nent; i++)
                len += strlen(ename(c, i)) + 4;
            if ((r = AllocVec(len, MEMF_ANY))) {
                q = r;
                for (i = 0; i < c->nent; i++) {
                    if (i)
                        *q++ = ' ';
                    *q++ = '"';
                    to_latin1(ename(c, i), strlen(ename(c, i)), q,
                              len - (q - r) - 3, '?');
                    q += strlen(q);
                    if (c->ent[i].dir == 1)
                        *q++ = '/';
                    *q++ = '"';
                }
                *q = 0;
                rx_res = r;
                rx_rc = 0;
            }
        }
        break;
    case FR_CD:
        to_utf8(rx_names[0], h, sizeof h);
        if (run_op(c, op_cd, h, NULL)) {
            refresh_list(c);
            to_latin1(c->cwd, strlen(c->cwd), h, sizeof h, '?');
            rx_result(h, -1);
            rx_rc = 0;
        }
        break;
    case FR_GET: {
        const char **nm = AllocVec(rx_n * (sizeof(char *) + sizeof(ULONG)
                                           + 256), MEMF_ANY);
        ULONG *sz;
        char *store;
        if (!nm)
            break;
        sz = (ULONG *)(nm + rx_n);
        store = (char *)(sz + rx_n);
        if (!r_ready(c)) {
            FreeVec(nm);
            break;
        }
        for (i = 0; i < rx_n; i++) {
            host_name(c, rx_names[i], store + i * 256, 256);
            nm[i] = store + i * 256;
            sz[i] = r_size(c, nm[i]);
        }
        if (rx_to) {
            strncpy(dir, rx_to, sizeof dir - 1);
            dir[sizeof dir - 1] = 0;
        } else
            down_dir(c, dir);
        got = xfer_batch(c, FALSE, nm, sz, rx_n, dir, FALSE);
        rx_rc = got == rx_n ? 0 : got > 0 ? 5 : 10;
        FreeVec(nm);
        break;
    }
    case FR_PUT:
        if (r_ready(c)) {
            got = xfer_batch(c, TRUE, rx_names, NULL, rx_n, NULL, FALSE);
            rx_rc = got == rx_n ? 0 : got > 0 ? 5 : 10;
            refresh_list(c);
        }
        break;
    case FR_MKDIR:
        to_utf8(rx_names[0], h, sizeof h);
        if (r_do(c, FR_MKDIR, h, NULL))
            rx_rc = 0;
        refresh_list(c);
        break;
    case FR_DELETE:
        host_name(c, rx_names[0], h, sizeof h);
        i = ent_find(c, h);
        if (r_do(c, i >= 0 && c->ent[i].dir == 1 ? FR_RMDIR : FR_DELETE, h,
                 NULL))
            rx_rc = 0;
        refresh_list(c);
        break;
    case FR_RENAME:
        host_name(c, rx_names[0], h, sizeof h);
        to_utf8(rx_to ? rx_to : "", h2, sizeof h2);
        if (h2[0] && r_do(c, FR_RENAME, h, h2))
            rx_rc = 0;
        refresh_list(c);
        break;
    }
    busy(c, FALSE);
    if (!c->failed && c->w)
        status_idle(c);
    if (c->wantclose)
        hang_up(c);
}

static int f_rexx(int what, const char *const *names, int n, const char *to,
                  char **res)
{
    rx_what = what;
    rx_names = names;
    rx_n = n;
    rx_to = to;
    deep_call(f_rexx_);
    *res = rx_res;
    return rx_rc;
}

static BOOL f_idle(void)
{
    return !conns;
}

APTR part_main(void)
{
    static struct FilesPart p = { f_call, f_menu, f_events, f_sigmask,
                                  f_tab_gone, f_screen, f_rexx, f_idle };
    return &p;
}
