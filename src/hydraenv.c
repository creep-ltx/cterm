/*
 * hydraenv - HydraCom's hydra.c (the HYDRA bi-directional file transfer
 * protocol, designed by Arjen G. Lentz and Joaquim H. Homrighausen; the
 * HydraCom code by Arjen G. Lentz, LENTZ SOFTWARE-DEVELOPMENT) running
 * inside CTerm: its serial port is CTerm's connection and its files are
 * CTerm's, both through XferIO (xfer.h), as for Zmodem.
 *
 * hy_session() is one Hydra session: what is queued to send goes, what
 * the other side sends comes, both at once.
 */
#include "hydraenv.h"
#include "xfer.h"

word    cur_speed = 38400, com_speed = 38400;
char   *download = "";
char    single_file[20];
boolean single_done, noresume = 1, nostamp = 1, nooriginator, hdxsession,
        mailer, parity;
dword   hydra_options;
long    hydra_txwindow, hydra_rxwindow;     /* 0: full streaming */
word    chatfill;
long    chattimer, lasttimer;
int     loglevel = 1;                   /* 0 writes HydraCom's debug lines */
boolean hy_nomem;

static XferIO *hio;
static const unsigned char *pre;        /* what came before the session */
static long pren, prep;
static unsigned char ib[2048];
static long ibn, ibp;
static unsigned char ob[4096];
static long obn;
static int gone;

/* handles: 0 the file being received, 1 the one being sent */
static void *fh[2];
static unsigned long fpos[2], fsize[2];
static char fname[2][256];
static int nrecv, nsent;

#define RXMARK "\001rx"                 /* xfer_init's "path" */

/* ---- strings ---- */

char *hy_strupr(char *s)
{
    char *p;
    for (p = s; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    return s;
}

char *hy_strlwr(char *s)
{
    char *p;
    for (p = s; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return s;
}

int hy_strnicmp(const char *a, const char *b, int n)
{
    for (; n > 0; n--, a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a)
            return d;
    }
    return 0;
}

/* ---- the screen: nothing; progress goes to show() from the files ---- */

void message(int level, char *fmt, ...) { (void)level; (void)fmt; }
void hydra_gotoxy(int x, int y) { (void)x; (void)y; }
void hydra_printf(char *fmt, ...) { (void)fmt; }
void hydra_clreol(void) { }
void rem_chat(byte *data, word len) { (void)data; (void)len; }
void resultlog(boolean xmit, char *fn, long bytes, long xfertime)
{ (void)xmit; (void)fn; (void)bytes; (void)xfertime; }

char *h_revdate(long revstamp)
{
    static char s[12];
    unsigned long v = (unsigned long)revstamp;
    int i;
    for (i = 7; i >= 0; i--, v >>= 4)
        s[i] = "0123456789abcdef"[v & 15];
    s[8] = 0;
    return s;
}

/* "already have it?" (HydraCom's skip check): CTerm never overwrites,
 * it makes a new name instead, so no */
int fexist(char *filename) { (void)filename; return 0; }

long freespace(char *drivepath) { (void)drivepath; return 0x7fffffffL; }
int keyabort(void) { return hio->stop && hio->stop(hio->u); }
int carrier(void) { return !gone; }

/* ---- the line ---- */

void com_flush(void)
{
    if (obn) {
        hio->put(hio->u, ob, obn);
        obn = 0;
    }
}

void com_putbyte(byte c)
{
    if (obn >= (long)sizeof ob)
        com_flush();
    ob[obn++] = c;
}

void com_putblock(byte *s, word len)
{
    while (len--)
        com_putbyte(*s++);
    com_flush();
}

int com_outfull(void) { return 0; }
void com_purge(void) { ibn = ibp = 0; prep = pren; }
void com_dump(void) { obn = 0; }
void sys_idle(void) { com_flush(); }

/* A byte, or EOF (-1) when none came within a moment: hydra() polls. */
int com_getbyte(void)
{
    if (prep < pren)
        return pre[prep++];
    if (ibp >= ibn) {
        long k;
        com_flush();
        if (gone)
            return -1;
        k = hio->get(hio->u, ib, sizeof ib, 20);
        if (k < 0) {
            gone = 1;                   /* carrier() tells hydra() */
            return -1;
        }
        if (k == 0)
            return -1;
        ibn = k;
        ibp = 0;
    }
    return ib[ibp++];
}

/* ---- the files ---- */

int hy_stat(char *pathname, long *size, long *ftime)
{
    unsigned long sz = 0;
    void *f = hio->ropen(hio->u, pathname, &sz);
    if (!f)
        return -1;
    hio->rclose(hio->u, f);
    *size = (long)sz;
    *ftime = (long)time(NULL);
    return 0;
}

int dos_sopen(char *pathname, byte create)
{
    const char *p, *base = pathname;
    if (!strcmp(pathname, RXMARK))
        return fh[0] ? 0 : -1;          /* opened by xfer_init */
    if (create)
        return -1;
    if (fh[1])
        return -1;
    fh[1] = hio->ropen(hio->u, pathname, &fsize[1]);
    if (!fh[1])
        return -1;
    for (p = pathname; *p; p++)
        if (*p == '/' || *p == ':')
            base = p + 1;
    strncpy(fname[1], base, 255);
    fname[1][255] = 0;
    fpos[1] = 0;
    hio->show(hio->u, fname[1], 0, fsize[1], 0);
    return 1;
}

int dos_close(int handle)
{
    if (handle < 0 || handle > 1 || !fh[handle])
        return -1;
    if (handle == 0)
        hio->wclose(hio->u, fh[0], 1);
    else {
        hio->rclose(hio->u, fh[1]);
        if (fpos[1] >= fsize[1]) {      /* all of it went */
            hio->show(hio->u, fname[1], fpos[1], fsize[1], 1);
            nsent++;
        }
    }
    fh[handle] = NULL;
    return 0;
}

long dos_seek(int handle, long offset, int fromwhere)
{
    if (handle < 0 || handle > 1 || !fh[handle])
        return -1;
    if (handle == 0)                    /* written as it comes: no resume */
        return (long)fpos[0];
    if (fromwhere == 2)
        offset += (long)fsize[1];
    else if (fromwhere == 1)
        offset += (long)fpos[1];
    if (hio->rseek(hio->u, fh[1], (unsigned long)offset))
        return -1;
    fpos[1] = (unsigned long)offset;
    return offset;
}

long dos_tell(int handle)
{
    return handle >= 0 && handle <= 1 ? (long)fpos[handle] : -1;
}

int dos_read(int handle, void *buf, word len)
{
    long k;
    if (handle != 1 || !fh[1])
        return -1;
    k = hio->rread(hio->u, fh[1], buf, len);
    if (k > 0) {
        fpos[1] += (unsigned long)k;
        hio->show(hio->u, fname[1], fpos[1], fsize[1], 0);
    }
    return (int)k;
}

int dos_write(int handle, void *buf, word len)
{
    long k;
    if (handle != 0 || !fh[0])
        return -1;
    k = hio->wwrite(hio->u, fh[0], buf, len);
    if (k > 0) {
        fpos[0] += (unsigned long)k;
        hio->show(hio->u, fname[0], fpos[0], fsize[0], 0);
    }
    return (int)k;
}

/* A file coming: opened in the download drawer under its own name (the
 * caller makes it unique); hydra.c then "opens" RXMARK. */
char *xfer_init(char *fn, long fsz, long ftime)
{
    (void)ftime;
    if (fh[0])
        return NULL;
    fh[0] = hio->wopen(hio->u, fn, (unsigned long)fsz, fname[0],
                       sizeof fname[0]);
    if (!fh[0]) {
        hio->show(hio->u, fn, 0, (unsigned long)fsz, 3);
        return NULL;
    }
    fpos[0] = 0;
    fsize[0] = (unsigned long)fsz;
    hio->show(hio->u, fname[0], 0, fsize[0], 0);
    return (char *)RXMARK;
}

boolean xfer_bad(void)
{
    hio->show(hio->u, fname[0], fpos[0], fsize[0], 3);
    return false;                       /* the part is kept as it is */
}

char *xfer_okay(void)
{
    hio->show(hio->u, fname[0], fpos[0], fsize[0], 1);
    nrecv++;
    return NULL;
}

/* ---- the session ---- */

int hy_detect(const unsigned char *b, long n, long *at)
{
    /* HydraCom's autostart: the START packet as a sender sends it */
    static const unsigned char start[] = "\030cA\\f5\\a3\030a";
    long i;
    for (i = 0; i + 11 <= n; i++)
        if (!memcmp(b + i, start, 11)) {
            if (at)
                *at = i;
            return 1;
        }
    return 0;
}

int hy_session(XferIO *io, const char *const *paths, int npaths,
               const unsigned char *done, long n, int originator)
{
    int i, rc = 0;
    hio = io;
    pre = done;
    pren = done ? n : 0;
    prep = 0;
    ibn = ibp = obn = 0;
    gone = 0;
    fh[0] = fh[1] = NULL;
    nrecv = nsent = 0;
    nooriginator = !originator;
    hy_nomem = false;
    hydra_init(hydra_options);
    if (hy_nomem)
        return -1;
    for (i = 0; i < npaths; i++) {
        int r = hydra((char *)paths[i], NULL);
        if (r == XFER_ABORT) {
            rc = -1;
            break;
        }
    }
    if (rc == 0 && hydra(NULL, NULL) == XFER_ABORT)
        rc = -1;
    hydra_deinit();
    com_flush();
    if (fh[0]) {                        /* left open by an abort */
        hio->wclose(hio->u, fh[0], 0);
        fh[0] = NULL;
    }
    if (fh[1]) {
        hio->rclose(hio->u, fh[1]);
        fh[1] = NULL;
    }
    return rc < 0 ? -1 : nrecv + nsent;
}
