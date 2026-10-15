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
long zinflate(const uint8_t *in, long n, uint8_t *out, long max);  /* inflate.c */

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

    /* ---- sixel's aspect: P1 (none: 2:1, 7-9: 1:1) and "Pan;Pad ---- */
    {
        static const struct { const char *args; int h; const char *msg; } as[] = {
            { "aspect 16 12 0 none", 24, "sixel: no P1, no raster: 2:1" },
            { "aspect 16 12 7 none", 12, "sixel: P1=7: 1:1" },
            { "aspect 16 12 2 none", 60, "sixel: P1=2: 5:1" },
            { "aspect 16 12 0 '2;1'", 24, "sixel: raster 2;1 (Pv=12 rows of 2)" },
            { "aspect 16 12 0 '1;1'", 12, "sixel: raster 1;1 says again" },
            { "aspect 16 12 9 '3;1'", 36, "sixel: raster 3;1 over P1=9" },
        };
        char cmd[128];
        int i;
        for (i = 0; i < (int)(sizeof as / sizeof as[0]); i++) {
            const VtPic *p;
            feed(v, "\x1b[H\x1b[2J\x1b[3J");
            snprintf(cmd, sizeof cmd, GEN "%s", as[i].args);
            s = slurp(cmd, &n);
            feedn(v, s, n);
            p = vt_pic(v, cell(v, 0, 0, 0));
            CHECK(p && p->w == 16 && p->h == as[i].h, as[i].msg);
            if (p && as[i].h == 24)     /* each sixel row two pixel rows */
                CHECK(pix(v, 0, 0, 0, 0) == 0xFFFFFF && pix(v, 0, 0, 0, 11) == 0xFFFFFF &&
                      pix(v, 0, 1, 7, 15) == 0 && pix(v, 1, 1, 7, 7) == 0,
                      "sixel 2:1: the pixels doubled down");
            free(s);
        }
    }

    /* ---- iTerm2's MultipartFile: as one File= ---- */
    feed(v, "\x1b[H\x1b[2J\x1b[3J");
    s = slurp("python3 tests/pictures/mksixel.py multipart /tmp/pic_test.ppm 'width=2' 50", &n);
    feedn(v, s, n);
    vt_cursor(v, &x, &y, &vis);
    {
        const VtPic *p = vt_pic(v, cell(v, 0, 0, 0));
        CHECK(p && p->w == 16 && p->h == 16 && x == 0 && y == 1,
              "MultipartFile: as File=, 2 cells, the cursor below");
        CHECK(pix(v, 0, 0, 0, 0) == 0xFFFFFF && pix(v, 0, 1, 7, 0) == 0x000000,
              "MultipartFile: pixels");
    }
    free(s);
    feed(v, "\x1b[H\x1b[2J\x1b]1337;MultipartFile=inline=1\x07"
            "\x1b]1337;FilePart=AAAA\x07X\x1b]1337;FileEnd\x07Y");
    CHECK(cell(v, 0, 0, 0) == 'X' && cell(v, 0, 1, 0) == 'Y' && vt_pic_bytes(v) == 0,
          "MultipartFile: text between, a broken one: nothing");

    /* ---- inflate.c against Python's zlib: stored, fixed and dynamic
     * blocks (levels 0, 1, 9), several blocks ---- */
    {
        static const int lv[3] = { 0, 1, 9 };
        int i;
        for (i = 0; i < 3; i++) {
            char cmd[400];
            long zn, rn, got;
            char *z, *raw;
            uint8_t *out;
            static const char data[] = "import random,sys;random.seed(5);"
                "d=bytes(random.choice(b'abcdefgh  \\n') for _ in range(150000))+bytes(range(256))*40;";
            snprintf(cmd, sizeof cmd, "python3 -c \"%s import zlib;sys.stdout.buffer.write(zlib.compress(d,%d))\"",
                     data, lv[i]);
            z = slurp(cmd, &zn);
            snprintf(cmd, sizeof cmd, "python3 -c \"%s sys.stdout.buffer.write(d)\"", data);
            raw = slurp(cmd, &rn);
            out = malloc(rn + 16);
            got = zinflate((uint8_t *)z, zn, out, rn);
            CHECK(rn > 150000 && got == rn && !memcmp(out, raw, rn), "inflate: as zlib's");
            CHECK(zinflate((uint8_t *)z, zn, out, rn - 1) == -1, "inflate: more than max: -1");
            CHECK(zinflate((uint8_t *)z, zn / 2, out, rn) == -1, "inflate: cut short: -1");
            free(z);
            free(raw);
            free(out);
        }
    }

    /* ---- kitty's graphics ---- */
    {
        Vt *k = vt_new(20, 6, 100);
        const VtPic *p, *q;
        char kc[256];
        vt_inflate = zinflate;
        vt_set_pictures(k, 1, 1L << 20);
        vt_set_cell_px(k, 8, 16, 0x102030);
        /* icat's question, then DA1: the answer comes first */
        reply_is(k, "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c",
                 "\x1b_Gi=31;OK\x1b\\\x1b[?62;4;22c", "kitty: a=q answered before DA1");
        CHECK(vt_pic_count(k) == 0 && vt_pic_bytes(k) == 0, "kitty: a=q shows nothing");
        reply_is(k, "\x1b_Gi=32,s=1,v=1,a=q,t=f,f=24;L3RtcC94\x1b\\",
                 "\x1b_Gi=32;EBADF:only data in the code (t=d)\x1b\\",
                 "kitty: a file (t=f) refused");
        reply_is(k, "\x1b_Gi=33,s=1,v=1,a=q,f=24,o=z;AAAA\x1b\\",
                 "\x1b_Gi=33;EINVAL:broken zlib data\x1b\\", "kitty: broken zlib");
        /* a=T f=24 with an id: OK, 2x2 cells, the cursor below */
        s = slurp(GEN "kitty a=T,i=5 24 16 32", &n);
        feed(k, "\x1b[2;3H");
        reply_is(k, s, "\x1b_Gi=5;OK\x1b\\", "kitty: a=T,i=5 OK");
        vt_cursor(k, &x, &y, &vis);
        p = vt_pic(k, cell(k, 1, 2, 0));
        CHECK(p && p->w == 16 && p->h == 32 && VT_IS_PIC(cell(k, 2, 3, 0)) &&
              !VT_IS_PIC(cell(k, 1, 4, 0)) && x == 2 && y == 3,
              "kitty f=24: 2x2 cells at the cursor, the cursor below");
        CHECK(pix(k, 1, 2, 0, 0) == 0xFFFFFF && pix(k, 1, 2, 2, 5) == 0xFFFF00 &&
              pix(k, 2, 3, 7, 15) == 0x000000, "kitty f=24: pixels");
        CHECK(p && p->kept && vt_pic_count(k) == 1, "kitty: kept by its id, shown itself");
        free(s);
        /* f=32, zlib'ed, in chunks of 64; no id: no answer; the last bar
         * see-through: the background */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=T 32 16 16 64 z", &n);
        reply_is(k, s, "", "kitty: no id, no answer");
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 16 && p->h == 16 && pix(k, 0, 0, 0, 0) == 0xFFFFFF &&
              pix(k, 0, 1, 0, 3) == 0xFF00FF && pix(k, 0, 1, 2, 3) == 0xFF0000 &&
              pix(k, 0, 1, 7, 7) == 0x102030,
              "kitty f=32 o=z in chunks: pixels, alpha over the background");
        free(s);
        /* the same in chunks of 4 as it comes in pieces of 37 */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=T 32 16 16 4 z", &n);
        feedn(k, s, n);
        CHECK(VT_IS_PIC(cell(k, 0, 1, 0)) && pix(k, 0, 1, 0, 3) == 0xFF00FF,
              "kitty: tiny chunks");
        free(s);
        /* a=t keeps it unseen; a=p shows it, c x r cells (stretched);
         * first d=A: i=5 is no longer kept */
        feed(k, "\x1b_Ga=d,d=A\x1b\\");
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=t,i=7 24 16 32", &n);
        reply_is(k, s, "\x1b_Gi=7;OK\x1b\\", "kitty: a=t OK");
        free(s);
        CHECK(cell(k, 0, 0, 0) == 0 && vt_pic_count(k) == 1 &&
              vt_pic_bytes(k) == 16 * 32 * 3, "kitty: a=t kept, not shown");
        reply_is(k, "\x1b_Ga=p,i=7,c=4,r=1\x1b\\", "\x1b_Gi=7;OK\x1b\\", "kitty: a=p OK");
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 32 && p->h == 16 && VT_IS_PIC(cell(k, 0, 3, 0)) &&
              !VT_IS_PIC(cell(k, 0, 4, 0)) && !VT_IS_PIC(cell(k, 1, 0, 0)),
              "kitty: a=p c=4 r=1: 4x1 cells");
        CHECK(pix(k, 0, 0, 0, 0) == 0xFFFFFF && pix(k, 0, 3, 7, 15) == 0,
              "kitty: a=p stretched pixels");
        feed(k, "\r\n");
        reply_is(k, "\x1b_Ga=p,i=7,x=8,y=0,w=8,h=16\x1b\\", "\x1b_Gi=7;OK\x1b\\",
                 "kitty: a=p a part");
        vt_cursor(k, &x, &y, &vis);
        p = vt_pic(k, cell(k, 1, 0, 0));
        CHECK(p && p->w == 8 && p->h == 16 && pix(k, 1, 0, 0, 0) == 0xFF00FF,
              "kitty: a=p x,y,w,h: its part (magenta's bar first)");
        reply_is(k, "\x1b_Ga=p,i=7,C=1\x1b\\", "\x1b_Gi=7;OK\x1b\\", "kitty: a=p C=1");
        {
            int x2, y2;
            vt_cursor(k, &x2, &y2, &vis);
            CHECK(x2 == x && y2 == y && VT_IS_PIC(cell(k, y, 0, 0)) &&
                  vt_pic(k, cell(k, y, 0, 0))->kept, "kitty: C=1 the cursor stays; itself shown");
        }
        reply_is(k, "\x1b_Ga=p,i=99\x1b\\", "\x1b_Gi=99;ENOENT:no such image\x1b\\",
                 "kitty: a=p of none");
        reply_is(k, "\x1b_Ga=p,i=99,q=2\x1b\\", "", "kitty: q=2 quiet");
        reply_is(k, "\x1b_Ga=p,i=7,q=1\x1b\\", "", "kitty: q=1 no OK");
        /* deleted: d=i its pictures (kept), d=I the kept one too */
        feed(k, "\x1b_Ga=d,d=i,i=7\x1b\\");
        for (r = 0, c = 0; r < 6; r++)
            for (x = 0; x < 20; x++)
                c += VT_IS_PIC(cell(k, r, x, 0));
        CHECK(c == 0 && vt_pic_count(k) == 1, "kitty: d=i: none shown, kept");
        reply_is(k, "\x1b_Ga=p,i=7\x1b\\", "\x1b_Gi=7;OK\x1b\\", "kitty: shown again");
        feed(k, "\x1b_Ga=d,d=I,i=7\x1b\\");
        CHECK(vt_pic_count(k) == 0 && vt_pic_bytes(k) == 0, "kitty: d=I: all gone");
        reply_is(k, "\x1b_Ga=p,i=7\x1b\\", "\x1b_Gi=7;ENOENT:no such image\x1b\\",
                 "kitty: d=I: no longer kept");
        /* d=a: kitty's on the screen go, a sixel stays */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "bars 16 16", &n);
        feedn(k, s, n);
        free(s);
        s = slurp(GEN "kitty a=T,q=2 24 16 16", &n);
        feedn(k, s, n);
        free(s);
        CHECK(VT_IS_PIC(cell(k, 0, 0, 0)) && VT_IS_PIC(cell(k, 1, 0, 0)), "two pictures");
        feed(k, "\x1b_Ga=d\x1b\\");
        CHECK(VT_IS_PIC(cell(k, 0, 0, 0)) && !VT_IS_PIC(cell(k, 1, 0, 0)) &&
              vt_pic_count(k) == 1, "kitty: d=a: kitty's gone, the sixel stays");
        /* d=c at the cursor's cell, d=p at a cell */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=T,C=1 24 16 16", &n);
        feedn(k, s, n);
        feed(k, "\x1b_Ga=d,d=p,x=2,y=1\x1b\\");
        CHECK(!VT_IS_PIC(cell(k, 0, 0, 0)), "kitty: d=p at cell 2;1");
        feedn(k, s, n);
        feed(k, "\x1b_Ga=d,d=c\x1b\\");
        CHECK(!VT_IS_PIC(cell(k, 0, 0, 0)), "kitty: d=c at the cursor");
        free(s);
        /* f=100: a file through the decoder (the tests': a PPM) */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kittyfile a=T,i=9,c=2 /tmp/pic_test.ppm 100", &n);
        reply_is(k, s, "\x1b_Gi=9;OK\x1b\\", "kitty f=100 OK");
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 16 && p->h == 16 && pix(k, 0, 0, 0, 0) == 0xFFFFFF,
              "kitty f=100: c=2 in shape");
        free(s);
        /* the same id again: the new one kept */
        s = slurp(GEN "kitty a=t,i=9 24 8 8", &n);
        feedn(k, s, n);
        free(s);
        feed(k, "\x1b[5;1H\x1b_Ga=p,i=9\x1b\\");
        p = vt_pic(k, cell(k, 4, 0, 0));
        CHECK(p && p->w == 8 && p->h == 8, "kitty: an id sent again: the new image");
        /* I= alone: an id given back */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=t,I=3 24 8 8", &n);
        reply_is(k, s, "\x1b_Gi=1073741825,I=3;OK\x1b\\", "kitty: I= gets an id");
        free(s);
        reply_is(k, "\x1b_Ga=p,I=3\x1b\\", "\x1b_Gi=1073741825,I=3;OK\x1b\\", "kitty: a=p by I=");
        /* nonsense: swallowed, no text, no picture */
        feed(k, "\x1b[H\x1b[2J\x1b[3J\x1b_Gzz,a=Q,=,,;;;\x1b\\A\x1b_Hello\x1b\\B"
                "\x1b_Ga=T,f=24,s=99999,v=1;AAAA\x1b\\C\x1b_Ga=T,f=7;AAAA\x1b\\D");
        CHECK(cell(k, 0, 0, 0) == 'A' && cell(k, 0, 1, 0) == 'B' &&
              cell(k, 0, 2, 0) == 'C' && cell(k, 0, 3, 0) == 'D' && cell(k, 0, 4, 0) == 0,
              "kitty: nonsense swallowed");
        /* unicode placeholders (U=1): refused, not shown; kept by its id */
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "kitty a=T,U=1,i=40 24 8 8", &n);
        reply_is(k, s, "\x1b_Gi=40;EINVAL:no unicode placeholders\x1b\\", "kitty: U=1 refused");
        free(s);
        CHECK(!VT_IS_PIC(cell(k, 0, 0, 0)), "kitty: U=1 not shown");
        reply_is(k, "\x1b_Ga=p,i=40,U=1\x1b\\", "\x1b_Gi=40;EINVAL:no unicode placeholders\x1b\\",
                 "kitty: a=p U=1 refused");
        reply_is(k, "\x1b_Ga=p,i=40\x1b\\", "\x1b_Gi=40;OK\x1b\\", "kitty: then shown without U");
        /* too big for the cap: refused, nothing kept */
        vt_set_pictures(k, 1, 10000);
        s = slurp(GEN "kitty a=T,i=4 24 64 64", &n);
        snprintf(kc, sizeof kc, "%s", "");
        reply_is(k, s, "\x1b_Gi=4;ENOMEM:too big\x1b\\", "kitty: over the cap: ENOMEM");
        free(s);
        feed(k, "\x1b[H\x1b[2J\x1b[3J\x1b_Ga=d,d=A\x1b\\");
        CHECK(vt_pic_bytes(k) == 0, "kitty: all memory back");
        /* pictures off: swallowed, not answered */
        vt_set_pictures(k, 0, 1L << 20);
        reply_is(k, "\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\Z", "", "kitty off: no answer");
        CHECK(cell(k, 0, 0, 0) == 'Z', "kitty off: swallowed");

        /* ---- a new letter size: the pictures scaled to it ---- */
        vt_set_pictures(k, 1, 1L << 20);
        feed(k, "\x1b[H\x1b[2J\x1b[3J");
        s = slurp(GEN "bars 16 32", &n);
        feedn(k, s, n);
        free(s);
        vt_set_cell_px(k, 16, 32, 0x102030);
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 32 && p->h == 64 && p->cw == 16 && p->orgb &&
              vt_pic_bytes(k) == 16 * 32 * 3 + 32 * 64 * 3,
              "new size 16x32: scaled 2x, the pixels as they came kept");
        CHECK(pix(k, 0, 0, 0, 0) == 0xFFFFFF && pix(k, 0, 0, 5, 0) == 0xFFFF00 &&
              pix(k, 1, 1, 15, 31) == 0 && pix(k, 1, 1, 0, 0) == 0xFF00FF,
              "new size: the bars twice as wide");
        printf("memory: 16x32 picture %ld bytes at 8x16, %ld at 16x32\n",
               16L * 32 * 3, vt_pic_bytes(k));
        vt_set_cell_px(k, 8, 8, 0x102030);    /* half as tall: in shape */
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 8 && p->h == 16 && vt_pic_bytes(k) == 16 * 32 * 3 + 8 * 16 * 3,
              "new size 8x8: in shape inside its box (8x16)");
        vt_set_cell_px(k, 8, 16, 0x102030);
        p = vt_pic(k, cell(k, 0, 0, 0));
        CHECK(p && p->w == 16 && p->h == 32 && !p->orgb && vt_pic_bytes(k) == 16 * 32 * 3 &&
              pix(k, 1, 1, 7, 15) == 0, "back: the pixels as they came, the copy gone");
        /* no room for the copy: its own pixels, cut at the new cell */
        vt_set_pictures(k, 1, 16 * 32 * 3 + 100);
        vt_set_cell_px(k, 16, 32, 0x102030);
        p = vt_pic(k, cell(k, 0, 0, 0));
        q = vt_pic(k, cell(k, 1, 1, 0));
        CHECK(p && p == q && p->w == 16 && p->cw == 16 && vt_pic_count(k) == 1 &&
              pix(k, 0, 0, 15, 31) == 0x000000 && pix(k, 0, 1, 0, 0) == 0xFE000000,
              "no room: as it is, cut at the new cell (nothing dropped)");
        vt_set_cell_px(k, 8, 16, 0x102030);
        vt_set_pictures(k, 1, 1L << 20);
        /* through the history too */
        feed(k, "\r\n\r\n\r\n\r\n\r\n\r\n");
        vt_set_cell_px(k, 16, 32, 0x102030);
        p = NULL;
        for (r = 0; r < 6 && !p; r++)
            if (VT_IS_PIC(cell(k, r, 0, vt_history(k))))
                p = vt_pic(k, cell(k, r, 0, vt_history(k)));
        CHECK(p && p->w == 32, "a picture in the history scaled too");
        vt_inflate = NULL;
        vt_free(k);
    }

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
