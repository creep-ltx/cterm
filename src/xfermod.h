/*
 * xfermod - a transfer protocol as a file of its own (PROGDIR:Protocols/),
 * loaded with LoadSeg when a transfer starts and unloaded when it ends: a
 * stock A1200 keeps the memory, and Hydra, maybe never used, costs nothing
 * until it is. The protocol's code is the same as on the PC tests; it
 * reaches CTerm only through XferIO, and for memory and the clock through
 * XferEnv (a LoadSeg'd file has no C library start-up of its own).
 *
 * In the file: XFERMOD_HEAD, XFERMOD_VERSION, then the address of
 * xfermod_start(); CTerm finds that in the loaded segments and, only when
 * the version is its own, calls it with the environment and gets the
 * table back. A file of another version is refused before any of its
 * code runs.
 */
#ifndef XFERMOD_H
#define XFERMOD_H

#include "xfer.h"

#define XFERMOD_MAGIC   0x43544d32UL    /* "CTM2": this interface */
#define XFERMOD_HEAD    0x43544d48UL    /* "CTMH": the header's mark */
#define XFERMOD_VERSION 2UL             /* after the mark: XferIO's shape
                                         * (2: wopen's have, rest) */

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
