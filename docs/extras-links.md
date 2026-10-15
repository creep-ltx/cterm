# Extras 2026-10-15: links, drag and drop, the fallback font, ARexx PANE options

Design notes from the helper that built these (branch of the 0.3b43 work), for the
main session to fold into design.md. PART_VERSION is 16 (PartEnv got `dwindle`,
`fallfile`, `ttfall`).

## OSC 8 hyperlinks (vt.c, find.c)

- `ESC ] 8 ; params ; URI ST` ... `ESC ] 8 ; ; ST` (ST as ESC \ or BEL). The link is
  part of the look: **VtPen.link** (uint16, 0 none). VtPen grew from 10 to 12 bytes on
  the Amiga (12 on Linux, padding); cells still carry only a pen number, so the cell
  stays 6 bytes. intern()/pen_hash compare and hash the link too.
- The pen a linked look gets has VT_UNDERLINE added in cur_pen (the look itself keeps
  what the program set): draw.c needed no change to show links underlined. TrueType
  underlines glyph by glyph, so the space inside "click here" is not underlined.
- SGR 0 does not end a link (reset_pen keeps look.link; only OSC 8 with an empty URI
  ends it); RIS (ESC c) does. DECSC/DECRC save and restore it with the look.
- The addresses: `links[]` per Vt, each once (the same URI gets the same number; the
  `id=` parameter is ignored: CTerm does not highlight a link's other cells on hover).
  Caps: LINK_MAX 512 slots, LINK_BYTES 32 KB, an address over 2048 bytes is not kept.
  Full: **link_gc** walks every cell's pen (pen_walk, as pen_gc), keeps the links of
  pens in use, of the look and of both saved cursors, frees the rest. A freed number
  may be given to a new address while an unused pen still names it: harmless, no cell
  uses that pen (and a new look with that number means the new address). Still full:
  the text comes without its link. This also answers "the pen table never shrinks" for
  links: the pen table itself is unchanged.
- `vt_link(vt, n)` / `vt_link_count(vt)` in vt.h.
- find.c: `osc_link(L, c0, c1)` - the first linked cell in a range; link_click and
  link_menu try it before the text scan (url_at).
- Tests: tests/link_test.c (built with a cap of 4 addresses so the clean-up runs):
  cells, ST forms, SGR through a link, dedup, wide letters, history, the cap, a storm
  of 3000 random steps (resizes, the alternate screen, resets) checking that every
  linked cell's address is kept; also run under ASan+UBSan with PEN_MAX=64 (pen_gc and
  link_gc together): clean. compare.py (pyte), shadow, lines, pic tests unchanged and
  green. tests/fuzz seeds 1-3 clean.

## Links that wrap (find.c)

- vt.c already had the mark: **RWRAP(row)** (`row[-1].pen`), set when the text ran on
  at the right edge (both put() and the fast lane), copied into the history, kept by
  reflow. Exposed as `VT_ROW_WRAP(l)` in vt.h. link_test checks it on screen, in the
  history and after a reflow.
- url_at joins line L with the rows before it that wrapped into it (up to 8) and the
  rows after it while they wrap (up to 16), into fbuf (now 2 KB), and maps the clicked
  columns by L's offset in the joined text. Both Ctrl+click and Edit/Open link use it.

## Drag and drop (drop.c, new; transfer.c hook)

- An AppWindow (workbench.library 37, `AddAppWindowA`) on CTerm's window when it is on
  **the Workbench screen** (`scr->Flags & SCREENTYPE == WBENCHSCREEN`): Workbench gives
  drops only to its own screen's windows; on CTerm's own screen there is none. Added
  after the window opens (open_display), removed before every CloseWindow
  (close_display), so hiding/screen switches redo it.
- drop.c has its own workbench.library base (`#define WorkbenchBase DropWBBase` before
  the includes): hide.c opens and CLOSES its own for the AppIcon, which would pull the
  library away under the AppWindow.
- An AppMessage is answered at once (Workbench waits); the names are made with
  NameFromLock + AddPart first (a drawer or disk dropped: its own name). The pane under
  am_MouseX/Y is activated, then: Shell tab - names typed through con_paste, quoted
  when they hold a space or one of ` ;"'*?#()[]|<>`$&~{}!` (a `"` inside becomes `*"`);
  file browser tab - files_rexx(FR_PUT, ...) (the existing ARexx PUT path; no new code
  in files.c/ftp.c); host tab - **queued** for the tab's next Zmodem upload request:
  transfer.c's "the host waits for files" (ZRINIT) branch takes drop_uploads() after an
  ARexx UPLOAD's files and before the requester. Decision: not starting a Zmodem send at
  once, because a board's menu (or a shell without rz) would get ZRQINIT as typed junk;
  with a queue the person starts the upload on the host as always and the requester is
  skipped. The queue goes with its tab (transfer_tab_gone -> drop_tab_gone) and is
  replaced by the next drop.
- Up to 16 files a drop.

## The fallback font (cmap.c new, fonts.c, draw.c, setwin.c)

- **ttengine.library 7.1 reaches only the BMP.** Verified with tests/links/ttplanes.c on
  FS-UAE: U+1D670 (which DejaVu Sans Mono has) and U+1F600 come out as boxes through
  TT_Encoding_UTF32_BE, UTF16_BE with a surrogate pair, and UTF8 alike; U+231A from Noto
  Emoji and U+E600 from the remapped font draw. So: **tools/emojifont.py** (fontTools)
  makes a copy of an emoji font with U+1F000-1FAFF also at U+E000 + (cp - 0x1F000) and
  U+FDD0 as the mark; CTerm (tt_fallcp) draws from there when the fallback has the
  mark. A variable font (Noto Emoji[wght].ttf from Google Fonts) is instanced at weight
  400 on the way (2 MB -> 0.9 MB). A font with its own letters at those PUA places
  (a Nerd Font) is refused by the tool; without the mark CTerm never remaps.
- Which letters a font has: ttengine cannot say (it draws its box). **cmap.c** reads
  the cmap table (format 12 from (3,10)/(0,x) first, else format 4, glyph 0 entries
  left out) into sorted ranges; fonts.c reads only the table directory and the cmap
  table from the file (not 1-2 MB into memory). Linux test: tests/cmap_test +
  cmap_check.sh against `fc-query --format '%{charset}'`: the same sets for DejaVu Sans
  Mono (and Bold), PxPlus VGA, Adwaita Mono/Sans, Liberation Mono, four icon fonts,
  Noto Emoji (variable) and the remapped copy. `make test` runs it on PxPlus and DejaVu.
- tt_missing(cp): the fallback is open, cp > 0xFF, the main font's cmap was read and
  lacks cp, and the fallback has tt_fallcp(cp) within the BMP. A one-entry memo.
- Two faces: **ttfall** sized so a probe emoji (U+1F600 / U+231A / U+2603 / U+4E00,
  the first the font has) is at most 2 cells wide, **ttfall1** at most 1 cell (the same
  face when the sizes match). The vt counts U+2100-2E7F as one cell (on purpose, as
  pyte's tests expect), so U+231A and friends are drawn small in one cell; U+1F300-
  1F64F and U+1F900-1F9FF are two cells; U+1F680 (rocket) is one. The vt's widths were
  not changed.
- draw.c: draw_cells' TrueType run splits a run's glyphs into main / fallback-wide /
  fallback-narrow (ttbuf is 12 KB now, made on first TrueType draw), tt_run draws each
  list with `tt_over` set (its vertical clip already handles taller glyphs);
  cell_tt_glyph (the RTG painter's cell cache) picks the face by the cell width. The
  cache's `made` notes `fallgen`, so a new fallback face redraws.
- Settings' Text tab: "Fallback font" getfile + None (S_FALL, S_NOFALL); Cancel
  restores it; prefs `fallbackfont=`. Disabled without TrueType.

## ARexx PANE DWINDLE / GAPS (rexx.c)

- `PANE DWINDLE ON|OFF` toggles through pane_do(PO_DWINDLE) only when it differs (the
  title hint as the menu's); `PANE DWINDLE` alone answers ON/OFF.
- `PANE GAPS [n] [OUTER ON|OFF] [BORDER n]` (template grew `OUTER/K,BORDER/K/N`; OUTER
  and BORDER with any other PANE word are an error), ranges 0-MAXGAPS, 0-MAXBORDER,
  then panes_changed(). Alone: RESULT "gaps ON|OFF border". Both are LATER (run at the
  top of the loop), queries too, so a query after a set sees it. CTerm saves its prefs
  at quit, so they last like Settings'.

## Verified (FS-UAE, a1200-32-mui5 on port 1253, Workbench window, 4 colours)

- OSC 8 links underlined; Open link on "here" gave http://127.0.0.1:2151/osc8 to
  `browser=` (a script writing RAM:url.txt).
- A URL wrapped over 2 rows, and over 3 rows in a split pane: Open link from the first,
  the middle and the last row gave the whole address.
- Drag and drop (wasabi mouse down/move/up from a RAM: drawer window): Shell tab typed
  `"Ram Disk:My Clock"`; a call to tests/xfer/zbbs.py: the title said the file waits,
  `u` on the board sent it with no requester, the file arrived identical (cmp); an FTP
  browser tab (tests/ftp/ftpd.py): uploaded, listed by ARexx LIST.
- Fallback font: BMP symbols (U+231A, 23F0, 26BD, 26C4) from Noto Emoji; U+1F600,
  1F431 (wide) and 1F680 (narrow) from the remapped copy; Settings' row, None, Cancel.
- t-panegaps.rexx: every set/read/refusal as expected; the Settings window then showed
  gaps 6, outer off, border 3; two panes drawn with them.

## Not verified

- Ctrl+click itself (wasabi cannot hold Ctrl with a click): it uses the same osc_link /
  url_at as the menu.
- The RTG painter's path (cell_tt_glyph with the fallback): the rig is a 4-colour
  planar Workbench; the code is the same face choice.
- Not on the real A1200. Not with Symbola.
- A drop on CTerm's own screen: no AppWindow by design.
- `telnet://` OSC 8 link opening a tab (the text-scan path for it was verified in
  0.3b43; the OSC 8 path hands the same string to the same link_open).
