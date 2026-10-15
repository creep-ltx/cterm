/* pptime [N] - the planar painter (ppaint.s) timed on the Amiga itself:
 * rows like conbench's (one colour, 8-letter colour runs, a colour per
 * letter) painted N times into four planes (and one) of a 640-pixel
 * bitmap, at the Workbench window's bit phase (x = 4); prints
 * microseconds per row. CHIP as 2nd argument: the planes in chip RAM
 * (the screen's cost, with the display's DMA), else fast RAM (the
 * painter's own instructions). tests/perf/pptime.sh runs it on the
 * ctspeed rig. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/ppaint.h"

struct Device *TimerBase;
static struct timerequest treq;

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 100, chip = argc > 2, i, k, t, np;
    LONG bpr = 80, cols = 76;
    UBYTE *pl[4], *mem, g[8];
    int slots[95];
    static const char *names[] = { "plain", "sgr", "perchar" };
    struct EClockVal a, b;
    ULONG f;
    /* no timer (vamos): counted, not timed */
    if (!OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0))
        TimerBase = treq.tr_node.io_Device;
    mem = AllocVec(4 * bpr * 16, (chip ? MEMF_CHIP : TimerBase ? MEMF_FAST : 0) | MEMF_CLEAR);
    if (!mem || !pp_init(8, 256))
        return 20;
    for (i = 0; i < 4; i++)
        pl[i] = mem + i * bpr * 16;
    for (i = 0; i < 95; i++) {          /* letter-like glyphs */
        for (k = 0; k < 8; k++)
            g[k] = (UBYTE)((i * 37 + k * 11) ^ (k * 0x55));
        slots[i] = pp_add(g);
    }
    f = TimerBase ? ReadEClock(&a) : 1;
    a.ev_lo = b.ev_lo = 0;
    for (t = 0; t < 3; t++)
        for (np = 4; np >= 1; np -= 3) {
            for (i = 0; i < cols; i++) {
                int fg = t == 0 ? 1 : t == 1 ? 1 + (i / 8) % 7 : 1 + i % 7;
                pp_gp[i] = PP_BLOCK(slots[(i * 7) % 95]);
                pp_pw[i] = (UWORD)(fg << 8);       /* bg pen 0: fx = fg */
            }
            if (TimerBase)
                ReadEClock(&a);
            for (i = 0; i < n; i++)
                pp_paint(pl, 4, np == 4 ? 0x0F : 0x01, bpr, 4, 0, cols, 0, 0);
            if (TimerBase)
                ReadEClock(&b);
            Printf((STRPTR)"%-8s %ld plane%s %5ld us/row\n", (LONG)names[t],
                   (LONG)np, (LONG)(np > 1 ? "s" : " "),
                   (LONG)((unsigned long long)(b.ev_lo - a.ev_lo) * 1000000 / f / n));
        }
    pp_free();
    FreeVec(mem);
    if (TimerBase)
        CloseDevice(&treq.tr_node);
    return 0;
}
