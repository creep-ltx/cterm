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
    return l[c].cp ? (char)l[c].cp : ' ';
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
                if (!l || l[k].cp != (uint32_t)want[k]) ok = 0;
        }
        CHECK(ok, "the oldest history lines are lines 246.. in order");
        vt_resize(h, 30, 3);
        CHECK(vt_history(h) == 50 && vt_cols(h) == 30, "resize keeps going");
        feed(h, "after\r\n");
        vt_free(h);
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    vt_free(v);
    return fails != 0;
}
