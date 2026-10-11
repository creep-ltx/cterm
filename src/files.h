/*
 * files - the file browser's insides (Parts/Files), shared by files.c
 * (the window, the transfers, SFTP through sftp_ops) and ftp.c (FTP).
 * One RConn per place connected: its tab (where its messages go), its
 * connection, the listing of the drawer it is in, its window.
 */
#ifndef FILES_H
#define FILES_H

#define AGAIN (-2)                      /* a read or write would wait */

/* An entry of the listing: its name as the host has it (UTF-8, most
 * likely), in the pool at noff */
struct REnt {
    LONG noff;
    ULONG size;
    char date[18];                      /* "2026-10-11 07:50", or "" */
    UBYTE dir;                          /* 1 a drawer, 2 a link of either */
};

enum { W_UP = 1, W_PATH, W_REFRESH, W_LIST, W_STATUS, W_GAUGE, W_GET,
       W_PUT, W_MKDIR, W_REN, W_DEL, W_STOP, W_N };

struct RConn {
    struct RConn *next;
    struct Tab *tab;
    int kind;                           /* FK_SFTP, FK_FTP */
    char cwd[400];                      /* where it is on the host */
    struct REnt *ent;                   /* the listing, sorted */
    int nent, capent;
    char *pool;                         /* its names */
    LONG plen, pcap;
    BOOL busy;                          /* a command at work: no other */
    BOOL stop;                          /* Stop: the work gives up */
    BOOL lost;                          /* the connection went */
    BOOL timedout;                      /* the host stopped answering */
    BOOL wantclose;                     /* closed while busy: when done */
    struct Xfer *x;                     /* the transfer now, or NULL */
    char pass[64];                      /* a password typed: for calling
                                           again (wiped at the end) */
    /* FTP: ftp.c */
    LONG cfd, dfd;                      /* the control and data sockets */
    UBYTE in[512];                      /* reply bytes not yet read */
    int inlen;
    char reply[256];                    /* the last reply's text */
    int code;                           /* ... its number */
    char peer[20];                      /* the host's address */
    BOOL nomlsd, noepsv, chatty;        /* MLSD, EPSV refused; replies
                                           shown in the tab */
    BOOL quiet;                         /* a refusal expected: not shown */
    /* SFTP: sftp_ops */
    void *sf, *sfh;                     /* the connection; a file open */
    /* the window: files.c */
    Object *wobj, *g[W_N], *wlayout;
    struct Window *w;
    struct List lbl;                    /* the listing's nodes */
    BOOL reopen;                        /* closed with the screen */
    char status[160];                   /* the status line's text */
    char title[120];                    /* the window's */
    ULONG polled;                       /* a transfer last let CTerm on */
    ULONG moved;                        /* bytes this transfer moved */
    BOOL failed;                        /* a failure said: in the status */
};

/* files.c, for ftp.c */
BOOL await(struct RConn *c, LONG fd, int dirs, LONG secs);
BOOL given_up(struct RConn *c);
void tsay(struct RConn *c, const char *a, const char *b, const char *d);
void tsay_host(struct RConn *c, const char *line);
void ent_clear(struct RConn *c);
BOOL ent_add(struct RConn *c, const char *name, int len, ULONG size,
             const char *date, int dir);
void ymd(LONG days, int *y, int *m, int *d);   /* days since 1970 */
LONG today(void);                       /* yyyymmdd */

/* ftp.c */
BOOL ftp_open(struct RConn *c);         /* connected, logged in, binary */
void ftp_close(struct RConn *c);
BOOL ftp_cd(struct RConn *c, const char *path);    /* NULL: up */
BOOL ftp_list(struct RConn *c);
BOOL ftp_do(struct RConn *c, int what, const char *a, const char *b);
LONG ftp_size(struct RConn *c, const char *name);  /* -1: not known */
/* a file's data: RETR (how 0, from *from on; *from 0 when the host
 * cannot go on from there) or STOR (1); the data socket, -1 failed */
LONG ftp_xopen(struct RConn *c, const char *name, int how, ULONG *from);
BOOL ftp_xend(struct RConn *c, BOOL stopped);
#define FR_RMDIR 100                    /* ftp_do: a drawer goes */

#endif
