/* cmap_test FONT.ttf... - the characters CTerm reads from each font's
 * cmap (src/cmap.c), printed as fontconfig prints a charset (hex ranges,
 * "a-b" or "a"), to be compared with `fc-query --format '%{charset}'`;
 * and a few known ones checked. Reads only the table directory and the
 * cmap table, as CTerm does on the Amiga. */
#include "../src/cmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *al(unsigned long n) { return malloc(n); }

int main(int argc, char **argv)
{
    int i, fails = 0;
    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        unsigned char head[12 + 16 * 64], *cm;
        unsigned long hl, off, len;
        long k;
        CmapRanges r;
        memset(&r, 0, sizeof r);
        r.alloc = al;
        r.release = free;
        if (!f) {
            perror(argv[i]);
            return 2;
        }
        hl = fread(head, 1, sizeof head, f);
        if (!cmap_where(head, hl, &off, &len)) {
            printf("%s: no cmap\n", argv[i]);
            return 1;
        }
        cm = malloc(len);
        fseek(f, off, SEEK_SET);
        len = fread(cm, 1, len, f);
        fclose(f);
        if (!cmap_table(&r, cm, len)) {
            printf("%s: cmap not read\n", argv[i]);
            return 1;
        }
        for (k = 0; k < r.n; k++) {
            if (r.r[2 * k] == r.r[2 * k + 1])
                printf("%s%lx", k ? " " : "", r.r[2 * k]);
            else
                printf("%s%lx-%lx", k ? " " : "", r.r[2 * k], r.r[2 * k + 1]);
        }
        printf("\n");
        if (r.n == 0)
            fails++;
        if (cmap_has(&r, 0x10FFFF + 1))
            fails++;
        free(cm);
        cmap_free(&r);
    }
    return fails != 0;
}
