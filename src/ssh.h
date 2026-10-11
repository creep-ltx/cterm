/*
 * ssh - CTerm's ssh connection: libssh2 on AmiSSL (third_party/).
 * Opens AmiSSL only when a connection is made.
 */
#ifndef SSH_H
#define SSH_H

#include <exec/types.h>

/* What ssh needs from CTerm while it sets up: a line typed in the
 * window (echo FALSE for a password; FALSE: the window closed), and a
 * message shown in it. */
typedef BOOL (*ssh_ask_fn)(const char *prompt, char *buf, int max, BOOL echo);
typedef void (*ssh_say_fn)(const char *text);

/* From net.c, for both telnet and ssh: wait until fd can be read (rd)
 * or written (wr), at most secs, the Shells and the window served; Esc
 * or the close gadget gives up. 1: ready, 0: time up, -1: given up. */
int net_wait(LONG fd, int rd, int wr, LONG secs);
/* A socket connected to host:port, -1 if not (and why, said). */
LONG net_connect(const char *host, LONG port, ssh_say_fn say);
void net_lookups_wait(void);        /* at quit: lookups given up on */

/* user@host[:port]: connect, check the host key against ENVARC:CTerm/known_hosts
 * (asking about a new or changed one), log in (key file, then password /
 * keyboard-interactive), open a shell on an xterm-256color pty. */
void *ssh_open(const char *target, int cols, int rows,
               ssh_ask_fn ask, ssh_say_fn say);   /* NULL: not connected */
void ssh_set_key(const char *path);     /* KEY: use this private key */
LONG ssh_socket(void *h);               /* for WaitSelect */
LONG ssh_read(void *h, UBYTE *buf, LONG max);  /* >0 data, 0 none, <0 gone */
void ssh_write(void *h, const UBYTE *b, LONG n);
void ssh_resize(void *h, int cols, int rows);
void ssh_close(void *h);

/* SFTP on an ssh connection of its own (CTermSSH: ssh.c), for the file
 * browser (Parts/Files), which reaches it through sftp_ops (NULL in the
 * small CTerm). open connects, checks the host key and logs in as
 * ssh_open does, then starts SFTP. Every other call may answer SF_AGAIN:
 * wait for the socket the way dirs says (SF_IN, SF_OUT) and make the
 * same call again. Other answers below 0 are failures (error says
 * which); read and readdir answer 0 at the end. */
#define SF_AGAIN (-37)                  /* LIBSSH2_ERROR_EAGAIN */
#define SF_IN    1
#define SF_OUT   2
#define SF_LOST(rc) ((rc) == -7 || (rc) == -13 || (rc) == -26 || \
                     (rc) == -30 || (rc) == -43 || (rc) == -1)
typedef struct {
    ULONG size, mtime;                  /* bytes; seconds since 1970 */
    UBYTE dir, link;                    /* a directory; a symbolic link */
} SfAttr;
struct SftpOps {
    void *(*sf_open)(const char *target, ssh_ask_fn ask, ssh_say_fn say);
    void (*sf_close)(void *h);
    LONG (*sf_socket)(void *h);
    int (*sf_dirs)(void *h);
    LONG (*sf_opendir)(void *h, const char *path, void **dh);
    LONG (*sf_readdir)(void *dh, char *name, int max, SfAttr *a);
    LONG (*sf_closedir)(void *dh);
    LONG (*sf_realpath)(void *h, const char *path, char *out, int max);
    LONG (*sf_stat)(void *h, const char *path, SfAttr *a);
    /* how: 0 to read, 1 to write a new one, 2 to add to its end */
    LONG (*sf_fopen)(void *h, const char *path, int how, void **fh);
    LONG (*sf_read)(void *fh, UBYTE *b, LONG n);
    LONG (*sf_write)(void *fh, const UBYTE *b, LONG n);
    void (*sf_seek)(void *fh, ULONG pos);
    LONG (*sf_fclose)(void *fh);
    LONG (*sf_mkdir)(void *h, const char *path);
    LONG (*sf_rmdir)(void *h, const char *path);
    LONG (*sf_unlink)(void *h, const char *path);
    LONG (*sf_rename)(void *h, const char *from, const char *to);
    /* the last failure: the SFTP status (>0: 2 no such file, 3 not
     * allowed, 4 failed...) or libssh2's code (<0) */
    LONG (*sf_error)(void *h);
};

#ifdef NO_SSH
/* The small CTerm: ssh lives in CTermSSH, which CTerm starts instead
 * (a stock Amiga never loads libssh2; AmiSSL wants fast memory anyway). */
#define ssh_open(t, c, r, a, s) ((void *)0)
#define ssh_set_key(p) ((void)0)
#define ssh_socket(h) (-1L)
#define ssh_read(h, b, m) (-1L)
#define ssh_write(h, b, n) ((void)0)
#define ssh_resize(h, c, r) ((void)0)
#define ssh_close(h) ((void)0)
#endif

#endif
