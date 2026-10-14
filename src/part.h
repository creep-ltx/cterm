/*
 * part - a part of CTerm in a file of its own (PROGDIR:Parts/): the
 * settings window, the phone book's window, ARexx's commands, the Call
 * window, the Transfers window, the speed test, the file browser,
 * pictures in the terminal. Each is loaded with
 * LoadSeg the first time it is wanted and (but ARexx's) unloaded when
 * its work is done, as the protocols are (xfermod.h): on a stock A1200
 * CTerm's code sits in chip memory, which the Shell's programs need.
 *
 * A part is CTerm's own source, built with -DPART: it sees CTerm only
 * through struct PartEnv, a pointer to each of CTerm's variables and
 * calls it uses (the list is partlist.h). partuse.h, made from that list
 * by the Makefile, turns each name into its pointer: in a part `theme`
 * is (*penv->theme) and say(s) is (penv->say)(s). CTerm reaches a part
 * only through the table its part_start gives back (below).
 *
 * In the file: PART_HEAD, PART_ABI (the version and the size of
 * PartEnv), then the address of part_start(). A file of another version
 * is refused before any of its code runs.
 */
#ifndef PART_H
#define PART_H

#define PART_HEAD    0x43545048UL       /* "CTPH": the header's mark */
#define PART_VERSION 13UL               /* PartEnv's names and the tables
                                         * below */
#define PART_ABI     ((PART_VERSION << 16) | sizeof(struct PartEnv))

/* CTerm's variables and calls, for the parts */
struct PartEnv {
#define V(x) __typeof__(&x) x;
#define A(x) __typeof__(&(x)[0]) x;
#define F(x) __typeof__(&x) x;
#include "partlist.h"
#undef V
#undef A
#undef F
};

typedef APTR (*PartStart)(const struct PartEnv *env);

/* What each part gives back */
struct SetPart {                        /* Parts/Settings: setwin.c */
    void (*open)(void);
    void (*close)(void);
    void (*events)(void);
    void (*cancel)(void);
};
struct BookPart {                       /* Parts/PhoneBook: bookwin.c */
    void (*open)(void);
    void (*events)(void);
    void (*close)(void);
    ULONG (*sigmask)(void);
};
struct RexxPart {                       /* Parts/ARexx: rexx.c */
    void (*serve)(void);
    void (*run)(void);
    void (*feed)(struct Tab *t, const UBYTE *b, LONG n);
    void (*tab_gone)(struct Tab *t);
    void (*tab_over)(struct Tab *t);
    void (*xfer_done)(struct Tab *t, int got);
    int (*uploads)(struct Tab *t, char (*store)[256], const char **paths,
                   int max);
    BOOL (*start)(const char *path, struct Tab *t);
    void (*after_call)(void);
    void (*menu)(void);
    BOOL (*wake)(BOOL any);
    void (*close)(void);
    void (*watch)(struct Tab *t);
};
struct CallPart {                       /* Parts/Call: callwin.c */
    void (*dialog)(void);
};
struct XwinPart {                       /* Parts/Transfers: xwin.c */
    void (*open)(void);
    void (*update)(void);
    void (*events)(void);
    void (*close)(void);
    ULONG (*sigmask)(void);
};
struct SpeedPart {                      /* Parts/SpeedTest: bench.c */
    void (*test)(void);
};
struct FilesPart {                      /* Parts/Files: files.c, fileui.c,
                                           ftp.c */
    BOOL (*call)(void);
    void (*menu)(void);
    void (*key)(int k);
    void (*mouse)(int what, int r, int c, ULONG s, ULONG us);
    void (*resized)(struct Tab *t);
    void (*tab_gone)(struct Tab *t);
    int (*rexx)(int what, const char *const *names, int n, const char *to,
                char **res);
    BOOL (*idle)(void);                 /* nothing open: it may go */
};
struct PicsPart {                       /* Parts/Pictures: pics.c */
    int (*decode)(const UBYTE *file, LONG len, ULONG **argb, int *w,
                  int *h);
    void (*decode_free)(ULONG *argb);
    void (*pens)(const UBYTE *rgb, int n, int x, int y, UBYTE *out);
};

#ifdef PART
extern const struct PartEnv *penv;
APTR part_main(void);                   /* the part's own: its table */
#include "partuse.h"
#endif

#endif
