# Pictures in the terminal: the 2026-10-15 extras

Four things the 0.3b16 pictures left open (design.md, "Not done"): kitty's
graphics shown, sixel's pixel aspect, iTerm2's MultipartFile, and pictures
redrawn at a new letter size. All in vt.c's picture code (no Amiga headers,
tested on Linux), the zlib unpacking in Parts/Pictures. PART_VERSION 16
(struct PicsPart got `inflate`).

## What was built, and how

### kitty's graphics protocol (APC `ESC _ G keys ; base64 ESC \`)

- **Parsing:** `ESC _` (and C1 0x9F in Latin-1) goes to S_APC while pictures
  are on (off: S_STR, swallowed as before). The keys collect in `v->osc`
  (the OSC buffer: an APC and an OSC are never open at once); at `;` the
  data goes through S_FILE, the same base64 loop and growing buffer as
  iTerm2's files (`struct FileIn`, now with `kind` FI_FILE / FI_PART /
  FI_KITTY, `more` and the keys `kv[52]`, A-Z a-z). An APC without `;`
  (a=d, a=p, a=q, an empty last chunk) ends in `apc_done`. Anything not
  starting with `G` is swallowed. `file_stop` decides at ST what a FileIn
  does: iTerm2's ends, a MultipartFile piece waits, kitty's waits while
  `m=1`.
- **Chunks:** the first chunk brings all keys; later ones only `m=` (and
  `q=`). `=` padding in the middle of the data resets the leftover bits
  (kitty's chunks may each be padded); a continuous base64 stream never has
  `=` inside, so iTerm2's is unaffected.
- **Formats:** f=24 (RGB), f=32 (RGBA, alpha over the theme's background,
  as Parts/Pictures does for PNGs), f=100 (PNG, through `vt_decode`: the
  datatypes, as iTerm2's files). o=z: zlib, through the new hook
  `vt_inflate` (CTerm: `pics_inflate` -> Parts/Pictures -> src/inflate.c,
  ~1.7 KB, puff.c's way: canonical Huffman a bit at a time, every read and
  write bounded; Adler-32 not checked). o=z only with f=24/32 (the size is
  known: s*v*bpp); a zlib'ed PNG is refused. Only t=d (data in the code):
  t=f/t/s (a file or shared memory on the other computer) answer EBADF,
  and programs (icat) fall back to t=d.
- **Images and placements in the tile design:** a kitty image is a VtPic
  like any other (`kitty`, `kid` = i=, `knum` = I=, `kept`). a=t keeps it
  (by id) with no cell showing it: pic_gc keeps `kept` pictures. a=T with
  an id keeps it and shows it; without one it is shown only. a=p shows a
  kept one: at its own size and the current cell, the kept VtPic itself
  is placed (no copy); otherwise (c=/r=, x/y/w/h, a new letter size since)
  a scaled copy (`pic_scale`, nearest pixel), marked with the same kid so
  deletes find it. While the copy is made, the source is `pic_busy`:
  pic_gc never drops it to make room. Over the cap, pic_gc drops kitty's
  kept-but-unshown images first, then the oldest.
- **Sizes:** c and r cells (both: stretched to the box, as kitty; one: in
  shape), x/y/w/h a part of the image, cut at the right edge (as kitty
  clips; the source part is cut to match, so no pixels are kept that no
  cell shows) and at 511 tile rows. X/Y (pixel offsets in the cell) and z
  (under the text) are ignored: a tile is a whole cell, written over the
  text.
- **The cursor:** as iTerm2's (PL_FILE: the line below, the next LF not a
  second one); C=1: PL_STAY, the picture at the cursor, no scroll (cut at
  the bottom), the cursor stays. kitty itself leaves the cursor right of
  the last row; CTerm keeps the iTerm2 way it already had (a prompt never
  lands beside the picture).
- **Deletes (a=d):** d=a (all of kitty's on the screen), i (by id, also in
  the history), n (by number), r (ids x..y), c (at the cursor), p (cell
  x;y), x (column), y (row). Upper case: the kept image goes too. Others
  (z, f, q) do nothing.
- **Answers:** only when i= or I= was given and q= allows (q=1 no OK, q=2
  nothing): `ESC _G i=ID[,I=N][,p=P];OK ESC \` or `;ECODE:message`. a=q
  checks the data (decodes it) and answers without showing. I= without
  i=: an id is made (0x40000000 + n) and answered. Pictures off: no
  answer, so programs see no support (they send DA1 after the question,
  and DA1 comes first).

### Sixel's pixel aspect

P1 (0, 1, 5, 6 and none: 2:1; 2: 5:1; 3, 4: 3:1; 7-9: 1:1, the VT340's
table) and the raster attributes `"Pan;Pad;Ph;Pv` (they say it again, and
img2sixel/chafa/ImageMagick send `"1;1`). A sixel pixel is drawn `ay`
pixels tall (round(Pan/Pad), 1..10) and `ax` wide (when Pad > Pan); Ph/Pv
count sixel pixels and are multiplied. Done in sixel_put / '-' (6*ay rows a
band), no copy afterwards. **Behaviour change:** a sixel with no raster
attributes and P1 0/1 (or none) is now twice as tall, as on a VT340 and
xterm; before, every sixel was 1:1 (as foot).

### iTerm2's MultipartFile

`OSC 1337;MultipartFile=args ST` opens a FileIn (FI_PART) from osc_done;
each `OSC 1337;FilePart=` switches to S_FILE at the `=` (so a piece may be
longer than OSCMAX); ST leaves it waiting; `OSC 1337;FileEnd ST` ends it
as one File= (the same sizing, placement, cursor). Text between pieces
shows as text. A new File=, MultipartFile or kitty picture drops an
unfinished one.

### Pictures at a new letter size

`vt_set_cell_px` with a new cell size and pictures in the tab calls
`pic_recut`. Each picture keeps its box of cells (ow/ocw x oh/och, the
cells it was placed in: nothing moves in the text) and is scaled to the new
cell in shape (the smaller of the two factors, so a font of another shape
leaves room beside or below, never stretches), from its pixels as they came
(`orgb`, kept while `rgb` is a copy; back at the cell it came for, the copy
is freed and the originals used). The copy counts against the cap with
`pic_fits` (no picture is dropped for it); no room: the picture's own
pixels, cut at the new cell size (`p->cw/ch` set: it shows at its pixel
size, cropped to its box or with room around it). draw.c's `pic_tile` now
returns "nothing" for a tile past the picture's pixels (it gave a negative
width: pic_pens8 would have written before its buffer).

Every tab on the shown workspace gets the new cell size (layout_and_resize),
so a tab switch between tabs with different letters (phone book entries'
fonts) rescales the hidden tab's pictures too, and back when it shows. The
copies cost time only then.

## Traps found

- **Typing ahead while a picture is typed in the Shell:** the console
  echoes typed-ahead keys into the same stream at once, so the echo of a
  command typed while `Type Pictures/bars.kitty` ran landed inside the
  picture's code and was taken as base64 (the command still ran; its line
  just never showed, and the next picture started right of the prompt).
  Not picture code; the same can happen with any escape sequence a program
  is printing. Seen in FS-UAE 2026-10-15.
- **Settings, Text tab, Cancel, then ARexx QUIT freezes the Amiga** (FS-UAE,
  own 8-bit screen): reproduced twice with this branch and once with the
  build before it (13735e8): not from the pictures. Without opening the
  Text tab, QUIT is fine. Left for the main session.
- The PNG decode through datatypes takes several seconds on the emulated
  A1200: kitty's OK for an f=100 picture came after the test server's 1.5 s
  wait (it waits 4 s now).
- kitty's `kitty_image` is the biggest piece; the whole kitty part is
  ~4 KB of 68020 code (-Os), sixel aspect and MultipartFile a few hundred
  bytes. CTerm 271,612 -> 278,308 bytes (vt.o text 29,792 -> ~35,000);
  Parts/Pictures 3,560 -> 5,264 (inflate). Kept in vt.c: the logic lives
  in the picture table and the cells.

## Verified

- **Linux** (tests/pic_test.c in `make test`): kitty's question answered
  before DA1, t=f refused, broken zlib, a=T f=24 placed (cells, cursor,
  pixels, kept), f=32 zlib'ed in chunks of 64 and of 4 fed in pieces of 37
  (alpha over the background), a=t kept unseen then a=p c=4 r=1, a part
  (x,y,w,h), C=1, ENOENT, q=1/q=2, deletes d=i / d=I / d=a (a sixel stays)
  / d=p / d=c, f=100 through the PPM decoder, the same id sent again, I=
  given an id, nonsense swallowed, over the cap: ENOMEM, all memory back,
  pictures off: no answer. Sixel aspect: P1 0/7/2 without raster, raster
  2;1, 1;1 and 3;1 over P1=9 (sizes and doubled pixels). MultipartFile:
  as File= (size, cursor, pixels), and a broken one with text between.
  New letter size: 8x16 -> 16x32 scaled 2x (memory: 1,536 bytes -> 7,680
  with the original kept), 8x8 in shape (8x16 inside its box), back to
  8x16 (copy freed, exact pixels), no room (shown as it is, nothing
  dropped), a picture in the history scaled too. inflate.c against
  Python's zlib, levels 0/1/9 (stored, fixed, dynamic blocks, 160 KB),
  more than max and cut short give -1.
- **Fuzzer** (tests/fuzz/fz.c, ASan/UBSan): kitty, MultipartFile and
  aspect fragments, random cell sizes, broken zlib (4000 bit-flipped
  dynamic and stored streams a seed); the invariant is now "inside its box
  of cells". Seeds 1..200 passed (gcov: kitty_*, pic_recut, osc_1337 all
  reached).
- **FS-UAE** (2026-10-15, A1200 + 8 MB, own 8-bit screen and a 4-colour
  Workbench, tests/pictures/kittyserve.py on 2141 over the emulated
  bsdsocket): `ESC_Gi=31;OK ESC\` then `ESC[?62;4;22c` for icat's question;
  a=T f=24 i=5 OK; f=32 zlib'ed in chunks of 64 (Parts/Pictures' inflate);
  the Kickstart PNG as f=100 c=24 through datatypes; a=t + a=p c=16 r=1;
  sixel 2:1 twice as tall as 1:1; MultipartFile PNG width=20; d=i i=9
  removed it from screen and history, a=p showed it again, d=I then a=p:
  ENOENT. Font topaz 8 -> courier 15 (pictures scaled in shape in their
  boxes; Avail: the copies took 102,944 bytes) -> topaz 8 (back exactly,
  the memory back). In the Shell tab: `Type` of bars.kitty, kickstart.kitty
  and bars-2to1.six; kickstart.kitty took 108,392 bytes, a clear gave all
  but 1,128 back (lines of history). Settings' Text tab with the longer
  label: same width.

## Not verified

- A real `kitty +kitten icat`, timg, chafa or yazi (none installed here):
  the streams were made by tests/pictures/mksixel.py from kitty's
  protocol description; icat's question is copied from its source.
- RTG (no Picasso96 on the test machine) and the real A1200.
- Not done: kitty's animation (a=f/a=a/a=c) and unicode placeholders
  (U=1, tmux's way: the image kept, not shown) are answered EINVAL;
  z-index and pixel offsets X/Y are ignored.
