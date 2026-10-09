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

## Milestones

1. The emulator core, tested on Linux against pyte. A window on Workbench
   showing a PC program through the relay, with Amiga fonts.
2. TrueType through ttengine: glyph cache, antialiasing on deep screens.
3. The local Amiga Shell through a `CTERM:` handler.
4. Settings window, menus, own screen / full screen, mouse selection and
   the clipboard, scroll bar on/off.
5. Dialling out: telnet; maybe ssh later (AmiSSL 5.27 is installed).

## Reuse

AmiClaude (`~/Projects/AmiClaude/amiga/amiclaude.c`), all proven on the
A1200 on 2026-10-09:
- own public screen with themed `SA_Pens`;
- full screen;
- the border scroll bar;
- RGB pens;
- the settings window with ASL screen mode and font requesters;
- prefs in ENV/ENVARC.
