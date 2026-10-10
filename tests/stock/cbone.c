/* cbone TEST [N] - one conbench shape written to Output() N times
 * (sgr, perchar, plain, frame, cursor), for profiling one test at a
 * time on the stock rig. No stdio (libnix's stdio start-up fails there). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

static int num(char *d, int v)
{
    char t[12];
    int n = 0, k = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) d[k++] = t[--n];
    return k;
}

int main(void)
{
    LONG a[2] = { 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"TEST/A,N/N", a, NULL);
    BPTR out = Output();
    char s[400], blk[80], longl[300];
    int i, k, n, c = 33;
    const char *t;
    if (!rd)
        return 20;
    t = (const char *)a[0];
    n = a[1] ? *(LONG *)a[1] : 1;
    for (i = 0; i < 78; i++) { blk[i] = c; if (++c > 126) c = 33; }
    blk[78] = 10;
    c = 33;
    for (i = 0; i < 300; i++) { longl[i] = c; if (++c > 126) c = 33; }
    while (n-- > 0) {
        if (!strcmp(t, "sgr")) {
            for (i = 0; i < 300; i++) {
                for (k = 0; k < 8; k++) {
                    int m = 0;
                    s[m++] = 27; s[m++] = '['; s[m++] = '3'; s[m++] = '0' + k;
                    s[m++] = 'm';
                    memcpy(s + m, "12345678", 8); m += 8;
                    Write(out, s, m);
                }
                Write(out, "\n", 1);
            }
        } else if (!strcmp(t, "perchar")) {
            for (i = 0; i < 60; i++) {
                int m = 0;
                for (k = 0; k < 40; k++) {
                    s[m++] = 27; s[m++] = '['; s[m++] = '3'; s[m++] = '0' + k % 8;
                    s[m++] = 'm'; s[m++] = 65 + (k + i) % 26;
                }
                s[m++] = '\n';
                Write(out, s, m);
            }
        } else if (!strcmp(t, "plain")) {
            for (i = 0; i < 816; i++)
                Write(out, blk, 79);
        } else if (!strcmp(t, "frame")) {
            int f, j;
            for (f = 0; f < 15; f++)
                for (j = 1; j <= 28; j++) {
                    int m = 0;
                    s[m++] = 27; s[m++] = '['; m += num(s + m, j);
                    s[m++] = ';'; s[m++] = '1'; s[m++] = 'H';
                    s[m++] = 27; s[m++] = '['; s[m++] = 'K';
                    memcpy(s + m, longl + (j * 7 + f) % 240, 60); m += 60;
                    Write(out, s, m);
                }
        } else if (!strcmp(t, "cursor")) {
            for (i = 1; i <= 700; i++) {
                int m = 0;
                s[m++] = 27; s[m++] = '['; m += num(s + m, i % 20 + 1);
                s[m++] = ';'; m += num(s + m, i % 60 + 1); s[m++] = 'H';
                memcpy(s + m, "-cursor-", 8); m += 8;
                Write(out, s, m);
            }
        }
    }
    Write(out, "\x1b[0m\n", 5);
    FreeArgs(rd);
    return 0;
}
