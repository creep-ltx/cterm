/* vtlooks COLS ROWS FILE [CHUNK] [amiga|cp437|utf8 [CHECK]] - every cell of the screen and
 * the history, as character, colours and style, after feeding FILE: to
 * check that a change to the cell model keeps every look (built against
 * the old and the new vt.c, the outputs must be equal). Also the cursor
 * and what vt_take_changes says is to be drawn: a parser change that
 * marks less leaves stale text on screen. amiga: the Amiga Shell's
 * Latin-1; cp437: code page 437; CHECK: a checkpoint every CHECK chunks
 * (50). Arguments, not the environment: built for the 68020 and run
 * under vamos (tests/perf/vtsame.sh), getenv reads ENV:. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vt.h"

static void dump(Vt *v, int y, int back)
{
    int w, x;
    const VtCell *r = vt_line(v, y, back, &w);
#ifdef VT_PENS
    const VtPen *pens = vt_pens(v);
#endif
    for (x = 0; r && x < w; x++) {
#ifdef VT_PENS
        const VtPen *p = &pens[r[x].pen];
        printf("%x/%x/%x/%x ", r[x].cp, p->fg, p->bg, p->attr);
#else
        printf("%x/%x/%x/%x ", r[x].cp, r[x].fg, r[x].bg, r[x].attr);
#endif
    }
    putchar('\n');
}

int main(int argc, char **argv)
{
    int cols = atoi(argv[1]), rows = atoi(argv[2]), chunk = argc > 4 ? atoi(argv[4]) : 0;
    FILE *f = fopen(argv[3], "rb");
    static unsigned char buf[1 << 20];
    int n = fread(buf, 1, sizeof buf, f), i, y, k = 0, every = 50;
    Vt *v = vt_new(cols, rows, getenv("HIST") ? atoi(getenv("HIST")) : 300);
    fclose(f);
    if (argc > 5 && !strcmp(argv[5], "cp437"))
        vt_set_cp437(v, 1);
    else if (argc > 5 && strcmp(argv[5], "utf8")) {
        vt_set_utf8(v, 0);
        vt_set_amiga(v, 1);
    }
    if (argc > 6)
        every = atoi(argv[6]);
    if (chunk <= 0) chunk = n;
    for (i = 0; i < n; i += chunk) {
        vt_write(v, buf + i, n - i < chunk ? n - i : chunk);
        if (++k % every == 0 || i + chunk >= n) {   /* checkpoints */
            VtChanges c;
            int cx, cy, cv;
            vt_cursor(v, &cx, &cy, &cv);
            printf("-- %d cursor %d %d %d\n", i, cx, cy, cv);
            if (vt_take_changes(v, &c)) {
                printf("changes all %d scroll %d %d %d\n", c.all,
                       c.scroll_top, c.scroll_bottom, c.scroll_n);
                for (y = 0; y < rows && !c.all; y++)
                    if (c.dirty[y])
                        printf("dirty %d %d-%d\n", y, c.x0[y], c.x1[y]);
            }
            for (y = 0; y < rows; y++)
                dump(v, y, 0);
            for (y = 0; y < rows && y < vt_history(v); y++)
                dump(v, y, vt_history(v));
        }
    }
    vt_free(v);
    return 0;
}
