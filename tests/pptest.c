/* pptest - the planar painter's asm (ppaint.s) against its C reference,
 * on random rows: glyphs, pens, phases, widths, heights, plane masks,
 * line lengths. Amiga program; run under vamos (make pptest). */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ppaint.h"

static ULONG seed = 12345;
static ULONG rnd(void) { seed = seed * 1103515245 + 12345; return seed >> 8; }

int main(int argc, char **argv)
{
    int it, iters = argc > 1 ? atoi(argv[1]) : 2000, bad = 0;
    static UBYTE mem1[8 * 64 * 200], mem2[8 * 64 * 200];
    for (it = 0; it < iters; it++) {
        int ch = 1 + rnd() % 32, depth = 1 + rnd() % 8, i, p, slots[64], ns;
        LONG bpr = 2 * (20 + rnd() % 30);      /* even, as graphics has */
        int n, x, y = rnd() % 4;
        UBYTE mask = (UBYTE)rnd(), *pl1[8], *pl2[8], g[32];
        if (rnd() % 4 == 0) mask = 0xFF;
        else if (rnd() % 2) mask = 1 << (rnd() % depth);   /* one plane */
        if (!pp_init(ch, 64)) { printf("init\n"); return 20; }
        ns = 1 + rnd() % 40;
        slots[0] = 0;
        for (i = 1; i < ns; i++) {
            int k;
            for (k = 0; k < ch; k++) g[k] = (UBYTE)rnd();
            slots[i] = pp_add(g);
        }
        x = rnd() % (bpr * 8 - 16);
        if (rnd() % 3 == 0) x &= ~7;                /* phase 0 */
        n = 1 + rnd() % ((bpr * 8 - x) / 8);
        if (rnd() % 3 == 0) n = 1 + rnd() % 3;
        if (x + 8 * n > bpr * 8) n = (bpr * 8 - x) / 8;
        if (n < 1) continue;
        /* half the time a few colours over and over (colour text: the
         * same patterns of pens in group after group, row after row) */
        UWORD pal[4], leadpw = (UWORD)(rnd() & 0xFFFF);
        int lead = rnd() % 2 ? 0 : rnd() % 10;     /* 9: refused */
        int few = rnd() % 2, rep, nrep = few ? 1 + rnd() % 4 : 1;
        for (i = 0; i < 4; i++) {
            UBYTE fg = (UBYTE)rnd(), bg = (UBYTE)rnd();
            pal[i] = ((fg ^ bg) << 8) | bg;
        }
        for (rep = 0; rep < nrep; rep++) {
        for (i = 0; i < n; i++) {
            UBYTE fg = (UBYTE)rnd(), bg = (UBYTE)rnd();
            if (i && rnd() % 3) { pp_pw[i] = pp_pw[i - 1]; }
            else if (few) pp_pw[i] = pal[rnd() % 4];
            else pp_pw[i] = ((fg ^ bg) << 8) | bg;
            pp_gp[i] = PP_BLOCK(slots[rnd() % ns]);
        }
        for (i = 0; i < (int)sizeof mem1; i++) mem1[i] = mem2[i] = (UBYTE)rnd();
        for (p = 0; p < 8; p++) {
            pl1[p] = mem1 + p * bpr * 40;
            pl2[p] = mem2 + p * bpr * 40;
        }
        pp_paint(pl1, depth, mask, bpr, x, y, n, lead, leadpw);
        pp_paint_c(pl2, depth, mask, bpr, x, y, n, lead, leadpw);
        if (memcmp(mem1, mem2, sizeof mem1)) {
            for (i = 0; i < (int)sizeof mem1 && mem1[i] == mem2[i]; i++) ;
            printf("DIFF it %d ch %d depth %d mask %02x bpr %ld x %d n %d lead %d: byte %d (plane %ld line %ld byte %ld) asm %02x c %02x\n",
                   it, ch, depth, mask, bpr, x, n, lead, i, i / (bpr * 40),
                   (i % (bpr * 40)) / bpr, i % bpr, mem1[i], mem2[i]);
            if (++bad > 5) break;
        }
        }
        if (bad > 5) break;
    }
    pp_free();
    printf("%d rows, %d bad\n", it, bad);
    return bad ? 10 : 0;
}
