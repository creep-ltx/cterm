/* scrolltime [N] - what a text window's scroll costs on this Amiga: a
 * 640x245 window on the Workbench, ScrollRaster of its 616x224 text area by
 * one 8-pixel row, N times, with the write mask at 1, 2 and all planes;
 * prints milliseconds per scroll (E clock). Run by hand on the ctspeed rig:
 * the floor under CTerm's scrolls (tests/perf/pptime.sh is the painter's). */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <stdlib.h>

struct Device *TimerBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
static struct timerequest treq;

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 50, i, m;
    static const UBYTE masks[] = { 0x01, 0x03, 0xFF };
    struct Window *w;
    struct RastPort *rp;
    struct EClockVal a, b;
    ULONG f;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !GfxBase ||
        OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0))
        return 20;
    TimerBase = treq.tr_node.io_Device;
    w = OpenWindowTags(NULL, WA_Left, 0, WA_Top, 11, WA_Width, 640,
                       WA_Height, 245, WA_Title, (ULONG)"scrolltime",
                       WA_SmartRefresh, TRUE, WA_Activate, TRUE, TAG_END);
    if (!w)
        return 20;
    rp = w->RPort;
    SetAPen(rp, 1);
    for (i = 0; i < 28; i++) {          /* something to move */
        Move(rp, 8, w->BorderTop + 6 + i * 8);
        Text(rp, (STRPTR)"The quick brown fox jumps over the lazy dog 0123456789", 54);
    }
    f = ReadEClock(&a);
    for (m = 0; m < 3; m++) {
        SetWriteMask(rp, masks[m]);
        ReadEClock(&a);
        for (i = 0; i < n; i++)
            ScrollRaster(rp, 0, 8, 6, w->BorderTop, 621, w->BorderTop + 223);
        ReadEClock(&b);
        Printf((STRPTR)"mask %02lx: %ld us a scroll\n", (LONG)masks[m],
               (LONG)((unsigned long long)(b.ev_lo - a.ev_lo) * 1000000 / f / n));
    }
    CloseWindow(w);
    CloseDevice(&treq.tr_node);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
