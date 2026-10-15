/* vttime [N] [TEST...] - the emulator alone (vt.c, no drawing) timed on the
 * Amiga itself: conbench's shapes (the same as vtbench.c's) fed N times
 * each, the E clock read around them; prints microseconds per item. On a
 * cycle-exact emulator the numbers repeat to the tick, so a change of a
 * few instructions per letter shows (tests/perf/vttime.sh runs it on the
 * ctspeed rig). Built by vttime.sh with the program's own flags. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/vt.h"

struct Device *TimerBase;
static struct timerequest treq;
static char blk[80], longl[300];

static int fmt_num(char *d, int v)
{
    char t[12];
    int n = 0, k = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) d[k++] = t[--n];
    return k;
}

static void take(Vt *vt)
{
    VtChanges c;
    vt_take_changes(vt, &c);
}

static const char *tests[] = { "plain", "block", "sgr", "perchar", "cursor",
                               "frame", "nl", "eol", "byte", "wrap", "calib", "mem", "chars", "lfs", "take", NULL };
static ULONG cbuf[64];

/* one test, n items: E clock ticks */
static ULONG run(Vt *vt, const char *t, int n)
{
    struct EClockVal a, b;
    static char s[4200];
    int i, k;
    a.ev_lo = b.ev_lo = 0;
    if (TimerBase)
        ReadEClock(&a);
    for (i = 0; i < n; i++) {
        if (!strcmp(t, "calib")) {
            /* 1000 rounds of a 2-instruction loop (subq, bne) */
            int z = 1000;
            asm volatile("1: subq.l #1,%0\n\tbne.s 1b" : "+d"(z));
            continue;
        } else if (!strcmp(t, "mem")) {
            /* 1000 long stores to memory (fast RAM): move.l, subq, bne */
            int z = 1000;
            ULONG *q = cbuf;
            asm volatile("1: move.l %0,(%1)\n\tsubq.l #1,%0\n\tbne.s 1b" : "+d"(z) : "a"(q) : "memory");
            continue;
        } else if (!strcmp(t, "chars")) {
            /* the letters alone: 51 lines of 78 and a CR, no scroll */
            if (!i)
                for (k = 0; k < 51; k++) {
                    memcpy(s + k * 79, blk, 78);
                    s[k * 79 + 78] = 13;
                }
            vt_write(vt, (const uint8_t *)s, 51 * 79);
        } else if (!strcmp(t, "lfs")) {
            /* the new lines alone: 51 CR LF, each its own scroll */
            if (!i)
                for (k = 0; k < 51; k++)
                    s[k * 2] = 13, s[k * 2 + 1] = 10;
            vt_write(vt, (const uint8_t *)s, 102);
        } else if (!strcmp(t, "take")) {
            vt_write(vt, (const uint8_t *)"x", 1);
            take(vt);
            continue;
        } else if (!strcmp(t, "plain")) {
            vt_write(vt, (const uint8_t *)blk, 79);
        } else if (!strcmp(t, "block")) {
            /* 51 lines in one write (block-4k): built once */
            if (!i)
                for (k = 0; k < 51; k++)
                    memcpy(s + k * 79, blk, 79);
            vt_write(vt, (const uint8_t *)s, 51 * 79);
        } else if (!strcmp(t, "sgr")) {
            for (k = 0; k < 8; k++) {
                int m = 0;
                s[m++] = 27; s[m++] = '['; s[m++] = '3'; s[m++] = '0' + k;
                s[m++] = 'm';
                memcpy(s + m, "12345678", 8); m += 8;
                vt_write(vt, (const uint8_t *)s, m);
            }
            vt_write(vt, (const uint8_t *)"\n", 1);
        } else if (!strcmp(t, "perchar")) {
            int m = 0;
            for (k = 0; k < 40; k++) {
                s[m++] = 27; s[m++] = '['; s[m++] = '3'; s[m++] = '0' + k % 8;
                s[m++] = 'm'; s[m++] = 65 + (k + i) % 26;
            }
            s[m++] = '\n';
            vt_write(vt, (const uint8_t *)s, m);
        } else if (!strcmp(t, "cursor")) {
            int m = 0;
            s[m++] = 27; s[m++] = '[';
            m += fmt_num(s + m, i % 20 + 1); s[m++] = ';';
            m += fmt_num(s + m, i % 60 + 1); s[m++] = 'H';
            memcpy(s + m, "-cursor-", 8); m += 8;
            vt_write(vt, (const uint8_t *)s, m);
        } else if (!strcmp(t, "frame")) {
            int m = 0;
            s[m++] = 27; s[m++] = '[';
            m += fmt_num(s + m, i % 28 + 1);
            s[m++] = ';'; s[m++] = '1'; s[m++] = 'H'; s[m++] = 27; s[m++] = '[';
            s[m++] = 'K';
            memcpy(s + m, longl + (i * 7) % 240, 60); m += 60;
            vt_write(vt, (const uint8_t *)s, m);
        } else if (!strcmp(t, "byte")) {
            vt_write(vt, (const uint8_t *)blk + i % 78, 1);
        } else if (!strcmp(t, "nl")) {
            vt_write(vt, (const uint8_t *)"\n\n\n\n\n\n\n\n\n\n", 10);
        } else if (!strcmp(t, "wrap")) {
            vt_write(vt, (const uint8_t *)longl, 300);
        } else if (!strcmp(t, "eol")) {
            int m = 0;
            s[m++] = 13;
            memcpy(s + m, "working: ", 9); m += 9;
            m += fmt_num(s + m, i % 100); s[m++] = '%';
            s[m++] = 27; s[m++] = '['; s[m++] = 'K';
            vt_write(vt, (const uint8_t *)s, m);
        }
        if (i % 4 == 3)
            take(vt);
    }
    if (TimerBase)
        ReadEClock(&b);
    return b.ev_lo - a.ev_lo;
}

int main(int argc, char **argv)
{
    Vt *vt;
    int i, n = argc > 1 ? atoi(argv[1]) : 200, c = 33;
    ULONG f, sum = 0;
    /* no timer (vamos, tests/perf/vtcount.sh): counted, not timed */
    if (!OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0))
        TimerBase = treq.tr_node.io_Device;
    for (i = 0; i < 78; i++) { blk[i] = c; if (++c > 126) c = 33; }
    blk[78] = 10;
    c = 33;
    for (i = 0; i < 300; i++) { longl[i] = c; if (++c > 126) c = 33; }
    vt = vt_new(76, 28, 2000);
    vt_set_utf8(vt, 0);
    vt_set_amiga(vt, 1);
    /* warm up: past the history's growth, so scrolls reuse its rows */
    for (i = 0; i < 2100; i++)
        vt_write(vt, (const uint8_t *)blk, 79);
    take(vt);
    if (TimerBase) {
        struct EClockVal e;
        f = ReadEClock(&e);             /* ticks a second */
    } else
        f = 709379;
    PutStr((STRPTR)"BENCHSTART\n");
    for (i = 0; tests[i]; i++) {
        int k, want = argc <= 2;
        ULONG t;
        for (k = 2; k < argc; k++)
            if (!strcmp(argv[k], tests[i]))
                want = 1;
        if (!want)
            continue;
        t = run(vt, tests[i], n);
        sum += t;
        Printf((STRPTR)"%-8s %6ld us/item\n", (LONG)tests[i],
               (LONG)((unsigned long long)t * 1000000 / f / n));
    }
    Printf((STRPTR)"total    %6ld ms\n", (LONG)((unsigned long long)sum * 1000 / f));
    vt_free(vt);
    if (TimerBase)
        CloseDevice(&treq.tr_node);
    return 0;
}
