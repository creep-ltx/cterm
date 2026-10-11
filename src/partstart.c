/*
 * partstart - the way into a part (see part.h): the header CTerm finds
 * in the loaded segments (PART_HEAD, PART_ABI, then the address of
 * part_start), and what the C library's start-up would have given the
 * part: memory (malloc/free).
 */
#include "cterm.h"

const struct PartEnv *penv;
struct ExecBase *SysBase;               /* exec's calls, libnix's too */
#undef UtilityBase
struct Library *UtilityBase = NULL;     /* libnix's stricmp() calls it by
                                         * this name: CTerm's, copied */

static APTR part_start(const struct PartEnv *e)
{
    penv = e;
    SysBase = *(struct ExecBase **)4;
    UtilityBase = (struct Library *)*e->UtilityBase;
    return part_main();
}

/* What CTerm looks for after LoadSeg: the mark, the version, then the
 * way in. */
const struct { ULONG mark, abi; PartStart start; }
    part_head __attribute__((used)) = { PART_HEAD, PART_ABI, part_start };

void *malloc(size_t n) { return AllocVec(n, MEMF_ANY); }
void free(void *p) { if (p) FreeVec(p); }
void *calloc(size_t n, size_t k) { return AllocVec(n * k, MEMF_ANY | MEMF_CLEAR); }
