/* The Amiga console personality: known answers. */
#include "../src/vt.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void feed(Vt *v, const char *s) { vt_write(v, (const uint8_t *)s, strlen(s)); }

static char at(Vt *v, int r, int c)
{
    int w;
    const VtCell *l = vt_line(v, r, 0, &w);
    return c < w && l[c].cp ? (char)l[c].cp : ' ';
}

int main(void)
{
    Vt *v = vt_new(40, 10, 100);
    char rep[64];
    int n, x, y, vis;
    vt_set_amiga(v, 1);

    feed(v, "one\ntwo");                         /* LF = new line */
    vt_cursor(v, &x, &y, &vis);
    CHECK(at(v, 1, 0) == 't' && x == 3 && y == 1, "LF starts the next line");

    feed(v, "\x9b" "2J\x9b" "1;1Hhome");         /* 8-bit CSI */
    CHECK(at(v, 0, 0) == 'h' && at(v, 1, 0) == ' ', "0x9B is CSI");

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
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    vt_free(v);
    return fails != 0;
}
