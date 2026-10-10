/* CTermTour - a tour of what CTerm does, page by page, in the Shell of a
 * CTerm window: styles, colours, lines and blocks, characters, cursor
 * shapes, a Boing ball drawn in 24-bit colour, and a race against the
 * Amiga's own CON: window.
 *
 *   CTermTour [SECS n] [PAGE n]    Space/Return/Right: next page, B/Left:
 *                                  back, Esc (any other key): the end.
 *                                  Pages turn by themselves after SECS
 *                                  seconds (8; 0: never).
 *   CTermTour ... RACEWIN CON:x/y/w/h/title   where the race's CON: opens
 *   CTermTour FLOOD [LINES n]      the race's text, timed (the tour runs
 *                                  this in a CON: window beside CTerm)
 *
 * The text is UTF-8: the tour switches the window to it (ESC % G) and back
 * to the Amiga's Latin-1 (ESC % @) when it ends. Built by `make tour`. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static const char version[] = "$VER: CTermTour 1.0 (10.10.2026)";

static BPTR out, in;
static int cols = 80, rows = 24;
static LONG secs = 8;

/* ---- output, in big writes --------------------------------------------- */

static char ob[16384];
static int on;

static void flush(void)
{
    if (on) {
        Write(out, ob, on);
        on = 0;
    }
}

static void put(const char *s)
{
    int n = strlen(s);
    if (on + n > (int)sizeof ob)
        flush();
    memcpy(ob + on, s, n);
    on += n;
}

static void putf(const char *fmt, ...)
{
    char b[600];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    put(b);
}

static void at(int r, int c) { putf("\x1b[%d;%dH", r, c); }
static void fg24(int r, int g, int b) { putf("\x1b[38;2;%d;%d;%dm", r, g, b); }
static void bg24(int r, int g, int b) { putf("\x1b[48;2;%d;%d;%dm", r, g, b); }
static void reset(void) { put("\x1b[0m"); }

/* a time in 50ths of a second */
static LONG ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Minute * 60 * 50) + d.ds_Tick;
}

/* ---- keys --------------------------------------------------------------- */

enum { K_NEXT, K_BACK, K_END, K_NONE, K_RACE };

/* a key within us microseconds (0: none waiting) */
static int key_wait(LONG us)
{
    UBYTE c;
    flush();
    if (!WaitForChar(in, us))
        return K_NONE;
    if (Read(in, &c, 1) != 1)
        return K_END;
    if (c == 0x9B || c == 0x1B) {       /* an arrow: CSI C / CSI D */
        UBYTE d = 0;
        if (c == 0x1B && WaitForChar(in, 20000)) {
            Read(in, &d, 1);
            if (d != '[')
                return K_END;
        } else if (c == 0x1B)
            return K_END;               /* Esc on its own */
        while (WaitForChar(in, 20000) && Read(in, &d, 1) == 1)
            if (d >= 0x40 && d <= 0x7E)
                break;
        return d == 'C' ? K_NEXT : d == 'D' ? K_BACK : K_END;
    }
    if (c == ' ' || c == '\r' || c == '\n' || c == 'n' || c == 'N')
        return K_NEXT;
    if (c == 'b' || c == 'B')
        return K_BACK;
    if (c == 'r' || c == 'R')
        return K_RACE;
    return K_END;
}

/* the window's size, asked of the console (the Amiga's window bounds report) */
static void ask_size(void)
{
    UBYTE b[32];
    int n = 0, i, num[4] = { 0, 0, 0, 0 }, k = 0;
    Write(out, "\x9b" "0 q", 4);
    while (n < (int)sizeof b - 1 && WaitForChar(in, 300000)) {
        if (Read(in, b + n, 1) != 1)
            break;
        if (b[n++] == 'r')
            break;
    }
    for (i = 0; i < n && k < 4; i++) {
        if (b[i] >= '0' && b[i] <= '9')
            num[k] = num[k] * 10 + b[i] - '0';
        else if (b[i] == ';')
            k++;
    }
    if (k == 3 && num[2] >= 20 && num[3] >= 60) {
        rows = num[2];
        cols = num[3];
    }
}

/* ---- the frame of every page -------------------------------------------- */

static int page, npages;

static void status(const char *title)
{
    char l[300];
    const char *keys = "Space: next  B: back  Esc: end";
    int n, k;
    sprintf(l, " Tour %d/%d  %s", page + 1, npages, title);
    n = strlen(l);
    k = strlen(keys);
    at(rows, 1);
    put("\x1b[0;7m");
    put(l);
    if (n + 2 + k + 1 < cols) {         /* the keys, if there is room */
        while (n < cols - 1 - k - 1) {
            put(" ");
            n++;
        }
        put(keys);
        put(" ");
    }
    reset();
}

static void clear(void)
{
    put("\x1b[0m\x1b[H\x1b[2J");
}

static void heading(const char *h, const char *sub)
{
    at(1, 2);
    put("\x1b[1m");
    put(h);
    reset();
    if (sub) {
        at(2, 2);
        put("\x1b[2m");
        put(sub);
        reset();
    }
}

/* a box of light lines, inside: rows r0..r1, columns c0..c1 */
static void box(int r0, int c0, int r1, int c1, const char *tl, const char *h,
                const char *tr, const char *v, const char *bl, const char *br)
{
    int r, c;
    at(r0, c0);
    put(tl);
    for (c = c0 + 1; c < c1; c++)
        put(h);
    put(tr);
    for (r = r0 + 1; r < r1; r++) {
        at(r, c0);
        put(v);
        at(r, c1);
        put(v);
    }
    at(r1, c0);
    put(bl);
    for (c = c0 + 1; c < c1; c++)
        put(h);
    put(br);
}

/* ---- the pages ------------------------------------------------------------ */

/* CTerm in big letters: five rows of a 4x5 block font */
static const char *logo[5] = {
    " ███ █████ ████ ████  █   █",
    "█      █   █    █   █ ██ ██",
    "█      █   ███  ████  █ █ █",
    "█      █   █    █  █  █   █",
    " ███   █   ████ █   █ █   █",
};

static void p_title(void)
{
    int r, i, c0 = (cols - 27) / 2;
    for (r = 0; r < 5; r++) {
        const unsigned char *s = (const unsigned char *)logo[r];
        at(3 + r, c0);
        /* a 24-bit sunset down the letters, a colour per column */
        for (i = 0; *s; i++) {
            int n = (*s & 0x80) ? 3 : 1;
            char ch[4];
            memcpy(ch, s, n);
            ch[n] = 0;
            fg24(255 - i * 4, 80 + i * 6, 120 + i * 4);
            put(ch);
            s += n;
        }
        reset();
    }
    at(9, (cols - 34) / 2);
    put("a modern terminal for AmigaOS 3.2");
    at(11, (cols - 56) / 2);
    put("\x1b[1mbold\x1b[0m  \x1b[2mfaint\x1b[0m  \x1b[3mitalic\x1b[0m  "
        "\x1b[4munderline\x1b[0m  \x1b[7minverse\x1b[0m  \x1b[9mstrike\x1b[0m  "
        "\x1b[5mblink\x1b[0m");
    c0 = (cols - 62) / 2;
    box(13, c0, 19, c0 + 61, "┌", "─", "┐", "│", "└", "┘");
    at(14, c0 + 2);
    put("\x1b[1;36mspeaks\x1b[0m   VT100, xterm, the Amiga's own console");
    at(15, c0 + 2);
    put("\x1b[1;36mcolours\x1b[0m  2 to 16 million: 16, 256 and 24-bit codes");
    at(16, c0 + 2);
    put("\x1b[1;36mletters\x1b[0m  Amiga fonts, TrueType, Latin-1, UTF-8, the PC's");
    at(17, c0 + 2);
    put("\x1b[1;36mcalls\x1b[0m    telnet, ssh, BBSes, Zmodem, Hydra, a phone book");
    at(18, c0 + 2);
    put("\x1b[1;36mfast\x1b[0m     letters painted in 68020 assembly - race on p.11");
    at(21, (cols - 40) / 2);
    put("\x1b[2ma tour of what CTerm does - Space: on\x1b[0m");
}

static void p_styles(void)
{
    static const struct { const char *code, *name; } st[] = {
        { "1", "bold" }, { "2", "faint" }, { "3", "italic" },
        { "4", "underline" }, { "5", "blink (a bright background)" },
        { "7", "inverse" }, { "8", "hidden" }, { "9", "strike" },
        { "1;4", "bold underline" }, { "3;9", "italic strike" },
        { "1;31", "bold red" }, { "4;32;44", "underline, green on blue" },
        { "7;33", "inverse yellow" }, { "2;36", "faint cyan" },
    };
    int i;
    heading("Text styles", "what a program asks for with ESC [ n m (SGR)");
    for (i = 0; i < (int)(sizeof st / sizeof st[0]); i++) {
        at(4 + i, 3);
        putf("\x1b[2mSGR %-8s\x1b[0m \x1b[%sm%s\x1b[0m", st[i].code, st[i].code,
             st[i].name);
        if (!strcmp(st[i].code, "8"))
            put("  \x1b[2m<- there, hidden\x1b[0m");
    }
    at(4, 46);
    put("\x1b[2mSome come from the font\x1b[0m");
    at(5, 46);
    put("\x1b[2m(bold, italic), CTerm draws\x1b[0m");
    at(6, 46);
    put("\x1b[2mthe rest into each letter's\x1b[0m");
    at(7, 46);
    put("\x1b[2mpicture: once, then reused.\x1b[0m");
}

static void p_colours(void)
{
    int i, r, x;
    heading("Colours", "16, 256 and 16 million: CTerm finds the nearest pen on any screen");
    at(4, 3);
    put("16  ");
    for (i = 0; i < 16; i++)
        putf("\x1b[%d;%dm%2d \x1b[0m", i < 8 ? 30 + (i == 0 ? 7 : 0) : 90,
             i < 8 ? 40 + i : 100 + i - 8, i);
    /* the 6x6x6 cube: two colours a cell with an upper half block */
    at(6, 3);
    put("256");
    for (r = 0; r < 3; r++) {
        at(6 + r, 8);
        for (x = 0; x < 36; x++) {
            int top = 16 + (r * 2) * 36 + x, bot = 16 + (r * 2 + 1) * 36 + x;
            putf("\x1b[38;5;%d;48;5;%dm▀", top, bot);
        }
        reset();
    }
    at(9, 8);
    for (i = 232; i < 256; i++)
        putf("\x1b[48;5;%dm ", i);
    reset();
    at(11, 3);
    put("24-bit");
    for (r = 0; r < 4; r++) {
        at(11 + r, 10);
        for (x = 0; x < cols - 14; x++) {
            /* a rainbow left to right, light to dark top to bottom */
            int h = x * 1536 / (cols - 14), p, t, R, G, B, k;
            for (k = 0; k < 2; k++) {
                int l = 255 - (r * 2 + k) * 28;
                p = h / 256;
                t = h % 256;
                R = p == 0 || p == 5 ? 255 : p == 1 ? 255 - t : p == 4 ? t : 0;
                G = p == 0 ? t : p == 1 || p == 2 ? 255 : p == 3 ? 255 - t : 0;
                B = p == 2 ? t : p == 3 || p == 4 ? 255 : p == 5 ? 255 - t : 0;
                if (k == 0)
                    fg24(R * l / 255, G * l / 255, B * l / 255);
                else
                    bg24(R * l / 255, G * l / 255, B * l / 255);
            }
            put("▀");
        }
        reset();
    }
    at(16, 3);
    put("\x1b[2mOn a 16-colour Workbench the pens are shared out; on a graphics\x1b[0m");
    at(17, 3);
    put("\x1b[2mcard every colour is itself. Settings > Theme: 12 themes, or your own.\x1b[0m");
}

static void p_lines(void)
{
    int i, c;
    heading("Lines and blocks", "box drawing, blocks and shades - drawn by CTerm, any font");
    box(4, 3, 8, 20, "┌", "─", "┐", "│", "└", "┘");
    at(6, 6);
    put("light");
    box(4, 23, 8, 40, "┏", "━", "┓", "┃", "┗", "┛");
    at(6, 26);
    put("heavy");
    box(4, 43, 8, 60, "╔", "═", "╗", "║", "╚", "╝");
    at(6, 46);
    put("double");
    at(10, 3);
    put("┌──────┬──────┐  ╔══════╦══════╗");
    at(11, 3);
    put("│ \x1b[32mOK\x1b[0m   │ \x1b[31mFAIL\x1b[0m │  ║ ░░░░ ║ ▓▓▓▓ ║");
    at(12, 3);
    put("├──────┼──────┤  ╠══════╬══════╣");
    at(13, 3);
    put("│ 1.2s │ 0.3s │  ║ ▒▒▒▒ ║ ████ ║");
    at(14, 3);
    put("└──────┴──────┘  ╚══════╩══════╝");
    /* a bar chart of eighths */
    at(10, 40);
    put("\x1b[2mthe CPU, last minute\x1b[0m");
    for (c = 0; c < 30; c++) {
        static const char *e[9] = { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
        int v = (c * 7 + (c * c) % 11) % 24, row;
        for (row = 0; row < 3; row++) {
            int level = v - (2 - row) * 8;
            at(11 + row, 40 + c);
            fg24(80 + c * 5, 200 - c * 4, 120);
            put(level >= 8 ? e[8] : level > 0 ? e[level] : " ");
        }
    }
    reset();
    at(16, 3);
    put("DEC line graphics (ESC ( 0), as vt100 programs draw: ");
    put("\x1b(0lqqwqqk tqqnqqu mqqvqqj\x1b(B");
    at(18, 3);
    for (i = 0; i < 4; i++) {
        static const char *s[4] = { "░", "▒", "▓", "█" };
        int k;
        fg24(90 + i * 50, 140, 255 - i * 40);
        for (k = 0; k < 6; k++)
            put(s[i]);
    }
    reset();
    put("  \x1b[2mshades: dots, as the PC's font has them\x1b[0m");
}

static void p_chars(void)
{
    heading("Characters", "Latin-1 (the Amiga's own), UTF-8, and the PC's code page");
    at(4, 3);
    put("\x1b[1mSvenska\x1b[0m   Räksmörgås, Åsa och Örjan på ön      \x1b[1mDeutsch\x1b[0m  Grüße, Straße");
    at(5, 3);
    put("\x1b[1mFrançais\x1b[0m  garçon, crème brûlée, œuvre            \x1b[1mEspañol\x1b[0m  señor, ¿qué?");
    at(7, 3);
    put("\x1b[1mGreek\x1b[0m     αβγδε ΑΒΓΔ λόγος                       \x1b[1mCyrillic\x1b[0m Привет, мир");
    at(8, 3);
    put("\x1b[1mArrows\x1b[0m    ← ↑ → ↓ ↔ ⇐ ⇒                         \x1b[1mMaths\x1b[0m    ∑ √ ∞ ≠ ≤ ≥ ± π");
    at(9, 3);
    put("\x1b[1mSymbols\x1b[0m   ♠ ♥ ♦ ♣ ★ ☺ ✓ ✗ • … € £ © ®");
    at(11, 3);
    put("\x1b[1mWide\x1b[0m      中文 日本語 한국어   \x1b[2m(two cells each, as a PC terminal)\x1b[0m");
    at(13, 3);
    put("\x1b[2mWith a TrueType font every letter the font has is drawn; an Amiga\x1b[0m");
    at(14, 3);
    put("\x1b[2mbitmap font shows its Latin-1 and stands in for the rest.\x1b[0m");
    at(16, 3);
    put("\x1b[1mThe PC\x1b[0m    CTerm PC: code page 437, the PC's 16 colours and the");
    at(17, 3);
    put("          IBM VGA 8x16 font, for ANSI art on PC BBSes:");
    at(18, 13);
    put("\x1b[1;33;44m ░▒▓█ \x1b[0;1;37;41m ╔═╗ \x1b[0;30;46m ▄▀▄ \x1b[0;1;35;40m ♫ ☼ ♦ \x1b[0m");
}

/* cursor shapes: changed every second while the page is up */
static int p_cursor_anim(int n)
{
    static const char *shape[6] = { "\x1b[2 q", "\x1b[4 q", "\x1b[6 q",
                                    "\x1b[1 q", "\x1b[3 q", "\x1b[5 q" };
    static const char *name[6] = { "block", "underline", "bar",
                                   "block, blinking", "underline, blinking",
                                   "bar, blinking" };
    put(shape[n % 6]);
    at(8, 3);
    putf("now: \x1b[1m%-22s\x1b[0m", name[n % 6]);
    at(10, 3);
    put("type here > ");
    return 0;
}

static void p_cursor(void)
{
    heading("Cursor shapes", "a program asks for block, underline or bar (DECSCUSR, ESC [ n SP q)");
    at(4, 3);
    put("vim's insert mode shows a bar, its normal mode a block. CTerm draws");
    at(5, 3);
    put("whichever is asked for, blinking or not, in the letter's colour.");
    at(13, 3);
    put("\x1b[2mThe Amiga's own Shell keeps CTerm's block cursor.\x1b[0m");
}

/* ---- the Boing ball, in 24-bit colour, with synchronized output --------- */

#define BW 26                           /* the ball's box in pixels */
#define BH 26
static signed char bu[BH][BW], bv[BH][BW];  /* texture coordinates, -1 outside */
static UBYTE bl[BH][BW];                     /* light, 0..255 */

/* made once, in whole numbers: a stock 68020 has no FPU */
static void ball_make(void)
{
    int x, y, r = BW / 2 - 1, cx = BW / 2, cy = BH / 2;
    for (y = 0; y < BH; y++)
        for (x = 0; x < BW; x++) {
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy, z;
            if (d2 > r * r) {
                bu[y][x] = -1;
                continue;
            }
            /* z = sqrt(r*r - d2), by steps */
            for (z = 0; (z + 1) * (z + 1) <= r * r - d2; z++)
                ;
            /* longitude from x over the visible width at this height,
             * latitude from y: 8 squares around, 8 down */
            {
                int w = 0, lon, lat;
                while ((w + 1) * (w + 1) <= r * r - dy * dy)
                    w++;
                lon = w ? (dx + w) * 8 / (2 * w + 1) : 0;
                lat = (dy + r) * 8 / (2 * r + 1);
                bu[y][x] = (signed char)lon;
                bv[y][x] = (signed char)lat;
            }
            /* light from the upper left */
            {
                int l = (-dx * 5 - dy * 6 + z * 9) * 255 / (r * 12);
                l = l < 70 ? 70 : l > 255 ? 255 : l;
                /* four steps of light, not hundreds: on a 16-colour
                 * screen each new colour is a search for the nearest pen */
                bl[y][x] = 96 + (l - 70) * 4 / 186 * 53;
            }
        }
}

static int bx, by, ox = -100, oy = -100, frames;
static LONG t0;

static void pixel_bg(int px, int py, int *R, int *G, int *B)
{
    /* the Boing grey with its purple grid */
    if (px % 8 == 0 || py % 8 == 0) {
        *R = 150; *G = 40; *B = 160;
    } else {
        *R = 170; *G = 170; *B = 170;
    }
}

/* a colour code, quickly: no sprintf (on a 14 MHz 68020 it was a good
 * part of a frame) */
static char *num3(char *d, int v)
{
    if (v >= 100) *d++ = '0' + v / 100;
    if (v >= 10) *d++ = '0' + v / 10 % 10;
    *d++ = '0' + v % 10;
    return d;
}

static void col(int which, int r, int g, int b)    /* which: 38 or 48 */
{
    char e[24], *d = e;
    *d++ = 0x1b; *d++ = '[';
    *d++ = which == 38 ? '3' : '4'; *d++ = '8'; *d++ = ';'; *d++ = '2'; *d++ = ';';
    d = num3(d, r); *d++ = ';';
    d = num3(d, g); *d++ = ';';
    d = num3(d, b); *d++ = 'm';
    *d = 0;
    put(e);
}

/* what each cell of the field shows now: only changed cells are sent,
 * as a program animating in a terminal would (0: not known) */
#define FMAXR 20
#define FMAXC 80
static ULONG shtop[FMAXR][FMAXC], shbot[FMAXR][FMAXC];

/* the box of cells r0..r1, c0..c1 (pixels: two rows a cell) drawn again */
static void ball_cells(int r0, int r1, int c0, int c1, int top, int phase)
{
    int r, c;
    ULONG lf = ~0UL, lb = ~0UL;
    for (r = r0; r <= r1 && r < FMAXR; r++) {
        int skipped = 1;
        for (c = c0; c <= c1 && c < FMAXC; c++) {
            int k, R[2], G[2], B[2];
            ULONG pt, pb;
            for (k = 0; k < 2; k++) {
                int px = c, py = r * 2 + k, x = px - bx, y = py - by;
                if (x >= 0 && x < BW && y >= 0 && y < BH && bu[y][x] >= 0) {
                    int sq = ((bu[y][x] + phase) + bv[y][x]) & 1, l = bl[y][x];
                    R[k] = (sq ? 230 : 255) * l / 255;
                    G[k] = (sq ? 20 : 255) * l / 255;
                    B[k] = (sq ? 20 : 255) * l / 255;
                } else
                    pixel_bg(px, py, &R[k], &G[k], &B[k]);
            }
            pt = 0x1000000UL | R[0] << 16 | G[0] << 8 | B[0];
            pb = 0x1000000UL | R[1] << 16 | G[1] << 8 | B[1];
            if (shtop[r][c] == pt && shbot[r][c] == pb) {
                skipped = 1;
                continue;
            }
            if (skipped)
                at(top + r, 3 + c);
            skipped = 0;
            if (pt != lf) {
                col(38, R[0], G[0], B[0]);
                lf = pt;
            }
            if (pb != lb) {
                col(48, R[1], G[1], B[1]);
                lb = pb;
            }
            put("▀");
            shtop[r][c] = pt;
            shbot[r][c] = pb;
        }
    }
    reset();
}

static int fw, fh;                      /* the field, in cells */

static void p_boing(void)
{
    heading("Boing!", "24-bit colour, two pixels a cell, and synchronized output (mode 2026)");
    fw = cols - 6;
    fh = rows - 7;
    if (fh > 18)
        fh = 18;
    by = 2;
    bx = 4;
    frames = 0;
    memset(shtop, 0, sizeof shtop);     /* the screen was cleared */
    memset(shbot, 0, sizeof shbot);
    ball_cells(0, fh - 1, 0, fw - 1, 4, 0);
    ox = bx;                            /* what is on screen now */
    oy = by;
    t0 = ticks();
}

static int p_boing_anim(int n)
{
    static int dx = 2, phase;
    int r0, r1, c0, c1, ny, t = n % 40;
    LONG el;
    /* x bounces off the sides, y falls and rises */
    if (bx + dx < 0 || bx + dx + BW > fw)
        dx = -dx;
    bx += dx;
    phase = (phase + (dx > 0 ? 7 : 1)) & 7;
    ny = (fh * 2 - BH) * (t < 20 ? t * t : (40 - t) * (40 - t)) / 400;
    by = ny < 0 ? 0 : ny;
    put("\x1b[?2026h");                 /* begin the frame */
    c0 = bx < ox ? bx : ox;
    c1 = (bx > ox ? bx : ox) + BW - 1;
    r0 = (by < oy ? by : oy) / 2;
    r1 = ((by > oy ? by : oy) + BH - 1) / 2;
    if (ox < 0) {
        c0 = bx; c1 = bx + BW - 1; r0 = by / 2; r1 = (by + BH - 1) / 2;
    }
    if (c0 < 0) c0 = 0;
    if (c1 > fw - 1) c1 = fw - 1;
    if (r1 > fh - 1) r1 = fh - 1;
    ball_cells(r0, r1, c0, c1, 4, phase);
    ox = bx;
    oy = by;
    frames++;
    el = ticks() - t0;
    at(fh + 5, 3);
    if (el > 25)
        putf("\x1b[1mframe %5d\x1b[0m   %3ld frames a second   \x1b[2m(every frame is ~%d KB of escape codes)\x1b[0m",
             frames, frames * 50L / el, on / 1024 + 1);
    put("\x1b[?2026l");                 /* show it whole */
    return 0;
}

static void p_shell(void)
{
    heading("The Amiga Shell, at home", "CTerm is a console of its own: no CON:, no KingCON needed");
    at(4, 3);
    put("\x1b[1;32m1.Work:>\x1b[0m list \x1b[7m#?.info\x1b[0m       \x1b[2mTab completes names (a menu if several)\x1b[0m");
    at(5, 3);
    put("\x1b[1;32m1.Work:>\x1b[0m                    \x1b[2mUp/Down: the history, Ctrl+R searches it\x1b[0m");
    at(7, 3);
    put("- line editing: Shift/Ctrl+arrows by word, Ctrl+A/E/K/U/W, Alt keys");
    at(8, 3);
    put("- Ed, More, MultiView and friends work as in a CON: window");
    at(9, 3);
    put("- the text flows again when the window gets wider or narrower");
    at(10, 3);
    put("- scrollback: the wheel, the scroll bar, Shift+PageUp; search it");
    at(11, 3);
    put("- mark text with the mouse: drag, double-click a path, triple-click");
    at(12, 3);
    put("  a line; Right Amiga+C copies, Right Amiga+V pastes line by line");
    at(13, 3);
    put("- on Workbench, on a screen of its own, or full screen; a picture");
    at(14, 3);
    put("  behind the text (24-bit screens)");
    box(16, 3, 20, 58, "╭", "─", "╮", "│", "╰", "╯");
    at(17, 5);
    put("\x1b[1mTabs\x1b[0m: Right Amiga+T a new Shell, Right Amiga+1..9 to switch.");
    at(18, 5);
    put("A tab you are not looking at keeps running, and the window");
    at(19, 5);
    put("only draws the tab you see.");
}

static void p_calls(void)
{
    int c0 = 3;
    heading("Calling out", "telnet, ssh, BBSes - Project/Call... (Right Amiga+K) or the phone book");
    box(4, c0, 12, c0 + 56, "┌", "─", "┐", "│", "└", "┘");
    at(4, c0 + 2);
    put(" Phone book ");
    at(5, c0 + 2);
    put("\x1b[7m Particles BBS      particles.bbs.org     telnet  PC  \x1b[0m");
    at(6, c0 + 2);
    put(" Amiga Nexus        nexus.amiga.org       telnet  L1 ");
    at(7, c0 + 2);
    put(" home server        creep@192.168.1.18    ssh     UTF ");
    at(8, c0 + 2);
    put(" Synchronet         vert.synchro.net      telnet  PC  ");
    at(10, c0 + 2);
    put("\x1b[2meach with its letters, its characters, its drawers\x1b[0m");
    at(11, c0 + 2);
    put("\x1b[2mand your name and password (Right Amiga+U / +P)\x1b[0m");
    at(14, 3);
    put("- \x1b[1mssh\x1b[0m: AmiSSL; a key or a password; the host's key checked");
    at(15, 3);
    put("- \x1b[1mxterm\x1b[0m for programs: the mouse (vim, htop, mc), Alt as Meta,");
    at(16, 3);
    put("  focus reports, bracketed paste, OSC 52 copy to the Amiga's clipboard");
    at(17, 3);
    put("- \x1b[1mPC BBSes\x1b[0m: code page 437, iCE colours, the VGA font");
    at(18, 3);
    put("- \x1b[1mlog\x1b[0m a session to a file, colour codes and all");
}

static int bar_n;

static void bar(int r, int c, int w, int pct)
{
    int i, f = w * pct / 100;
    at(r, c);
    put("\x1b[32m");
    for (i = 0; i < w; i++)
        put(i < f ? "█" : "░");
    putf("\x1b[0m %3d%%", pct);
}

static void p_xfer(void)
{
    heading("File transfers", "Zmodem and Hydra start by themselves; Ymodem, Xmodem from the menu");
    at(4, 3);
    put("A BBS download or `sz file`: CTerm takes it, into the drawer you chose,");
    at(5, 3);
    put("never over a file that is there; an upload asks which files to send.");
    at(6, 3);
    put("CTerm goes on meanwhile: other tabs, other calls, the Shell.");
    box(9, 3, 15, 60, "┌", "─", "┐", "│", "└", "┘");
    at(10, 5);
    put("\x1b[1mZmodem\x1b[0m  receiving AmigaOS-3.2-update.lha");
    at(11, 5);
    put("into Work:Downloads");
    bar_n = 0;
    at(16, 3);
    put("\x1b[2mThe protocols are files of their own (Protocols/), loaded only\x1b[0m");
    at(17, 3);
    put("\x1b[2mwhen a transfer starts: what is not used takes no memory.\x1b[0m");
}

static int p_xfer_anim(int n)
{
    int pct = n * 3 % 101;
    at(12, 5);
    put("file  ");
    bar(12, 11, 34, pct);
    at(13, 5);
    put("batch ");
    bar(13, 11, 34, pct / 3);
    at(14, 5);
    putf("\x1b[2m%3d KB/s   %2d s left   [ Stop ]\x1b[0m", 5 + n % 3, (100 - pct) / 4);
    return 0;
}

/* ---- the race: the same text into CON: and into CTerm -------------------- */

static LONG flood_lines = 1500;
static char fname[40] = "CON:";       /* FLOOD: the console's name */
static char conwin[200] = "CON:676/18/600/560/CON: the Amiga's console/CLOSE/WAIT";

/* text like a build's output, in the 8 colours both consoles know */
static int flood_line(char *s, int i)
{
    static const char *word[8] = { "compiling", "linking", "src/draw.c",
                                   "ppaint.s", "warning:", "ok", "vt.c", "-O2" };
    int n = 0, k;
    n += sprintf(s + n, "\x1b[33m%5d\x1b[0m ", i);
    for (k = 0; k < 4; k++)             /* 46 letters: no wrapping in either,
                                           whatever the Workbench font */
        n += sprintf(s + n, "\x1b[3%dm%-9s\x1b[0m ", 1 + (i + k) % 7, word[(i * 3 + k) % 8]);
    n += sprintf(s + n, "\n");
    return n;
}

static LONG flood(BPTR o)
{
    char s[200];
    LONG i, t = ticks(), n;
    for (i = 1; i <= flood_lines; i++) {
        n = flood_line(s, (int)i);
        Write(o, s, n);                 /* a line a write, as programs do */
    }
    return ticks() - t;
}

static void p_race(void)
{
    {
        char sub[100];
        sprintf(sub, "the same %ld coloured lines into the Amiga's own CON: and into CTerm",
                (long)flood_lines);
        heading("The race", sub);
    }
    at(4, 3);
    put("Press \x1b[1mR\x1b[0m: a CON: window opens beside this one and gets the text");
    at(5, 3);
    put("first, a line a write, as programs write. Then CTerm gets the same.");
    at(6, 3);
    put("Each runs alone, so neither slows the other down.");
    at(8, 3);
    put("\x1b[2mWhy CTerm is quicker: it takes a write and answers at once, reads the\x1b[0m");
    at(9, 3);
    put("\x1b[2mtext in batches, and draws what is on screen at most a few dozen times\x1b[0m");
    at(10, 3);
    put("\x1b[2ma second - in 68020 assembly on a plain screen, a row at a time on a\x1b[0m");
    at(11, 3);
    put("\x1b[2mgraphics card, never reading the card's memory back.\x1b[0m");
}

/* a bar of t ticks, the longest (of 'most') filling w cells */
static void race_bar(int r, const char *who, LONG t, LONG most, int w, int col)
{
    int i, n = most ? (int)((t * w + most - 1) / most) : 0;
    at(r, 3);
    putf("\x1b[1m%-6s\x1b[0m ", who);
    if (col == 31)
        fg24(220, 70, 70);
    else
        fg24(90, 210, 110);
    for (i = 0; i < n && i < w; i++)
        put("█");
    putf("\x1b[0m %ld.%02ld s", (long)(t / 50), (long)((t % 50) * 2));
}

static int race_go(void)
{
    char cmd[300], name[200], res[24];
    LONG t, them = 0;
    BPTR con, f;
    struct Process *me = (struct Process *)FindTask(NULL);
    /* the CON: window runs this program in FLOOD mode, first and alone */
    if (!GetProgramName(name, sizeof name))
        strcpy(name, "CTermTour");
    if (me->pr_HomeDir) {
        char dir[160];
        if (NameFromLock(me->pr_HomeDir, dir, sizeof dir)) {
            AddPart(dir, FilePart(name), sizeof dir);
            strcpy(name, dir);
        }
    }
    sprintf(cmd, "\"%s\" FLOOD LINES %ld RESULT T:CTermRace", name, (long)flood_lines);
    at(13, 3);
    put("\x1b[1;33mCON: first...\x1b[0m");
    flush();
    con = Open((STRPTR)conwin, MODE_NEWFILE);
    if (!con) {
        at(13, 3);
        put("\x1b[31mNo CON: window\x1b[0m");
        return 0;
    }
    DeleteFile((STRPTR)"T:CTermRace");
    SystemTags((STRPTR)cmd, SYS_Input, 0, SYS_Output, con, TAG_END);
    /* a System() that waits leaves its handles to us: closed here, the
     * window stays until its gadget is clicked (WAIT) - its time is in it */
    Close(con);
    if ((f = Open((STRPTR)"T:CTermRace", MODE_OLDFILE))) {
        LONG n = Read(f, res, sizeof res - 1);
        res[n > 0 ? n : 0] = 0;
        them = atol(res);
        Close(f);
        DeleteFile((STRPTR)"T:CTermRace");
    }
    /* then CTerm, alone */
    put("\x1b[H\x1b[2J");
    flush();
    t = flood(out);
    put("\x1b[0m\x1b[H\x1b[2J");
    {
        char sub[100];
        sprintf(sub, "the same %ld coloured lines, a line a write, each console alone",
                (long)flood_lines);
        heading("The race", sub);
    }
    race_bar(5, "CON:", them, them > t ? them : t, cols - 22, 31);
    race_bar(7, "CTerm", t, them > t ? them : t, cols - 22, 32);
    at(10, 3);
    if (t > 0 && them > t)
        putf("CTerm is \x1b[1;32m%ld times\x1b[0m as fast here.", (long)(them / t));
    else if (t == 0)
        put("CTerm took less than a 50th of a second.");
    at(12, 3);
    put("\x1b[2mTimes are the Amiga's own clock (50ths of a second); CON: wrote its\x1b[0m");
    at(13, 3);
    put("\x1b[2mown time at the end of its window.\x1b[0m");
    return 1;
}

static void p_end(void)
{
    int c0 = (cols - 50) / 2;
    box(5, c0, 13, c0 + 49, "╔", "═", "╗", "║", "╚", "╝");
    at(7, c0 + 4);
    put("\x1b[1mCTerm\x1b[0m - the Amiga Shell and the world,");
    at(8, c0 + 4);
    put("in one window, quickly.");
    at(10, c0 + 4);
    put("\x1b[2mWritten in C and 68020 assembly for\x1b[0m");
    at(11, c0 + 4);
    put("\x1b[2mAmigaOS 3.2, on a real A1200.\x1b[0m");
    at(15, (cols - 30) / 2);
    put("Thank you for taking the tour.");
}

/* ---- the tour ----------------------------------------------------------- */

struct Page {
    const char *title;
    void (*draw)(void);
    int (*anim)(int n);                 /* called while the page is up */
    LONG every;                         /* ... every this many us */
};

static const struct Page pages[] = {
    { "CTerm, a modern terminal for AmigaOS", p_title, NULL, 0 },
    { "Text styles a program can ask for", p_styles, NULL, 0 },
    { "16, 256 and 24-bit colours", p_colours, NULL, 0 },
    { "Box drawing, blocks, shades", p_lines, NULL, 0 },
    { "Latin-1, UTF-8 and the PC's letters", p_chars, NULL, 0 },
    { "Cursor shapes", p_cursor, p_cursor_anim, 1000000 },
    { "Boing: 24-bit colour, whole frames", p_boing, p_boing_anim, 1 },
    { "The Amiga Shell in a window of its own", p_shell, NULL, 0 },
    { "telnet, ssh, BBSes, the phone book", p_calls, NULL, 0 },
    { "File transfers that do not stop you", p_xfer, p_xfer_anim, 200000 },
    { "Speed: CTerm against CON: (press R)", p_race, NULL, 0 },
    { "The end", p_end, NULL, 0 },
};

static void tour(LONG first)
{
    page = first > 0 && first <= npages ? first - 1 : 0;
    for (;;) {
        const struct Page *p = &pages[page];
        LONG until = secs > 0 ? ticks() + secs * 50 : 0;
        int k = K_NONE, n = 0;
        put("\x1b[?25l");               /* no cursor while a page is drawn */
        clear();
        p->draw();
        status(p->title);
        if (p->anim != p_cursor_anim)
            put("\x1b[?25l");
        else
            put("\x1b[?25h");
        if (p->draw == p_boing && secs > 0)
            until = ticks() + secs * 50 * 2;    /* the ball gets longer */
        for (;;) {
            LONG left;
            if (p->anim) {
                p->anim(n++);
                if (p->anim == p_cursor_anim)
                    put("\x1b[10;15H");
            }
            left = until ? (until - ticks()) * 20000 : 0x7FFFFFFF;
            if (until && left <= 0) {
                k = K_NEXT;
                break;
            }
            k = key_wait(p->anim ? (p->every < left ? p->every : left) : left);
            if (k == K_RACE && p->draw == p_race) {
                /* R: run the race, then stay until a key */
                race_go();
                status(p->title);
                until = 0;
                continue;
            }
            if (k == K_RACE)
                k = K_NEXT;
            if (k != K_NONE)
                break;
        }
        if (k == K_END)
            break;
        if (k == K_BACK)
            page = page > 0 ? page - 1 : 0;
        else if (++page >= npages)
            break;
    }
}

int main(void)
{
    LONG a5[7] = { 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"SECS/K/N,PAGE/K/N,FLOOD/S,LINES/K/N,"
                                 "RACEWIN/K,NAME/K,RESULT/K", a5, NULL);
    (void)version;
    out = Output();
    in = Input();
    npages = sizeof pages / sizeof pages[0];
    if (!rd) {
        PrintFault(IoErr(), (STRPTR)"CTermTour");
        return RETURN_FAIL;
    }
    if (a5[0])
        secs = *(LONG *)a5[0];
    if (a5[3])
        flood_lines = *(LONG *)a5[3];
    if (a5[5]) {
        strncpy(fname, (char *)a5[5], sizeof fname - 1);
        fname[sizeof fname - 1] = 0;
    }
    if (a5[4]) {                        /* where the race's CON: opens */
        strncpy(conwin, (char *)a5[4], sizeof conwin - 1);
        conwin[sizeof conwin - 1] = 0;
    }
    if (a5[2]) {
        /* the race's other half: time the text into this console */
        char s[120];
        LONG t;
        Delay(5);                       /* the window is up */
        t = flood(out);
        sprintf(s, "\x1b[0m\n\x1b[1m%s  %ld lines in %ld.%02ld s  (%ld a second)\x1b[0m\n",
                fname, (long)flood_lines, (long)(t / 50), (long)((t % 50) * 2),
                (long)(t ? flood_lines * 50 / t : 0));
        Write(out, s, strlen(s));
        if (a5[6]) {                    /* the tour reads the time from here */
            BPTR f = Open((STRPTR)a5[6], MODE_NEWFILE);
            if (f) {
                sprintf(s, "%ld\n", (long)t);
                Write(f, s, strlen(s));
                Close(f);
            }
        }
        FreeArgs(rd);
        return RETURN_OK;
    }
    if (!IsInteractive(in) || !IsInteractive(out)) {
        PutStr("CTermTour: run me in a CTerm window\n");
        FreeArgs(rd);
        return RETURN_WARN;
    }
    SetMode(in, 1);                     /* keys as they are pressed */
    ask_size();
    Write(out, "\x1b%G", 3);            /* UTF-8 from here */
    ball_make();
    tour(a5[1] ? *(LONG *)a5[1] : 1);
    put("\x1b[0m\x1b[2 q\x1b[?25h\x1b[H\x1b[2J");
    flush();
    Write(out, "\x1b%@", 3);            /* the Amiga's Latin-1 again */
    SetMode(in, 0);
    FreeArgs(rd);
    return RETURN_OK;
}
