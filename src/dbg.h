/* dbg - debug lines to RawPutChar (wasabi debug shows them); only in a
 * -DDBG build. */
#ifndef DBG_H
#define DBG_H
#ifdef DBG
#include <exec/types.h>
static void dbg_c(char c)
{
    register char d0 __asm("d0") = c;
    __asm volatile("move.l 4.w,a6\n\tjsr -516(a6)" : : "d"(d0) : "a0", "a1", "a6", "d1");
}
static void dbg_s(const char *s) { while (*s) dbg_c(*s++); }
static void dbg_x(unsigned long v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) dbg_c("0123456789abcdef"[(v >> i) & 15]);
}
#define DBGS(s) dbg_s(s)
#define DBGX(v) dbg_x((unsigned long)(v))
#else
#define DBGS(s) ((void)0)
#define DBGX(v) ((void)0)
#endif
#endif
