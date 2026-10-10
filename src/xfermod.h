/*
 * xfermod - a transfer protocol as a file of its own (PROGDIR:Protocols/),
 * loaded with LoadSeg when a transfer starts and unloaded when it ends: a
 * stock A1200 keeps the memory, and Hydra, maybe never used, costs nothing
 * until it is. The protocol's code is the same as on the PC tests; it
 * reaches CTerm only through XferIO, and for memory and the clock through
 * XferEnv (a LoadSeg'd file has no C library start-up of its own).
 *
 * In the file: XFERMOD_HEAD followed by the address of xfermod_start();
 * CTerm finds that pair in the loaded segments, calls it with the
 * environment and gets the table back.
 */
#ifndef XFERMOD_H
#define XFERMOD_H

#include "xfer.h"

#define XFERMOD_MAGIC   0x43544d31UL    /* "CTM1": this interface */
#define XFERMOD_HEAD    0x43544d48UL    /* "CTMH": the header's mark */

struct XferEnv {
    void *(*alloc)(unsigned long n);    /* memory, freed with release */
    void (*release)(void *p);
    long (*now)(void);                  /* seconds, for timeouts */
};

struct XferMod {
    unsigned long magic;
    const char *name;
    /* NULL where the file has no such protocol */
    int (*zm_receive)(XferIO *io, const unsigned char *done, long n);
    int (*zm_send)(XferIO *io, const char *const *paths, int npaths,
                   const unsigned char *done, long n);
    int (*xy_receive)(XferIO *io, int ymodem, const char *xname);
    int (*xy_send)(XferIO *io, int ymodem, int onek,
                   const char *const *paths, int npaths);
    int (*hy_session)(XferIO *io, const char *const *paths, int npaths,
                      const unsigned char *done, long n, int originator);
};

typedef struct XferMod *(*XferModStart)(struct XferEnv *env);

#endif
