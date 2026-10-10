/* vtbench TEST N - feed conbench's shapes (plain, sgr, perchar, cursor,
 * frame, nl, eol) into the emulator N times, no drawing: run under
 * vamos -C 68020 -I to count instructions per line (tests/perf/vtcount.sh). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/vt.h"

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

int main(int argc, char **argv)
{
    Vt *vt = vt_new(76, 28, 100);
    int i, j, k, n = argc > 2 ? atoi(argv[2]) : 10;
    const char *t = argc > 1 ? argv[1] : "plain";
    char s[400];
    int c = 33;
    for (i = 0; i < 78; i++) { blk[i] = c; if (++c > 126) c = 33; }
    blk[78] = 10;
    c = 33;
    for (i = 0; i < 300; i++) { longl[i] = c; if (++c > 126) c = 33; }
    vt_set_utf8(vt, 0);
    vt_set_amiga(vt, 1);
    /* warm up: fill the screen so scrolls happen */
    for (i = 0; i < 150; i++)
        vt_write(vt, (const uint8_t *)blk, 79);
    take(vt);
    PutStr((STRPTR)"BENCHSTART\n");
    for (i = 0; i < n; i++) {
        if (!strcmp(t, "plain")) {
            vt_write(vt, (const uint8_t *)blk, 79);
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
        } else if (!strcmp(t, "nl")) {
            vt_write(vt, (const uint8_t *)"\n\n\n\n\n\n\n\n\n\n", 10);
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
    PutStr((STRPTR)"BENCHEND\n");
    (void)j;
    vt_free(vt);
    return 0;
}
