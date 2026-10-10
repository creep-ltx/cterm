/*
 * hcshim - what HydraCom 1.00's hydra.c and fmisc.c need from the rest
 * of HydraCom (MS-DOS: hydracom.c, misc.c, dos_file.c, syspc.c), for
 * Linux: the serial port is stdin/stdout (a socketpair from the test),
 * files are POSIX files, the screen is stderr (HC_LOG=1). A test
 * reference for CTerm's own Hydra, built 32-bit (-m32) so a long is 4
 * bytes as the code assumes. Not part of CTerm.
 *
 *   hcref [-o] [-d downloaddir/] [files...]    -o: we are the originator
 *
 * HydraCom: COPYRIGHT (C) 1991-1993 Arjen G. Lentz, LENTZ SOFTWARE-
 * DEVELOPMENT; the HYDRA protocol by Arjen G. Lentz and Joaquim H.
 * Homrighausen. See third_party/src/hydracom/license.doc.
 */
#define MAIN
#include "hydracom.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <utime.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

void com_flush(void);
static unsigned char ibuf[8192];
static int ilen, ipos;
static unsigned char obuf[16384];
static int olen;
static int verbose;

void message(int level, char *fmt, ...)
{
    va_list ap;
    (void)level;
    if (!verbose)
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
void cprint(char *fmt, ...) { (void)fmt; }
void hydra_gotoxy(int x, int y) { (void)x; (void)y; }
void hydra_printf(char *fmt, ...) { (void)fmt; }
void hydra_clreol(void) { }
int keyabort(void) { return 0; }
void endprog(int errcode) { com_flush(); exit(errcode); }
void rem_chat(byte *data, word len) { (void)data; (void)len; }
int carrier(void) { return 1; }
void resultlog(boolean xmit, char *fname, long bytes, long xfertime)
{ (void)xmit; (void)fname; (void)bytes; (void)xfertime; }
char *h_revdate(long revstamp)
{
    static char s[32];
    sprintf(s, "%ld", revstamp);
    return s;
}
long freespace(char *drivepath) { (void)drivepath; return 0x7fffffffL; }
int fexist(char *filename) { return access(filename, F_OK) == 0; }
void splitpath(char *filepath, char *path, char *file)
{
    char *p = strrchr(filepath, '/');
    if (p) {
        strncpy(path, filepath, p - filepath + 1);
        path[p - filepath + 1] = 0;
        strcpy(file, p + 1);
    } else {
        path[0] = 0;
        strcpy(file, filepath);
    }
}
void mergepath(char *filepath, char *path, char *file)
{
    strcpy(filepath, path);
    strcat(filepath, file);
}
FILE *sfopen(char *name, char *mode, int shareflag)
{
    char m[8];
    (void)shareflag;
    strncpy(m, mode, 7);
    m[7] = 0;
    { char *t = strchr(m, 't'); if (t) memmove(t, t + 1, strlen(t)); }
    return fopen(name, m);
}
void setstamp(char *name, long tim)
{
    struct utimbuf u;
    u.actime = u.modtime = tim;
    utime(name, &u);
}

/* ---- dos_file.c ---- */
int dos_open(char *pathname, byte create)
{
    return open(pathname, create == 0 ? O_RDONLY
                : create == 1 ? (O_RDWR | O_CREAT) : (O_RDWR | O_CREAT | O_TRUNC), 0644);
}
int dos_sopen(char *pathname, byte create) { return dos_open(pathname, create); }
int dos_sappend(char *pathname, byte create)
{
    int fd = dos_open(pathname, create);
    if (fd >= 0) lseek(fd, 0, SEEK_END);
    return fd;
}
int dos_close(int handle) { return close(handle); }
int dos_lock(int h, long o, long l) { (void)h; (void)o; (void)l; return 0; }
int dos_unlock(int h, long o, long l) { (void)h; (void)o; (void)l; return 0; }
long dos_seek(int handle, long offset, int fromwhere)
{
    return (long)lseek(handle, offset, fromwhere);
}
long dos_tell(int handle) { return (long)lseek(handle, 0, SEEK_CUR); }
int dos_read(int handle, void *buf, word len) { return (int)read(handle, buf, len); }
int dos_write(int handle, void *buf, word len) { return (int)write(handle, buf, len); }

/* ---- syspc.c: the "serial port" is fd 0 / fd 1 ---- */
void com_flush(void)
{
    int done = 0;
    while (done < olen) {
        int k = (int)write(1, obuf + done, olen - done);
        if (k <= 0) {
            if (errno == EINTR) continue;
            exit(3);
        }
        done += k;
    }
    olen = 0;
}
void com_putbyte(byte c)
{
    if (olen >= (int)sizeof obuf)
        com_flush();
    obuf[olen++] = c;
}
void com_putblock(byte *s, word len)
{
    while (len--)
        com_putbyte(*s++);
    com_flush();
}
int com_outfull(void) { return 0; }
void com_purge(void) { ilen = ipos = 0; }
void com_dump(void) { olen = 0; }
int com_getbyte(void)
{
    if (ipos >= ilen) {
        struct pollfd p = { 0, POLLIN, 0 };
        com_flush();
        if (poll(&p, 1, 10) <= 0)       /* sys_idle's pace */
            return EOF;
        ilen = (int)read(0, ibuf, sizeof ibuf);
        ipos = 0;
        if (ilen <= 0) {
            ilen = 0;
            exit(4);                    /* the line is gone */
        }
    }
    return ibuf[ipos++];
}
void sys_idle(void) { com_flush(); }
void dtr_out(byte flag) { (void)flag; }
void com_flow(byte flags) { (void)flags; }
void com_setspeed(word speed) { (void)speed; }
void sys_init(void) { }
void sys_reset(void) { }

int main(int argc, char **argv)
{
    int i = 1, rc = 0;
    char dl[PATHLEN] = "./";
    boolean orig = false;
    verbose = getenv("HC_LOG") != NULL;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-o")) orig = true;
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            strncpy(dl, argv[++i], PATHLEN - 2);
            if (dl[strlen(dl) - 1] != '/') strcat(dl, "/");
        }
        i++;
    }
    download = dl;
    single_file[0] = 0;
    nooriginator = !orig;
    hdxsession = false;
    noresume = false;
    nostamp = false;
    mailer = false;
    cur_speed = com_speed = 38400;
    hydra_options = 0;                  /* the defaults: as HydraCom's */
    hydra_init(hydra_options);
    for (; i < argc; i++)
        switch (hydra(argv[i], NULL)) {
        case XFER_ABORT: rc = 1; goto out;
        default: break;
        }
    if (hydra(NULL, NULL) == XFER_ABORT)
        rc = 1;
out:
    hydra_deinit();
    com_flush();
    return rc;
}
