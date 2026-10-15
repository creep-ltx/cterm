/*
 * files - the file browser for SFTP and FTP places (Parts/Files, with
 * fileui.c and ftp.c): the connection, the two listings (the Amiga's
 * drawer and the host's), the transfers, ARexx's commands. The panels
 * themselves, drawn in the place's tab like a program in a terminal,
 * are fileui.c's.
 *
 * Each place has a tab of its own. While the call is set up its
 * messages go there as text (the host key question, a password asked,
 * the host's words); once connected the panels come up on the tab's
 * alternate screen, and later messages wait for the main screen (F9
 * shows it) and show on the panels' message line. The part stays
 * loaded while a place is connected and leaves memory with the last.
 *
 * Every wait (await) is CTerm's own round, loop_round: the other tabs,
 * the window and this one go on while a listing or a file comes. A
 * transfer shows as Zmodem's does (the tab's name, the Transfers
 * window) and on the panels' message line; the stop key, Esc, and the
 * Transfers window's Stop end it, one at a time in all of CTerm. A
 * download is made as name.part and renamed when whole; a later
 * download of the same file goes on from the part (FTP's REST, a seek
 * in SFTP); a file there is never written over (a number is added); a
 * name from the host is made safe first (no ':' or '/' to climb out of
 * the drawer). SFTP is CTermSSH's (sftp_ops); FTP is ftp.c.
 */
#include "cterm.h"
#include "files.h"
#include <libraries/locale.h>
#include <proto/locale.h>
#pragma GCC optimize("Os")              /* called rarely: small before quick */

#define BUFSZ  32768                    /* a transfer's bytes at a time */
#define OPS    sftp_ops
#define PLOG   4096                     /* messages kept for the main screen */

static struct RConn *conns;             /* every place connected */
static int tidy;                        /* ending what was begun: a Stop
                                           does not stop it */
static BOOL hush;                       /* a refusal that may come (a drawer
                                           made that may be there): unsaid */
static struct RConn *asking;            /* its password, for f_ask */
static BOOL asked;
static struct RConn *saying;            /* the place a call's words are for */

/* ---- small things ---- */

static struct RConn *conn_of(struct Tab *t)
{
    struct RConn *c;
    for (c = conns; c && c->tab != t; c = c->next)
        ;
    return c;
}

/* Text in the place's tab (a, b, d: any may be NULL; Latin-1, as UTF-8
 * for a UTF-8 tab). With the panels up it waits for the main screen
 * and shows on the message line, its colours and brackets off (a red
 * one red). */
void tsay(struct RConn *c, const char *a, const char *b, const char *d)
{
    struct Tab *keep = T;
    const char *p[3];
    char u[512], m[160], *q = m;
    const char *s;
    int i;
    p[0] = a; p[1] = b; p[2] = d;
    if (!c->ui || c->mode == U_LOG) {   /* the main screen is shown */
        T = c->tab;
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
        return;
    }
    for (i = 0; i < 3; i++)
        if (p[i]) {
            LONG n;
            to_utf8(p[i], u, sizeof u);
            n = strlen(u);
            if (c->plog && c->ploglen + n > PLOG) {
                ui_stop(c);             /* full: written out now */
                ui_start(c);
            }
            if (c->plog && c->ploglen + n <= PLOG) {
                CopyMem(u, c->plog + c->ploglen, n);
                c->ploglen += n;
            }
            for (s = p[i]; *s && q < m + sizeof m - 1; s++) {
                if (*s == 0x1b) {       /* ESC [ ... m */
                    while (s[1] && *s != 'm')
                        s++;
                    continue;
                }
                if ((UBYTE)*s >= ' ' && *s != '[' && *s != ']')
                    *q++ = *s;
            }
        }
    *q = 0;
    if (m[0])
        ui_note(c, m, a && !strncmp(a, "\x1b[1;31m", 7));
}

/* A call's own words (connecting, the host key): in the place's tab. */
void fsay(const char *text)
{
    struct RConn *c = saying ? saying : conn_of(T);
    if (c)
        tsay(c, text, NULL, NULL);
    else
        say(text);
}

/* A line asked in the tab (a password, yes or no to a host key): the
 * panels make way for the question and come back after it. */
BOOL ask_tab(struct RConn *c, const char *prompt, char *buf, int max,
             BOOL echo)
{
    BOOL was = c->ui, ok;
    struct Tab *keep = T;
    if (was)
        ui_stop(c);
    T = c->tab;
    ok = term_ask(prompt, buf, max, echo);
    T = keep;
    if (was)
        ui_start(c);
    return ok;
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

/* "2026-10-11 07:50" from days since 1970 and minutes of the day */
static void date_text(LONG days, int mi, char *date)
{
    int y, m, d;
    ymd(days, &y, &m, &d);
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

/* Days since 1970 from a date (H. Hinnant's days_from_civil). */
LONG days_of(int y, int m, int d)
{
    LONG era, yoe, doy;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

/* ---- the Amiga's time zone ----
 * Hosts give UTC (MLSD, MDTM, SFTP); the Amiga's clock is local. Its
 * zone: ENV:TZ when it is set, as POSIX writes it ("CET-1CEST,M3.5.0,
 * M10.5.0/3": the standard zone's hours west, the summer zone and the
 * days it starts and ends), else Locale's GMT offset (Prefs/Locale's
 * map; no summer time in it). */
static BOOL tz_read;
static LONG tz_std, tz_dst;             /* seconds east of UTC */
static BOOL tz_has;                     /* summer time rules */
static UBYTE tz_m[2], tz_w[2], tz_d[2]; /* Mm.w.d: its start, its end */
static LONG tz_t[2];                    /* ... at this local time */

static const char *tz_num(const char *p, LONG *v)
{
    *v = 0;
    if (*p < '0' || *p > '9')
        return NULL;
    while (*p >= '0' && *p <= '9')
        *v = *v * 10 + *p++ - '0';
    return p;
}

/* [+-]hh[:mm[:ss]] in seconds */
static const char *tz_time(const char *p, LONG *s)
{
    LONG h, m = 0, x = 0, sg = 1;
    if (*p == '+' || *p == '-')
        sg = *p++ == '-' ? -1 : 1;
    if (!(p = tz_num(p, &h)))
        return NULL;
    if (*p == ':' && (p = tz_num(p + 1, &m)) && *p == ':')
        p = tz_num(p + 1, &x);
    if (!p)
        return NULL;
    *s = sg * (h * 3600 + m * 60 + x);
    return p;
}

static const char *tz_name(const char *p)
{
    if (*p == '<') {
        while (*p && *p != '>')
            p++;
        return *p ? p + 1 : p;
    }
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
        p++;
    return p;
}

/* ",Mm.w.d[/time]" */
static const char *tz_rule(const char *p, int i)
{
    LONG v;
    if (*p++ != ',' || *p++ != 'M' || !(p = tz_num(p, &v)) || v < 1 ||
        v > 12)
        return NULL;
    tz_m[i] = v;
    if (*p++ != '.' || !(p = tz_num(p, &v)) || v < 1 || v > 5)
        return NULL;
    tz_w[i] = v;
    if (*p++ != '.' || !(p = tz_num(p, &v)) || v > 6)
        return NULL;
    tz_d[i] = v;
    tz_t[i] = 7200;
    if (*p == '/' && !(p = tz_time(p + 1, &tz_t[i])))
        return NULL;
    return p;
}

static void tz_init(void)
{
    char v[80];
    const char *p;
    LONG s;
    tz_read = TRUE;
    tz_has = FALSE;
    tz_std = tz_dst = 0;
    if (GetVar((STRPTR)"TZ", (STRPTR)v, sizeof v, 0) > 0 &&
        (p = tz_name(v)) != v && (p = tz_time(p, &s))) {
        tz_std = tz_dst = -s;           /* POSIX counts west */
        if (*p) {
            const char *q = tz_name(p);
            if (q == p)
                return;
            tz_dst = tz_std + 3600;
            if (*q && *q != ',' && (q = tz_time(q, &s)))
                tz_dst = -s;
            if (q && (q = tz_rule(q, 0)) && tz_rule(q, 1))
                tz_has = TRUE;
        }
        return;
    }
    {
        struct Library *LocaleBase = OpenLibrary((STRPTR)"locale.library", 38);
        struct Locale *l;
        if (LocaleBase) {
            if ((l = OpenLocale(NULL))) {
                tz_std = tz_dst = -l->loc_GMTOffset * 60;   /* minutes west */
                CloseLocale(l);
            }
            CloseLibrary(LocaleBase);
        }
    }
}

/* Mm.w.d of year y: its day (days since 1970) */
static LONG tz_day(int y, int i)
{
    LONG first = days_of(y, tz_m[i], 1);
    LONG next = tz_m[i] == 12 ? days_of(y + 1, 1, 1)
                              : days_of(y, tz_m[i] + 1, 1);
    LONG d = first + (tz_d[i] - (first + 4) % 7 + 7) % 7 + (tz_w[i] - 1) * 7;
    while (d >= next)
        d -= 7;                         /* week 5: the last */
    return d;
}

LONG tz_east(ULONG utc)
{
    int y, m, d;
    LONG on, off;
    if (!tz_read)
        tz_init();
    if (!tz_has)
        return tz_std;
    ymd(utc / 86400, &y, &m, &d);
    on = tz_day(y, 0) * 86400 + tz_t[0] - tz_std;   /* both in UTC */
    off = tz_day(y, 1) * 86400 + tz_t[1] - tz_dst;
    if (on < off ? (LONG)utc >= on && (LONG)utc < off
                 : (LONG)utc >= on || (LONG)utc < off)
        return tz_dst;
    return tz_std;
}

/* The host's UTF-8 as Latin-1, the Amiga's: what Latin-1 has not
 * becomes bad; bytes that are not UTF-8 are taken as Latin-1 already. */
void to_latin1(const char *in, int len, char *out, int max, char bad)
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
void to_utf8(const char *in, char *out, int max)
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

void size_text(ULONG n, char *o)
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
void down_dir(struct RConn *c, char *dir)
{
    BPTR l;
    strcpy(dir, c->tab->t_down[0] ? c->tab->t_down : dldir[0] ? dldir
                                                   : "PROGDIR:Downloads");
    if (!(l = Lock((STRPTR)dir, ACCESS_READ)))
        l = CreateDir((STRPTR)dir);
    if (l)
        UnLock(l);
}

/* ---- the listings ---- */

static void pan_clear(struct Panel *p)
{
    p->nent = 0;
    p->plen = 0;
}

static BOOL pan_add(struct Panel *p, const char *name, int len, ULONG size,
                    const char *date, int dir)
{
    struct REnt *e;
    if (p->nent == p->capent) {
        int cap = p->capent ? p->capent * 2 : 64;
        struct REnt *n = AllocVec(cap * sizeof *n, MEMF_ANY);
        if (!n)
            return FALSE;
        if (p->ent) {
            CopyMem(p->ent, n, p->nent * sizeof *n);
            FreeVec(p->ent);
        }
        p->ent = n;
        p->capent = cap;
    }
    if (p->plen + len + 1 > p->pcap) {
        LONG cap = p->pcap ? p->pcap * 2 : 4096;
        char *n;
        while (cap < p->plen + len + 1)
            cap *= 2;
        if (!(n = AllocVec(cap, MEMF_ANY)))
            return FALSE;
        if (p->pool) {
            CopyMem(p->pool, n, p->plen);
            FreeVec(p->pool);
        }
        p->pool = n;
        p->pcap = cap;
    }
    e = &p->ent[p->nent++];
    e->noff = p->plen;
    memcpy(p->pool + p->plen, name, len);
    p->pool[p->plen + len] = 0;
    p->plen += len + 1;
    e->size = size;
    strncpy(e->date, date, sizeof e->date - 1);
    e->date[sizeof e->date - 1] = 0;
    e->dir = dir;
    e->mark = 0;
    e->when = 0;
    e->tz = 0;
    return TRUE;
}

void pan_free(struct Panel *p)
{
    if (p->ent)
        FreeVec(p->ent);
    if (p->pool)
        FreeVec(p->pool);
    p->ent = NULL;
    p->pool = NULL;
    p->nent = p->capent = 0;
    p->plen = p->pcap = 0;
}

const char *pname(struct Panel *p, int i)
{
    return p->pool + p->ent[i].noff;
}

static struct Panel *lpan(struct RConn *c)
{
    return c->lp ? c->lp : &c->pn[1];
}

void ent_clear(struct RConn *c)
{
    pan_clear(lpan(c));
}

BOOL ent_add(struct RConn *c, const char *name, int len, ULONG size,
             const char *date, int dir)
{
    return pan_add(lpan(c), name, len, size, date, dir);
}

/* The entry just added: its date, from days since 1970 and seconds of
 * the day - UTC (utc), else the host's own clock, shown as it is. */
void ent_when(struct RConn *c, LONG days, LONG secs, BOOL utc)
{
    struct Panel *p = lpan(c);
    struct REnt *e;
    ULONG t;
    if (!p->nent || days < 2922)
        return;                         /* before 1978: the Amiga has none */
    e = &p->ent[p->nent - 1];
    t = (ULONG)days * 86400 + secs;
    if (utc)
        t += tz_east(t);
    if (utc || !e->date[0])             /* the host's own text: kept */
        date_text(t / 86400, t % 86400 / 60, e->date);
    e->when = t - 2922UL * 86400;
    e->tz = utc ? 2 : 1;
}

/* Drawers first, then by name (any case): a shell sort. */
static int ent_cmp(struct Panel *p, const struct REnt *a,
                   const struct REnt *b)
{
    if ((a->dir == 1) != (b->dir == 1))
        return a->dir == 1 ? -1 : 1;
    return stricmp(p->pool + a->noff, p->pool + b->noff);
}

static void pan_sort(struct Panel *p)
{
    int gap, i, j;
    for (gap = p->nent / 2; gap > 0; gap /= 2)
        for (i = gap; i < p->nent; i++) {
            struct REnt t = p->ent[i];
            for (j = i; j >= gap && ent_cmp(p, &p->ent[j - gap], &t) > 0;
                 j -= gap)
                p->ent[j] = p->ent[j - gap];
            p->ent[j] = t;
        }
}

/* The entry of this name (as the host has it, or as the Amiga shows
 * it), -1 none. */
int pan_find(struct Panel *p, const char *name)
{
    int i;
    char seen[256];
    for (i = 0; i < p->nent; i++)
        if (!strcmp(pname(p, i), name))
            return i;
    for (i = 0; i < p->nent; i++) {
        to_latin1(pname(p, i), strlen(pname(p, i)), seen, sizeof seen,
                  '?');
        if (!stricmp(seen, name))
            return i;
    }
    return -1;
}

/* The name under the bar ("" none), and the bar back on it after the
 * listing is read again. */
static void pan_keep(struct Panel *p, char *keep)
{
    int i = p->cur - (p->up ? 1 : 0);
    keep[0] = 0;
    if (i >= 0 && i < p->nent) {
        strncpy(keep, pname(p, i), 255);
        keep[255] = 0;
    }
}

void pan_seek(struct Panel *p, const char *name)
{
    int i = name[0] ? pan_find(p, name) : -1, n = p->nent + (p->up ? 1 : 0);
    if (i >= 0)
        p->cur = i + (p->up ? 1 : 0);
    if (p->cur >= n)
        p->cur = n - 1;
    if (p->cur < 0)
        p->cur = 0;
}

/* A name the Amiga gave (a script, a typed line), as the host has it. */
void host_name(struct RConn *c, const char *name, char *out, int max)
{
    int i = pan_find(&c->pn[1], name);
    if (i >= 0) {
        strncpy(out, pname(&c->pn[1], i), max - 1);
        out[max - 1] = 0;
    } else
        to_utf8(name, out, max);
}

/* The Amiga's drawer (or, path "", the volumes), read into panel 0. */
BOOL loc_list(struct RConn *c)
{
    struct Panel *p = &c->pn[0];
    struct FileInfoBlock *fib;
    char keep[256], date[18];
    BPTR l;
    pan_keep(p, keep);
    pan_clear(p);
    if (!p->path[0]) {
        struct DosList *dl;
        p->up = FALSE;
        dl = LockDosList(LDF_VOLUMES | LDF_READ);
        while ((dl = NextDosEntry(dl, LDF_VOLUMES))) {
            UBYTE *b = BADDR(dl->dol_Name);
            char n[40];
            int k = b[0] < 36 ? b[0] : 36;
            memcpy(n, b + 1, k);
            n[k++] = ':';
            pan_add(p, n, k, 0, "", 1);
        }
        UnLockDosList(LDF_VOLUMES | LDF_READ);
        pan_sort(p);
        pan_seek(p, keep);
        return TRUE;
    }
    p->up = TRUE;
    if (!(l = Lock((STRPTR)p->path, ACCESS_READ))) {
        tsay(c, "\x1b[1;31m[", p->path, ": not there]\x1b[0m\r\n");
        pan_seek(p, keep);
        return FALSE;
    }
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(l, fib) && fib->fib_DirEntryType > 0)
            while (ExNext(l, fib)) {
                LONG t = fib->fib_DirEntryType;
                int dir = t == ST_SOFTLINK ? 2 : t > 0;    /* 2: a link */
                date_text(fib->fib_Date.ds_Days + 2922,
                          fib->fib_Date.ds_Minute, date);
                if (pan_add(p, (char *)fib->fib_FileName,
                            strlen((char *)fib->fib_FileName),
                            dir ? 0 : fib->fib_Size, date, dir))
                    p->ent[p->nent - 1].when =
                        fib->fib_Date.ds_Days * 86400 +
                        fib->fib_Date.ds_Minute * 60 +
                        fib->fib_Date.ds_Tick / 50;
            }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(l);
    pan_sort(p);
    pan_seek(p, keep);
    return TRUE;
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
    char m[300];
    if (rc == SF_AGAIN || SF_LOST(rc) || !c->sf) {
        if (SF_LOST(rc))
            c->lost = TRUE;
        sf_drop(c);
        return FALSE;
    }
    if (rc < 0)
        rc = OPS->sf_error(c->sf);
    to_latin1(what, strlen(what), m, 200, '?');
    strcat(m, ": ");
    strcat(m, rc > 0 && rc <= 20 && why[rc][0] ? why[rc]
                                               : "the host said no");
    if (!hush)
        tsay(c, "\x1b[1;31m[", m, "]\x1b[0m\r\n");
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

/* A drawer's listing into lpan: path NULL the drawer it is in, else
 * one under it ("sub/drawer") */
static BOOL sf_list(struct RConn *c, const char *path)
{
    struct Panel *p = lpan(c);
    void *dh;
    LONG rc, r2;
    char name[512], dir[400];
    SfAttr a;
    int i, links = 0;
    if (path)
        rpath(c, path, dir, sizeof dir);
    else
        strcpy(dir, c->cwd);
    while ((rc = OPS->sf_opendir(c->sf, dir, &dh)) == SF_AGAIN &&
           sf_wait(c))
        ;
    if (rc)
        return sf_fail(c, rc, dir);
    pan_clear(p);
    for (;;) {
        while ((rc = OPS->sf_readdir(dh, name, sizeof name, &a)) ==
               SF_AGAIN && sf_wait(c))
            ;
        if (rc <= 0)
            break;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        pan_add(p, name, rc, a.dir ? 0 : a.size, "",
                a.dir ? 1 : a.link ? 2 : 0);
        if (a.mtime)
            ent_when(c, a.mtime / 86400, a.mtime % 86400, TRUE);
    }
    tidy++;
    while ((r2 = OPS->sf_closedir(dh)) == SF_AGAIN && sf_wait(c))
        ;
    tidy--;
    if (rc < 0)
        return sf_fail(c, rc, dir);
    /* links followed, to tell drawers from files (the first 32); not in
     * a drawer's walk: a link to a drawer above would never end */
    for (i = 0; i < p->nent && links < 32 && !path; i++)
        if (p->ent[i].dir == 2) {
            links++;
            rpath(c, pname(p, i), name, sizeof name);
            while ((rc = OPS->sf_stat(c->sf, name, &a)) == SF_AGAIN &&
                   sf_wait(c))
                ;
            if (rc == SF_AGAIN)
                return sf_fail(c, rc, name);
            if (!rc) {
                p->ent[i].dir = a.dir ? 1 : 0;
                p->ent[i].size = a.dir ? 0 : a.size;
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
    if (!(c ? ask_tab(c, prompt, buf, max, echo)
            : term_ask(prompt, buf, max, echo)))
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
    saying = c;
    if (c->kind == FK_FTP) {
        ok = ftp_open(c);
        saying = NULL;
        return ok;
    }
    if (!OPS) {
        saying = NULL;
        tsay(c, "\x1b[1;31m[SFTP is CTermSSH's: call it from there]"
             "\x1b[0m\r\n", NULL, NULL);
        return FALSE;
    }
    asking = c;
    asked = FALSE;
    c->sf = OPS->sf_open(c->tab->t_dialhost, f_ask, fsay);
    asking = NULL;
    saying = NULL;
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

/* a listing into lpan: a, a drawer under the one it is in (NULL: that) */
static BOOL op_list(struct RConn *c, const char *a, const char *b)
{
    BOOL ok = c->kind == FK_FTP ? ftp_list(c, a) : sf_list(c, a);
    (void)b;
    if (ok)
        pan_sort(lpan(c));
    return ok;
}

/* The host's drawer read again, the bar on the name it was on. */
BOOL rem_list(struct RConn *c)
{
    struct Panel *p = &c->pn[1];
    char keep[256];
    BOOL ok;
    pan_keep(p, keep);
    ok = run_op(c, op_list, NULL, NULL);
    p->up = strcmp(c->cwd, "/") != 0;
    pan_seek(p, keep);
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

/* Into a drawer of the host's (NULL: up), and its listing. */
BOOL rem_cd(struct RConn *c, const char *path)
{
    if (!run_op(c, op_cd, path, NULL))
        return FALSE;
    c->pn[1].cur = c->pn[1].top = 0;
    c->pn[1].nent = 0;
    return rem_list(c);
}

/* mkdir, rmdir, delete, rename: what is FR_* (FR_RMDIR) */
static int op_what;
static BOOL op_do(struct RConn *c, const char *a, const char *b)
{
    char pa[400], pb[400];
    LONG rc;
    if (c->kind == FK_FTP) {
        BOOL ok;
        c->quiet = hush;
        ok = ftp_do(c, op_what, a, b);
        c->quiet = FALSE;
        if (ok)
            return TRUE;
        if (!c->lost && c->code < 400 && !hush)
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

BOOL rem_do(struct RConn *c, int what, const char *a, const char *b)
{
    op_what = what;
    return run_op(c, op_do, a, b);
}

/* ---- a file's bytes, either way ---- */

static ULONG xfrom, xfrom0;             /* x_open: where it goes on from */
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
    /* an upload going on: opened to add to it (2), not cut short */
    while ((rc = OPS->sf_fopen(c->sf, p, xhow && xfrom ? 2 : xhow,
                               &c->sfh)) == SF_AGAIN && sf_wait(c))
        ;
    if (rc)
        return sf_fail(c, rc, name);
    if (xfrom)
        OPS->sf_seek(c->sfh, xfrom);
    return TRUE;
}

/* how: 0 a download, 1 an upload; from *from on (0: from the start; set
 * to 0 when the host cannot go on) */
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
BOOL ready_now(LONG fd, BOOL wr)
{
    fd_set s;
    struct timeval tv;
    FD_ZERO(&s);
    FD_SET(fd, &s);
    tv.tv_sec = tv.tv_usec = 0;
    return WaitSelect(fd + 1, wr ? NULL : &s, wr ? &s : NULL, NULL, &tv,
                      NULL) > 0;
}

/* >0 bytes, 0 the end, -1 failed, AGAIN. FTP's data socket is read
 * only when it has something (a write: room for 2 KB, the stack's
 * low-water mark). */
static LONG x_read(struct RConn *c, UBYTE *b, LONG n)
{
    LONG k;
    if (c->kind == FK_FTP)
        return ftp_dread(c, b, n);
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
    if (c->kind == FK_FTP)
        return ftp_dwrite(c, b, n);
    k = OPS->sf_write(c->sfh, b, n);
    if (k == SF_AGAIN)
        return AGAIN;
    if (k < 0)
        sf_fail(c, k, "writing");
    return k < 0 ? -1 : k;
}

static BOOL x_wait(struct RConn *c, BOOL up)
{
    if (c->kind == FK_FTP)              /* (TLS: the way it said) */
        return await(c, c->dfd, c->dwant ? c->dwant : up ? SF_OUT : SF_IN,
                     120);
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

/* ---- what a job holds: files and drawers, and what is in them ---- */

void job_free(struct Job *j)
{
    if (j->it)
        FreeVec(j->it);
    if (j->pool)
        FreeVec(j->pool);
    memset(j, 0, sizeof *j);
}

/* a + "/" + b (b NULL: a; dir: AddPart's way) into the pool: its offset,
 * -1 no room */
static LONG job_str(struct Job *j, const char *a, const char *b, BOOL dir)
{
    LONG need = strlen(a) + (b ? strlen(b) + 2 : 1), off = j->plen;
    if (need > 500)
        return -1;
    if (j->plen + need > j->pcap) {
        LONG cap = j->pcap ? j->pcap * 2 : 4096;
        char *n;
        while (cap < j->plen + need)
            cap *= 2;
        if (!(n = AllocVec(cap, MEMF_ANY)))
            return -1;
        if (j->pool) {
            CopyMem(j->pool, n, j->plen);
            FreeVec(j->pool);
        }
        j->pool = n;
        j->pcap = cap;
    }
    strcpy(j->pool + off, a);
    if (b) {
        if (dir)
            AddPart((STRPTR)(j->pool + off), (STRPTR)b, need);
        else {
            if (*a)
                strcat(j->pool + off, "/");
            strcat(j->pool + off, b);
        }
    }
    j->plen += strlen(j->pool + off) + 1;
    return off;
}

static struct XItem *job_new(struct Job *j)
{
    struct XItem *it;
    if (j->n == j->cap) {
        int cap = j->cap ? j->cap * 2 : 16;
        struct XItem *n = AllocVec(cap * sizeof *n, MEMF_ANY);
        if (!n)
            return NULL;
        if (j->it) {
            CopyMem(j->it, n, j->n * sizeof *n);
            FreeVec(j->it);
        }
        j->it = n;
        j->cap = cap;
    }
    it = &j->it[j->n];
    memset(it, 0, sizeof *it);
    return it;
}

/* The next: a drawer's path and a name (dir NULL: the name), its size;
 * on the Amiga's side its name over there is the last part, on the
 * host's side the name itself. */
BOOL job_add(struct Job *j, const char *dir, const char *name, ULONG size,
             int isdir)
{
    struct XItem *it = job_new(j);
    LONG o;
    if (!it || (o = job_str(j, dir ? dir : name, dir ? name : NULL, TRUE)) < 0)
        return FALSE;
    it->noff = it->roff = o;
    if (!j->side)
        it->roff = o + ((char *)FilePart((STRPTR)(j->pool + o)) -
                        (j->pool + o));
    it->size = size;
    it->dir = isdir;
    j->n++;
    return TRUE;
}

/* An entry of a drawer walked, after its drawer (item i) */
static BOOL job_child(struct Job *j, int i, const struct REnt *e,
                      const char *name)
{
    struct XItem *it = job_new(j), *d;
    LONG o, r;
    if (!it)
        return FALSE;
    d = &j->it[i];                      /* (job_new may have moved it) */
    if ((o = job_str(j, JNAME(j, i), name, !j->side)) < 0)
        return FALSE;
    r = o;
    if (!j->side && (r = job_str(j, JREL(j, i), name, FALSE)) < 0)
        return FALSE;
    d = &j->it[i];
    it->noff = o;
    it->roff = r;
    it->size = e->dir == 1 ? 0 : e->size;
    it->when = e->when;
    it->tz = e->tz;
    it->dir = e->dir;
    it->exist = d->exist;               /* in a drawer there: maybe there */
    it->deep = 1;
    j->n++;
    return TRUE;
}

/* What is in the job's drawers, and in theirs, after them (a drawer
 * comes before what is in it; deleting goes from the end). The host's
 * drawers are listed as the panels list them (MLSD or LIST of the path,
 * SFTP's opendir); links are not followed. FALSE: given up, or a
 * listing failed (said). */
BOOL job_walk(struct RConn *c, struct Job *j)
{
    struct Panel tmp;
    int i, k, depth;
    BOOL ok = TRUE;
    memset(&tmp, 0, sizeof tmp);
    for (i = 0; i < j->n && ok; i++) {
        const char *s;
        if (j->it[i].dir != 1)
            continue;
        if (given_up(c)) {
            ok = FALSE;
            break;
        }
        for (depth = 0, s = JREL(j, i); *s; s++)
            depth += *s == '/';
        if (depth > 30 || j->n > 20000) {
            tsay(c, "\x1b[1;31m[too deep, or too many files: the rest is "
                 "left out]\x1b[0m\r\n", NULL, NULL);
            break;
        }
        if (j->side) {
            char m[80];
            c->lp = &tmp;
            to_latin1(JNAME(j, i), strlen(JNAME(j, i)), m, 60, '?');
            ui_note(c, m, FALSE);
            ok = run_op(c, op_list, JNAME(j, i), NULL);
            c->lp = NULL;
        } else {
            BPTR l = Lock((STRPTR)JNAME(j, i), ACCESS_READ);
            struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
            pan_clear(&tmp);
            ok = l && fib && Examine(l, fib) && fib->fib_DirEntryType > 0;
            if (ok)
                while (ExNext(l, fib)) {
                    LONG t = fib->fib_DirEntryType;
                    if (pan_add(&tmp, (char *)fib->fib_FileName,
                                strlen((char *)fib->fib_FileName),
                                t > 0 ? 0 : fib->fib_Size, "",
                                t == ST_SOFTLINK ? 2 : t > 0))
                        tmp.ent[tmp.nent - 1].when =
                            fib->fib_Date.ds_Days * 86400 +
                            fib->fib_Date.ds_Minute * 60 +
                            fib->fib_Date.ds_Tick / 50;
                }
            if (fib)
                FreeDosObject(DOS_FIB, fib);
            if (l)
                UnLock(l);
            if (!ok)
                tsay(c, "\x1b[1;31m[cannot read ", JNAME(j, i),
                     "]\x1b[0m\r\n");
        }
        for (k = 0; ok && k < tmp.nent; k++)
            if (!job_child(j, i, &tmp.ent[k], pname(&tmp, k))) {
                tsay(c, "\x1b[1;31m[out of memory]\x1b[0m\r\n", NULL, NULL);
                ok = FALSE;
            }
    }
    pan_free(&tmp);
    return ok;
}

/* ---- transfers ---- */

/* How it goes: the tab's name and the Transfers window (a few times a
 * second), the panels' message line; and CTerm's round now and then,
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
    if (x->tlast != was)
        ui_progress(c);
}

/* A host's path (sub/drawer/name) under dir, each part made safe */
static void local_path(const char *dir, const char *rel, char *out, int max)
{
    char part[260], nm[112];
    strncpy(out, dir, max - 1);
    out[max - 1] = 0;
    while (*rel) {
        int k = 0;
        while (*rel && *rel != '/' && k < (int)sizeof part - 1)
            part[k++] = *rel++;
        part[k] = 0;
        while (*rel == '/')
            rel++;
        local_name(part, nm);
        AddPart((STRPTR)out, (STRPTR)nm, max);
    }
}

/* A file's date set, the Amiga's seconds */
static void set_date(const char *path, ULONG when)
{
    struct DateStamp ds;
    ds.ds_Days = when / 86400;
    ds.ds_Minute = when % 86400 / 60;
    ds.ds_Tick = when % 60 * TICKS_PER_SECOND;
    SetFileDate((STRPTR)path, &ds);
}

/* One file (or drawer) from the host into dir (a file inside a drawer
 * copied: into its drawer there): 1 done, 2 there already, 0 left out,
 * 3 a drawer made, -1 failed, -2 stopped. */
static int get_one(struct RConn *c, struct XItem *it, const char *rname,
                   const char *dir, UBYTE *buf)
{
    struct Xfer *x = c->x;
    char nm[112], cand[112], path[400], part[408], ldir[400];
    ULONG have = 0, got, size = it->size, when = it->when;
    LONG k, ps;
    BPTR f;
    BOOL ok = FALSE, stopped;
    int n = 0;
    if (it->deep) {                     /* its drawers' names, made safe */
        local_path(dir, rname, ldir, sizeof ldir);
        *PathPart((STRPTR)ldir) = 0;
        dir = ldir;
    }
    local_name(rname, nm);
    if (it->dir == 1) {                 /* a drawer: made, or there */
        BPTR l;
        strcpy(path, dir);
        AddPart((STRPTR)path, (STRPTR)nm, sizeof path);
        if ((ps = fsize(path)) == -2)
            return 3;
        if (ps == -1 && (l = CreateDir((STRPTR)path))) {
            UnLock(l);
            return 3;
        }
        tsay(c, "\x1b[1;31m[cannot make the drawer ", path, "]\x1b[0m\r\n");
        return -1;
    }
    strcpy(cand, nm);
    for (;;) {                          /* never over a file that is there */
        strcpy(path, dir);
        AddPart((STRPTR)path, (STRPTR)cand, sizeof path);
        if ((ps = fsize(path)) == -1)
            break;
        /* a drawer copied again: what it has already is left out */
        if (it->deep && !n && size && (ULONG)ps == size) {
            tsay(c, "\x1b[2m[", cand, ": there already]\x1b[0m\r\n");
            x->bdone += size;
            return 2;
        }
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
    x->name[sizeof x->name - 1] = 0;
    x->size = size;
    x->done = 0;
    ui_progress(c);
    /* the time it has there: UTC from MDTM, unless the listing gave it
     * (MLSD, SFTP); LIST's own clock only when the host has no MDTM */
    if (c->kind == FK_FTP && it->tz != 2 && r_ready(c)) {
        ULONG u = ftp_mdtm(c, rname);
        if (u > 2922UL * 86400)
            when = u + tz_east(u) - 2922UL * 86400;
    }
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
        char m[160];
        strcpy(m, FilePart((STRPTR)part));
        strcat(m, ", ");
        size_text(have, m + strlen(m));
        strcat(m, " there]\x1b[0m\r\n");
        tsay(c, "\x1b[2m[going on from ", m, NULL);
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
            if (c->kind == FK_FTP && !c->dtl && !ready_now(c->dfd, FALSE))
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
        else {
            if (when)
                set_date(path, when);   /* the date it has on the host */
            tsay(c, "\x1b[2m[", cand, " received]\x1b[0m\r\n");
        }
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

/* A file's size on the host now (SIZE, SFTP's stat; called again when
 * the line was lost): -1 not there or not known */
static LONG op_sz;
static BOOL op_size(struct RConn *c, const char *rname, const char *b)
{
    (void)b;
    op_sz = -1;
    if (c->kind == FK_FTP) {
        op_sz = ftp_size(c, rname);
        return op_sz >= 0;
    }
    {
        char p[400];
        SfAttr a;
        LONG rc;
        rpath(c, rname, p, sizeof p);
        while ((rc = OPS->sf_stat(c->sf, p, &a)) == SF_AGAIN && sf_wait(c))
            ;
        if (rc == SF_AGAIN || SF_LOST(rc))
            return sf_fail(c, rc, rname);
        if (!rc && !a.dir)
            op_sz = a.size;
        return op_sz >= 0;
    }
}

static LONG r_size_now(struct RConn *c, const char *rname)
{
    run_op(c, op_size, rname, NULL);
    return op_sz;
}

/* One file (or drawer) to the host, under the name rel (in the drawer
 * it is in): a file of the name there is replaced, or (upmode 1, or
 * the upload that broke off before) gone on from where it ends - left
 * out when it is as long. 1 done, 2 there already, 0 left out, 3 a
 * drawer made, -1 failed, -2 stopped. */
static int put_one(struct RConn *c, struct XItem *it, const char *lpath,
                   const char *rel, UBYTE *buf)
{
    struct Xfer *x = c->x;
    char rname[256];
    const char *fp = (const char *)FilePart((STRPTR)lpath);
    LONG size = fsize(lpath), k;
    ULONG from = 0, sent;
    BPTR f;
    BOOL ok = FALSE, again;
    to_utf8(rel, rname, sizeof rname);
    if (it->dir == 1) {
        if (size != -2) {
            tsay(c, "\x1b[2m[", lpath, ": not a drawer, not sent]\x1b[0m\r\n");
            return 0;
        }
        hush = it->exist;               /* may be there: no word on it */
        ok = rem_do(c, FR_MKDIR, rname, NULL);
        hush = FALSE;
        if (!ok && !it->exist) {
            tsay(c, "\x1b[1;31m[", rel, ": no drawer made]\x1b[0m\r\n");
            return given_up(c) ? -2 : -1;
        }
        return 3;
    }
    if (size < 0) {
        tsay(c, "\x1b[2m[", lpath, ": not a file, not sent]\x1b[0m\r\n");
        return 0;
    }
    again = !strcmp(rname, c->upart);
    if (again || (it->exist && c->upmode == 1)) {
        LONG there = r_size_now(c, rname);
        if (there == size && !again) {
            tsay(c, "\x1b[2m[", rel, ": there already]\x1b[0m\r\n");
            x->bdone += size;
            return 2;
        }
        if (there > size && !again) {
            tsay(c, "\x1b[2m[", rel, ": longer on the host, left as it "
                 "is]\x1b[0m\r\n");
            return 0;
        }
        if (there > 0 && there < size)
            from = there;
    }
    if (!(f = Open((STRPTR)lpath, MODE_OLDFILE))) {
        tsay(c, "\x1b[1;31m[cannot read ", lpath, "]\x1b[0m\r\n");
        return -1;
    }
    strncpy(x->name, fp, sizeof x->name - 1);
    x->name[sizeof x->name - 1] = 0;
    x->size = size;
    x->done = 0;
    ui_progress(c);
    if (!x_open(c, rname, 1, &from)) {
        Close(f);
        return given_up(c) ? -2 : -1;
    }
    if (from && Seek(f, from, OFFSET_BEGINNING) < 0)
        from = size + 1;                /* cannot: it fails below */
    if (from) {
        char m[160];
        strncpy(m, fp, 100);
        m[100] = 0;
        strcat(m, ", ");
        size_text(from, m + strlen(m));
        strcat(m, " there]\x1b[0m\r\n");
        tsay(c, "\x1b[2m[going on from ", m, NULL);
    }
    sent = x->done = from;
    x->bdone += from;
    for (;;) {
        LONG off = 0;
        k = from > (ULONG)size ? -1 : Read(f, buf, BUFSZ);
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
        strcpy(c->upart, rname);        /* the next upload of it goes on */
        tsay(c, "\x1b[2m[", fp, ": stopped - sending it again goes on "
             "from where it ends]\x1b[0m\r\n");
        return -2;
    }
    if (!x_close(c, !ok) || !ok) {
        strcpy(c->upart, rname);
        tsay(c, "\x1b[1;31m[", fp, " failed]\x1b[0m\r\n");
        return -1;
    }
    if (again)
        c->upart[0] = 0;
    tsay(c, "\x1b[2m[", fp, " sent]\x1b[0m\r\n");
    x->files++;
    return 1;
}

/* The job's files and drawers to (up: the Amiga's paths) or from the
 * host (names from the drawer it is in, into dir), one after another,
 * shown as a transfer. The files done; -1: none could start. */
int xfer_batch(struct RConn *c, BOOL up, struct Job *j, const char *dir)
{
    struct Xfer x;
    UBYTE *buf;
    int i, done = 0, r = 0, failed = 0;
    char m[100];
    if (xfer_busy()) {
        tsay(c, "\x1b[1;31m[one transfer at a time: wait for the other "
             "one]\x1b[0m\r\n", NULL, NULL);
        return -1;
    }
    if (!(buf = AllocVec(BUFSZ, MEMF_ANY)))
        return -1;
    memset(&x, 0, sizeof x);
    x.proto = c->kind == FK_SFTP ? "SFTP" : c->tls ? "FTPS" : "FTP";
    x.dir = up ? 2 : 1;
    if (up)
        to_latin1(c->cwd, strlen(c->cwd), x.where, sizeof x.where, '?');
    else
        strncpy(x.where, dir, sizeof x.where - 1);
    for (i = 0; i < j->n; i++) {
        LONG z;
        if (j->it[i].dir == 1)
            continue;
        x.nfiles++;
        z = up ? fsize(JNAME(j, i)) : (LONG)j->it[i].size;
        if (z > 0)
            x.bsize += z;
    }
    c->x = &x;
    c->moved = 0;
    c->polled = ticks50();
    xfer_show(c->tab, &x, 1);
    for (i = 0; i < j->n && r != -2; i++) {
        x.fbase = x.bdone;
        r = up ? put_one(c, &j->it[i], JNAME(j, i), JREL(j, i), buf)
               : get_one(c, &j->it[i], JNAME(j, i), dir, buf);
        if (r == 1 || r == 2)
            done++;
        if (r == 2)
            x.files++;                  /* there already: as if done */
        else if (r == -1)
            failed++;
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
    strcat(m, " KB");
    if (failed) {
        strcat(m, ", ");
        numstr_cat(m, failed);
        strcat(m, " failed");
    }
    strcat(m, "]\x1b[0m\r\n");
    tsay(c, m, NULL, NULL);
    return done;
}

/* A name of the host's (not in its panel, maybe): what it is, how big,
 * its time - from the panel, else asked (SIZE, SFTP's stat). */
static void r_info(struct RConn *c, const char *rname, struct XItem *it)
{
    int i = pan_find(&c->pn[1], rname);
    if (i >= 0) {
        const struct REnt *e = &c->pn[1].ent[i];
        it->size = e->dir == 1 ? 0 : e->size;
        it->dir = e->dir == 1;
        it->when = e->when;
        it->tz = e->tz;
        return;
    }
    if (c->kind == FK_FTP) {
        LONG k = ftp_size(c, rname);
        it->size = k > 0 ? (ULONG)k : 0;
    } else {
        char p[400];
        SfAttr a;
        LONG rc;
        rpath(c, rname, p, sizeof p);
        while ((rc = OPS->sf_stat(c->sf, p, &a)) == SF_AGAIN && sf_wait(c))
            ;
        if (!rc) {
            it->size = a.dir ? 0 : a.size;
            it->dir = a.dir;
            if (a.mtime > 2922UL * 86400) {
                it->when = a.mtime + tz_east(a.mtime) - 2922UL * 86400;
                it->tz = 2;
            }
        }
    }
}

/* ---- the panels' own ---- */

void hang_up(struct RConn *c)
{
    r_drop(c);
    c->tab->ended = TRUE;               /* the tab goes, and with it c */
}

/* A command at work: no other one starts (busy), Stop is for it. */
void work(struct RConn *c, BOOL on)
{
    c->busy = on;
    if (on)
        c->stop = FALSE;
}

/* ---- the part's way in (part.h: FilesPart) ---- */

static void conn_free(struct RConn *c)
{
    struct RConn **q;
    if (c->ui && !c->tab->ended)
        ui_stop(c);                     /* the tab stays: its messages */
    ui_free(c);
    r_drop(c);
    ftps_done(c);                       /* the last FTPS place: AmiSSL goes */
    pan_free(&c->pn[0]);
    pan_free(&c->pn[1]);
    for (q = &conns; *q && *q != c; q = &(*q)->next)
        ;
    if (*q)
        *q = c->next;
    c->tab->t_rf = NULL;
    c->tab->t_fui = FALSE;
    memset(c->pass, 0, sizeof c->pass);
    FreeVec(c);
}

static BOOL callok;

/* T's place: connected (if it is not), its listings, its panels. */
static void f_call_(void)
{
    struct RConn *c = conn_of(T);
    callok = FALSE;
    if (!c) {
        if (!(c = AllocVec(sizeof *c, MEMF_ANY | MEMF_CLEAR)))
            return;
        c->tab = T;
        c->kind = T->t_fkind == FK_FTPS ? FK_FTP : T->t_fkind;
        c->tls = T->t_fkind == FK_FTPS; /* FTP, with AUTH TLS */
        c->cfd = c->dfd = -1;
        c->side = 1;
        strcpy(c->pass, T->t_pass);
        c->next = conns;
        conns = c;
        T->t_rf = c;
    }
    if (!c->busy && !is_up(c)) {
        BOOL ok;
        work(c, TRUE);
        ok = r_connect(c);
        if (ok && !rem_list(c) && !is_up(c))
            ok = FALSE;
        work(c, FALSE);
        if (!ok) {
            if (!c->ui)
                conn_free(c);           /* nothing open: the part may go */
            return;
        }
    }
    callok = TRUE;
    if (!c->ui) {
        BPTR l;
        char dir[160];
        tsay(c, "\x1b[2m[connected: the panels are up - F9 there shows "
             "these messages, any key goes back]\x1b[0m\r\n", NULL, NULL);
        down_dir(c, dir);
        c->pn[0].path[0] = 0;
        if ((l = Lock((STRPTR)dir, ACCESS_READ))) {
            if (!NameFromLock(l, (STRPTR)c->pn[0].path,
                              sizeof c->pn[0].path))
                c->pn[0].path[0] = 0;
            UnLock(l);
        }
        loc_list(c);
        ui_start(c);
    } else
        ui_draw(c);
}

static BOOL f_call(void)
{
    deep_call(f_call_);
    return callok;
}

/* Project/Open SFTP browser: from an ssh tab, its host and login in a
 * tab of its own (its own connection); from a browser's tab, its panels
 * drawn again. */
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
    say_key(" for SFTP... ", KA_STOP, " gives up]\x1b[0m\r\n");
    refresh();
    f_call_();
    if (!callok)
        say_key("\x1b[2m[", KA_REDIAL, " calls again]\x1b[0m\r\n");
    refresh();
}

static void f_menu(void)
{
    deep_call(f_menu_);
}

/* A key or a click in a tab whose panels are up (T) */
static int in_k, in_r, in_c;
static ULONG in_s, in_us;

static void f_key_(void)
{
    struct RConn *c = conn_of(T);
    int k = in_k;
    if (c)
        ui_key(c, k);
}

static void f_key(int k)
{
    in_k = k;
    deep_call(f_key_);
}

static void f_mouse_(void)
{
    struct RConn *c = conn_of(T);
    int what = in_k, r = in_r, col = in_c;
    if (c && c->ui)
        ui_mouse(c, what, r, col, in_s, in_us);
}

static void f_mouse(int what, int r, int col, ULONG s, ULONG us)
{
    in_k = what;
    in_r = r;
    in_c = col;
    in_s = s;
    in_us = us;
    deep_call(f_mouse_);
}

/* The tab's size changed: drawn again at the new one. */
static void f_resized(struct Tab *t)
{
    struct RConn *c = conn_of(t);
    if (c && c->ui)
        ui_draw(c);
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
    struct Panel *p;
    char h[256], h2[256], dir[300];
    int i, got;
    rx_rc = 10;
    rx_res = NULL;
    if (!c || c->busy)
        return;                         /* no place here; at work */
    p = &c->pn[1];
    work(c, TRUE);
    switch (rx_what) {
    case FR_LIST:
        if (rem_list(c)) {
            LONG len = 1;
            char *r, *q;
            for (i = 0; i < p->nent; i++)
                len += strlen(pname(p, i)) + 4;
            if ((r = AllocVec(len, MEMF_ANY))) {
                q = r;
                for (i = 0; i < p->nent; i++) {
                    if (i)
                        *q++ = ' ';
                    *q++ = '"';
                    to_latin1(pname(p, i), strlen(pname(p, i)), q,
                              len - (q - r) - 3, '?');
                    q += strlen(q);
                    if (p->ent[i].dir == 1)
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
        if (rem_cd(c, !strcmp(h, "..") ? NULL : h)) {
            to_latin1(c->cwd, strlen(c->cwd), h, sizeof h, '?');
            rx_result(h, -1);
            rx_rc = 0;
        }
        break;
    case FR_GET: {
        struct Job j;
        int nf = 0;
        memset(&j, 0, sizeof j);
        j.side = 1;
        if (!r_ready(c))
            break;
        for (i = 0; i < rx_n; i++) {
            host_name(c, rx_names[i], h, sizeof h);
            if (!job_add(&j, NULL, h, 0, 0))
                break;
            r_info(c, h, &j.it[j.n - 1]);   /* a drawer: all of it */
        }
        /* TO, else the Amiga panel's drawer, else the download drawer */
        if (rx_to) {
            strncpy(dir, rx_to, sizeof dir - 1);
            dir[sizeof dir - 1] = 0;
        } else if (c->pn[0].path[0])
            strcpy(dir, c->pn[0].path);
        else
            down_dir(c, dir);
        got = -1;
        if (i == rx_n && job_walk(c, &j)) {
            for (i = 0; i < j.n; i++)
                nf += j.it[i].dir != 1;
            got = xfer_batch(c, FALSE, &j, dir);
        }
        rx_rc = got >= 0 && got == nf ? 0 : got > 0 ? 5 : 10;
        job_free(&j);
        loc_list(c);
        break;
    }
    case FR_PUT: {
        struct Job j;
        int nf = 0;
        memset(&j, 0, sizeof j);
        c->upmode = 0;                  /* names there: replaced */
        for (i = 0; i < rx_n; i++) {
            LONG z = fsize(rx_names[i]);
            if (!job_add(&j, NULL, rx_names[i], z > 0 ? z : 0, z == -2))
                break;
            to_utf8(JREL(&j, j.n - 1), h, sizeof h);
            j.it[j.n - 1].exist = pan_find(p, h) >= 0;
        }
        got = -1;
        if (i == rx_n && r_ready(c) && job_walk(c, &j)) {
            for (i = 0; i < j.n; i++)
                nf += j.it[i].dir != 1;
            got = xfer_batch(c, TRUE, &j, NULL);
        }
        rx_rc = got >= 0 && got == nf ? 0 : got > 0 ? 5 : 10;
        job_free(&j);
        rem_list(c);
        break;
    }
    case FR_MKDIR:
        to_utf8(rx_names[0], h, sizeof h);
        if (rem_do(c, FR_MKDIR, h, NULL))
            rx_rc = 0;
        rem_list(c);
        break;
    case FR_DELETE:
        host_name(c, rx_names[0], h, sizeof h);
        i = pan_find(p, h);
        if (rem_do(c, i >= 0 && p->ent[i].dir == 1 ? FR_RMDIR : FR_DELETE,
                   h, NULL))
            rx_rc = 0;
        rem_list(c);
        break;
    case FR_RENAME:
        host_name(c, rx_names[0], h, sizeof h);
        to_utf8(rx_to ? rx_to : "", h2, sizeof h2);
        if (h2[0] && rem_do(c, FR_RENAME, h, h2))
            rx_rc = 0;
        rem_list(c);
        break;
    }
    work(c, FALSE);
    if (c->ui && !c->tab->ended)
        ui_draw(c);
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
    static struct FilesPart p = { f_call, f_menu, f_key, f_mouse,
                                  f_resized, f_tab_gone, f_rexx, f_idle };
    return &p;
}
