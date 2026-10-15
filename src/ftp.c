/*
 * ftp - FTP for the file browser (files.h): one control connection, a
 * passive data connection per listing or file (EPSV, else PASV - its
 * address is not trusted: the data goes to the host already called, as
 * curl does), binary, listings from MLSD where the host has it, else
 * LIST (Unix ls -l, or Windows' DOS style), REST to go on from a part.
 * Plain FTP: the password goes as it is, as FTP has always sent it.
 * Part of Parts/Files; every wait is files.c's await (CTerm goes on).
 */
#include "cterm.h"
#include "files.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

void sock_nonblock(LONG s)
{
    LONG on = 1;
    if (s >= 0)
        IoctlSocket(s, NB_FIONBIO, (APTR)&on);
}

static void lost(struct RConn *c)
{
    if (c->cfd >= 0)
        CloseSocket(c->cfd);
    c->cfd = -1;
    c->inlen = 0;
    c->lost = TRUE;
}

/* A command to the host (PASS never shown). FALSE: the line is gone. */
static BOOL put_cmd(struct RConn *c, const char *cmd, const char *arg)
{
    char line[540];
    LONG n, done = 0;
    if (c->cfd < 0)
        return FALSE;
    strcpy(line, cmd);
    if (arg) {
        strcat(line, " ");
        strncat(line, arg, 500);
    }
    strcat(line, "\r\n");
    n = strlen(line);
    while (done < n) {
        LONG k;
        if (!await(c, c->cfd, SF_OUT, 30)) {
            if (c->timedout)
                lost(c);
            return FALSE;
        }
        k = send(c->cfd, line + done, n - done, 0);
        if (WOULDBLOCK(k))
            continue;                   /* no room after all: wait again */
        if (k <= 0) {
            lost(c);
            return FALSE;
        }
        done += k;
    }
    return TRUE;
}

/* A line of the host's reply, its CR LF off. FALSE: none came. */
static BOOL get_line(struct RConn *c, char *out, int max, LONG secs)
{
    for (;;) {
        int i;
        LONG k;
        for (i = 0; i < c->inlen && c->in[i] != '\n'; i++)
            ;
        if (i < c->inlen || c->inlen == (int)sizeof c->in) {
            int n = i < max - 1 ? i : max - 1;
            memcpy(out, c->in, n);
            out[n] = 0;
            if (n && out[n - 1] == '\r')
                out[n - 1] = 0;
            if (i < c->inlen)
                i++;                    /* the \n */
            c->inlen -= i;
            memmove(c->in, c->in + i, c->inlen);
            return TRUE;
        }
        if (c->cfd < 0)
            return FALSE;
        if (!await(c, c->cfd, SF_IN, secs)) {
            if (c->timedout)
                lost(c);                /* silent: no use any more */
            return FALSE;
        }
        k = recv(c->cfd, c->in + c->inlen, sizeof c->in - c->inlen, 0);
        if (WOULDBLOCK(k))
            continue;                   /* "readable", yet nothing: wait */
        if (k <= 0) {
            lost(c);
            return FALSE;
        }
        c->inlen += k;
    }
}

static int code_of(const char *l)
{
    if (l[0] >= '1' && l[0] <= '5' && l[1] >= '0' && l[1] <= '9' &&
        l[2] >= '0' && l[2] <= '9')
        return (l[0] - '0') * 100 + (l[1] - '0') * 10 + (l[2] - '0');
    return 0;
}

/* The host's reply: its number (0: none came). "230-" lines go on to
 * the "230 " line; its text is kept in c->reply. Shown in the tab when
 * chatty (the greeting, the login) or when it is an error. */
static int reply_t(struct RConn *c, LONG secs)
{
    char line[256];
    int first = 0;
    for (;;) {
        int k;
        if (!get_line(c, line, sizeof line, secs))
            return c->code = 0;
        k = code_of(line);
        if (c->chatty || ((first ? first : k) >= 400 && !c->quiet))
            tsay_host(c, line);
        if (!first) {
            if (!k)
                continue;               /* not a reply: noise */
            first = k;
        }
        if (k == first && line[3] != '-') {
            strncpy(c->reply, line, sizeof c->reply - 1);
            c->reply[sizeof c->reply - 1] = 0;
            if (k == 421)
                lost(c);                /* the host hangs up (idle) */
            return c->code = k;
        }
    }
}

static int reply(struct RConn *c)
{
    return reply_t(c, 60);
}

static int cmd(struct RConn *c, const char *cmd_, const char *arg)
{
    if (!put_cmd(c, cmd_, arg))
        return c->code = 0;
    return reply(c);
}

/* PWD's answer: 257 "/the/path" (a quote inside is doubled). */
static void pwd(struct RConn *c)
{
    const char *p;
    int k = 0;
    if (cmd(c, "PWD", NULL) != 257 || !(p = strchr(c->reply, '"')))
        return;
    for (p++; *p && k < (int)sizeof c->cwd - 1; p++) {
        if (*p == '"') {
            if (p[1] != '"')
                break;
            p++;
        }
        c->cwd[k++] = *p;
    }
    c->cwd[k] = 0;
}

BOOL ftp_open(struct RConn *c)
{
    char host[128], *colon, pw[64];
    const char *user;
    LONG port = 21;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;
    int r;
    c->inlen = 0;
    c->dfd = -1;
    c->lost = FALSE;
    strncpy(host, c->tab->t_dialhost, sizeof host - 1);
    host[sizeof host - 1] = 0;
    if ((colon = strchr(host, ':'))) {
        *colon = 0;
        port = atol(colon + 1);
    }
    T = c->tab;
    c->cfd = net_connect(host, port, fsay);
    sock_nonblock(c->cfd);
    if (c->cfd < 0)
        return FALSE;
    c->peer[0] = 0;
    if (!getpeername(c->cfd, (struct sockaddr *)&peer, &len))
        strncpy(c->peer, (char *)Inet_NtoA(peer.sin_addr.s_addr),
                sizeof c->peer - 1);
    if (!c->peer[0])
        strncpy(c->peer, host, sizeof c->peer - 1);
    c->chatty = TRUE;
    if (reply(c) != 220)
        goto fail;
    user = c->tab->t_user[0] ? c->tab->t_user : "anonymous";
    r = cmd(c, "USER", user);
    if (r == 331 || r == 332) {
        const char *pass = c->pass[0] ? c->pass : c->tab->t_pass;
        if (!pass[0] && !c->tab->t_user[0])
            pass = "cterm@amiga";       /* anonymous: an address, by custom */
        if (!pass[0]) {
            char q[100];
            strcpy(q, user);
            strcat(q, "'s password: ");
            if (!ask_tab(c, q, pw, sizeof pw, FALSE))
                goto fail;
            strcpy(c->pass, pw);        /* for calling again */
            memset(pw, 0, sizeof pw);
            pass = c->pass;
        }
        r = cmd(c, "PASS", pass);
    }
    if (r != 230 && r != 202) {
        tsay(c, "\x1b[1;31m[not logged in]\x1b[0m\r\n", NULL, NULL);
        c->pass[0] = 0;                 /* a wrong one: asked again */
        goto fail;
    }
    c->chatty = FALSE;
    if (cmd(c, "TYPE", "I") != 200)
        goto fail;
    if (!c->cwd[0] && c->tab->t_fdir[0] &&
        cmd(c, "CWD", c->tab->t_fdir) != 250)
        tsay(c, "\x1b[2m[no drawer ", c->tab->t_fdir, " there]\x1b[0m\r\n");
    if (c->cwd[0] && cmd(c, "CWD", c->cwd) != 250)
        c->cwd[0] = 0;                  /* called again: where it was */
    pwd(c);
    if (!c->cwd[0])
        strcpy(c->cwd, "/");
    return TRUE;
fail:
    c->chatty = FALSE;
    if (c->cfd >= 0)
        CloseSocket(c->cfd);
    c->cfd = -1;
    return FALSE;
}

void ftp_close(struct RConn *c)
{
    if (c->dfd >= 0)
        CloseSocket(c->dfd);
    c->dfd = -1;
    if (c->cfd >= 0 && !quitting && put_cmd(c, "QUIT", NULL))
        reply_t(c, 3);                  /* a goodbye, not waited long */
    if (c->cfd >= 0)
        CloseSocket(c->cfd);
    c->cfd = -1;
    c->inlen = 0;
}

BOOL ftp_cd(struct RConn *c, const char *path)
{
    int r = path ? cmd(c, "CWD", path) : cmd(c, "CDUP", NULL);
    if (r != 250 && r != 200)
        return FALSE;
    pwd(c);
    return TRUE;
}

/* A passive data connection, before the command that uses it: the
 * socket, -1 none. Non-blocking (files.h), and read only when
 * WaitSelect says so. */
static LONG pasv_open(struct RConn *c)
{
    LONG port = 0;
    const char *p;
    if (!c->noepsv) {
        int r;
        c->quiet = TRUE;                /* an old host: PASV instead */
        r = cmd(c, "EPSV", NULL);
        c->quiet = FALSE;
        if (r == 229 && (p = strstr(c->reply, "(|||")))
            port = atol(p + 4);         /* (|||port|) */
        else if (r >= 500)
            c->noepsv = TRUE;           /* PASV, from now on */
        else if (!r)
            return -1;
    }
    if (!port) {
        int h[6], i = 0;
        c->quiet = TRUE;                /* refused: active FTP instead */
        i = cmd(c, "PASV", NULL);
        c->quiet = FALSE;
        if (i != 227)
            return -1;
        i = 0;
        /* h1,h2,h3,h4,p1,p2 - in brackets or not */
        for (p = c->reply + 4; *p && (*p < '0' || *p > '9'); p++)
            ;
        while (*p && i < 6) {
            h[i] = atol(p);
            while (*p >= '0' && *p <= '9')
                p++;
            if (*p == ',')
                p++;
            i++;
        }
        if (i < 6)
            return -1;
        port = h[4] * 256 + h[5];
    }
    T = c->tab;
    {
        LONG d = net_connect(c->peer, port, fsay);
        sock_nonblock(d);
        return d;
    }
}

/* Active FTP: a socket listening on the Amiga's address as the control
 * line has it (getsockname: the address the host is reached from - on a
 * home network the Amiga's own; through a router's NAT the host could
 * not call it back, which is why passive comes first), told to the host
 * by PORT. The socket, -1 none. */
static LONG port_open(struct RConn *c)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    LONG s;
    UBYTE *ip, *pt;
    char arg[32];
    int i;
    if (getsockname(c->cfd, (struct sockaddr *)&a, &len) ||
        (s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
    a.sin_port = 0;                     /* any port */
    len = sizeof a;
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 1) ||
        getsockname(s, (struct sockaddr *)&a, &len)) {
        CloseSocket(s);
        return -1;
    }
    ip = (UBYTE *)&a.sin_addr.s_addr;   /* in the network's order, as */
    pt = (UBYTE *)&a.sin_port;          /* PORT writes them */
    arg[0] = 0;
    for (i = 0; i < 6; i++) {
        if (i)
            strcat(arg, ",");
        numstr_cat(arg, i < 4 ? ip[i] : pt[i - 4]);
    }
    if (cmd(c, "PORT", arg) != 200) {
        CloseSocket(s);
        return -1;
    }
    sock_nonblock(s);
    return s;
}

/* The data's socket, before the command that uses it: passive (the
 * Amiga calls the host), else active - from the first time passive
 * fails on, or after `passive` was typed. *lis: it is active's socket
 * listening, which data_go turns into the host's call. -1 none. */
static LONG data_open(struct RConn *c, BOOL *lis)
{
    *lis = FALSE;
    if (!c->active) {
        LONG d = pasv_open(c);
        if (d >= 0 || c->cfd < 0 || given_up(c))
            return d;
        tsay(c, "\x1b[2m[passive FTP failed: active FTP from now on - the "
             "command passive switches back]\x1b[0m\r\n", NULL, NULL);
        c->active = TRUE;
    }
    *lis = TRUE;
    return port_open(c);
}

/* After the command's 1xx: the host's call taken (active FTP: it calls
 * the socket listening); a passive socket as it is. -1: no call came. */
static LONG data_go(struct RConn *c, LONG d, BOOL lis)
{
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    LONG s = -1;
    if (!lis)
        return d;
    if (await(c, d, SF_IN, 30))
        s = accept(d, (struct sockaddr *)&a, &len);
    CloseSocket(d);
    if (s < 0) {
        if (!given_up(c) && !c->timedout)
            tsay(c, "\x1b[1;31m[the host did not call back (active FTP)]"
                 "\x1b[0m\r\n", NULL, NULL);
        return -1;
    }
    sock_nonblock(s);
    return s;
}

/* ---- listings ---- */

static int month(const char *m)
{
    static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for (i = 0; i < 12; i++)
        if (!strnicmp((char *)m, (char *)names + 3 * i, 3))
            return i + 1;
    return 0;
}

static void two(char *o, int v)
{
    o[0] = '0' + v / 10 % 10;
    o[1] = '0' + v % 10;
}

/* "2026-10-11 07:50" (no time: the day alone) */
static void datestr(char *o, int y, int mo, int d, int h, int mi)
{
    o[0] = '0' + y / 1000 % 10;
    o[1] = '0' + y / 100 % 10;
    two(o + 2, y);
    o[4] = '-';
    two(o + 5, mo);
    o[7] = '-';
    two(o + 8, d);
    o[10] = 0;
    if (h >= 0) {
        o[10] = ' ';
        two(o + 11, h);
        o[13] = ':';
        two(o + 14, mi);
        o[16] = 0;
    }
}

static int num(const char *v, int n)
{
    int k = 0;
    while (n--)
        k = k * 10 + (*v++ - '0');
    return k;
}

/* The words of a line: where each starts (at most max). */
static int words(const char *l, const char **w, int max)
{
    int n = 0;
    while (*l && n < max) {
        while (*l == ' ' || *l == '\t')
            l++;
        if (!*l)
            break;
        w[n++] = l;
        while (*l && *l != ' ' && *l != '\t')
            l++;
    }
    return n;
}

/* MLSD: "type=file;size=12;modify=20261011075012; name" */
static void mlsd_line(struct RConn *c, const char *l)
{
    const char *name = strstr(l, "; ");
    const char *f = l;
    ULONG size = 0;
    int dir = 0;
    LONG days = 0, secs = 0;
    if (!name)
        return;
    while (f < name) {
        const char *e = f;
        while (e < name && *e != ';')
            e++;
        if (!strnicmp((char *)f, "type=", 5)) {
            if (!strnicmp((char *)f + 5, "cdir", 4) ||
                !strnicmp((char *)f + 5, "pdir", 4))
                return;                 /* . and .. */
            dir = !strnicmp((char *)f + 5, "dir", 3);
            if (!strnicmp((char *)f + 5, "OS.unix=slink", 13) ||
                !strnicmp((char *)f + 5, "OS.unix=symlink", 15))
                dir = 2;
        } else if (!strnicmp((char *)f, "size=", 5))
            size = strtoul(f + 5, NULL, 10);
        else if (!strnicmp((char *)f, "modify=", 7) && e - f >= 21) {
            const char *v = f + 7;      /* YYYYMMDDHHMMSS, UTC */
            days = days_of(num(v, 4), num(v + 4, 2), num(v + 6, 2));
            secs = num(v + 8, 2) * 3600 + num(v + 10, 2) * 60 +
                   num(v + 12, 2);
        }
        f = e + 1;
    }
    if (ent_add(c, name + 2, strlen(name + 2), size, "", dir) && days)
        ent_when(c, days, secs, TRUE);  /* shown in the Amiga's time */
}

/* LIST: Unix "drwxr-xr-x 2 user group 4096 Oct 11 07:50 name" (or the
 * year for the time), or DOS "10-11-26  07:50AM  <DIR>  name". */
static void list_line(struct RConn *c, const char *l)
{
    const char *w[12];
    int n = words(l, w, 12), i, dir;
    char date[18];
    date[0] = 0;
    if (n >= 4 && w[0][2] == '-' && w[0][5] == '-' &&
        w[1][2] == ':') {               /* DOS */
        int mo = atoi(w[0]), d = atoi(w[0] + 3), y = atoi(w[0] + 6);
        int h = atoi(w[1]) % 12, mi = atoi(w[1] + 3);
        if (w[1][5] == 'P' || w[1][5] == 'p')
            h += 12;
        datestr(date, y < 100 ? (y < 70 ? 2000 + y : 1900 + y) : y, mo, d,
                h, mi);
        dir = !strncmp(w[2], "<DIR>", 5);
        if (ent_add(c, w[3], strlen(w[3]), dir ? 0 : strtoul(w[2], NULL, 10),
                    date, dir))
            ent_when(c, days_of(y < 100 ? (y < 70 ? 2000 + y : 1900 + y) : y,
                                mo, d), h * 3600L + mi * 60, FALSE);
        return;
    }
    if (n < 6 || !strchr("-dlbcps", w[0][0]) || strlen(w[0]) < 10)
        return;                         /* "total 12" and the like */
    /* the month: the size is the word before it, the name after the
     * day and the time or year (it may hold spaces) */
    for (i = 3; i + 3 < n; i++)
        if (month(w[i]) && w[i][3] <= ' ' && w[i + 1][0] >= '0' &&
            w[i + 1][0] <= '9')
            break;
    if (i + 3 >= n)
        return;
    {
        int mo = month(w[i]), d = atoi(w[i + 1]), y, h = -1, mi = 0;
        const char *name, *arrow;
        int len;
        if (w[i + 2][1] == ':' || w[i + 2][2] == ':') {
            LONG now = today();         /* yyyymmdd */
            h = atoi(w[i + 2]);
            mi = atoi(strchr(w[i + 2], ':') + 1);
            y = now / 10000;
            if (mo * 100 + d > now % 10000 + 1)
                y--;                    /* in the past half year */
        } else
            y = atoi(w[i + 2]);
        datestr(date, y, mo, d, h, mi);
        name = w[i + 2];
        while (*name && *name != ' ' && *name != '\t')
            name++;
        if (*name)
            name++;                     /* one space: a name may start
                                         * with more */
        len = strlen(name);
        dir = w[0][0] == 'd' ? 1 : w[0][0] == 'l' ? 2 : 0;
        if (dir == 2 && (arrow = strstr(name, " -> ")))
            len = arrow - name;
        if (!len || (len == 1 && name[0] == '.') ||
            (len == 2 && name[0] == '.' && name[1] == '.'))
            return;
        /* the host's own clock: shown as it is (no zone is given) */
        if (ent_add(c, name, len, strtoul(w[i - 1], NULL, 10), date, dir))
            ent_when(c, days_of(y, mo, d), h < 0 ? 0 : h * 3600L + mi * 60,
                     FALSE);
    }
}

/* A drawer's listing (path NULL: the one it is in; else a drawer under
 * it, "sub/drawer", given to MLSD or LIST) */
BOOL ftp_list(struct RConn *c, const char *path)
{
    char line[600];
    int ll = 0;
    for (;;) {
        BOOL lis, mlsd = !c->nomlsd, ok = TRUE;
        LONG d = data_open(c, &lis);
        int r;
        if (d < 0)
            return FALSE;
        c->quiet = mlsd;                /* no MLSD: LIST instead */
        r = cmd(c, mlsd ? "MLSD" : "LIST", path);
        c->quiet = FALSE;
        if (r != 150 && r != 125) {
            CloseSocket(d);
            if (mlsd && r >= 500 && r != 550) {
                c->nomlsd = TRUE;       /* LIST, from now on */
                continue;
            }
            return FALSE;
        }
        if ((d = data_go(c, d, lis)) < 0) {
            reply_t(c, 10);             /* its 425, if it says one */
            return FALSE;
        }
        ent_clear(c);
        for (;;) {
            UBYTE b[512];
            LONG k, i;
            if (!await(c, d, SF_IN, 60)) {
                ok = FALSE;
                break;
            }
            if ((k = recv(d, b, sizeof b, 0)) == 0)
                break;
            if (WOULDBLOCK(k))
                continue;
            if (k < 0) {
                ok = FALSE;
                break;
            }
            for (i = 0; i < k; i++) {
                if (b[i] == '\n' || ll == (int)sizeof line - 1) {
                    line[ll] = 0;
                    if (ll && line[ll - 1] == '\r')
                        line[ll - 1] = 0;
                    if (mlsd)
                        mlsd_line(c, line);
                    else
                        list_line(c, line);
                    ll = 0;
                } else
                    line[ll++] = b[i];
            }
        }
        CloseSocket(d);
        r = reply(c);
        return ok && (r == 226 || r == 250);
    }
}

/* MKD, RMD, DELE, RNFR + RNTO */
BOOL ftp_do(struct RConn *c, int what, const char *a, const char *b)
{
    switch (what) {
    case FR_MKDIR: return cmd(c, "MKD", a) == 257;
    case FR_RMDIR: return cmd(c, "RMD", a) == 250;
    case FR_DELETE: return cmd(c, "DELE", a) == 250;
    case FR_RENAME:
        return cmd(c, "RNFR", a) == 350 && cmd(c, "RNTO", b) == 250;
    }
    return FALSE;
}

LONG ftp_size(struct RConn *c, const char *name)
{
    int r;
    c->quiet = TRUE;
    r = cmd(c, "SIZE", name);
    c->quiet = FALSE;
    return r == 213 ? atol(c->reply + 4) : -1;
}

/* MDTM: a file's time, UTC seconds since 1970 (0: not known) */
ULONG ftp_mdtm(struct RConn *c, const char *name)
{
    const char *v = c->reply + 4;
    if (c->nomdtm)
        return 0;
    c->quiet = TRUE;
    if (cmd(c, "MDTM", name) != 213 || strlen(v) < 14) {
        if (c->code >= 500 && c->code != 550)
            c->nomdtm = TRUE;           /* the host has none */
        c->quiet = FALSE;
        return 0;
    }
    c->quiet = FALSE;
    return (ULONG)days_of(num(v, 4), num(v + 4, 2), num(v + 6, 2)) * 86400 +
           num(v + 8, 2) * 3600 + num(v + 10, 2) * 60 + num(v + 12, 2);
}

LONG ftp_xopen(struct RConn *c, const char *name, int how, ULONG *from)
{
    LONG d;
    int r;
    BOOL lis, app = FALSE;
    if ((d = data_open(c, &lis)) < 0)
        return -1;
    if (*from) {                        /* REST right before RETR or STOR */
        char n[12];
        n[0] = 0;
        numstr_cat(n, (LONG)*from);
        c->quiet = how;                 /* an upload: APPE instead */
        r = cmd(c, "REST", n);
        c->quiet = FALSE;
        if (!r) {
            CloseSocket(d);
            return -1;                  /* the line went: not a refusal */
        }
        if (r != 350) {
            if (how)
                app = TRUE;             /* added to the end of it */
            else
                *from = 0;              /* from the start, then */
        }
    }
    r = cmd(c, how ? app ? "APPE" : "STOR" : "RETR", name);
    if (r != 150 && r != 125) {
        CloseSocket(d);
        return -1;
    }
    if ((d = data_go(c, d, lis)) < 0) {
        reply_t(c, 10);
        return -1;
    }
    return c->dfd = d;
}

/* The data's end: the host's word on it. After a stop the host may say
 * 426 and 226, or one of them: a NOOP's 200 shows where they end (no
 * answer at all: the line is called again next time). */
BOOL ftp_xend(struct RConn *c, BOOL stopped)
{
    int r;
    if (c->dfd >= 0)
        CloseSocket(c->dfd);
    c->dfd = -1;
    if (!stopped) {
        r = reply_t(c, 120);
        return r == 226 || r == 250;
    }
    c->stop = FALSE;                    /* the talk after it must not stop */
    if (c->x)
        c->x->stop = 0;
    c->quiet = TRUE;                    /* its 426: the stop said so */
    if (put_cmd(c, "NOOP", NULL))
        while ((r = reply_t(c, 10)) && r != 200)
            ;
    c->quiet = FALSE;
    if (c->code != 200)
        lost(c);
    return FALSE;
}
