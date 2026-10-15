/*
 * cmap - which characters a TrueType font has: its cmap table read into
 * ranges of code points (format 12, else format 4), so a letter the main
 * font lacks can come from the fallback font (fonts.c, draw.c).
 * ttengine.library cannot say whether a glyph is there (it draws the
 * font's empty box). Plain C: tested on Linux (tests/cmap_test.c).
 */
#include "cmap.h"

#include <string.h>

static unsigned long be16(const unsigned char *p)
{
    return (unsigned long)p[0] << 8 | p[1];
}

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
           (unsigned long)p[2] << 8 | p[3];
}

/* a..b (inclusive) into the list, joined to the last range when they
 * touch (the tables come in order) */
static int add(CmapRanges *r, unsigned long a, unsigned long b)
{
    if (r->n && a <= r->r[2 * r->n - 1] + 1 && a >= r->r[2 * r->n - 2]) {
        if (b > r->r[2 * r->n - 1])
            r->r[2 * r->n - 1] = b;
        return 1;
    }
    if (r->n == r->cap) {
        long cap = r->cap ? r->cap * 2 : 64;
        unsigned long *nr = r->alloc(cap * 2 * sizeof *nr);
        if (!nr)
            return 0;
        if (r->n)
            memcpy(nr, r->r, r->n * 2 * sizeof *nr);
        if (r->r)
            r->release(r->r);
        r->r = nr;
        r->cap = cap;
    }
    r->r[2 * r->n] = a;
    r->r[2 * r->n + 1] = b;
    r->n++;
    return 1;
}

static int fmt12(CmapRanges *r, const unsigned char *t, unsigned long len)
{
    unsigned long n, i;
    if (len < 16)
        return 0;
    n = be32(t + 12);
    if (n > (len - 16) / 12)
        n = (len - 16) / 12;
    for (i = 0; i < n; i++) {
        const unsigned char *g = t + 16 + i * 12;
        unsigned long a = be32(g), b = be32(g + 4), gl = be32(g + 8);
        if (b < a || b > 0x10FFFF)
            continue;
        if (gl == 0)                    /* its first maps to .notdef */
            a++;
        if (a <= b && !add(r, a, b))
            return 0;
    }
    return 1;
}

static int fmt4(CmapRanges *r, const unsigned char *t, unsigned long len)
{
    unsigned long segs, i, c;
    const unsigned char *ends, *starts, *deltas, *offs;
    if (len < 14)
        return 0;
    segs = be16(t + 6) / 2;
    if (14 + segs * 8 + 2 > len)
        return 0;
    ends = t + 14;
    starts = ends + segs * 2 + 2;       /* (a reserved word between) */
    deltas = starts + segs * 2;
    offs = deltas + segs * 2;
    for (i = 0; i < segs; i++) {
        unsigned long a = be16(starts + 2 * i), b = be16(ends + 2 * i);
        unsigned long d = be16(deltas + 2 * i), o = be16(offs + 2 * i);
        long run = -1;
        if (a > b || a == 0xFFFF)
            continue;
        if (b == 0xFFFF)
            b = 0xFFFE;
        for (c = a; c <= b; c++) {
            unsigned long gl;
            if (o == 0)
                gl = (c + d) & 0xFFFF;
            else {
                const unsigned char *p = offs + 2 * i + o + 2 * (c - a);
                if (p + 2 > t + len)
                    gl = 0;
                else if ((gl = be16(p)) != 0)
                    gl = (gl + d) & 0xFFFF;
            }
            if (gl && run < 0)
                run = (long)c;
            if (!gl && run >= 0) {
                if (!add(r, (unsigned long)run, c - 1))
                    return 0;
                run = -1;
            }
        }
        if (run >= 0 && !add(r, (unsigned long)run, b))
            return 0;
    }
    return 1;
}

/* The cmap table's place in the file, from its start (the table
 * directory: 12 bytes and 16 a table) - 0: not found. */
int cmap_where(const unsigned char *head, unsigned long hlen,
               unsigned long *off, unsigned long *len)
{
    unsigned long nt, i;
    if (hlen < 12)
        return 0;
    nt = be16(head + 4);
    for (i = 0; i < nt && 12 + i * 16 + 16 <= hlen; i++) {
        const unsigned char *e = head + 12 + i * 16;
        if (!memcmp(e, "cmap", 4)) {
            *off = be32(e + 8);
            *len = be32(e + 12);
            return *len >= 4;
        }
    }
    return 0;
}

int cmap_table(CmapRanges *r, const unsigned char *cm, unsigned long len)
{
    unsigned long nt, i, best = 0;
    int bestrank = 0;
    r->n = 0;
    if (len < 4)
        return 0;
    nt = be16(cm + 2);
    /* the subtable that knows the most: a 32-bit one (format 12) for
     * Unicode first, then the 16-bit one (format 4) */
    for (i = 0; i < nt && 4 + i * 8 + 8 <= len; i++) {
        const unsigned char *e = cm + 4 + i * 8;
        unsigned long pid = be16(e), eid = be16(e + 2), so = be32(e + 4);
        unsigned long fmt;
        int rank = 0;
        if (so + 2 > len)
            continue;
        fmt = be16(cm + so);
        if (fmt == 12 && ((pid == 3 && eid == 10) || pid == 0))
            rank = 3;
        else if (fmt == 4 && ((pid == 3 && eid == 1) || pid == 0))
            rank = 2;
        if (rank > bestrank) {
            bestrank = rank;
            best = so;
        }
    }
    if (!bestrank)
        return 0;
    if (bestrank == 3)
        return fmt12(r, cm + best, len - best);
    return fmt4(r, cm + best, len - best);
}

int cmap_has(const CmapRanges *r, unsigned long cp)
{
    long lo = 0, hi = r->n - 1;
    while (lo <= hi) {
        long m = (lo + hi) / 2;
        if (cp < r->r[2 * m])
            hi = m - 1;
        else if (cp > r->r[2 * m + 1])
            lo = m + 1;
        else
            return 1;
    }
    return 0;
}

void cmap_free(CmapRanges *r)
{
    if (r->r)
        r->release(r->r);
    r->r = NULL;
    r->n = r->cap = 0;
}
