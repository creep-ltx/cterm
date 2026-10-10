# CTerm — design

A modern terminal for AmigaOS 3.2, in C. Replaces the Amiga E CTerm in
`~/Projects/AmigaTools/cterm` (which hands its window to `CON:`/`CCON:`
and so gets only console.device's ANSI). This one does its own
VT100/xterm emulation. Decided 2026-10-09.

## What it must do (the brief)

- Open on Workbench, or on its own public screen, windowed or full screen.
- Scroll bar on or off.
- Amiga bitmap fonts and TrueType fonts (ttengine.library, installed on the
  A1200 along with DejaVu Sans Mono).
- From 2 colours up to 24-bit ("unlimited").
- Latin-1 and UTF-8.
- Good enough for real programs: bash, vim, htop, and Claude Code's TUI.

## Parts

| Part | File | Amiga-specific? |
|---|---|---|
| Emulator: parser + screen model + scrollback | `src/vt.c`, `src/vt.h` | No: also built on Linux and tested against pyte |
| Display: window/screen/full screen, scroll bar, fonts, pens, drawing | `src/display.c` | Yes |
| Keys: raw keys to xterm sequences | `src/keys.c` | Yes |
| Connection: telnet over TCP (listen for the PC relay; later dial out) | `src/net.c` | Yes |
| Main loop, settings, menus | `src/main.c` | Yes |
| PC relay: runs a program in a pty, connects to the Amiga | `pc/cterm-relay.py` | PC |

### The emulator (`vt.c`)

- No Amiga headers.
- The cell holds a code point, fg, bg and attributes. Colours are default,
  palette index 0–255, or 24-bit RGB; the display maps them to what the
  screen can show.
- What it handles:
  - C0 controls;
  - ESC sequences (save/restore, IND/NEL/RI, charsets incl. DEC special
    graphics, RIS, keypad modes);
  - CSI (cursor, erase, insert/delete, scroll regions, SGR 16/256/RGB,
    modes, DA/DSR replies);
  - OSC title; DCS ignored.
- Alternate screen (`?1049`), bracketed paste, cursor visibility,
  application cursor keys, autowrap with the pending-wrap rule, origin
  mode, tab stops.
- UTF-8, or Latin-1 where `0x9B` is CSI, as on the Amiga.
- Wide characters (2 cells) and zero-width marks.
- Replies to the host (DA, cursor position) are queued for the main loop
  to send.
- Change tracking for the display: dirty rows, plus a pending scroll of a
  region, so a scroll is one blit instead of a repaint.

### Characters a bitmap font lacks

Amiga fonts are Latin-1.
- Box drawing (U+2500–257F) and block elements (U+2580–259F) are drawn
  as lines and rectangles from the cell size, as modern terminals do, so
  they join perfectly at any size.
- With TrueType (DejaVu Sans Mono) almost everything comes from the font.
- Anything else falls back to a near Latin-1 character or `?`.

### Colours

- Own screen with 2–256 colours: CTerm owns the palette (as AmiClaude).
- Workbench or a deep screen: pens are obtained by RGB (`ObtainBestPen`)
  and cached. 256 pens cover the xterm palette exactly on a 24-bit
  screen. Direct RGB drawing (cybergraphics) is a later step for
  24-bit images.

### The connection

- Telnet over TCP: NAWS (window size), BINARY, SGA, ECHO, with IAC
  escaping.
- CTerm listens, the PC relay calls in: the PC's firewall drops calls from
  the LAN. Later CTerm also dials out (telnet BBSes).
- The local Amiga Shell needs a console handler of CTerm's own (`CTERM:`);
  that is milestone 3. Notes: `Knowledge/amiga/amigados-handlers.md`.

## What CTerm is (decided 2026-10-09)

One terminal program for both:
- **The local Amiga Shell:** everything the Shell does (`List`, `Dir`,
  `Ed`, `More`...), with Amiga bitmap fonts in Latin-1 when wanted, or
  TrueType and Unicode.
- **Remote services:** the PC relay (Claude Code, bash), telnet, ssh and
  the like.

Like foot or xterm on Linux: the terminal does not care whether the
program inside is local or remote.

**CCON stays a separate product,** for people who only want a better
console handler. CTerm does not replace it and does not touch it.
CTerm's own console handler is new C code; it takes CCON's knowledge
(the packet protocol, what Ed/More/List expect) but not its code.

## Milestones

1. **Done 2026-10-09.** The emulator core, tested on Linux against pyte. A
   window on Workbench showing a PC program through the relay, with
   Amiga fonts.
2. **Done 2026-10-09.** TrueType through ttengine (DejaVu Sans Mono;
   14–28 ms per full screen on the A1200).
3. **The local Amiga Shell:**
   - a console handler of CTerm's own (`CTERM:`), mounted by CTerm, that
     relays bytes between the Shell and CTerm's window and does not draw;
   - the Amiga console's own codes in the emulator (CSI as `0x9B`,
     window-size reports, raw/cooked mode, raw key events);
   - line editing in the handler (history first, completion later,
     CCON's keys as a guide).
4. **Done 2026-10-10. Dialling out:** `HOST name[:port]` and Project/Call...
   (the last eight places kept); telnet answers the terminal-type question;
   `PC` for PC BBSes (CP437, iCE colours, dotted shades). The PC relay kept.
5. **Done 2026-10-10. ssh:** libssh2 1.11.1 on AmiSSL 5.27 (third_party/,
   with a patch to libssh2's Amiga branch); known hosts, key and password
   logins, a pty that follows the window. Password logins not yet tried
   against a real server.
6. **Done 2026-10-10. Comfort:** the window's place and size remembered;
   mouse selection, Copy, Paste; a settings window (screen and mode,
   fonts, TrueType size, line spacing, characters, six themes and your
   own colours; Save/Use/Cancel); the PC's VGA font in PC BBS mode;
   tabs (Shells, telnet and ssh side by side in one window).

## Still to do

In the order the user and I agreed (2026-10-10):
- **Done 2026-10-10:** the settings window in tabs (ReAction, real
  clicktab tabs; the user chose the look from a test). Was:
  **the settings window in tabs** (the user's wish): one page each for
  Screen, Text, Colours and picture, Keys, so it is not one long cluttered
  window. The bottom limit is AmigaOS 3.2 (the user, 2026-10-10), so the
  plan is the whole window in ReAction with the real clicktab tabs; first
  a small test window, a screenshot from the A1200, and the user decides
  on the look. The other way: tabs drawn by CTerm, like cdiff's
  (AmigaTools/cdiff, ltx_drawtabs in ltxwin.c).
- Search the scrollback;
  clickable links. (Done 2026-10-10: double/triple-click, OSC 52 copy, logging.)
- **Done 2026-10-10 (A1200 tests pending: ssh login, letters):** the
  phone book. Was: **the phone book** (the user's spec, 2026-10-10). Each entry: a name;
  the address (IP, host name or URL) and port; telnet or ssh; a user name
  and password (saved), sent at the host's prompt with Right Amiga+U (the
  name) and Right Amiga+P (the password), both keys configurable; the
  letters (Amiga font, TrueType, PC VGA font) and the mode (PC BBS / CP437,
  Amiga / Latin-1, UTF-8). Calling an entry opens a tab dressed that way.
  The passwords are kept on the Amiga only (ENVARC:CTerm/), never shown.
- **Done 2026-10-10: Zmodem, Ymodem, Xmodem and Hydra** (tested against
  lrzsz and HydraCom itself on the PC, and every one from CTerm in FS-UAE
  through tests/xfer/zbbs.py). **Zmodem verified on the real A1200
  2026-10-10** through pc/cterm-relay.py running lsz (the PC calls out, so
  the firewall does not matter): 20 MB at ~500 KB/s, md5-identical; Esc
  keeps name.part and a new send goes on from it. To do: Hydra, Ymodem
  and Xmodem on the A1200 (the same relay way: run HydraCom / lsz --ymodem
  / lsz --xmodem through cterm-relay), a real board; SFTP and FTP later. Was: **File
  transfers:** Zmodem, Ymodem, Xmodem and Hydra first (BBSes);
  SFTP, FTP and the like later. Settings has a download drawer (where
  received files go) and an upload drawer (where the file requester opens
  on Upload). Each phone book entry may have its own two drawers; an
  empty one means the global setting.
- **Transfer progress without locking** (the user, 2026-10-10): each
  transfer in its own process so the window and tabs stay usable; a panel
  at the bottom of that tab (file name and drawer, a bar per file and one
  for the batch, speed, time left, Stop); the tab's name shows the
  percentage. **Done 2026-10-10** (the user's go): a transfer waits in
  loop_round(), so CTerm goes on; panel, tab name, Transfers window.
- Later idea: Hydra's chat with the other side during a transfer
  (HydraCom's CON device channel; AmiExpress has it) - niche, parked.
- Also: an ssh password login tried against a real server, ARexx,
  dropping files on the window, emoji fallback, more stock-A1200 speed.
- Keys configurable (the rule for all the user's programs).

- **After the audit of 2026-10-10** (Knowledge/amiga/cterm-audit-2026-10-10.md;
  every finding fixed, commits b6f36fc..cefd2fd). Left open:
  - not yet tried on the A1200: an ssh login (password, keyboard-interactive,
    a key) - it now uses CTerm's own wiping allocator in libssh2; a changed
    host key needing "yes"; known_hosts past 4 KB; Hydra/Ymodem/Xmodem;
    two transfers at once (the second refused); the own-screen visitor
    question at quit; ghost letters on a planar screen during a transfer;
  - the pen hash table (3a058dd) gave a 16-colour screen's pens on the
    Workbench after a screen switch; reverted (040e092), cause unknown -
    test a screen switch on the A1200 before trying it again;
  - measure conbench on the stock A1200 (FS-UAE) again: the emulator's new
    fast lane for CP437 and box lines (6af565f) was only measured on the PC
    (0.30 -> 0.11 s for 50 MB of CP437 art);
  - the vt.c fuzzer is `make fuzz` (tests/fuzz/fz.c, ASan/UBSan, 10 seeds);
  - the name lookup runs in a process of its own (net.c lookup(), its own
    bsdsocket base): the window goes on, Esc and 30 s give up; one given
    up on frees itself, CTerm waits for it at quit (0.3b6, A1200).

- **Memory, load on demand** (the user, 2026-10-10): done - the
  protocols are files (PROGDIR:Protocols/), ttengine.library (109 KB)
  opens only when TrueType is used, ssh is CTermSSH started only for an
  ssh call, bsdsocket opens only for a call. Not worth it: telnet,
  transfers and the phone book are ~24 KB of code together. The real
  knob is the history (most of CTerm's ~3 MB); TrueType costs ~3.6 MB
  more than an Amiga font (measured on the A1200) - worth a look.
- **An assembly engine** (the user: "custom assembler is the path to
  speed, ask CCON"). Keep CTerm in C; write the inner loops in 68k
  assembly (vasm, as CCON's engine/pgroups.s), linked in. In order of
  what it should win:
  1. **Planar screens** (the stock A1200, 42 s vs CCON's 26 s): CTerm
     draws there with Text(). Replace it with a painter like CCON's:
     glyphs pre-shifted in a cache, a 4x4 byte transpose per four
     cells, each needed plane written in longs (plane mask as today),
     loops small enough for the 020's 256-byte cache, no loop state in
     memory. One row per call, straight from the cell model.
  2. **RTG painter** (the PiStorm A1200, 4.2 s vs 3.85 s): the row
     builder that copies cached cell pixels into the row buffer - an
     unrolled move.l loop per cell line.
  3. **The parser's fast path**: runs of printable bytes and SGR colour
     (CTerm loses most in conbench's sgr-colour and bytewise): scan a
     run in asm, write cells in one go, return at the first control.
  Each step: measure with the PROFILE_ECLOCK build first, keep the C as
  the reference and test the asm against it (tests/streams), one step
  at a time on both A1200s.
  **State 2026-10-10 evening:** step 1 done (src/ppaint.s: pp_row for any
  planes, pp_row1/pp_row1z for one plane; tests/pptest under vamos, make
  pptest). On a chip-only Amiga Text() keeps one-colour rows (its blitter
  is as quick there) and the painter takes a row from its 2nd colour run;
  with fast RAM it draws every row. A 256-colour card got a painter of pen
  rows (WriteChunkyPixels) from the same glyph store. Cells are now 6
  bytes (character + pen number, vt_pens). Stock conbench 42.5 -> 39.5 s
  (CCON 26.3); fast-RAM A1200 24.8 -> 23.3 s; RTG A1200 unchanged at
  ~4.4 s (per-packet cost, not drawing). Numbers and traps:
  Knowledge/amiga/graphics-and-performance.md ("A CPU text painter").
  Left on stock: colour rows over 4 planes (~8 ms a row), the 4-plane
  scroll, the parser's per-line overhead; step 2 (RTG row builder in asm)
  is not worth it while drawing is not the RTG cost.
  **Later the same evening** (the user: "check CCON's tricks; report when
  near >NIL: speeds"): a Shell's writes are taken at once and parsed in
  batches (term_take/term_sync), the main loop answers Shell packets and
  waits again without its other checks, floods are drawn every 3x a
  draw's cost. Stock 30.9 s (CCON 26.3, packets-only floor 13.8, NIL:
  6.7); PiStorm A1200 4.10 s (packets-only floor 3.54, CCON 4.26, NIL:
  0.18). NIL: needs no packet, a console always does: the floor is the
  packet round trip. Left on stock: ~7 s of parser straight-line code
  (per line, per escape), ~8 s drawing (colour rows over 4 planes,
  partial rows by Text()). Later, if it pays, the engine per CPU
  (68000 / 68020+) as a file chosen at start - the plugin pattern
  (Knowledge/amiga/load-on-demand-and-plugins.md).

  **Audit of the speed work (2026-10-10 night, 0.3b4):** the short loop
  ate the frame timer (its bit is in the Shells' mask): drawing waited
  for an IntuiTick, and an inactive window drew nothing until the next
  READ - fixed, and every draw is now paced by its cost (not only in a
  flood), which kept stock conbench at 30.4 s (30.5 before). Also fixed:
  a stale pen number in the fast lane after a pen clean-up, Å/Ä/Ö + 9B
  CSI in a UTF-8 Shell, the deep painter's row buffer after a 256-colour
  screen, flood pacing kept from an AGA screen, the full-screen window
  never repaired (grey after a window closed), ESC % @ forcing Latin-1.
  Then (0.3b5; the user: right on screen before speed): the painter
  paints only cells whose whole longs lie in our own unobscured layer
  and on this line of the bitmap; the edge cells go to Text(). Stock
  conbench 30.6 s (same); a window dragged to x=13 is pixel-identical to
  TEXTDRAW but for italic spill, nothing outside it touched. Wide letters
  were not a painter bug: with a bitmap font Text() shows the same
  one-cell stand-in. Left open: the pen table never shrinks (~450 KB per
  tab at the cap) and walks all history per new look when full; a 2-byte
  UTF-8 letter ending in 9B (Û, ś, ě) cannot come from an Amiga program
  in a UTF-8 Shell.

Speed on a stock A1200 (FS-UAE, conbench): 99 s at first, 39.6 s on
2026-10-10, CCON 26.0 s; on the PiStorm A1200 4.45 s against CCON's 4.26.
Notes and what is left: Knowledge/amiga/graphics-and-performance.md.

## Reuse

AmiClaude (`~/Projects/AmiClaude/amiga/amiclaude.c`), all proven on the
A1200 on 2026-10-09:
- own public screen with themed `SA_Pens`;
- full screen;
- the border scroll bar;
- RGB pens;
- the settings window with ASL screen mode and font requesters;
- prefs in ENV/ENVARC.

**Open, 2026-10-10 night:** the user saw one yellow (recoverable) guru on
an FS-UAE A1200 030/50 (AGA), switching by the menu from own screen 16
colours + full screen + scroll bar to "Own screen, like Workbench" -
maybe after running CTermTour. Number not noted. Not reproduced in three
runs of the same menu steps (wasabid's Alert() hook saw nothing). If it
comes back: note the number, and whether the tour ran first.
