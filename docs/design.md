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
- **Done 2026-10-10: Zmodem, Ymodem, Xmodem** (tested against lrzsz,
  and Zmodem end to end in FS-UAE). Hydra still to do. Was: **File
  transfers:** Zmodem, Ymodem, Xmodem and Hydra first (BBSes);
  SFTP, FTP and the like later. Settings has a download drawer (where
  received files go) and an upload drawer (where the file requester opens
  on Upload). Each phone book entry may have its own two drawers; an
  empty one means the global setting.
- Also: an ssh password login tried against a real server, ARexx,
  dropping files on the window, emoji fallback, more stock-A1200 speed.
- Keys configurable (the rule for all the user's programs).

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
