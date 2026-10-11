/* The Amiga console personality: known answers. */
#include "../src/vt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void feed(Vt *v, const char *s) { vt_write(v, (const uint8_t *)s, strlen(s)); }

static char at(Vt *v, int r, int c)
{
    int w;
    const VtCell *l = vt_line(v, r, 0, &w);
    return c < w && l[c].cp ? (char)l[c].cp : ' ';
}

static int room_left, room_asked;
static int room(long n)
{
    (void)n;
    room_asked++;
    return room_left-- > 0;
}

int main(void)
{
    Vt *v = vt_new(40, 10, 100);
    char rep[64];
    int n, x, y, vis;
    vt_set_utf8(v, 0);                  /* the Latin-1 Shell */
    vt_set_amiga(v, 1);

    feed(v, "one\ntwo");                         /* LF = new line */
    vt_cursor(v, &x, &y, &vis);
    CHECK(at(v, 1, 0) == 't' && x == 3 && y == 1, "LF starts the next line");

    feed(v, "\x9b" "2J\x9b" "1;1Hhome");         /* 8-bit CSI */
    CHECK(at(v, 0, 0) == 'h' && at(v, 1, 0) == ' ', "0x9B is CSI");

    {   /* ESC % G: UTF-8 in the Amiga Shell; ESC % @: Latin-1 and 9B again */
        int w;
        const VtCell *l;
        feed(v, "\x0c" "\x1b%G" "\xe2\x94\x80" "\x1b%@" "\xe4" "\x9b" "2;1H" "z");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 2 && l[0].cp == 0x2500 && l[1].cp == 0xE4, "ESC % G / ESC % @");
        CHECK(at(v, 1, 0) == 'z', "0x9B is CSI again after ESC % @");
    }
    {   /* CSI 5 SP q: a bar cursor, no report; CSI 0 SP q: the report */
        int blink;
        feed(v, "\x9b" "5 q");
        CHECK(vt_take_reply(v, rep, sizeof rep) == 0 &&
              vt_cursor_shape(v, &blink) == 2, "CSI 5 SP q sets a bar");
        feed(v, "\x9b" "2 q");
    }
    {   /* the Shell switched to UTF-8 (the menu): the console's 9B CSI
         * still works, Latin-1 bytes from programs show as Latin-1 */
        int w;
        const VtCell *l;
        vt_set_utf8(v, 1);
        feed(v, "\x0c" "\x9b" "2;1H" "z");
        CHECK(at(v, 1, 0) == 'z', "UTF-8 Shell: 9B is still CSI");
        feed(v, "\x0c" "\xe5" "x" "\xc3\xa5" "\xe5" "\x9b" "3;1H" "q" "\xe2\x94" "y");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 4 && l[0].cp == 0xE5 && l[1].cp == 'x' && l[2].cp == 0xE5 &&
              l[3].cp == 0xE5, "UTF-8 Shell: Latin-1 bytes, UTF-8 letters");
        CHECK(at(v, 2, 0) == 'q', "UTF-8 Shell: 9B after a Latin-1 letter");
        l = vt_line(v, 2, 0, &w);
        CHECK(w >= 3 && l[1].cp == 0xE2 && l[2].cp == 'y',
              "UTF-8 Shell: a broken sequence: its bytes as Latin-1 (94: C1)");
        /* a 2-byte lead (C4 C5 D6: Ä Å Ö) then 9B: the letter, then CSI */
        feed(v, "\x0c" "ab" "\xc5" "\x9b" "K" "\xc4" "\x9b" "Dz");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 4 && l[2].cp == 0xC5 && l[3].cp == 'z',
              "UTF-8 Shell: Å then 9B K / Ä then 9B D, not ś / ě");
        /* ... unless what follows the 9B is no Amiga CSI: then it is the
         * UTF-8 letter whose second byte is 9B (Л D0 9B, ś C5 9B) */
        feed(v, "\x0c" "\xd0\x9b" "\xd0\xb8\xd1\x81" "z");     /* Лис */
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 4 && l[0].cp == 0x41B && l[1].cp == 0x438 &&
              l[2].cp == 0x441 && l[3].cp == 'z', "UTF-8 Shell: Л then Cyrillic");
        feed(v, "\x0c" "\xc5\x9b" "nieg" "\xc5\x9b" "\r\n" "\xd0\x9b" "2026 ok");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 6 && l[0].cp == 0x15B && l[1].cp == 'n' && l[4].cp == 'g' &&
              l[5].cp == 0x15B, "UTF-8 Shell: ś then ASCII text, ś then CR");
        l = vt_line(v, 1, 0, &w);
        CHECK(w >= 8 && l[0].cp == 0x41B && l[1].cp == '2' && l[5].cp == ' ' &&
              l[6].cp == 'o', "UTF-8 Shell: Л then digits and a space");
        feed(v, "\x0c" "abc" "\xc5" "\x9b" "K" "\r\n" "xy" "\xc4" "\x9b" "1D" "z");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 4 && l[3].cp == 0xC5, "UTF-8 Shell: Å then CSI K");
        l = vt_line(v, 1, 0, &w);
        CHECK(w >= 3 && l[0].cp == 'x' && l[1].cp == 'y' && l[2].cp == 'z',
              "UTF-8 Shell: Ä then CSI 1 D (z over the Ä)");
        feed(v, "\x0c" "\xc5\x9b");     /* the 9B ends one write... */
        feed(v, "1;33m" "q" "\xd0\x9b");
        feed(v, "\xd0\xb0" "\xc4\x9b");
        feed(v, "3");
        feed(v, ";1H" "w");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 5 && l[0].cp == 0xC5 && l[1].cp == 'q' && l[2].cp == 0x41B &&
              l[3].cp == 0x430 && l[4].cp == 0xC4, "UTF-8 Shell: 9B held across writes");
        CHECK(at(v, 2, 0) == 'w', "UTF-8 Shell: ... its CSI split over writes");
        feed(v, "\x0c" "\xc5\x9b" "0 p" "\xd0\x9b" " x");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 3 && l[0].cp == 0xC5 && l[1].cp == 0x41B && l[2].cp == ' ' &&
              l[3].cp == 'x', "UTF-8 Shell: CSI 0 SP p; Л then a space");
        feed(v, "\x0c" "\xe2\x80\x9b" "\xe2\x9b\x94" "z");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 3 && l[0].cp == 0x201B && l[1].cp == 0x26D4 &&
              l[2].cp == 'z', "UTF-8 Shell: 3-byte letters with a 9B in them");
        /* a program's ESC % G ... ESC % @ leaves the tab's own UTF-8 on */
        feed(v, "\x0c" "\x1b%G" "\x1b%@" "\xc3\xa5" "z");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 2 && l[0].cp == 0xE5 && l[1].cp == 'z',
              "UTF-8 Shell: ESC % @ goes back to the tab's UTF-8");
        vt_set_utf8(v, 0);              /* back to Latin-1 (the menu) */
        feed(v, "\x0c" "\x1b%G" "\xc3\xa5" "\x1b%@" "\xc3\xa5");
        l = vt_line(v, 0, 0, &w);
        CHECK(w >= 3 && l[0].cp == 0xE5 && l[1].cp == 0xC3 && l[2].cp == 0xA5,
              "Latin-1 Shell: ESC % G for a while, ESC % @ back to Latin-1");
    }
    {   /* a new Shell tab in UTF-8 (the default): vt_set_amiga after
         * vt_set_utf8 keeps it, as the menu shows; Latin-1 stays Latin-1 */
        Vt *u = vt_new(20, 4, 0);
        int w;
        const VtCell *l;
        vt_set_utf8(u, 1);
        vt_set_amiga(u, 1);
        feed(u, "\xc3\x85" "\xe5" "\x9b" "2;1H" "z");
        l = vt_line(u, 0, 0, &w);
        CHECK(w >= 2 && l[0].cp == 0xC5 && l[1].cp == 0xE5 && at(u, 1, 0) == 'z',
              "new UTF-8 Shell tab: UTF-8 letters, Latin-1 bytes, 9B CSI");
        vt_free(u);
        u = vt_new(20, 4, 0);
        vt_set_utf8(u, 0);
        vt_set_amiga(u, 1);
        feed(u, "\xc5" "\xc3\xa5");
        l = vt_line(u, 0, 0, &w);
        CHECK(w >= 3 && l[0].cp == 0xC5 && l[1].cp == 0xC3 && l[2].cp == 0xA5,
              "new Latin-1 Shell tab: bytes as Latin-1");
        vt_free(u);
    }
    feed(v, "\x0c" "page");                      /* FF = clear page */
    CHECK(at(v, 0, 0) == 'p' && at(v, 0, 4) == ' ', "FF clears and homes");

    feed(v, "\x9b" "3;1Hx\x0by");                /* VT = one line up */
    CHECK(at(v, 1, 1) == 'y', "VT moves up a line");

    feed(v, "\x9b" "0 q");                       /* window bounds */
    n = vt_take_reply(v, rep, sizeof rep);
    rep[n] = 0;
    CHECK(!strcmp(rep, "\x9b" "1;1;10;40 r"), "window bounds report");

    feed(v, "\x9b" "6n");                        /* cursor position, 8-bit */
    n = vt_take_reply(v, rep, sizeof rep);
    rep[n] = 0;
    CHECK(rep[0] == (char)0x9b && strchr(rep, 'R'), "DSR answers with 0x9B");

    feed(v, "\x9b" "0 p");
    vt_cursor(v, &x, &y, &vis);
    CHECK(!vis, "CSI 0 SP p hides the cursor");
    feed(v, "\x9b" " p");
    vt_cursor(v, &x, &y, &vis);
    CHECK(vis, "CSI SP p shows it");

    feed(v, "\x9b" "12{\x9b" "2{\x9b" "10{\x9b" "11{");     /* Ed's requests */
    CHECK(vt_raw_events(v) == ((1UL << 12) | (1UL << 2) | (1UL << 10) |
                               (1UL << 11)), "raw event classes kept");
    feed(v, "\x9b" "12}");
    CHECK(!(vt_raw_events(v) & (1UL << 12)), "CSI n} drops a class");

    feed(v, "\x9b" ">?25l");                     /* Ed's private modes */
    vt_cursor(v, &x, &y, &vis);
    CHECK(vis, "CSI >?25l leaves the cursor alone");

    feed(v, "\x9b" "5;5H\x9b" "20u");            /* line length: not DECRC */
    vt_cursor(v, &x, &y, &vis);
    CHECK(x == 4 && y == 4, "CSI u is line length, not restore cursor");

    feed(v, "\x1b" "c" "a\nb");                  /* reset keeps LF = NL */
    CHECK(at(v, 1, 0) == 'b', "after ESC c, LF still starts a new line");

    feed(v, "\xe5\xe4\xf6");                     /* Latin-1 letters */
    CHECK(at(v, 1, 1) == (char)0xe5, "Latin-1 as it is");

    {   /* the scrollback after many scrolls: right lines, right order */
        Vt *h = vt_new(20, 5, 50);
        char buf[32];
        int i, w, ok = 1;
        for (i = 0; i < 300; i++) {
            sprintf(buf, "line %d\r\n", i);
            feed(h, buf);
        }
        CHECK(vt_history(h) == 50, "history keeps its 50 lines");
        for (i = 0; i < 5 && ok; i++) {
            /* 50 lines back, row i = history line i = "line 246+i" */
            const VtCell *l = vt_line(h, i, 50, &w);
            char want[16];
            int k;
            sprintf(want, "line %d", 246 + i);
            for (k = 0; want[k]; k++)
                if (!l || k >= w || l[k].cp != (uint32_t)want[k]) ok = 0;
        }
        CHECK(ok, "the oldest history lines are lines 246.. in order");
        vt_resize(h, 30, 3);
        CHECK(vt_history(h) == 50 && vt_cols(h) == 30, "resize keeps going");
        feed(h, "after\r\n");
        vt_free(h);
    }

    {   /* whole-screen scrolls both ways (the row ring), and a region */
        Vt *s2 = vt_new(10, 5, 20);
        int k;
        feed(s2, "a\r\nb\r\nc\r\nd\r\ne");
        feed(s2, "\x1b[2T");                       /* down 2 */
        CHECK(at(s2, 0, 0) == ' ' && at(s2, 1, 0) == ' ' && at(s2, 2, 0) == 'a' &&
              at(s2, 4, 0) == 'c', "CSI T scrolls the screen down");
        feed(s2, "\x1b[3S");                       /* up 3 */
        CHECK(at(s2, 0, 0) == 'b' && at(s2, 1, 0) == 'c' && at(s2, 2, 0) == ' ',
              "CSI S scrolls the screen up");
        feed(s2, "\x1b[H\x1bM\x1bM");             /* RI at the top, twice */
        CHECK(at(s2, 2, 0) == 'b' && at(s2, 0, 0) == ' ', "RI at the top scrolls down");
        feed(s2, "\x1b[2;4r\x1b[4;1Hx\n\x1b[r");    /* a region scrolls alone */
        for (k = 0; k < 7; k++)
            feed(s2, "\x1b[5;1Hl\n");
        CHECK(at(s2, 0, 0) == 'l' && at(s2, 3, 0) == 'l' && at(s2, 4, 0) == ' ', "bottom line scrolls the ring");
        vt_free(s2);
    }
    {   /* a run of new lines scrolls as many lines as one at a time */
        Vt *s3 = vt_new(10, 5, 50);
        int w;
        const VtCell *l;
        vt_set_amiga(s3, 1);
        feed(s3, "a\n\n\n\n\n\n\n\n\n\n\n\n\nb");
        l = vt_line(s3, 0, 9, &w);
        CHECK(vt_history(s3) == 9 && l && w > 0 && l[0].cp == 'a' &&
              at(s3, 4, 0) == 'b', "12 new lines: 9 history lines, 'a' first");
        feed(s3, "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        CHECK(vt_history(s3) == 29, "20 more: 29 history lines");
        vt_free(s3);
    }
    {   /* code page 437: a PC BBS's box and blocks, and 0x9B is a cent */
        Vt *pc = vt_new(20, 3, 0);
        int w;
        const VtCell *l;
        vt_set_cp437(pc, 1);
        feed(pc, "\xC9\xCD\xBB\xB0\xDB\x9B\x81");
        l = vt_line(pc, 0, 0, &w);
        CHECK(w >= 7 && l[0].cp == 0x2554 && l[1].cp == 0x2550 && l[2].cp == 0x2557 &&
              l[3].cp == 0x2591 && l[4].cp == 0x2588 && l[5].cp == 0xA2 &&
              l[6].cp == 0xFC, "CP437 high bytes are the PC's characters");
        feed(pc, "\r\n\x1b[3b");                /* REP repeats the last one */
        feed(pc, "\r\n\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"
                 "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBA");
        l = vt_line(pc, 2, 0, &w);
        CHECK(w >= 1 && l[0].cp == 0x2551, "a run of CP437 wraps at the edge");
        l = vt_line(pc, 0, 0, &w);
        CHECK(w >= 3 && l[0].cp == 0xFC && l[2].cp == 0xFC, "REP after CP437");
        vt_free(pc);
    }
    {   /* mouse, focus and sync modes */
        Vt *m = vt_new(20, 5, 0);
        int sgr;
        feed(m, "\x1b[?1000h\x1b[?1006h");
        CHECK(vt_mouse(m, &sgr) == 1000 && sgr == 1, "1000 + 1006 on");
        feed(m, "\x1b[?1003h\x1b[?1004h\x1b[?2026h");
        CHECK(vt_mouse(m, &sgr) == 1003 && vt_focus(m) && vt_sync(m),
              "1003, focus, sync on");
        feed(m, "\x1b[?1003l\x1b[?2026l\x1b" "c");
        CHECK(vt_mouse(m, &sgr) == 0 && !vt_sync(m) && !vt_focus(m) && !sgr,
              "off again, and RIS clears them");
        vt_free(m);
    }
    {   /* DECSCUSR in a host's terminal; the Amiga's ' q' stays a report */
        Vt *c = vt_new(20, 5, 0);
        int blink;
        feed(c, "\x1b[6 q");
        CHECK(vt_cursor_shape(c, &blink) == 2 && !blink, "6: steady bar");
        feed(c, "\x1b[3 q");
        CHECK(vt_cursor_shape(c, &blink) == 1 && blink, "3: blinking underline");
        vt_free(c);
    }
    {   /* reflow: a wrapped line joins at a wider width, wraps again */
        Vt *f = vt_new(10, 5, 50);
        int w, x, y, vis;
        const VtCell *l;
        vt_set_amiga(f, 1);
        feed(f, "0123456789ABCDEFGHIJKLMNO");
        vt_resize(f, 30, 5);
        l = vt_line(f, 0, 0, &w);
        vt_cursor(f, &x, &y, &vis);
        CHECK(w == 25 && l[24].cp == 'O' && l[10].cp == 'A' && y == 0 &&
              x == 25, "wider: one row of 25, cursor after it");
        vt_resize(f, 10, 5);
        vt_cursor(f, &x, &y, &vis);
        CHECK(at(f, 1, 0) == 'A' && at(f, 2, 4) == 'O' && y == 2 && x == 5,
              "narrow again: three rows, cursor at 2,5");
        feed(f, "\nabc\ndef");
        vt_resize(f, 30, 5);
        CHECK(at(f, 0, 24) == 'O' && at(f, 1, 0) == 'a' && at(f, 2, 0) == 'd',
              "new lines are not joined");
        vt_free(f);
    }
    {   /* reflow reaches the history */
        Vt *f = vt_new(10, 3, 50);
        int k;
        vt_set_amiga(f, 1);
        for (k = 0; k < 6; k++)
            feed(f, "abcdefgh\n");
        CHECK(vt_history(f) == 4, "four lines went into the history");
        vt_resize(f, 4, 3);
        CHECK(vt_history(f) == 10, "at 4 wide every line is two rows");
        vt_resize(f, 10, 3);
        CHECK(vt_history(f) == 4, "and back");
        vt_free(f);
    }
    {   /* OSC 52: a program's copy, decoded for the clipboard */
        Vt *o = vt_new(20, 5, 0);
        int len;
        char *t;
        feed(o, "\x1b]52;c;aGVsbG8gd8O2cmxk\x07");   /* "hello w\xc3\xb6rld" */
        t = vt_take_clip(o, &len);
        CHECK(t && len == 12 && !memcmp(t, "hello w\xc3\xb6rld", 12),
              "52: base64 decoded");
        free(t);
        CHECK(vt_take_clip(o, &len) == NULL, "taken once");
        feed(o, "\x1b]52;c;?\x1b\\");
        CHECK(vt_take_clip(o, &len) == NULL, "a query is not answered");
        vt_free(o);
    }
    {   /* a wrapped line pushed out by a lower window joins again */
        Vt *f = vt_new(10, 5, 50);
        vt_set_amiga(f, 1);
        feed(f, "0123456789ABC\nx\ny");
        vt_resize(f, 10, 2);
        CHECK(vt_history(f) == 2, "two rows to the history");
        vt_resize(f, 20, 2);
        CHECK(vt_history(f) == 1 && at(f, 0, 0) == 'x',
              "wider: the history's wrapped line is one again");
        vt_free(f);
    }
    {   /* CUU/CUD stop at a margin from inside the region, not outside */
        Vt *m = vt_new(20, 10, 0);
        int x, y, vis;
        feed(m, "\x1b[3;6r\x1b[5;4H\x1b[9A");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 2 && x == 3, "CUU inside: stops at the top margin");
        feed(m, "\x1b[9B");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 5, "CUD inside: stops at the bottom margin");
        feed(m, "\x1b[2;1H\x1b[9A");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 0, "CUU above the region: to the top line");
        feed(m, "\x1b[2;1H\x1b[9B");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 5, "CUD above the region: stops at its bottom");
        feed(m, "\x1b[8;5H\x1b[9E");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 9 && x == 0, "CNL below the region: to the last line");
        feed(m, "\x1b[8;5H\x1b[9F");
        vt_cursor(m, &x, &y, &vis);
        CHECK(y == 2 && x == 0, "CPL below the region: stops at its top");
        vt_free(m);
    }
    {   /* malformed UTF-8 shows U+FFFD, never the character it hides */
        Vt *u = vt_new(20, 3, 0);
        int w;
        const VtCell *l;
        feed(u, "\xc0\x9b" "a\xed\xa0\x80" "b\xf4\x90\x80\x80" "c\xe0\x80\xaf" "d");
        l = vt_line(u, 0, 0, &w);
        CHECK(w >= 8 && l[0].cp == 0xFFFD && l[1].cp == 'a' && l[2].cp == 0xFFFD &&
              l[3].cp == 'b' && l[4].cp == 0xFFFD && l[5].cp == 'c' &&
              l[6].cp == 0xFFFD && l[7].cp == 'd', "overlong, surrogate, past U+10FFFF");
        feed(u, "\r\xc2\x85" "e");           /* U+0085 is NEL, a control */
        l = vt_line(u, 1, 0, &w);
        CHECK(w >= 1 && l[0].cp == 'e', "a C1 control in UTF-8 acts");
        feed(u, "\x1b[H\x1b[2Jx\xe2\x94\n\n");   /* cut short by new lines */
        l = vt_line(u, 0, 0, &w);
        CHECK(w >= 2 && l[1].cp == 0xFFFD, "an unfinished sequence before LFs");
        vt_free(u);
    }
    {   /* ESC ends an OSC or DCS string and starts a new sequence */
        Vt *e = vt_new(20, 3, 0);
        int x, y, vis, ch;
        const char *t;
        feed(e, "\x1b]2;one\x1b[3;5H");
        vt_cursor(e, &x, &y, &vis);
        t = vt_title(e, &ch);
        CHECK(ch && !strcmp(t, "one") && y == 2 && x == 4,
              "OSC then ESC [: the title, then CUP");
        feed(e, "\x1bPq#0;1\x1b[1;2Hz");
        vt_cursor(e, &x, &y, &vis);
        CHECK(y == 0 && x == 2 && at(e, 0, 1) == 'z' && at(e, 0, 0) == ' ',
              "DCS then ESC [: the string ends, CUP acts");
        feed(e, "\x1b_hidden\x1b\\w");
        CHECK(at(e, 0, 2) == 'w', "APC ended by ST");
        vt_free(e);
    }
    {   /* a resize on the alternate screen: the main screen as without */
        Vt *a = vt_new(10, 5, 50);
        int x, y, vis;
        feed(a, "0123456789ABCDE\r\nxy\x1b[?1049hALT");
        vt_resize(a, 20, 5);
        feed(a, "\x1b[?1049l");
        vt_cursor(a, &x, &y, &vis);
        CHECK(at(a, 0, 14) == 'E' && at(a, 1, 1) == 'y' && y == 1 && x == 2,
              "wider in vim: the main screen flows again");
        vt_free(a);
        a = vt_new(10, 5, 50);
        feed(a, "a\r\nb\r\nc\r\nd\r\ne\x1b[?1049h");
        vt_resize(a, 10, 3);
        feed(a, "\x1b[?1049l");
        vt_cursor(a, &x, &y, &vis);
        CHECK(vt_history(a) == 2 && at(a, 0, 0) == 'c' && at(a, 2, 0) == 'e' &&
              y == 2 && x == 1, "lower in vim: the top lines go to the history");
        vt_free(a);
    }
    {   /* an edit that cuts a wide letter blanks both its halves */
        static const char *const how[] = {
            "\x1b[1;3H\x1b[@",             /* ICH on its right half */
            "\x1b[1;2H\x1b[P",             /* DCH of the cell before it */
            "\x1b[1;3H\x1b[X",             /* ECH of its right half */
            "\x1b[1;2H\x1b[1K",            /* EL to the left, to its middle */
            "\x1b[1;3H\x1b[0K",            /* EL from its middle */
            "\x1b[1;3H\x1b[4hZ\x1b[4l",   /* insert mode on its right half */
            "\x1b[1;8H\x1b[@",             /* ICH pushes half off the edge */
            "\x1b[1;8H\x1b[4hZ\x1b[4l",   /* and so does insert mode */
        };
        int k;
        for (k = 0; k < 8; k++) {
            Vt *d = vt_new(10, 2, 0);
            int x, w, ok = 1;
            const VtCell *l;
            /* a, a wide letter (cols 1-2), b; a wide one at cols 8-9 */
            feed(d, "a\xe4\xb8\xad" "b\x1b[1;9H\xe4\xb8\xad");
            feed(d, how[k]);
            l = vt_line(d, 0, 0, &w);
            for (x = 0; x < w; x++) {
                int head = l[x].cp == 0x4E2D;
                int tail = l[x].cp == VT_WIDE_TAIL;
                if ((head && (x + 1 >= w || l[x + 1].cp != VT_WIDE_TAIL)) ||
                    (tail && (x == 0 || l[x - 1].cp != 0x4E2D)))
                    ok = 0;
            }
            if (!ok)
                printf("FAIL: wide half left, case %d\n", k), fails++;
            vt_free(d);
        }
    }
    {   /* CHT stops at the last column; a huge OSC number is no title */
        Vt *t = vt_new(20, 3, 0);
        int x, y, vis, ch;
        feed(t, "\x1b[999999I");
        vt_cursor(t, &x, &y, &vis);
        CHECK(x == 19, "CSI 999999 I: the last column");
        feed(t, "\x1b[3Z");
        vt_cursor(t, &x, &y, &vis);
        CHECK(x == 0, "CSI 3 Z: back to the first");
        feed(t, "\x1b]99999999999;x\x07");
        vt_title(t, &ch);
        CHECK(!ch, "OSC 99999999999 is not OSC 0");
        vt_free(t);
    }
    {   /* the history stops growing when the host says no room, and is
         * not asked again for every line; a clear lets it grow again */
        Vt *t = vt_new(20, 3, 100);
        int i;
        room_left = 5;
        room_asked = 0;
        vt_room = room;
        for (i = 0; i < 50; i++)
            feed(t, "x\r\n");
        CHECK(vt_history(t) == 5, "history stops where memory ran short");
        CHECK(room_asked == 6, "no room asked again after a no");
        feed(t, "\x1b[3J");
        room_left = 10;
        for (i = 0; i < 50; i++)
            feed(t, "y\r\n");
        CHECK(vt_history(t) == 10, "after a clear it grows again");
        vt_room = NULL;
        vt_free(t);
    }
    {   /* a raw program's whole-screen draw gets the alternate screen;
         * cooked again, the Shell's screen is as it was */
        Vt *t = vt_new(20, 4, 100);
        int x, y, vis;
        vt_set_amiga(t, 1);
        feed(t, "1> ed foo\n");
        vt_amiga_raw(t, 1);
        feed(t, "key?");                /* raw, no whole-screen draw */
        CHECK(!vt_alt_screen(t), "raw alone: still the main screen");
        feed(t, "\x9b" "1;1H" "EDIT");
        CHECK(vt_alt_screen(t) && at(t, 0, 0) == 'E', "CSI H: own screen");
        feed(t, "\f" "page");
        CHECK(vt_alt_screen(t) && at(t, 0, 0) == 'p', "FF: still its own");
        feed(t, "\x1b" "c" "R");
        CHECK(vt_alt_screen(t) && at(t, 0, 0) == 'R', "ESC c: its own only");
        vt_amiga_raw(t, 0);
        vt_cursor(t, &x, &y, &vis);
        CHECK(!vt_alt_screen(t) && at(t, 0, 0) == '1' && at(t, 1, 0) == 'k' &&
              x == 4 && y == 1, "cooked: the Shell's screen back");
        vt_amiga_raw(t, 1);             /* "Press a key": no draw */
        vt_amiga_raw(t, 0);
        feed(t, "\f");                 /* the Shell's own FF: a clear */
        CHECK(!vt_alt_screen(t) && at(t, 0, 0) == ' ', "cooked FF clears");
        vt_free(t);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    vt_free(v);
    return fails != 0;
}
