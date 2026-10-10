/*
 * xfer - file transfers with a host (Zmodem first): the protocol engines
 * know nothing of the Amiga. They talk to the host and to files through
 * the calls in XferIO, which CTerm gives (net.c: telnet or ssh, AmigaDOS
 * files) and the PC tests give (a socketpair to lrzsz, stdio files).
 */
#ifndef XFER_H
#define XFER_H

typedef struct XferIO XferIO;
struct XferIO {
    void *u;
    /* Up to max bytes from the host, waiting at most ms: >0 bytes,
     * 0 nothing came, <0 the connection is gone. */
    long (*get)(void *u, unsigned char *buf, long max, long ms);
    /* Bytes to the host as they are (the telnet layer escapes IAC). */
    void (*put)(void *u, const unsigned char *b, long n);
    /* Nonzero: the person asked to stop. */
    int (*stop)(void *u);
    /* How it goes: file name, bytes done, its size (0: not known);
     * what: 0 going, 1 file done, 2 skipped, 3 failed. */
    void (*show)(void *u, const char *name, unsigned long done,
                 unsigned long size, int what);
    /* Files. A download is made in the download drawer under the name it
     * came with (the caller makes it unique); the handle is opaque. */
    void *(*wopen)(void *u, const char *name, unsigned long size,
                   char *made, int madelen);
    long (*wwrite)(void *u, void *f, const unsigned char *b, long n);
    void (*wclose)(void *u, void *f, int ok);
    /* An upload: open by the path the person chose; its size, its name. */
    void *(*ropen)(void *u, const char *path, unsigned long *size);
    long (*rread)(void *u, void *f, unsigned char *b, long n);
    int (*rseek)(void *u, void *f, unsigned long pos);
    void (*rclose)(void *u, void *f);
};

/* The start of a Zmodem session in what the host sent: 1 the host sends
 * (ZRQINIT: we receive), 2 the host receives (ZRINIT: we send), 0 none.
 * Looks for "**" ZDLE "B0" and the type; *at is where it begins. */
int zm_detect(const unsigned char *b, long n, long *at);

/* Receive what the host sends; done: the bytes seen since the start
 * header (they are fed in first). Returns the files received, -1 when
 * it was cancelled or failed. */
int zm_receive(XferIO *io, const unsigned char *done, long n);

/* Send files (paths) to a host whose receiver is waiting: its ZRINIT may
 * be in done. Returns the files sent, -1 cancelled or failed. */
int zm_send(XferIO *io, const char *const *paths, int npaths,
            const unsigned char *done, long n);

/* Xmodem (ymodem 0) and Ymodem (1), which the person starts: receive
 * (Xmodem has no names: xname is the file's), or send (onek: Xmodem 1K).
 * Return the files done, -1 cancelled or failed. */
int xy_receive(XferIO *io, int ymodem, const char *xname);
int xy_send(XferIO *io, int ymodem, int onek, const char *const *paths,
            int npaths);

/* Hydra (the HYDRA protocol by Arjen G. Lentz and Joaquim H.
 * Homrighausen, run by HydraCom's own code): its start in what the host
 * sent (the START packet), and a session - paths are sent while what the
 * other side sends is received, both at once. Returns the files that went
 * either way, -1 when it was cancelled or failed. */
int hy_detect(const unsigned char *b, long n, long *at);
int hy_session(XferIO *io, const char *const *paths, int npaths,
               const unsigned char *done, long n, int originator);

#endif
