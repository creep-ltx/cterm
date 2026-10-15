/* link_test - hyperlinks (OSC 8): the cells between the opening and the
 * closing code carry the link (underlined), SGR does not end it, the
 * addresses are kept once and dropped when no cell uses them (built
 * with a cap of 4 addresses to try that); and the row flag that says a
 * line wrapped at the right edge (find.c joins such rows for links). */
#include "../src/vt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void feed(Vt *v, const char *s) { vt_write(v, (const uint8_t *)s, strlen(s)); }

static const VtCell *cellp(Vt *v, int r, int c, int back)
{
    int w;
    const VtCell *l = vt_line(v, r, back, &w);
    static const VtCell none;
    return l && c < w ? &l[c] : &none;
}

/* the address of the cell at r, c (NULL: none) */
static const char *link_of(Vt *v, int r, int c, int back)
{
    return vt_link(v, vt_pens(v)[cellp(v, r, c, back)->pen].link);
}

static int under(Vt *v, int r, int c)
{
    return (vt_pens(v)[cellp(v, r, c, 0)->pen].attr & VT_UNDERLINE) != 0;
}

static int wrapped(Vt *v, int r, int back)
{
    int w;
    const VtCell *l = vt_line(v, r, back, &w);
    return l ? VT_ROW_WRAP(l) : 0;
}

/* how far back (vt_line's back for row 0) the newest line starting
 * with c is, -1: none */
static int back_of(Vt *v, uint32_t c)
{
    int b;
    for (b = 0; b <= vt_history(v); b++)
        if (cellp(v, 0, 0, b)->cp == c)
            return b;
    return -1;
}

int main(void)
{
    Vt *v = vt_new(20, 5, 50);
    const char *s;
    int i;
    char buf[200];

    /* ST as ESC \ and as BEL; the link's cells only */
    feed(v, "ab\033]8;;http://a.example/x\033\\link\033]8;;\033\\cd");
    s = link_of(v, 0, 2, 0);
    CHECK(s && !strcmp(s, "http://a.example/x"), "the link's first cell");
    CHECK(link_of(v, 0, 5, 0) == link_of(v, 0, 2, 0), "the link's last cell");
    CHECK(!link_of(v, 0, 1, 0) && !link_of(v, 0, 6, 0), "no link around it");
    CHECK(under(v, 0, 3) && !under(v, 0, 6), "a link is underlined, the rest not");
    feed(v, "\r\n\033]8;id=7;gemini://b\007x\033[1;31my\033[0mz\033]8;;\007.");
    s = link_of(v, 1, 2, 0);
    CHECK(s && !strcmp(s, "gemini://b"), "SGR 0 does not end a link");
    CHECK(link_of(v, 1, 0, 0) == s && link_of(v, 1, 1, 0) == s, "colours keep the link");
    CHECK(!link_of(v, 1, 3, 0), "BEL ends it");
    CHECK(vt_link_count(v) == 2, "two addresses");
    /* the same address again: the same number */
    feed(v, "\r\n\033]8;;http://a.example/x\033\\q\033]8;;\033\\");
    CHECK(link_of(v, 2, 0, 0) == link_of(v, 0, 2, 0), "one address kept once");
    CHECK(vt_link_count(v) == 2, "still two");
    /* a wide letter keeps it on both halves */
    feed(v, "\r\n\033]8;;http://w\033\\\xe7\x8c\xab\033]8;;\033\\");
    CHECK(link_of(v, 3, 1, 0) && link_of(v, 3, 1, 0) == link_of(v, 3, 0, 0),
          "a wide letter's two halves");

    /* the cap (4 addresses): the old ones go once no cell has them */
    feed(v, "\033[2J\033[H\033[3J");
    for (i = 0; i < 60; i++) {           /* push the old lines out */
        feed(v, "\r\n");
    }
    for (i = 0; i < 10; i++) {
        sprintf(buf, "\033[2J\033[H\033]8;;http://n%d\033\\L\033]8;;\033\\", i);
        feed(v, buf);
        for (int k = 0; k < 60; k++)
            feed(v, "\r\n");
    }
    feed(v, "\033[H\033]8;;http://last\033\\Z\033]8;;\033\\");
    s = link_of(v, 0, 0, 0);
    CHECK(s && !strcmp(s, "http://last"), "a new link after the old went");
    CHECK(vt_link_count(v) <= 4, "the cap holds");

    /* still linked while history keeps the cell */
    feed(v, "\033c");
    feed(v, "\033]8;;http://kept\033\\K\033]8;;\033\\\r\n");
    for (i = 0; i < 8; i++)
        feed(v, "x\r\n");
    i = back_of(v, 'K');
    CHECK(i > 0, "the linked line went into the history");
    s = link_of(v, 0, 0, i);
    CHECK(s && !strcmp(s, "http://kept"), "a link in the history");

    /* wrap flags: a wrapped row, a row ended by a new line */
    feed(v, "\033c");
    feed(v, "\033[2J\033[HABCDEFGHIJKLMNOPQRSTUVWXY\r\nshort\r\n");
    CHECK(wrapped(v, 0, 0) == 1, "the wrapped row is marked");
    CHECK(wrapped(v, 1, 0) == 0, "its rest is not");
    CHECK(wrapped(v, 2, 0) == 0, "a short line is not");
    for (i = 0; i < 6; i++)
        feed(v, "y\r\n");
    i = back_of(v, 'A');
    CHECK(i > 0 && wrapped(v, 0, i) == 1, "the mark goes into the history");
    CHECK(i > 0 && cellp(v, 0, 0, i - 1)->cp == 'U' && !wrapped(v, 0, i - 1),
          "the next line is its rest");
    vt_resize(v, 30, 5);
    vt_resize(v, 10, 5);                 /* reflow: 25 letters, 3 rows */
    {
        int h = vt_history(v), r, found = 0;
        for (r = 0; r < h + 5 && !found; r++) {
            int w;
            const VtCell *l = r < h ? vt_line(v, 0, h - r, &w) : vt_line(v, r - h, 0, &w);
            if (l && w >= 2 && l[0].cp == 'A' && l[1].cp == 'B') {
                found = 1;
                CHECK(VT_ROW_WRAP(l), "after reflow: the first row wraps");
            }
        }
        CHECK(found, "the line is still there after reflow");
    }

    /* a storm: links opened and left open, long and empty addresses,
     * SGR, resizes, the alternate screen, a reset; the cap holds and
     * every linked cell's address is one kept (run it under ASan too) */
    srand(7);
    for (i = 0; i < 3000; i++) {
        int k, n = rand() % 40;
        char u[64];
        switch (rand() % 9) {
        case 0: case 1: case 2:
            for (k = 0; k < n; k++)
                u[k] = 'a' + rand() % 26;
            u[k] = 0;
            sprintf(buf, "\033]8;;http://%s\033\\", u);
            feed(v, buf);
            break;
        case 3: feed(v, "\033]8;;\033\\"); break;
        case 4: feed(v, rand() % 2 ? "\033[0m" : "\033[1;4;33m"); break;
        case 5: vt_resize(v, 5 + rand() % 30, 2 + rand() % 8); break;
        case 6: feed(v, rand() % 2 ? "\033[?1049h" : "\033[?1049l"); break;
        case 7: if (rand() % 50 == 0) feed(v, "\033c"); break;
        default:
            for (k = 0; k < n; k++)
                u[k] = rand() % 5 ? 'a' + rand() % 26 : '\n';
            u[k] = 0;
            feed(v, u);
        }
    }
    CHECK(vt_link_count(v) <= 4, "the cap holds through the storm");
    {
        int r, c, w, bad = 0;
        for (r = 0; r < vt_rows(v); r++) {
            const VtCell *l = vt_line(v, r, 0, &w);
            for (c = 0; l && c < w; c++) {
                int id = vt_pens(v)[l[c].pen].link;
                if (id && !vt_link(v, id))
                    bad++;
            }
        }
        CHECK(!bad, "every linked cell's address is kept");
    }

    vt_free(v);
    if (fails) {
        printf("link_test: %d failed\n", fails);
        return 1;
    }
    printf("link_test: all passed\n");
    return 0;
}
