/*
 * modstart - the way into a protocol file (see xfermod.h): a header CTerm
 * finds in the loaded segments (XFERMOD_HEAD, then the address of
 * xfermod_start - wherever the linker put it), and what the C library's
 * start-up would have given the protocols: memory (malloc/free) and the
 * time.
 */
#include <stddef.h>
#include <time.h>
#include "xfermod.h"

static struct XferEnv *env;
void *SysBase;                          /* libnix's string calls may look */

int zm_receive(XferIO *io, const unsigned char *done, long n);
int zm_send(XferIO *io, const char *const *paths, int npaths,
            const unsigned char *done, long n);
int xy_receive(XferIO *io, int ymodem, const char *xname);
int xy_send(XferIO *io, int ymodem, int onek, const char *const *paths,
            int npaths);
int hy_session(XferIO *io, const char *const *paths, int npaths,
               const unsigned char *done, long n, int originator);

struct XferMod *xfermod_start(struct XferEnv *e)
{
    static struct XferMod m;
    env = e;
    SysBase = *(void **)4;
    m.magic = XFERMOD_MAGIC;
#ifdef MOD_ZMODEM
    m.name = "Zmodem";
    m.zm_receive = zm_receive;
    m.zm_send = zm_send;
#endif
#ifdef MOD_XYMODEM
    m.name = "XYmodem";
    m.xy_receive = xy_receive;
    m.xy_send = xy_send;
#endif
#ifdef MOD_HYDRA
    m.name = "Hydra";
    m.hy_session = hy_session;
#endif
    return &m;
}

/* What CTerm looks for after LoadSeg: the mark, then the way in. */
const struct { unsigned long mark; XferModStart start; }
    xfermod_head __attribute__((used)) = { XFERMOD_HEAD, xfermod_start };

void *malloc(size_t n) { return env->alloc(n); }
void free(void *p) { if (p) env->release(p); }
void *calloc(size_t n, size_t k)
{
    unsigned char *p = env->alloc(n * k), *q;
    size_t i;
    if (p)
        for (q = p, i = 0; i < n * k; i++)
            *q++ = 0;
    return p;
}
time_t time(time_t *t)
{
    time_t v = (time_t)env->now();
    if (t)
        *t = v;
    return v;
}
