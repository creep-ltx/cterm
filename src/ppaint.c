/* ppaint.c - the planar painter's glyph store and row set-up; the inner
 * loop is ppaint.s. See ppaint.h. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
#include "ppaint.h"

int pp_ch;                              /* lines per cell */
static int pp_cs;                       /* ... rounded up to 4 */
static int pp_slots, pp_used, pp_s;
static UBYTE *pp_bits;                  /* slot * cs: the glyphs' lines */
UBYTE *pp_blk;                          /* slot * bsz: shifted blocks */
LONG pp_bsz;                            /* 2 * cs */
ULONG *pp_blank;

/* The row's cells with four pads before and eight after: the groups at
 * the span's edges read cells c0-1 .. c0+3 beyond it. */
static ULONG *gpbuf[PP_MAXCOLS + 12];
static UWORD pwbuf[PP_MAXCOLS + 12];
ULONG **pp_gp = gpbuf + 4;
UWORD *pp_pw = pwbuf + 4;

void pp_free(void)
{
    if (pp_bits) FreeVec(pp_bits);
    if (pp_blk) FreeVec(pp_blk);
    pp_bits = pp_blk = NULL;
    pp_slots = pp_used = 0;
}

BOOL pp_init(int ch, int slots)
{
    pp_free();
    if (ch < 1 || ch > 32)
        return FALSE;
    pp_ch = ch;
    pp_cs = (ch + 3) & ~3;
    pp_bsz = 2 * pp_cs;
    pp_bits = AllocVec((LONG)slots * pp_cs, MEMF_ANY | MEMF_CLEAR);
    pp_blk = AllocVec((LONG)slots * pp_bsz, MEMF_ANY | MEMF_CLEAR);
    if (!pp_bits || !pp_blk) {
        pp_free();
        return FALSE;
    }
    pp_slots = slots;
    pp_used = 1;                        /* slot 0: blank */
    pp_s = 0;
    pp_blank = PP_BLOCK(0);
    return TRUE;
}

int pp_room(void)
{
    return pp_slots - pp_used;
}

void pp_clear(void)
{
    pp_used = 1;
}

/* One slot's block for phase s: per slab, hi = the four lines >> s,
 * lo = << 8-s (the part that spills into the next byte). */
static void shift_slot(int slot)
{
    const UBYTE *b = pp_bits + (LONG)slot * pp_cs;
    UBYTE *d = pp_blk + (LONG)slot * pp_bsz;
    int k, i;
    for (k = 0; k < pp_cs; k += 4, d += 8)
        for (i = 0; i < 4; i++) {
            d[i] = b[k + i] >> pp_s;
            d[4 + i] = pp_s ? (UBYTE)(b[k + i] << (8 - pp_s)) : 0;
        }
}

int pp_add(const UBYTE *lines)
{
    int slot;
    if (pp_used >= pp_slots)
        return -1;
    slot = pp_used++;
    memcpy(pp_bits + (LONG)slot * pp_cs, lines, pp_ch);
    memset(pp_bits + (LONG)slot * pp_cs + pp_ch, 0, pp_cs - pp_ch);
    shift_slot(slot);
    return slot;
}

void pp_phase(int s)
{
    int i;
    if (s == pp_s)
        return;
    pp_s = s;
    for (i = 1; i < pp_used; i++)
        shift_slot(i);
}

/* A block's glyph lines (one byte each): blocks are 2*cs bytes, lines cs */
const UBYTE *pp_lines(const ULONG *blk)
{
    return pp_bits + ((const UBYTE *)blk - pp_blk) / 2;
}

void pp_paint(UBYTE **planes, int depth, UBYTE mask, LONG bpr,
              LONG x, LONG y, int n)
{
    static struct PPRow f;
    LONG J0 = x >> 3, B0 = x, B1 = x + 8 * n, q0, q1, i, np = 0;
    ULONG mh, ml;
    int p;
    if (n <= 0 || n > PP_MAXCOLS)
        return;
    pp_phase(x & 7);
    /* the pads: blank glyphs in the edge cells' pens, so a row in one
     * colour has uniform edge groups too */
    for (i = 1; i <= 4; i++) {
        pp_gp[-i] = pp_blank;
        pp_pw[-i] = pp_pw[0];
    }
    for (i = 0; i < 8; i++) {
        pp_gp[n + i] = pp_blank;
        pp_pw[n + i] = pp_pw[n - 1];
    }
    q0 = B0 >> 5;
    q1 = (B1 - 1) >> 5;
    f.ng = q1 - q0 + 1;
    f.ch = pp_ch;
    f.bpr = bpr;
    f.gp = pp_gp + 4 * q0 - J0 - 1;
    f.pw = pp_pw + 4 * q0 - J0 - 1;
    f.ef = 0xFFFFFFFFUL >> (B0 - 32 * q0);
    f.el = B1 - 32 * q1 >= 32 ? 0xFFFFFFFFUL
         : ~(0xFFFFFFFFUL >> (B1 - 32 * q1));
    f.s = pp_s;
    mh = 0xFF >> pp_s;                  /* a byte's bits from its own cell */
    ml = ~mh & 0xFF;                    /* ... from the cell before */
    for (i = 0; i < 5; i++) {
        ULONG o = 0;
        if (i >= 1) o |= mh << ((4 - i) * 8);
        if (i <= 3) o |= ml << ((3 - i) * 8);
        f.own[i] = o;
    }
    for (p = 0; p < depth && p < 8; p++)
        if (mask & (1 << p)) {
            f.pt[np].dst = planes[p] + y * bpr + q0 * 4;
            f.pt[np].pbit = p;
            np++;
        }
    f.pt[np].dst = NULL;
    if (np)
        pp_row(&f);
}

void pp_paint_c(UBYTE **planes, int depth, UBYTE mask, LONG bpr,
                LONG x, LONG y, int n)
{
    int i, l, b, p;
    pp_phase(x & 7);
    for (i = 0; i < n; i++) {
        /* the glyph's lines back from its block: hi << s | lo >> 8-s */
        const UBYTE *blk = (const UBYTE *)pp_gp[i];
        UWORD pw = pp_pw[i];
        int fx = pw >> 8, fb = pw & 255;
        for (l = 0; l < pp_ch; l++) {
            const UBYTE *sl = blk + (l >> 2) * 8 + (l & 3);
            UBYTE g = (UBYTE)((sl[0] << pp_s) | (pp_s ? sl[4] >> (8 - pp_s) : 0));
            for (b = 0; b < 8; b++) {
                LONG px = x + i * 8 + b;
                UBYTE bit = 0x80 >> (px & 7);
                int on = g & (0x80 >> b) ? 1 : 0;
                for (p = 0; p < depth; p++) {
                    UBYTE *d = planes[p] + (y + l) * bpr + (px >> 3);
                    int v;
                    if (!(mask & (1 << p)))
                        continue;
                    v = (on & (fx >> p)) ^ ((fb >> p) & 1);
                    if (v) *d |= bit; else *d &= ~bit;
                }
            }
        }
    }
}
