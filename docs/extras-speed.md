# Speed on a stock A1200 with fast RAM: the 2026-10-15 extras

The target: an A1200 with its 14 MHz 68EC020 and 8 MB fast RAM (CTerm needs
fast RAM), conbench SYNC REPS 3 SCALE 1 in a 77x30 window (76x28 fits on the
640x256 Workbench), against CCON 1.2.8 on the same machine. Keep the
68030/50 no worse. Every step below measured alone, builds alternated in one
boot, three rounds unless said; every change pixel-identical and the Linux
tests green.

## The numbers (conbench TOTAL, s; fast-RAM A1200, FS-UAE cycle-exact)

| Build | rounds | mean |
|---|---|---|
| 0.3b41 (git 3ccd2b4) | 17.70, 17.76, 17.60 | 17.69 |
| 0.3b45 (today's main before this work) | 18.06, 17.98, 17.96 | 18.00 |
| this work, before planes_fit's last form | 15.24, 15.18, 15.06 | 15.16 |
| 0.3b45 again (a later boot) | 18.16, 17.86, 17.84 | 17.95 |
| this work, final | 15.02, 15.04, 15.00 | 15.02 |
| CCON 1.2.8 | 15.68, 15.66, 15.68 | 15.67 |

So today's extras (OSC 8 links in the pen table, kitty in vt.c, find, drop)
had cost ~0.3 s (+1.7%; the parser's sgr/perchar paths +3% in instructions);
this work is 16% faster than 0.3b45, 15% faster than 0.3b41, and 4% ahead
of CCON (it was 15% behind). The spread of the final build: 0.04 s.

Per test (s, means of the last three alternated rounds), 0.3b45 / final /
CCON:

| test | 0.3b45 | final | CCON |
|---|---|---|---|
| plain-lines | 1.32 | 0.95 | 1.01 |
| block-4k | 0.74 | 0.39 | 0.70 |
| block-32k | 0.65 | 0.34 | 0.52 |
| bytewise | 1.34 | 1.33 | 1.13 |
| scroll-nl | 1.09 | 0.81 | 0.78 |
| wrap-long | 0.49 | 0.28 | 0.35 |
| sgr-colour | 4.43 | 3.95 | 3.69 |
| sgr-perchar | 2.16 | 1.93 | 1.69 |
| cursor-pos | 1.25 | 1.19 | 1.20 |
| vt-frame | 0.96 | 0.87 | 0.94 |
| insdel-line | 0.15 | 0.12 | 0.29 |
| insdel-char | 0.62 | 0.58 | 0.55 |
| clear-page | 1.21 | 0.95 | 1.04 |
| erase-eol | 1.13 | 1.01 | 0.98 |
| sync-line | 0.42 | 0.34 | 0.80 |

Still behind CCON: bytewise (+0.20: one-byte writes are the packet round
trip, CTerm's floor there is 1.10 s), colour runs (sgr-colour +0.26,
sgr-perchar +0.24: four-plane rows in the painter, ~3.5 ms each) and
scroll-nl (+0.03).

Where the time is now (build switches, one round): packets alone
(-DNODRAW -DNOPARSE) 8.34 s, of which conbench itself (writing to NIL:) 3.92;
everything but drawing (-DNODRAW) 12.26 s, so the parser and model 3.9 s
(was 5.9) and drawing 3.1 s (was 3.6).

## The steps, each measured alone

| Step | conbench before -> after (s) |
|---|---|
| parser 1: the letter loop in 68020 asm (8 instructions a letter, gcc made 13); a CSI ending in `m` straight to sgr() (csi() has nothing of its own for it), parsed into v->param directly (libnix's memcpy is a CopyMem call in ROM, even for 4 bytes); a pen hash with one multiply (was three, ~44 cycles each); the fast lane's dirty marks inline and its new column without dividing by the cell's 6 bytes; CR and a single LF without byte()/control(); the whole-screen scroll with a full history in place (no swap_into_history call) | 17.89 -> 16.06 |
| parser 2: linefeed's usual case (whole screen, history full, blank new row) in a few stores; four letters checked at once (SWAR) from 8 on | 16.13 -> 15.64 |
| drawing 1: pp_pens' answers kept per look for the draw (64 entries, a new generation at every rp_forget) - working a look out (RGB, two pens) was ~1000 cycles, 40 times a sgr-perchar row | 15.60 -> 15.48 |
| drawing 2: the 2-pixel margin left of column 0 painted with the row by the painter (pp_paint's `lead`, the pad cell in the background's pens) instead of a RectFill per row (~0.3 ms each) | 15.57 -> 15.25 |
| drawing 3: planes_fit - after a draw of the only pane (a full redraw, or one that scrolled) the plane mask shrinks to the planes the looks on show use; nothing cleared (see below) | 14.96 -> 15.03 (conbench: the same; see below) |

planes_fit's worth is not in conbench (its best-of-3 takes the early tests
from the first round, before any colour): the plane mask (the planes some
pen on show differs from the background in; ScrollRaster and the painter
touch only those) only ever grew, so after one page of colours every scroll
moved all four planes until the next full clear. tests/perf/scrolltime.c:
a 616x224 ScrollRaster on this Amiga costs 12.2 / 24.4 / 48.6 ms for 1 / 2 /
4 planes. Measured (PROFILE build: colours, a flood of plain lines, then
cbone bytewise 2): 13 scrolls at 43 ms before, 31 scrolls at 12.7 ms with
the fit - the screen keeps up better in the same time.

How it stays correct: every row is drawn with all the mask's planes, so a
plane that none of a row's looks uses holds the background's bits there -
as the planes outside the mask already do. So the mask may shrink to what
the rows on show need with nothing cleared, but only *after* the draw: a
changed span drawn with the smaller mask would leave the old letters' bits
in a dropped plane (a first version cleared the dropped planes before the
draw - safe but a blit of the text area each time). The look at all cells
(~3 ms, with pp_pens' cache) runs only while the mask holds more than plain
text needs; after a look that found colours on show, the next two scrolling
draws skip it. Not with pictures on show, panes, the scratchpad, a transfer
panel or the Find line.

Tools (tests/perf/vttime.sh) on the same Amiga, us per item, 0.3b45 -> now:
a plain line (one write) 982 -> 691, 51 lines in one write 36130 -> 19152,
a line of 8 colour runs 2406 -> 1810, colour per letter 9366 -> 7053, ten
new lines 652 -> 454, a 300-letter wrapped line 2512 -> 1623.

## Measured and dropped

- **The painter's slab writes reordered** (per plane the cheap cases first:
  A = 0 writes X, A = -1 / X = 0 writes the glyph, lines 1-3 by scaled
  index, no line count for whole slabs): 12% fewer instructions per row
  (vamos), but 2% faster at best in fast RAM and 3% *slower* into chip RAM
  (pptime): four chip stores back to back stall the 020, where the old
  and/eor between them hid the chip bus. Kept the old loop.
- **A cache of mixed groups' A/X in pp_row** (32 entries by a hash of the
  five pens): -1 to -2% on colour rows, within the code-layout noise of
  unrelated builds (+-2%). Not worth 4 KB and the code.
- **Skipping the left margin RectFill outright** (15.50 -> 15.28): the same
  gain as painting it, but nothing would repair a margin something else
  drew into. The painter's lead does it safely.
- **The pacing** (3x a draw's cost, at most 320 ms) left alone: fewer draws
  in a flood would win conbench time but the screen would update less often.
- **A cache of what each screen row already shows** (skip repainting a row
  that would come out the same): conbench's lines are all alike, so it would
  flatter the benchmark far more than real output; and every way the screen
  can change behind it (pen renumbering, themes, damage, the scratchpad) would
  have to clear it. Not done.

## How it was measured (and the traps)

- **tests/stock/bench.sh** now runs on its own rig (`ctspeed`, wasabid port
  1254, a copy of the shared clean `a1200-32-mui5`, `accuracy = 1`), with
  conbench and CCON copied in from the old dev drive (CCON mounted as CCON:
  only, its open string `DEFAULTS`) and the old stock runs' Workbench (PAL
  Hires, 16 colours). The rig stays up; `bench.sh tests N BIN...` alternates
  the builds N rounds and prints the mean per test. CONF=030: the 68030/50.
- **Spread:** the same build three times in one boot spreads ~1.5% (0.28 s):
  flood pacing (a draw every 3x the last one's cost) makes how much is drawn
  depend on timing. CCON, without that, repeats to 0.02 s. Compare means of
  alternated rounds; trust a step only beyond ~0.15 s.
- **Leftover CTerms** make every run slower: ten left running by a test
  script took the rig's memory and conbench went from 15.2 to 19.7 s.
  bench.sh and look.sh end any CTerm left over with a Ctrl-C (ARexx QUIT
  needs Parts/ beside the program; SYS:T/CTerm has none).
- **tests/perf/vttime.sh** (vt.c alone, E clock, on the rig) and
  **pptime.sh** (the painter alone, fast or chip RAM) repeat to ~1%, but a
  change anywhere moves unrelated code against the 020's 256-byte cache:
  paths not touched moved up to 7%. Look at the item you changed.
- **Costs on this machine** (vttime's calibration items): a register ALU
  instruction ~2.7 cycles, a long load from fast RAM ~5.3, a store ~3.1,
  `movem.l` of three longs ~17, subq+bne 8.6; C compiled by gcc averages ~5
  cycles an instruction. Cutting instructions that are not memory accesses
  gains little; a 32-bit multiply is ~44 cycles (gcc uses one for x*6, the
  cell size).
- **tests/perf/vtsame.sh** builds this tree's vt.c and REF's (HEAD) for the
  68020, runs both under vamos over the test streams and mkstress.py's
  (conbench's shapes and seeded random mixes), whole and in 1/7/64-byte
  chunks, in UTF-8, the Amiga's Latin-1 and CP437, and compares every cell's
  look, the cursor and vt_take_changes' marks; also against the PC build (the
  C fallback of the asm). vamos traps: a path with a leading `/` is the
  parent directory (AmigaDOS), so it runs from the streams' drawer with bare
  names; getenv reads ENV:, not the host's environment (the mode is an
  argument); two vamos runs at once can break one (`~/.vamos/volumes/ram`) -
  a differing run is run again.
- **Pixels:** tests/stock/look.sh grabs the test page (lookpage.py) on the
  rig; SCROLL=1/2/3/4 (scrolled, colours flooded off and back, single lines
  after a flood, colours overwritten in place then scrolled) and MOVE=13 (a
  narrower window dragged off the 32-pixel grid: edge cells by Text(), the
  margin by RectFill). Every one the same
  pixels as 0.3b45 (tests/stock/samepng.py). make pptest (the painter's asm
  against its C reference, now with the margin pixels and repeated colour
  patterns) 20000 rows, 0 bad.

## Verified / not verified

- Verified (FS-UAE, the rig): the numbers above; pixel-identical grabs
  (plain page, scrolled, colours flooded off and back, colours overwritten
  in place then scrolled, dragged window); vtsame (all streams, chunks,
  modes); pptest; make test and make fuzz green; the 68030/50: conbench
  0.3b45 18.76 / 18.84 / 19.08 s, now 16.02 / 15.84 / 15.94 s (-15.7%).
- Not tried: the real A1200 (PiStorm/Emu68, 32-bit RTG: the painter there is
  the RTG one, untouched; the parser work applies); a chip-only Amiga
  (CTerm refuses it now); panes and the scratchpad with planes_fit (it stays
  off with more than one pane or the scratchpad shown).
