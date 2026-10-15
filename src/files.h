/*
 * files - the file browser's insides (Parts/Files), shared by files.c
 * (the connection, listings, transfers, ARexx), fileui.c (the panels
 * drawn in the tab, the keys, the command line) and ftp.c (FTP). One
 * RConn per place connected: its tab (where the panels are), its
 * connection, the two panels' listings.
 */
#ifndef FILES_H
#define FILES_H

#define AGAIN (-2)                      /* a read or write would wait */
/* FTP's sockets do not block: on the A1200's lwip a read that blocked
 * after WaitSelect said "readable" held the stack - every other
 * program's network (wasabid) stood still, and once never came back */
#define NB_FIONBIO 0x8004667EUL
#define EWOULDBLOCK_ 35
#define WOULDBLOCK(k) ((k) < 0 && Errno() == EWOULDBLOCK_)
void sock_nonblock(LONG s);

/* An entry of a listing: its name in the panel's pool at noff (the
 * host's bytes, UTF-8 most likely; the Amiga's are Latin-1) */
struct REnt {
    LONG noff;
    ULONG size;
    char date[18];                      /* "2026-10-11 07:50", or "" */
    ULONG when;                         /* the same, the Amiga's seconds
                                           (since 1978, its local time);
                                           0 not known */
    UBYTE dir;                          /* 1 a drawer, 2 a link of either */
    UBYTE mark;                         /* marked (Space) */
    UBYTE tz;                           /* when: 0 none, 1 the host's own
                                           clock (LIST), 2 from UTC */
};

/* A panel: the Amiga's drawer (pn[0]) or the host's (pn[1]) */
struct Panel {
    struct REnt *ent;                   /* the listing, sorted */
    int nent, capent;
    char *pool;                         /* its names */
    LONG plen, pcap;
    BOOL up;                            /* ".." is its first line */
    int cur, top;                       /* the bar's line; the first shown */
    char path[300];                     /* the Amiga's drawer ("": the
                                           volumes); the host's is cwd */
};

/* what the bottom line is doing (RConn.mode) */
enum { U_PANELS, U_VIEW, U_LOG, U_LINE, U_ASK };
/* a line typed for (lkind) */
enum { L_CMD, L_RENAME, L_MKDIR };
/* a yes/no for (akind) */
enum { A_DELETE, A_QUIT, A_REPLACE };

/* A file or drawer to copy or delete. Its name: the host's (UTF-8, from
 * the drawer the host is in: "sub/file" inside a drawer copied) or the
 * Amiga's whole path; rel: the name it has on the other side, from the
 * drawer copied into ("Drawer/sub/file" for an upload; = name for a
 * download) */
struct XItem {
    LONG noff, roff;                    /* the name, rel: in the pool */
    ULONG size;
    ULONG when;                         /* the Amiga's seconds, 0 none */
    UBYTE dir;                          /* 1 a drawer, 2 a link */
    UBYTE tz;                           /* REnt's */
    UBYTE exist;                        /* on the other side already (or
                                           maybe: inside such a drawer) */
    UBYTE deep;                         /* inside a drawer copied */
};

/* What the keys or a command act on, waiting for a yes or no (a delete,
 * an upload's replacing), then done: the items, their names in a pool.
 * A drawer's walk adds what is in it after it (job_walk). */
struct Job {
    int side;                           /* the panel they are on */
    int n, cap, nexist;                 /* how many (room for); how many
                                           there already */
    struct XItem *it;
    char *pool;
    LONG plen, pcap;
};
#define JNAME(j, i) ((j)->pool + (j)->it[i].noff)
#define JREL(j, i) ((j)->pool + (j)->it[i].roff)

#define NHIST 8                         /* command lines remembered */

struct RConn {
    struct RConn *next;
    struct Tab *tab;
    int kind;                           /* FK_SFTP, FK_FTP */
    char cwd[400];                      /* where it is on the host */
    struct Panel pn[2];                 /* the Amiga's side, the host's */
    struct Panel *lp;                   /* where a listing goes (NULL: pn[1];
                                           a drawer's walk: its own) */
    BOOL busy;                          /* a command at work: no other */
    BOOL stop;                          /* Stop: the work gives up */
    BOOL lost;                          /* the connection went */
    BOOL timedout;                      /* the host stopped answering */
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
    BOOL nomlsd, noepsv, nomdtm, chatty; /* MLSD, EPSV, MDTM refused; replies
                                           shown in the tab */
    BOOL quiet;                         /* a refusal expected: not shown */
    BOOL active;                        /* active FTP (PORT): passive failed,
                                           or `passive` typed */
    /* uploads */
    int upmode;                         /* names there already: 0 replaced,
                                           1 gone on from where they end */
    char upart[256];                    /* the upload that broke off (its
                                           name there): it goes on next time */
    /* SFTP: sftp_ops */
    void *sf, *sfh;                     /* the connection; a file open */
    ULONG polled;                       /* a transfer last let CTerm on */
    ULONG moved;                        /* bytes this transfer moved */
    /* the panels: fileui.c */
    BOOL ui;                            /* up (the alternate screen) */
    int side;                           /* the active panel */
    int mode;                           /* U_* */
    int lkind, akind;                   /* L_*, A_* */
    char line[256];                     /* a line being typed */
    int llen, lpos;
    char msg[160];                      /* the message line */
    BOOL msgred;
    char qs[32];                        /* a name being looked for */
    int qn;
    char *view;                         /* text shown over the panels */
    LONG vlen;
    int vtop, vn;                       /* its first line shown; lines */
    char vtitle[40];
    char *plog;                         /* messages for the main screen */
    LONG ploglen;
    int dc, dr;                         /* the size last drawn */
    struct Job job;                     /* waiting for yes or no */
    char hist[NHIST][128];              /* command lines typed */
    int hn, hpos;
    ULONG clicks, clickm;               /* the last click: double? */
    int clickrow;
    BOOL shifted;                       /* Shift held: the key bar shows
                                           Shift+F1..F10 */
};

/* files.c, for ftp.c and fileui.c */
BOOL await(struct RConn *c, LONG fd, int dirs, LONG secs);
BOOL given_up(struct RConn *c);
void tsay(struct RConn *c, const char *a, const char *b, const char *d);
void tsay_host(struct RConn *c, const char *line);
void fsay(const char *text);            /* tsay to the place calling */
BOOL ask_tab(struct RConn *c, const char *prompt, char *buf, int max,
             BOOL echo);
void ent_clear(struct RConn *c);        /* the host's listing */
BOOL ent_add(struct RConn *c, const char *name, int len, ULONG size,
             const char *date, int dir);
void ymd(LONG days, int *y, int *m, int *d);   /* days since 1970 */
LONG days_of(int y, int m, int d);      /* ... and back */
LONG tz_east(ULONG utc);                /* the Amiga's clock minus UTC, in
                                           seconds, at utc (since 1970) */
void ent_when(struct RConn *c, LONG days, LONG secs, BOOL utc);
LONG today(void);                       /* yyyymmdd */
void to_latin1(const char *in, int len, char *out, int max, char bad);
void to_utf8(const char *in, char *out, int max);
void size_text(ULONG n, char *o);
const char *pname(struct Panel *p, int i);
int pan_find(struct Panel *p, const char *name);   /* -1: none */
void pan_seek(struct Panel *p, const char *name);  /* the bar on it */
void pan_free(struct Panel *p);
void host_name(struct RConn *c, const char *name, char *out, int max);
BOOL rem_list(struct RConn *c);         /* the host's drawer, read again */
BOOL rem_cd(struct RConn *c, const char *path);    /* NULL: up */
BOOL rem_do(struct RConn *c, int what, const char *a, const char *b);
BOOL loc_list(struct RConn *c);         /* the Amiga's drawer, read */
int xfer_batch(struct RConn *c, BOOL up, struct Job *j, const char *dir);
BOOL job_add(struct Job *j, const char *dir, const char *name, ULONG size,
             int isdir);                /* dir NULL: the name alone */
void job_free(struct Job *j);
BOOL job_walk(struct RConn *c, struct Job *j);     /* drawers' insides */
BOOL loc_delete(struct RConn *c, const char *path);
void hang_up(struct RConn *c);
void work(struct RConn *c, BOOL on);    /* busy: a command at work */
void down_dir(struct RConn *c, char *dir);

/* fileui.c, for files.c */
void ui_start(struct RConn *c);         /* the panels up */
void ui_stop(struct RConn *c);          /* the main screen back */
void ui_draw(struct RConn *c);          /* everything drawn again */
void ui_note(struct RConn *c, const char *text, BOOL red);
void ui_progress(struct RConn *c);
void ui_key(struct RConn *c, int k);
void ui_mouse(struct RConn *c, int what, int r, int col, ULONG s, ULONG us);
void ui_free(struct RConn *c);

/* ftp.c */
BOOL ftp_open(struct RConn *c);         /* connected, logged in, binary */
void ftp_close(struct RConn *c);
BOOL ftp_cd(struct RConn *c, const char *path);    /* NULL: up */
BOOL ftp_list(struct RConn *c, const char *path);   /* NULL: the cwd */
ULONG ftp_mdtm(struct RConn *c, const char *name);  /* UTC; 0 none */
BOOL ftp_do(struct RConn *c, int what, const char *a, const char *b);
LONG ftp_size(struct RConn *c, const char *name);  /* -1: not known */
/* a file's data: RETR (how 0) or STOR (1), from *from on (REST; STOR
 * falls back on APPE; *from 0 when the host cannot go on from there);
 * the data socket, -1 failed */
LONG ftp_xopen(struct RConn *c, const char *name, int how, ULONG *from);
BOOL ftp_xend(struct RConn *c, BOOL stopped);
#define FR_RMDIR 100                    /* ftp_do: a drawer goes */

#endif
