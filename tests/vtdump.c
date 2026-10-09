/* vtdump COLS ROWS FILE [CHUNK] - feed FILE through the emulator (in
 * chunks of CHUNK bytes, default all at once) and print the screen as
 * UTF-8 text, trailing spaces trimmed. For comparing with references. */
#include "../src/vt.h"
#include <stdio.h>
#include <stdlib.h>

static void utf8(uint32_t c, FILE *f)
{
    if (c < 0x80) fputc((int)c, f);
    else if (c < 0x800) { fputc(0xC0 | (c >> 6), f); fputc(0x80 | (c & 63), f); }
    else if (c < 0x10000) { fputc(0xE0 | (c >> 12), f);
        fputc(0x80 | ((c >> 6) & 63), f); fputc(0x80 | (c & 63), f); }
    else { fputc(0xF0 | (c >> 18), f); fputc(0x80 | ((c >> 12) & 63), f);
        fputc(0x80 | ((c >> 6) & 63), f); fputc(0x80 | (c & 63), f); }
}

int main(int argc, char **argv)
{
    int cols = atoi(argv[1]), rows = atoi(argv[2]), chunk = argc > 4 ? atoi(argv[4]) : 0;
    FILE *f = fopen(argv[3], "rb");
    static unsigned char buf[1 << 22];
    int n = fread(buf, 1, sizeof buf, f), i, y, x, w;
    Vt *v = vt_new(cols, rows, 1000);
    if (chunk <= 0) chunk = n;
    for (i = 0; i < n; i += chunk)
        vt_write(v, buf + i, n - i < chunk ? n - i : chunk);
    for (y = 0; y < rows; y++) {
        const VtCell *r = vt_line(v, y, 0, &w);
        int last = -1;
        for (x = 0; x < w; x++) if (r[x].cp && r[x].cp != ' ' && r[x].cp != VT_WIDE_TAIL) last = x;
        for (x = 0; x <= last; x++)
            if (r[x].cp != VT_WIDE_TAIL) utf8(r[x].cp ? r[x].cp : ' ', stdout);
        putchar('\n');
    }
    vt_free(v);
    return 0;
}
