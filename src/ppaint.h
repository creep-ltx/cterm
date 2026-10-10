/* ppaint.h - the planar painter: text rows of 8-pixel cells straight
 * into the bitplanes of a plain (OCS/ECS/AGA) screen. The inner loop is
 * 68020 assembly (ppaint.s); ppaint.c keeps the glyph store and sets a
 * row up. No window code here: tests/pptest.c runs it under vamos. */
#ifndef PPAINT_H
#define PPAINT_H

#include <exec/types.h>

/* What ppaint.s reads (offsets in its PP_* names). */
struct PPRow {
    ULONG **gp;             /* glyph block of cell c0-1 of group 0 */
    LONG ng, ch;
    UWORD *pw;              /* pens (fx<<8 | fb) of cell c0-1 of group 0 */
    LONG bpr;
    ULONG ef, el;
    LONG s;
    ULONG own[5];
    LONG k;                 /* the rest: the asm's own */
    ULONG e;
    LONG key, yy, doff, ll;
    struct { UBYTE *dst; LONG pbit; ULONG a, x; } pt[9];
};
void pp_row(struct PPRow *f);
void pp_row1(struct PPRow *f);          /* one plane */
void pp_row1z(struct PPRow *f);         /* one plane, bit phase 0 */

#define PP_MAXCOLS 512      /* cells one row can have */

/* The glyph store: each glyph's lines as bytes (bit 7 = the cell's left
 * pixel), and a block pre-shifted for the bit phase. Slot 0 is blank. */
BOOL pp_init(int ch, int slots);
void pp_free(void);
int pp_room(void);                      /* slots still free */
void pp_clear(void);                    /* all but the blank one gone */
int pp_add(const UBYTE *lines);         /* ch bytes; the new slot */
void pp_phase(int s);                   /* blocks for this phase (0..7) */
extern int pp_ch;
const UBYTE *pp_lines(const ULONG *blk);  /* a block's ch lines */
extern ULONG *pp_blank;                 /* slot 0's block */
#define PP_BLOCK(slot) ((ULONG *)(pp_blk + (LONG)(slot) * pp_bsz))
extern UBYTE *pp_blk;
extern LONG pp_bsz;

/* One row: the caller fills pp_gp[0..n-1] (PP_BLOCK of each cell's
 * glyph) and pp_pw[0..n-1] (fx<<8 | fb), then calls pp_paint. planes[p]
 * is plane p at the screen's line 0, byte 0; x, y the screen pixel of
 * the first cell's top left; only planes in mask are written. */
extern ULONG **pp_gp;
extern UWORD *pp_pw;
void pp_paint(UBYTE **planes, int depth, UBYTE mask, LONG bpr,
              LONG x, LONG y, int n);
/* The same in plain C, pixel by pixel: the test's reference. */
void pp_paint_c(UBYTE **planes, int depth, UBYTE mask, LONG bpr,
                LONG x, LONG y, int n);

#endif
