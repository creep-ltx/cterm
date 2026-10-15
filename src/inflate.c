/*
 * inflate - zlib data unpacked (RFC 1950 around RFC 1951's DEFLATE), as
 * small as it goes: kitty's pictures come so (o=z) when they are raw
 * pixels. The way of Mark Adler's puff.c: a canonical Huffman code
 * decoded a bit at a time - slow beside zlib, but a picture comes seldom
 * and this is ~1.5 KB. Every read and write is checked: the data comes
 * from the other side of a connection.
 *
 * Built into Parts/Pictures (pics.c includes it) and the Linux tests.
 * No allocation: its tables are static (one picture at a time).
 */
#include <stdint.h>

typedef struct {
    const uint8_t *in, *end;
    uint32_t bits;
    int nb, err;
    uint8_t *out;
    long len, max;
} Inf;

typedef struct {
    uint16_t cnt[16];                   /* codes of each length */
    uint16_t sym[288];                  /* the symbols, by code */
} Huff;

static Huff inf_lit, inf_dist;

static unsigned inf_bits(Inf *s, int n)
{
    unsigned v;
    while (s->nb < n) {
        if (s->in >= s->end) {
            s->err = 1;
            return 0;
        }
        s->bits |= (uint32_t)*s->in++ << s->nb;
        s->nb += 8;
    }
    v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nb -= n;
    return v;
}

/* the code from its lengths; 0: none of them is used */
static int inf_build(Huff *h, const uint8_t *len, int n)
{
    uint16_t off[16];
    int i;
    for (i = 0; i < 16; i++)
        h->cnt[i] = 0;
    for (i = 0; i < n; i++)
        h->cnt[len[i]]++;
    if (h->cnt[0] == n)
        return 0;
    off[1] = 0;
    for (i = 1; i < 15; i++)
        off[i + 1] = off[i] + h->cnt[i];
    for (i = 0; i < n; i++)
        if (len[i])
            h->sym[off[len[i]]++] = (uint16_t)i;
    return 1;
}

static int inf_decode(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0, l;
    for (l = 1; l < 16; l++) {
        code |= inf_bits(s, 1);
        if (s->err)
            return -1;
        if (code - h->cnt[l] < first)
            return h->sym[index + (code - first)];
        index += h->cnt[l];
        first = (first + h->cnt[l]) << 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const uint8_t dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* a block's symbols until its end */
static int inf_codes(Inf *s)
{
    for (;;) {
        int sym = inf_decode(s, &inf_lit);
        if (sym < 0)
            return 0;
        if (sym < 256) {
            if (s->len >= s->max)
                return 0;
            s->out[s->len++] = (uint8_t)sym;
        } else if (sym == 256)
            return 1;
        else {
            long len, dist;
            sym -= 257;
            if (sym >= 29)
                return 0;
            len = lbase[sym] + inf_bits(s, lext[sym]);
            sym = inf_decode(s, &inf_dist);
            if (sym < 0 || sym >= 30)
                return 0;
            dist = dbase[sym] + inf_bits(s, dext[sym]);
            if (s->err || dist > s->len || len > s->max - s->len)
                return 0;
            while (len--) {
                s->out[s->len] = s->out[s->len - dist];
                s->len++;
            }
        }
    }
}

static int inf_dynamic(Inf *s)
{
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t len[320];
    int nlen = inf_bits(s, 5) + 257, ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4, i;
    if (s->err || nlen > 286 || ndist > 30)
        return 0;
    for (i = 0; i < 19; i++)
        len[order[i]] = i < ncode ? (uint8_t)inf_bits(s, 3) : 0;
    if (s->err || !inf_build(&inf_lit, len, 19))
        return 0;
    for (i = 0; i < nlen + ndist; ) {
        int sym = inf_decode(s, &inf_lit), rep, v = 0;
        if (sym < 0)
            return 0;
        if (sym < 16) {
            len[i++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            if (!i)
                return 0;
            v = len[i - 1];
            rep = 3 + inf_bits(s, 2);
        } else if (sym == 17)
            rep = 3 + inf_bits(s, 3);
        else
            rep = 11 + inf_bits(s, 7);
        if (s->err || i + rep > nlen + ndist)
            return 0;
        while (rep--)
            len[i++] = (uint8_t)v;
    }
    if (!len[256] || !inf_build(&inf_lit, len, nlen))
        return 0;
    inf_build(&inf_dist, len + nlen, ndist);    /* none: literals only */
    return inf_codes(s);
}

static int inf_fixed(Inf *s)
{
    uint8_t len[288];
    int i;
    for (i = 0; i < 288; i++)
        len[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    inf_build(&inf_lit, len, 288);
    for (i = 0; i < 30; i++)
        len[i] = 5;
    inf_build(&inf_dist, len, 30);
    return inf_codes(s);
}

static int inf_stored(Inf *s)
{
    long n;
    s->bits = 0;                        /* to the next whole byte */
    s->nb = 0;
    if (s->end - s->in < 4)
        return 0;
    n = s->in[0] | s->in[1] << 8;
    if ((s->in[2] | s->in[3] << 8) != (~n & 0xFFFF))
        return 0;
    s->in += 4;
    if (n > s->end - s->in || n > s->max - s->len)
        return 0;
    while (n--)
        s->out[s->len++] = *s->in++;
    return 1;
}

/* zlib data (in, n bytes) into out (at most max): bytes made, -1 broken */
long zinflate(const uint8_t *in, long n, uint8_t *out, long max)
{
    Inf s;
    int last, ok;
    if (n < 2 || (in[0] & 15) != 8 || (in[0] << 8 | in[1]) % 31 ||
        (in[1] & 32))
        return -1;                      /* not zlib's DEFLATE; a preset
                                           dictionary is never used */
    s.in = in + 2;
    s.end = in + n;
    s.bits = 0;
    s.nb = s.err = 0;
    s.out = out;
    s.len = 0;
    s.max = max;
    do {
        int type;
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err)
            return -1;
        ok = type == 0 ? inf_stored(&s) : type == 1 ? inf_fixed(&s)
           : type == 2 ? inf_dynamic(&s) : 0;
        if (!ok || s.err)
            return -1;
    } while (!last);
    return s.len;                       /* (the Adler-32 sum: not checked) */
}
