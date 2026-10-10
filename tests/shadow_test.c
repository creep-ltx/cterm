/* shadow_test - plays a display that only redraws what vt_take_changes
 * says changed (a scroll, then dirty column spans), over every stream in
 * tests/streams in small chunks; after each chunk the shadow must equal
 * the emulator's screen cell for cell. Catches a change that is made but
 * not reported - which on the Amiga is a stale letter left on screen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vt.h"

#define MAXC 200
#define MAXR 100
static VtCell sh[MAXR][MAXC];

static int same(const VtCell *a, const VtCell *b)
{
    return a->cp == b->cp && a->pen == b->pen;     /* one look, one pen */
}

static void cell_at(Vt *v, int y, int x, VtCell *out)
{
    int w;
    const VtCell *l = vt_line(v, y, 0, &w);
    if (l && x < w)
        *out = l[x];
    else
        memset(out, 0, sizeof *out);
}

static int run(const char *file, int cols, int rows, int amiga, int chunk)
{
    FILE *f = fopen(file, "rb");
    static unsigned char buf[1 << 20];
    int n, i, x, y, bad = 0;
    Vt *v;
    VtChanges c;
    if (!f) return 0;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    v = vt_new(cols, rows, 50);
    if (amiga) vt_set_amiga(v, 1);
    memset(sh, 0, sizeof sh);
    vt_take_changes(v, &c);
    for (i = 0; i < n && !bad; i += chunk) {
        vt_write(v, buf + i, n - i < chunk ? n - i : chunk);
        if (!vt_take_changes(v, &c))
            continue;
        if (c.all) {
            for (y = 0; y < rows; y++)
                for (x = 0; x < cols; x++)
                    cell_at(v, y, x, &sh[y][x]);
        } else {
            if (c.scroll_n > 0) {
                for (y = c.scroll_top; y <= c.scroll_bottom - c.scroll_n; y++)
                    memcpy(sh[y], sh[y + c.scroll_n], sizeof sh[y]);
                for (; y <= c.scroll_bottom; y++)
                    memset(sh[y], 0x55, sizeof sh[y]);   /* junk: must be redrawn */
            } else if (c.scroll_n < 0) {
                int k = -c.scroll_n;
                for (y = c.scroll_bottom; y >= c.scroll_top + k; y--)
                    memcpy(sh[y], sh[y - k], sizeof sh[y]);
                for (; y >= c.scroll_top; y--)
                    memset(sh[y], 0x55, sizeof sh[y]);
            }
            for (y = 0; y < rows; y++)
                if (c.dirty[y])
                    for (x = c.x0[y]; x <= c.x1[y]; x++)
                        cell_at(v, y, x, &sh[y][x]);
        }
        for (y = 0; y < rows && !bad; y++)
            for (x = 0; x < cols; x++) {
                VtCell m;
                cell_at(v, y, x, &m);
                if (!same(&m, &sh[y][x])) {
                    printf("%s chunk %d: row %d col %d stale (model %u, shown %u)\n",
                           file, chunk, y, x, m.cp, sh[y][x].cp);
                    bad = 1;
                    break;
                }
            }
    }
    vt_free(v);
    return bad;
}

int main(int argc, char **argv)
{
    int i, fails = 0;
    static const int chunks[] = { 1, 7, 64, 4096 };
    for (i = 1; i < argc; i++) {
        int k, cols = 80, rows = 24, amiga = strstr(argv[i], "amiga") != NULL;
        const char *b = strrchr(argv[i], '/');
        sscanf(b ? b + 1 : argv[i], "%*[^-]-%dx%d", &cols, &rows);
        for (k = 0; k < 4; k++)
            fails += run(argv[i], cols, rows, amiga, chunks[k]);
    }
    printf(fails ? "shadow: %d FAILED\n" : "shadow: all passed\n", fails);
    return fails != 0;
}
