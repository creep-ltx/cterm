/* pic_test - pictures in the terminal: sixel and iTerm2 files placed,
 * scrolled, erased, cut at the edges, dropped at the cap; the replies
 * programs ask before they send one. Streams from tests/pictures/
 * (mksixel.py; img2sixel's own when it is installed). */
#include "../src/vt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void feed(Vt *v, const char *s) { vt_write(v, (const uint8_t *)s, strlen(s)); }

static char *slurp(const char *cmd, long *n)
{
    FILE *f = popen(cmd, "r");
    char *b = malloc(1 << 22);
    *n = f ? (long)fread(b, 1, (1 << 22) - 1, f) : 0;
    if (f) pclose(f);
    b[*n] = 0;
    return b;
}

static void feedn(Vt *v, const char *b, long n)
{
    long i;                             /* in small writes, as a host sends */
    for (i = 0; i < n; i += 37)
        vt_write(v, (const uint8_t *)b + i, n - i < 37 ? (int)(n - i) : 37);
}

static uint32_t cell(Vt *v, int r, int c, int back)
{
    int w;
    const VtCell *l = vt_line(v, r, back, &w);
    return l && c < w ? l[c].cp : 0;
}

static int row_pics(Vt *v, int r, int back)
{
    int w;
    const VtCell *l = vt_line(v, r, back, &w);
    return l ? VT_ROW_PICS(l) : 0;
}

/* the picture's pixel at x, y of cell r, c */
static uint32_t pix(Vt *v, int r, int c, int x, int y)
{
    uint32_t cp = cell(v, r, c, 0);
    const VtPic *p;
    const uint8_t *q;
    if (!VT_IS_PIC(cp) || !(p = vt_pic(v, cp)))
        return 0xFF000000u;
    x += VT_PIC_TX(cp) * p->cw;
    y += VT_PIC_TY(cp) * p->ch;
    if (x >= p->w || y >= p->h)
        return 0xFE000000u;
    q = p->rgb + ((long)y * p->w + x) * 3;
    return (uint32_t)q[0] << 16 | q[1] << 8 | q[2];
}

static int near(uint32_t a, uint32_t b)
{
    int k;
    if ((a | b) >> 24)
        return 0;
    for (k = 0; k < 24; k += 8)
        if (abs((int)(a >> k & 255) - (int)(b >> k & 255)) > 2)
            return 0;
    return 1;
}

static void reply_is(Vt *v, const char *q, const char *want, const char *msg)
{
    char rep[128];
    int n;
    vt_take_reply(v, rep, sizeof rep);
    feed(v, q);
    n = vt_take_reply(v, rep, sizeof rep - 1);
    rep[n] = 0;
    if (strcmp(rep, want)) {
        printf("FAIL: %s: got ", msg);
        for (n = 0; rep[n]; n++)
            printf(rep[n] < 32 ? "^%c" : "%c", rep[n] < 32 ? rep[n] + 64 : rep[n]);
        printf("\n");
        fails++;
    }
}

/* the tests' PPM decoder for iTerm2 files (CTerm's is datatypes) */
static int ppm_decode(const uint8_t *f, long len, uint32_t **argb, int *w, int *h)
{
    int i, off = 0, mx;
    if (len < 11 || f[0] != 'P' || f[1] != '6' ||
        sscanf((const char *)f + 2, "%d %d %d%n", w, h, &mx, &off) != 3)
        return 0;
    off += 3;
    if (*w <= 0 || *h <= 0 || *w > 4096 || *h > 4096 ||
        len < off + (long)*w * *h * 3)
        return 0;
    *argb = malloc((size_t)*w * *h * 4);
    for (i = 0; i < *w * *h; i++)
        (*argb)[i] = 0xFF000000u | f[off + i * 3] << 16 | f[off + i * 3 + 1] << 8 |
                     f[off + i * 3 + 2];
    return 1;
}
static void ppm_free(uint32_t *p) { free(p); }

#define GEN "python3 tests/pictures/mksixel.py "

int main(void)
{
    Vt *v = vt_new(20, 6, 100);
    char *s;
    long n;
    int x, y, vis, r, c;
    setvbuf(stdout, NULL, _IONBF, 0);

    vt_decode = ppm_decode;
    vt_decode_free = ppm_free;

    /* off: swallowed, not announced */
    reply_is(v, "\x1b[c", "\x1b[?62;22c", "DA1 without pictures");
    s = slurp(GEN "bars 16 32", &n);
    feedn(v, s, n);
    feed(v, "A");
    CHECK(cell(v, 0, 0, 0) == 'A' && vt_pic_count(v) == 0, "off: sixel swallowed");

    vt_set_pictures(v, 1, 1L << 20);
    vt_set_cell_px(v, 8, 16, 0x102030);
    feed(v, "\x1b[H\x1b[2J");
    reply_is(v, "\x1b[c", "\x1b[?62;4;22c", "DA1 says sixel");
    reply_is(v, "\x1b[16t", "\x1b[6;16;8t", "CSI 16 t: the cell");
    reply_is(v, "\x1b[14t", "\x1b[4;96;160t", "CSI 14 t: the text area");
    reply_is(v, "\x1b[?1;1;0S", "\x1b[?1;0;256S", "XTSMGRAPHICS colours");
    reply_is(v, "\x1b[?2;1;0S", "\x1b[?2;0;160;96S", "XTSMGRAPHICS geometry");
    reply_is(v, "\x1b[?80$p", "\x1b[?80;2$y", "DECRQM 80");

    /* placed at the cursor: 16x32 px is 2x2 cells; the cursor below */
    feed(v, "\x1b[2;3H");
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    CHECK(vt_pic_count(v) == 1, "one picture");
    CHECK(VT_IS_PIC(cell(v, 1, 2, 0)) && VT_IS_PIC(cell(v, 2, 3, 0)) &&
          !VT_IS_PIC(cell(v, 1, 4, 0)) && !VT_IS_PIC(cell(v, 3, 2, 0)),
          "sixel: 2x2 cells at the cursor");
    CHECK(row_pics(v, 1, 0) && row_pics(v, 2, 0) && !row_pics(v, 0, 0),
          "rows marked");
    CHECK(x == 2 && y == 3, "cursor below the picture, its column");
    /* bars: 8 bars of 2 px: white, yellow, cyan, green | magenta... */
    CHECK(pix(v, 1, 2, 0, 0) == 0xFFFFFF && pix(v, 1, 2, 2, 5) == 0xFFFF00 &&
          pix(v, 1, 2, 7, 15) == 0x00FF00 && pix(v, 2, 3, 0, 15) == 0xFF00FF &&
          pix(v, 2, 3, 7, 15) == 0x000000, "the bars' colours");
    {
        const VtPic *p = vt_pic(v, cell(v, 1, 2, 0));
        CHECK(p && p->w == 16 && p->h == 32 && vt_pic_bytes(v) == 16 * 32 * 3,
              "its size and memory");
    }
    /* overwritten by text: the cell is a letter; the rest stays */
    feed(v, "\x1b[2;3HZ");
    CHECK(cell(v, 1, 2, 0) == 'Z' && VT_IS_PIC(cell(v, 1, 3, 0)), "overwrite");
    /* erased: gone, and its memory with it once no cell shows it */
    feed(v, "\x1b[2;1H\x1b[2K\x1b[3;1H\x1b[2K");
    CHECK(!VT_IS_PIC(cell(v, 2, 3, 0)), "erase line");
    feed(v, "\x1b[H\x1b[2J");
    CHECK(vt_pic_count(v) == 0 && vt_pic_bytes(v) == 0, "erased: memory back");
    free(s);

    /* scrolling: 3 rows high at the bottom row - the screen scrolls,
     * the picture's top goes into the history */
    feed(v, "\x1b[6;1H");
    s = slurp(GEN "gradient 24 40", &n);
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    CHECK(y == 5 && x == 0, "cursor on the last row after scrolling");
    CHECK(VT_IS_PIC(cell(v, 2, 0, 0)) && VT_IS_PIC(cell(v, 4, 2, 0)) &&
          VT_PIC_TY(cell(v, 4, 0, 0)) == 2 && !VT_IS_PIC(cell(v, 5, 0, 0)),
          "scrolled up with the picture");
    /* sixel colours are per cent: 128 comes back as 127 */
    CHECK(near(pix(v, 2, 0, 0, 0), 0x000080) && near(pix(v, 4, 2, 7, 7), 0xFFFF80) &&
          near(pix(v, 3, 1, 0, 0), 0x556680),
          "gradient pixels (24x40: 16 steps)");
    feed(v, "\r\n\r\n\r\n");
    CHECK(VT_IS_PIC(cell(v, 0, 1, 1)) && row_pics(v, 0, 1) &&
          vt_history(v) >= 1, "into the history");
    free(s);

    /* cut at the right edge: 160 px wide at column 15 of 20 */
    feed(v, "\x1b[H\x1b[2J\x1b[3J\x1b[1;16H");
    s = slurp(GEN "bars 160 16", &n);
    feedn(v, s, n);
    {
        const VtPic *p = vt_pic(v, cell(v, 0, 15, 0));
        CHECK(p && p->w == 40 && VT_IS_PIC(cell(v, 0, 19, 0)),
              "cut at the right edge (5 cells kept)");
    }
    free(s);

    /* DECSET 80: at the top left, the cursor stays, no scrolling */
    feed(v, "\x1b[H\x1b[2J\x1b[3J\x1b[5;5H\x1b[?80h");
    s = slurp(GEN "bars 16 200", &n);
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    CHECK(VT_IS_PIC(cell(v, 0, 0, 0)) && VT_IS_PIC(cell(v, 5, 1, 0)) &&
          x == 4 && y == 4 && vt_history(v) == 0, "DECSDM: top left, cut");
    free(s);
    feed(v, "\x1b[?80l");

    /* 8452: the cursor right of the picture */
    feed(v, "\x1b[H\x1b[2J\x1b[?8452h");
    s = slurp(GEN "bars 16 32", &n);
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    CHECK(x == 2 && y == 1, "8452: right of its last row");
    feed(v, "\x1b[?8452l");
    free(s);

    /* the cap: 1 MB; each 160x400 picture is 192000 bytes; 8 do not fit:
     * the oldest goes and its cells are blanks */
    feed(v, "\x1b[H\x1b[2J\x1b[3J");
    vt_set_pictures(v, 1, 3 * 160 * 400 * 3 + 1000);
    s = slurp(GEN "bars 160 400", &n);
    for (r = 0; r < 4; r++)
        feedn(v, s, n);
    CHECK(vt_pic_count(v) == 3 && vt_pic_bytes(v) == 3 * 160 * 400 * 3,
          "the cap: three kept");
    {
        int hist = vt_history(v), b, seen = 0, gone = 0;
        for (b = 0; b <= hist; b++)
            for (r = 0; r < 6; r++) {
                uint32_t cp = cell(v, r, 0, b);
                if (VT_IS_PIC(cp)) {
                    if (vt_pic(v, cp)) seen++; else gone++;
                }
            }
        CHECK(gone == 0 && seen > 0, "no cell shows a dropped picture");
    }
    free(s);
    vt_set_pictures(v, 1, 1L << 20);

    /* iTerm2: a PPM through the decoder, 2 cells wide as asked */
    feed(v, "\x1b[H\x1b[2J\x1b[3J");
    s = slurp(GEN "ppm 32 32 > /tmp/pic_test.ppm && python3 tests/pictures/mksixel.py iterm /tmp/pic_test.ppm 'width=2'", &n);
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    {
        const VtPic *p = vt_pic(v, cell(v, 0, 0, 0));
        CHECK(p && p->w == 16 && p->h == 16, "iTerm2: width=2 cells, in shape");
        CHECK(VT_IS_PIC(cell(v, 0, 1, 0)) && !VT_IS_PIC(cell(v, 1, 0, 0)) &&
              x == 0 && y == 1, "iTerm2: placed, the cursor below it (imgcat's LF: not a second)");
        CHECK(pix(v, 0, 0, 0, 0) == 0xFFFFFF && pix(v, 0, 1, 7, 0) == 0x000000,
              "iTerm2: pixels");
    }
    feed(v, "\n");
    vt_cursor(v, &x, &y, &vis);
    CHECK(y == 2, "iTerm2: the next new line counts");
    feed(v, "\x1b[H\x1b[2J");
    feedn(v, s, n - 1);                 /* without imgcat's LF */
    feed(v, "\r\n");
    vt_cursor(v, &x, &y, &vis);
    CHECK(x == 0 && y == 1, "iTerm2: CR LF after it is not a second line");
    feed(v, "\x1b[H\x1b[2J");
    feedn(v, s, n - 1);
    feed(v, "$ ");                      /* no new line: a prompt below it */
    vt_cursor(v, &x, &y, &vis);
    CHECK(x == 2 && y == 1 && cell(v, 1, 0, 0) == '$', "iTerm2: a prompt goes below it");
    feed(v, "\r\n");                   /* text on the line: its LF counts */
    vt_cursor(v, &x, &y, &vis);
    CHECK(x == 0 && y == 2, "iTerm2: a new line after text below it counts");
    free(s);
    feed(v, "\x1b[H\x1b[2J");
    s = slurp("python3 tests/pictures/mksixel.py iterm /tmp/pic_test.ppm 'width=100%;height=1'", &n);
    feedn(v, s, n);
    {
        const VtPic *p = vt_pic(v, cell(v, 0, 0, 0));
        CHECK(p && p->h == 16 && p->w == 16, "iTerm2: inside the box asked");
    }
    free(s);
    /* a download (inline=0) and a broken file: nothing, no text */
    feed(v, "\x1b[H\x1b[2J\x1b]1337;File=name=eA==;size=3:AAAA\x07X"
            "\x1b]1337;File=inline=1:bm90IGEgcGljdHVyZQ==\x1b\\Y");
    CHECK(cell(v, 0, 0, 0) == 'X' && cell(v, 0, 1, 0) == 'Y', "iTerm2: not shown");

    /* kitty's APC: swallowed */
    feed(v, "\x1b[H\x1b[2J\x1b_Gf=100,a=T;iVBORw0KGgo=\x1b\\K");
    CHECK(cell(v, 0, 0, 0) == 'K', "kitty APC swallowed");

    /* the picture through a resize (the text flows): its cells and marks */
    feed(v, "\x1b[H\x1b[2J\x1b[3Jab");
    s = slurp(GEN "bars 16 32", &n);
    feedn(v, s, n);
    vt_resize(v, 10, 6);
    for (r = 0, c = 0; r < 6; r++)
        for (x = 0; x < 10; x++)
            if (VT_IS_PIC(cell(v, r, x, 0))) {
                c++;
                CHECK(row_pics(v, r, 0) && vt_pic(v, cell(v, r, x, 0)),
                      "reflow keeps marks and pictures");
            }
    CHECK(c == 4, "reflow: all four tiles");
    free(s);

    /* img2sixel's own output, when it is there */
    if (!system("which img2sixel >/dev/null 2>&1")) {
        Vt *w = vt_new(80, 24, 100);
        vt_set_pictures(w, 1, 4L << 20);
        vt_set_cell_px(w, 8, 16, 0);
        s = slurp("python3 tests/pictures/mksixel.py ppm 64 48 > /tmp/pic_test2.ppm && "
                  "img2sixel /tmp/pic_test2.ppm", &n);
        feedn(w, s, n);
        if (!n)
            printf("img2sixel wrote nothing here: its test skipped\n");
        else {
            const VtPic *p = vt_pic(w, cell(w, 0, 0, 0));
            CHECK(p && p->w == 64 && p->h == 48, "img2sixel: its size");
            CHECK(pix(w, 0, 0, 1, 1) == 0xFFFFFF && pix(w, 0, 7, 7, 7) == 0,
                  "img2sixel: white and black bars");
        }
        free(s);
        vt_free(w);
    }

    vt_free(v);
    printf(fails ? "pictures: %d failed\n" : "pictures: all passed\n", fails);
    return fails != 0;
}
