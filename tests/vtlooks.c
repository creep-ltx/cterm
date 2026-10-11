/* vtlooks COLS ROWS FILE [CHUNK] - every cell of the screen and the
 * history, as character, colours and style, after feeding FILE: to
 * check that a change to the cell model keeps every look (built against
 * the old and the new vt.c, the outputs must be equal). */
#include <stdio.h>
#include <stdlib.h>
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
    static unsigned char buf[1 << 22];
    int n = fread(buf, 1, sizeof buf, f), i, y, k = 0;
    Vt *v = vt_new(cols, rows, getenv("HIST") ? atoi(getenv("HIST")) : 300);
    fclose(f);
    if (argc > 5) { vt_set_utf8(v, 0); vt_set_amiga(v, 1); }
    if (chunk <= 0) chunk = n;
    for (i = 0; i < n; i += chunk) {
        vt_write(v, buf + i, n - i < chunk ? n - i : chunk);
        if (++k % 50 == 0 || i + chunk >= n) {      /* checkpoints */
            printf("-- %d\n", i);
            for (y = 0; y < rows; y++)
                dump(v, y, 0);
            for (y = 0; y < rows && y < vt_history(v); y++)
                dump(v, y, vt_history(v));
        }
    }
    vt_free(v);
    return 0;
}
