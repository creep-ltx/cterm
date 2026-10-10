/*
 * bench - the speed test in the Project menu.
 */
#include "cterm.h"

static LONG us_since(const struct timeval *a);
static void bench_line(const char *what, LONG us, LONG n);
static void bench_primitives(void);

/* The drawing tools themselves, in microseconds each: what a painter
 * should be built from on this machine. */
static LONG us_since(const struct timeval *a)
{
    struct timeval b;
    term_now(&b);
    return (LONG)(b.tv_secs - a->tv_secs) * 1000000 +
           ((LONG)b.tv_micro - (LONG)a->tv_micro);
}

static void bench_line(const char *what, LONG us, LONG n)
{
    char l[100];
    strcpy(l, "  ");
    strcat(l, what);
    while (strlen(l) < 34)
        strcat(l, " ");
    numstr_cat(l, n ? us / n : 0);
    strcat(l, " us each\r\n");
    say(l);
}

static void bench_primitives(void)
{
    struct timeval a;
    int i, x1 = ox + cols * cw - 1, y1 = oy + rows * ch - 1;
    static UBYTE row[256];
    ULONG *argb = NULL;
    LONG w = cols * cw, h = ch;
    for (i = 0; i < (int)sizeof row; i++)
        row[i] = 'A' + i % 26;
    say("\r\n\x1b[1m[the drawing tools, timed]\x1b[0m\r\n");
    refresh();
    planes_full();                      /* every plane: the honest cost */

    bpen(defbg);
    term_now(&a);
    for (i = 0; i < 50; i++)
        ScrollRaster(rp, 0, ch, ox, oy, x1, y1);
    bench_line("ScrollRaster, a line", us_since(&a), 50);

    term_now(&a);
    for (i = 0; i < 50; i++)
        ScrollRaster(rp, 0, ch * (rows / 4), ox, oy, x1, y1);
    bench_line("ScrollRaster, a quarter", us_since(&a), 50);

    apen(defbg);
    term_now(&a);
    for (i = 0; i < 50; i++)
        RectFill(rp, ox, oy, x1, y1);
    bench_line("RectFill, the whole area", us_since(&a), 50);

    term_now(&a);
    for (i = 0; i < 200; i++)
        RectFill(rp, ox, oy, x1, oy + ch - 1);
    bench_line("RectFill, one row", us_since(&a), 200);

    SetFont(rp, font);
    apen(deffg);
    bpen(defbg);
    SetDrMd(rp, JAM2);
    term_now(&a);
    for (i = 0; i < 200; i++) {
        Move(rp, ox, oy + font->tf_Baseline);
        Text(rp, (STRPTR)row, cols);
    }
    bench_line("Text, a row (Amiga font)", us_since(&a), 200);

    if (usett && ttface[0]) {
        static ULONG u[256];
        for (i = 0; i < 256; i++)
            u[i] = 'A' + i % 26;
        SetDrMd(rp, JAM1);
        term_now(&a);
        for (i = 0; i < 200; i++) {
            Move(rp, ox, oy + cbase);
            TT_Text(rp, (APTR)u, cols);
        }
        bench_line("TT_Text, a row (TrueType)", us_since(&a), 200);
        SetDrMd(rp, JAM2);
    }

    if (CyberGfxBase)
        argb = AllocVec(w * h * 4, MEMF_ANY);
    if (argb) {
        for (i = 0; i < w * h; i++)
            argb[i] = (i & 8) ? 0x00D8DEE9 : 0x001A1D23;
        term_now(&a);
        for (i = 0; i < 200; i++)
            WritePixelArray(argb, 0, 0, w * 4, rp, ox, oy, w, h, RECTFMT_ARGB);
        bench_line("WritePixelArray, a row (ARGB)", us_since(&a), 200);
        term_now(&a);
        for (i = 0; i < 200; i++) {
            int k;                      /* the CPU building a row */
            for (k = 0; k < w * h; k++)
                argb[k] = (k & 8) ? 0x00D8DEE9 : 0x001A1D23;
        }
        bench_line("filling a row in memory (CPU)", us_since(&a), 200);
        FreeVec(argb);
    } else
        say("  (no cybergraphics: no WritePixelArray)\r\n");
    redraw_all();
}

/* Full redraws of a busy screen, timed: how fast this font draws here. */
void speed_test(void)
{
    struct DateStamp a, b;
    LONG ticks, i, r;
    char line[120], num[12];
    static const char sample[] =
        "\x1b[2J\x1b[H";
    say(sample);
    for (r = 0; r < rows - 1; r++) {
        int k;
        for (k = 0; k < cols - 1; k++) {
            char cc[24];
            int col = (k + r) % 8;
            if (k % 10 == 0) {
                strcpy(cc, "\x1b[3");      /* ESC [ 3, then the colour */
                cc[3] = '0' + (col ? col : 7);
                cc[4] = 'm';
                cc[5] = 0;
                say(cc);
            }
            cc[0] = "The quick brown fox jumps over the lazy dog "[(k + r) % 44];
            cc[1] = 0;
            say(cc);
        }
        say("\x1b[0m\r\n");
    }
    refresh();
    DateStamp(&a);
    for (i = 0; i < 10; i++)
        for (r = 0; r < rows; r++)
            draw_row(r);
    DateStamp(&b);
    ticks = (b.ds_Days - a.ds_Days) * 24 * 60 * 60 * 50 +
            (b.ds_Minute - a.ds_Minute) * 60 * 50 + (b.ds_Tick - a.ds_Tick);
    strcpy(line, "\r\n\x1b[1m[speed: 10 full screens of ");
    numstr_cat(line, cols); strcat(line, "x"); numstr_cat(line, rows);
    strcat(line, usett ? " in TrueType: " : " in a bitmap font: ");
    numstr_cat(line, ticks * 20);
    strcat(line, " ms, ");
    numstr_cat(line, ticks * 2);
    strcat(line, " ms each]\x1b[0m\r\n");
    (void)num;
    say(line);
    bench_primitives();
}
