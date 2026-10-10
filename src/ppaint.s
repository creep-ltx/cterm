; ppaint.s - CTerm's planar painter: one text row of 8-pixel-wide cells
; straight into the bitplanes of a plain (OCS/ECS/AGA) screen. vasm,
; Motorola syntax; linked into CTerm (draw.c builds the row, see
; "The planar painter" there).
;
; The idea is CCON's pfused (AmigaTools/ccon/engine/pgroups.s), written
; for the stock A1200: every chip access costs ~2.5 us against a 16-colour
; Hires display, so the work is counted in memory accesses. Per output
; long (four cells, a "group") and per four lines (a "slab"): the cells'
; glyph lines are read as longs (four lines of one cell each), put
; together and 4x4 byte-transposed in registers, then each plane gets its
; four longs, v = (glyph AND A) XOR X, written once. A Text() call per
; colour run, a template and a blit per plane, is what this replaces.
;
; Glyph blocks are pre-shifted for the window's bit phase s (the screen x
; of column 0, AND 7): per slab a "hi" long (the four lines >> s) and a
; "lo" long (<< 8-s), so output byte J = hi(cell J) | lo(cell J-1) and
; nothing carries between longs. A block is 2*cs bytes: slab k's hi at
; 8k, its lo at 8k+4. Any cell height up to 32 lines.
;
; Pens: each cell has a word fx<<8 | fb (fx = fg XOR bg, fb = bg); for
; plane p, A = -1 where fx has bit p, X = -1 where fb has. A group whose
; cells share pens is "uniform" (A/X are 0 or -1, worked out only when
; the pens change); otherwise each byte's two owners (cell J, cell J-1)
; give their bits through the owner masks OWN[0..4].
;
; Keep .slab .. .next short: it must run from the 020's 256-byte cache.

PP_GP	= 0		; ULONG *: glyph block of cell c0-1 of group 0
PP_NG	= 4		; groups (longs per line)
PP_CH	= 8		; lines
PP_PW	= 12		; UWORD *: pens of cell c0-1 of group 0
PP_BPR	= 16		; bytes per line
PP_EF	= 20		; the first group's edge mask
PP_EL	= 24		; the last group's
PP_S	= 28		; bit phase (0: cell c0-1 shows nothing)
PP_OWN	= 32		; 5 longs: bits of cell c0-1 .. c0+3 in a group
PP_K	= 52		; groups left
PP_E	= 56		; this group's edge mask (-1: whole)
PP_KEY	= 60		; pens of the A/X in PP_PT (-1: none)
PP_YY	= 64		; the slab's byte offset in a glyph block
PP_DOFF	= 68		; the slab's byte offset in a plane
PP_LL	= 72		; lines left from the slab on
PP_PT	= 76		; planes: dst.l, A.l, X.l, pbit.l ... then dst 0

	section	.text,code

;; void pp_row(struct PPRow *f)
	xdef	_pp_row
_pp_row:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a4
	move.l	PP_GP(a4),a0
	move.l	PP_BPR(a4),a5
	move.l	PP_NG(a4),PP_K(a4)
	moveq	#-1,d0
	move.l	d0,PP_KEY(a4)
	move.l	PP_EF(a4),d5		; the first group: its edge
.grp:	cmp.l	#1,PP_K(a4)
	bne.s	.nl
	and.l	PP_EL(a4),d5
.nl:	move.l	d5,PP_E(a4)
	; ---- the group's colour masks (only when its pens change) ----
	move.l	PP_PW(a4),a6
	move.l	2(a6),d0		; pens of c0, c0+1
	move.l	6(a6),d1		; c0+2, c0+3
	cmp.l	d0,d1
	bne	.mix
	move.l	d0,d2
	swap	d2
	cmp.l	d0,d2
	bne	.mix
	tst.l	PP_S(a4)
	beq.s	.uni
	cmp.w	(a6),d0			; c0-1 owns bits too
	bne	.mix
.uni:	moveq	#0,d2
	move.w	d0,d2
	cmp.l	PP_KEY(a4),d2
	beq	.slabs			; the same as the last group's
	move.l	d2,PP_KEY(a4)
	move.w	d2,d3
	lsr.w	#8,d3			; fx (d2: fb in the low byte)
	lea	PP_PT(a4),a2
.u:	tst.l	(a2)
	beq	.slabs
	move.l	12(a2),d6		; the plane's bit
	btst	d6,d3
	sne	d7
	extb.l	d7
	move.l	d7,4(a2)		; A
	btst	d6,d2
	sne	d7
	extb.l	d7
	move.l	d7,8(a2)		; X
	lea	16(a2),a2
	bra.s	.u
.mix:	moveq	#-1,d2
	move.l	d2,PP_KEY(a4)
	lea	PP_PT(a4),a2
.m:	tst.l	(a2)
	beq	.slabs
	move.l	12(a2),d6
	moveq	#0,d2			; A
	moveq	#0,d7			; X
	moveq	#0,d1			; owner * 4
	move.l	a6,a3
.mo:	move.w	(a3)+,d4
	btst	d6,d4
	beq.s	.m1
	or.l	PP_OWN(a4,d1.l),d7
.m1:	lsr.w	#8,d4
	btst	d6,d4
	beq.s	.m2
	or.l	PP_OWN(a4,d1.l),d2
.m2:	addq.l	#4,d1
	cmp.l	#20,d1
	bne.s	.mo
	move.l	d2,4(a2)
	move.l	d7,8(a2)
	lea	16(a2),a2
	bra.s	.m
	; ---- four lines at a time: the cached part ----
.slabs:	sub.l	a6,a6			; the slab's offset in a plane
	moveq	#0,d5
	move.l	d5,PP_YY(a4)
	move.l	PP_CH(a4),PP_LL(a4)
.slab:	move.l	PP_YY(a4),d5
	move.l	(a0),a1
	move.l	4(a1,d5.l),d0		; lo(c0-1)
	move.l	4(a0),a1
	or.l	0(a1,d5.l),d0		; hi(c0): byte column 0
	move.l	4(a1,d5.l),d1
	move.l	8(a0),a1
	or.l	0(a1,d5.l),d1
	move.l	4(a1,d5.l),d2
	move.l	12(a0),a1
	or.l	0(a1,d5.l),d2
	move.l	4(a1,d5.l),d3
	move.l	16(a0),a1
	or.l	0(a1,d5.l),d3
	move.l	#$FF00FF00,d6
	move.l	#$00FF00FF,d7
	move.l	d0,d4			; transpose a,b,c,d -> lines 0..3
	and.l	d6,d0
	lsl.l	#8,d4
	and.l	d6,d4
	move.l	d1,d5
	lsr.l	#8,d5
	and.l	d7,d5
	or.l	d5,d0
	and.l	d7,d1
	or.l	d4,d1
	move.l	d2,d4
	and.l	d6,d2
	lsl.l	#8,d4
	and.l	d6,d4
	move.l	d3,d5
	lsr.l	#8,d5
	and.l	d7,d5
	or.l	d5,d2
	and.l	d7,d3
	or.l	d4,d3
	move.l	d0,d5
	swap	d5
	move.w	d2,d5
	swap	d2
	move.w	d2,d0
	move.l	d5,d2
	move.l	d1,d5
	swap	d5
	move.w	d3,d5
	swap	d3
	move.w	d3,d1
	move.l	d5,d3
	; per plane only its dst, A and X are read: the slab's line count
	; (a1), its offset (a6) and whether it is an edge are known here
	move.l	PP_LL(a4),d5
	cmp.l	#4,d5
	ble.s	.ll
	moveq	#4,d5
.ll:	move.l	d5,a1
	lea	PP_PT(a4),a2
	cmp.l	#-1,PP_E(a4)
	bne	.pe
.pl:	movem.l	(a2),d4/d6/d7		; dst, A, X
	tst.l	d4
	beq.s	.pd
	lea	16(a2),a2
	move.l	d4,a3
	add.l	a6,a3
	move.l	a1,d5
	ifd	NOWRITE
	bra	.pl
	endif
	move.l	d0,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	subq.l	#1,d5
	beq.s	.pl
	add.l	a5,a3
	move.l	d1,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	subq.l	#1,d5
	beq.s	.pl
	add.l	a5,a3
	move.l	d2,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	subq.l	#1,d5
	beq.s	.pl
	add.l	a5,a3
	move.l	d3,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	bra.s	.pl
.pd:	addq.l	#8,PP_YY(a4)
	move.l	a5,d5
	lsl.l	#2,d5
	add.l	d5,a6
	subq.l	#4,PP_LL(a4)
	bgt	.slab
.next:	; the next group: four cells on, every plane's dst one long on
	lea	16(a0),a0
	addq.l	#8,PP_PW(a4)
	lea	PP_PT(a4),a2
.adv:	tst.l	(a2)
	beq.s	.add
	addq.l	#4,(a2)
	lea	16(a2),a2
	bra.s	.adv
.add:	moveq	#-1,d5
	subq.l	#1,PP_K(a4)
	bne	.grp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
; an edge group's lines (outside the cached loop): the pixels outside
; the span are kept, ((v ^ dst) & E) ^ dst
.pe:	movem.l	(a2),d4/d6/d7
	tst.l	d4
	beq	.pd
	lea	16(a2),a2
	move.l	d4,a3
	add.l	a6,a3
	move.l	a1,d5
	move.l	d0,d4
	bsr.s	.per
	beq.s	.pe
	move.l	d1,d4
	bsr.s	.per
	beq.s	.pe
	move.l	d2,d4
	bsr.s	.per
	beq.s	.pe
	move.l	d3,d4
	bsr.s	.per
	bra.s	.pe
.per:	and.l	d6,d4
	eor.l	d7,d4
	move.l	d6,-(sp)
	move.l	(a3),d6
	eor.l	d6,d4
	and.l	PP_E(a4),d4
	eor.l	d6,d4
	move.l	(sp)+,d6
	move.l	d4,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	rts

; ---------------------------------------------------------------------
; pp_row1 / pp_row1z - pp_row for ONE plane (PP_PT's first entry), the
; common row: text in one colour on a 16-colour Workbench differs from
; the background in one plane. A and X stay in d6/d7, the slab's index
; in a6, nothing per slab is read from memory but the glyphs. pp_row1z
; is for bit phase 0 (the window on a byte boundary): no lo parts, so
; four glyph reads a slab instead of eight. Same struct PPRow.

ROW1	macro				; \1: 1 = phase 0
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a4
	move.l	PP_GP(a4),a0
	move.l	PP_BPR(a4),a5
	move.l	PP_PT(a4),a2		; dst: this group, line 0
	move.l	PP_NG(a4),PP_K(a4)
	moveq	#-1,d0
	move.l	d0,PP_KEY(a4)
	move.l	PP_EF(a4),d5
.g\@:	cmp.l	#1,PP_K(a4)
	bne.s	.g1\@
	and.l	PP_EL(a4),d5
.g1\@:	move.l	d5,PP_E(a4)
	; ---- A (d6), X (d7) when the group's pens change ----
	move.l	PP_PW(a4),a1
	move.l	2(a1),d0
	move.l	6(a1),d1
	cmp.l	d0,d1
	bne.s	.mx\@
	move.l	d0,d2
	swap	d2
	cmp.l	d0,d2
	bne.s	.mx\@
	ifeq	\1
	cmp.w	(a1),d0			; c0-1 owns bits too
	bne.s	.mx\@
	endif
	moveq	#0,d2
	move.w	d0,d2
	cmp.l	PP_KEY(a4),d2
	beq.s	.sl0\@
	move.l	d2,PP_KEY(a4)
	move.l	PP_PT+12(a4),d4		; the plane's bit
	move.w	d2,d3
	lsr.w	#8,d3
	btst	d4,d3
	sne	d6
	extb.l	d6
	btst	d4,d2
	sne	d7
	extb.l	d7
	bra.s	.sl0\@
.mx\@:	moveq	#-1,d2
	move.l	d2,PP_KEY(a4)
	move.l	PP_PT+12(a4),d4
	moveq	#0,d6
	moveq	#0,d7
	moveq	#0,d1
.mo\@:	move.w	(a1)+,d2
	btst	d4,d2
	beq.s	.m1\@
	or.l	PP_OWN(a4,d1.l),d7
.m1\@:	lsr.w	#8,d2
	btst	d4,d2
	beq.s	.m2\@
	or.l	PP_OWN(a4,d1.l),d6
.m2\@:	addq.l	#4,d1
	cmp.l	#20,d1
	bne.s	.mo\@
	; ---- the slabs ----
.sl0\@:	sub.l	a6,a6			; slab offset in the blocks
	move.l	a2,a3			; the line written next
.sl\@:	ifne	\1
	move.l	4(a0),a1
	move.l	0(a1,a6.l),d0		; hi(c0): no lo parts at phase 0
	move.l	8(a0),a1
	move.l	0(a1,a6.l),d1
	move.l	12(a0),a1
	move.l	0(a1,a6.l),d2
	move.l	16(a0),a1
	move.l	0(a1,a6.l),d3
	else
	move.l	(a0),a1
	move.l	4(a1,a6.l),d0		; lo(c0-1)
	move.l	4(a0),a1
	or.l	0(a1,a6.l),d0		; hi(c0)
	move.l	4(a1,a6.l),d1
	move.l	8(a0),a1
	or.l	0(a1,a6.l),d1
	move.l	4(a1,a6.l),d2
	move.l	12(a0),a1
	or.l	0(a1,a6.l),d2
	move.l	4(a1,a6.l),d3
	move.l	16(a0),a1
	or.l	0(a1,a6.l),d3
	endif
	move.l	d0,d4			; transpose a,b,c,d -> lines 0..3
	and.l	#$FF00FF00,d0
	lsl.l	#8,d4
	and.l	#$FF00FF00,d4
	move.l	d1,d5
	lsr.l	#8,d5
	and.l	#$00FF00FF,d5
	or.l	d5,d0
	and.l	#$00FF00FF,d1
	or.l	d4,d1
	move.l	d2,d4
	and.l	#$FF00FF00,d2
	lsl.l	#8,d4
	and.l	#$FF00FF00,d4
	move.l	d3,d5
	lsr.l	#8,d5
	and.l	#$00FF00FF,d5
	or.l	d5,d2
	and.l	#$00FF00FF,d3
	or.l	d4,d3
	move.l	d0,d5
	swap	d5
	move.w	d2,d5
	swap	d2
	move.w	d2,d0
	move.l	d5,d2
	move.l	d1,d5
	swap	d5
	move.w	d3,d5
	swap	d3
	move.w	d3,d1
	move.l	d5,d3
	move.l	a6,d5			; lines left: ch - slab*4
	lsr.l	#1,d5
	neg.l	d5
	add.l	PP_CH(a4),d5
	cmp.l	#-1,PP_E(a4)
	bne.s	.pe\@
	move.l	d0,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	beq.s	.nx\@
	move.l	d1,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	beq.s	.nx\@
	move.l	d2,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	beq.s	.nx\@
	move.l	d3,d4
	and.l	d6,d4
	eor.l	d7,d4
	move.l	d4,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	beq.s	.nx\@
	addq.l	#8,a6
	bra	.sl\@
	; an edge group: the pixels outside the span are kept
.pe\@:	movem.l	d0-d3,-(sp)
	move.l	PP_E(a4),d0
	moveq	#0,d3
.pel\@:	move.l	0(sp,d3.l*4),d1
	and.l	d6,d1
	eor.l	d7,d1
	move.l	(a3),d2
	eor.l	d2,d1
	and.l	d0,d1
	eor.l	d2,d1
	move.l	d1,(a3)
	add.l	a5,a3
	subq.l	#1,d5
	beq.s	.ped\@
	addq.l	#1,d3
	cmp.l	#4,d3
	bne.s	.pel\@
	lea	16(sp),sp
	addq.l	#8,a6
	bra	.sl\@
.ped\@:	lea	16(sp),sp
	; ---- the next group ----
.nx\@:	lea	16(a0),a0
	addq.l	#8,PP_PW(a4)
	addq.l	#4,a2
	moveq	#-1,d5
	subq.l	#1,PP_K(a4)
	bne	.g\@
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	endm

	xdef	_pp_row1
_pp_row1:
	ROW1	0
	xdef	_pp_row1z
_pp_row1z:
	ROW1	1
