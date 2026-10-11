# CTerm — design

A modern terminal for AmigaOS 3.2, in C. Replaces the Amiga E CTerm in
`~/Projects/AmigaTools/cterm` (which hands its window to `CON:`/`CCON:`
and so gets only console.device's ANSI). This one does its own
VT100/xterm emulation. Decided 2026-10-09.

## Start here: where CTerm is (2026-10-11 evening, 0.3b24)

**Installed:** 0.3b27 on the A1200 (CTerm, CTermSSH; parts as 0.3b24) (Work:Utils/CTerm: CTerm, CTermSSH,
CTermTour, Parts/, Protocols/, Rexx/, Pictures/, Fonts/) and on both
hand-test emulator drives (~/FS-UAE/Hard Drives/AmigaOS3.2 and
AmigaOS3.2-030, drawer SYS:CTerm; configs ~/FS-UAE/Configurations/
A1200-Stock.fs-uae and A1200-030-50.fs-uae; the user starts them on the
desktop through `systemd-run --user` with SDL Wayland - see
Knowledge/amiga/toolchain-and-testing.md). `make deploy` installs all
files on the A1200 (parts must always be copied together: PART_VERSION).

**Requirements (the user):** AmigaOS 3.2 and fast RAM; CTerm says so
and quits on a chip-only Amiga. Speed yardstick: tests/stock/bench.sh
CONF=fast (stock A1200 + 8 MB fast, ~17.1 s) and the 030/50 (~21.4 s).

**The user's rules for CTerm:** correct rendering before speed; load
parts only when used (Parts/, Protocols/); every key configurable (one
file ENVARC:CTerm/CTerm.keys, Settings' Keys tab); features over a
connection stay in the terminal (a text UI in the tab, not a window);
measure free memory before/after each feature.

**Done 2026-10-10/11 (0.3b4-0.3b22):** speed-work audit and fixes; name
lookup in its own process; phone book window (Amiga+B); serial lines and
modems (Protocols/Serial; settings Modem tab); ARexx port CTERM(.n)
with WAITFOR/SEND for logins; hide/show (AppIcon, Exchange); split panes
then the i3/Sway way (Left Amiga keys: Return, Q, arrows, H/V, F, T/P,
1-9, Shift+arrows; tab mode/pane mode; dwindle LA+D; gaps 0..32 px with
a border round every pane; no tab bar while panes are shown); memory
work (parts, reserve, no per-write retries); SFTP/FTP as a Midnight
Commander-like browser in the tab (Parts/Files); pictures in the
terminal (sixel, iTerm2 files via datatypes, kitty swallowed;
Parts/Pictures; ShowPic); every key configurable; new tabs in the
default characters; the smarter Л/ś/Û rule.

**Verified on the A1200 itself:** FTP from ftp.aminet.net (md5-identical,
no wasabid stalls since the browser is in the tab), sixel/PNG pictures
on RTG, panes/dwindle/gaps on RTG, name lookup + Esc, ARexx.

**Next (agreed with the user):**
1. ~~The menus are a mess~~ - **done in 0.3b23** (the user left the
   choice to me): CTerm (New Shell tab, Close tab | Run ARexx script...,
   Speed test | About, Hide window, Quit) / Call (Call..., Phone book... |
   Serial terminal, Answer | Send user name, Send password) / Edit /
   Files (Open SFTP browser | Send file >, Receive file >, Show
   transfers | Start/Stop log) / Panes (the old Split left/right, Split
   top/bottom, Next pane, Close pane items are gone from the menu; their
   keys still work) / View (Bigger/Smaller text | Screen >, Full screen,
   Scroll bar | Characters > UTF-8, PC BBS) / Settings (Settings... |
   Amiga font..., TrueType font...). menu_ticks now finds ticks in
   submenus too (Characters). Checked on the A1200 by grabs.
1b. **Amiga programs get their own screen (0.3b24):** a program that
   takes the console raw (ACTION_SCREEN_MODE 1) and then draws a whole
   screen (FF, CSI H/f, CSI 2J, ESC c) is moved to the alternate screen,
   as ?1049h would; cooked again (it ended), the main screen and the
   history come back as they were. Raw without such a draw ("press a
   key") stays in the history. vt_amiga_raw (vt.c auto_alt), term_raw
   (main.c), console.c's SCREEN_MODE. Setting: Text page "Full-screen
   Amiga programs get their own screen" (prefs ownscreen=0 turns it
   off). Verified in FS-UAE with More and Ed; tests/amiga_test covers it.
1c. **About is a window (0.3b25):** a requester (BuildEasyRequest,
   answered in the main loop: about_sigmask/about_events in screen.c),
   not text in the terminal; the terminal goes on while it is open
   (verified in FS-UAE: typed and echoed with it open; Quit with it open
   is clean). close_display closes it. On the A1200 it opened and
   closed by OK.
1d. **TrueType rows as a PC sizes them (0.3b26):** the cell was
   TT_FontMaxTop + MaxBottom, the tallest glyph in the whole font; Noto
   Sans Mono (the user's PC terminal font) has stacked Vietnamese
   accents, so its rows were 24 px at size 14 against 18 for its Latin-1
   letters (DejaVu 21 -> 16). Now TT_TextExtent over Latin-1 (0x20-0x7E,
   0xA0-0xFF) sizes the row. A run with a glyph above U+00FF is measured
   (TT_TextExtent) and, if it is taller than the row, drawn through a
   clip region of the row (InstallClipRegion; layers.library auto-opened
   by libnix) - no stray pixels in the rows around (verified in FS-UAE).
   Bold beside a `-Regular.ttf` is `-Bold.ttf` (open_variant). Noto Sans
   Mono Regular + Bold are in Work:Utils/CTerm/Fonts on the A1200 (not
   in the repo; OFL); set as the user's font on 2026-10-11 (truetype=1, ttsize=16; the old prefs in ENVARC:CTerm.prefs.before-noto).
1e. **Braille drawn by CTerm (0.3b27)** for the terminal programs
   (~/Projects/Ideas/terminal-programs.md): U+2800-28FF is is_special
   (as lines and blocks, in TrueType too); draw_braille puts 2 x 4
   square dots on the cell grid (dot = min(cw/4, ch/8), at least 1 px),
   in the planar special path and make_cell (RTG and planar painters).
   Verified in FS-UAE with topaz 8 (own 8-bit screen) and Noto Sans Mono
   12 (Workbench window): all 256 patterns and a btop-style graph; on
   the A1200 too (RTG 24-bit, Noto Sans Mono 16, 2-pixel dots).
1f. **For programs inside the terminal (0.3b28, with Ahiru 0.3b1's
   terminal mode):** a Shell tab's raw program gets xterm mouse reports
   (mouse_to_host: local tabs while con_raw; mouse_out -> con_reply),
   the wheel as reports too; Shift+arrows and the other history keys go
   to the program while it is on the alternate screen (no history
   there); CSI 14/16/18 t are answered in Shell tabs (the Amiga's page
   length meaning was ignored anyway). Key bytes in a Shell tab (seen
   with a raw reader): arrows 9B A-D, Shift+Left 9B 20 41, F1-F10
   9B 0~ - 9B 9~, Help 9B ?~, Esc 1B, Backspace 08, Del 7F. Verified in
   FS-UAE with Ahiru inside CTerm (keys, clicks, wheel, panes, sixel).
2. Open, never reproduced: one yellow (recoverable) guru on the 030/50
   when switching own screen 16 colours + full screen to "Own screen,
   like Workbench" by the menu, maybe after CTermTour (number not noted).
3. Not yet tried on real hardware: RTS/CTS and carrier detect with a real
   modem/cable; an ssh login and SFTP on the A1200; Hydra/Ymodem/Xmodem
   on the A1200; a real BBS on the internet.
4. Smaller: inactive pane borders are dim (mix 25%) - fine for now;
   kitty pictures only swallowed; ARexx has no PANE DWINDLE / GAPS yet;
   the file browser's key bar shows no shifted labels.

**Testing traps (learned):** the A1200's address can change after a
reboot (DHCP; the user may reserve it in the Deco app) - `wasabi
discover` first, and pass --host explicitly, else wasabi may fall back to
another session's FS-UAE on 127.0.0.1. Several sessions share the A1200:
check `wasabi ps` for a CTerm the user runs before deploying or killing.
The user often runs CTerm on its own full screen (no window title):
`wasabi wait window CTerm` then misses it - use `wasabi ps`.

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
| Serial lines: the settings, the tabs (CTerm's side) | `src/serial.c`, `src/serial.h` | Yes |
| The line's code, loaded on demand (`Protocols/Serial`): the device, dialling, the modem's words | `src/sermod.c`, `src/sermod.h` | Yes |
| A serial line's settings window (phone book, Settings) | `src/serwin.c` | Yes |
| The ARexx port: commands, WAITFOR, starting scripts | `src/rexx.c` | Yes |
| Hiding the window: the AppIcon, the commodity broker | `src/hide.c` | Yes |
| Panes: the window split, the layout tree, dividers | `src/pane.c` | Yes |
| Main loop, settings, menus | `src/main.c` | Yes |
| The parts' loader, their stubs, the ARexx port | `src/parts.c`, `src/part.h`, `src/partlist.h` | Yes |
| Parts (files of their own, `Parts/`): settings window, phone book window, ARexx's commands, Call window, Transfers window, speed test, the SFTP/FTP file browser, pictures (iTerm2 files, pens) | `src/setwin.c`, `src/bookwin.c` (+ `src/serwin.c`, `src/askwin.c`), `src/rexx.c`, `src/callwin.c`, `src/xwin.c`, `src/bench.c`, `src/files.c` + `src/fileui.c` + `src/ftp.c`, `src/pics.c` | Yes |
| SFTP's calls for the browser (CTermSSH only, through `sftp_ops`) | `src/ssh.c`, `src/ssh.h` | Yes |
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
- **Done 2026-10-11: serial lines and modems** (the user's spec, 2026-10-10):
  a serial tab per line (any device and unit), a phone book kind
  "serial" (device, unit, speed, data bits, parity, stop bits, flow
  control, buffer, baud locked, phone number, own init and dial prefix),
  a modem profile on the Settings' Modem tab (two init strings, dial
  prefix and suffix, hang-up by +++/command and DTR or DTR only, the
  call's time limit, redial on BUSY/NO ANSWER, answer on RING), Project/
  Serial terminal and Answer call (ATA), `CTerm SERIAL [DEVICE UNIT BAUD]`.
  How it works (serial.c): one 1-byte CMD_READ always out; when it comes
  back, SDCMD_QUERY says what else waits and that is read at once into
  the line's 4 KB buffer; every line's requests and two timers (waits,
  the 1 s carrier check) reply to one port, whose signal is in
  loop_round's wait; a transfer takes the bytes from the buffer itself
  (serial_take), no telnet anywhere. Writes are SendIO and a wait with
  Esc and a time limit. Tested in FS-UAE through tests/serial/fakemodem.py
  (verified, 2026-10-11): null modem both ways; dial, CONNECT, the far
  end's NO CARRIER, Return calls again; BUSY with 2 redials; Esc while
  dialling (CR, ATH0); +++/ATH0 on closing; RING answered; 7E1 seen as
  even parity bits on the PC side; the speed following CONNECT; Zmodem
  down (370 KB, md5 identical, twice) and up (226 KB), Hydra down; telnet
  still the same. Traps found: 3.2's serial.device fills the request at
  OpenDevice without the SERF_7WIRE asked for - SETPARAMS must ask again
  or RTS/CTS is silently off; overrun/parity/break read errors are lost
  bytes, not a dead device; FS-UAE's TCP serial swallows everything sent
  with 7WIRE on, and reports CD off always (carrier loss there is seen by
  NO CARRIER only). Left open:
  - RTS/CTS and carrier detect on a real modem (the emulator cannot);
    the escalation for a write CTS holds (AbortIO, then CMD_RESET) is
    untested - serial.device returned such writes at once in FS-UAE;
  - Ymodem/Xmodem over serial not run (same XferIO path as Zmodem);
  - SERF_RAD_BOOGIE for 57600+ not used (unverified on hardware);
  - **done 2026-10-11 (0.3b9): the line's code loaded on demand.** The
    device's reading and writing, dialling, the modem's words and hanging
    up are `src/sermod.c`, built as `Protocols/Serial` (10,140 bytes, no C
    start-up, as the protocols); CTerm keeps the glue in serial.c (the
    settings, the phone book's fields, the tabs, the menu) and serwin.c.
    CTerm 227,812 -> 220,024 bytes (-7.8 KB; the file itself is bigger
    than what left CTerm: its own string calls, its own copy of the entry
    and LTO no longer inlining across). The interface is `src/sermod.h`:
    SerEnv (CTerm's calls: words into the tab, waits serving the window
    and the Shells, transfers, the clock) and SerMod (call, close, take,
    write, poll...); each tab's SerTab (its line, number, init, the open
    device) is all the file sees of a tab. Loaded by serial_call(),
    unloaded when no line is open and none of its code is on the stack
    (a counter around every call into it: its waits serve the window and
    the Shells, so a close from in there must not pull the code away -
    serial_poll unloads later). A write CTS keeps for ever (the "stuck"
    line) keeps the file loaded: the device still holds a request. A
    missing file: "[serial support missing: PROGDIR:Protocols/Serial not
    found]" in the tab, Return tries again. **Why Protocols/ and not a new
    Parts/ drawer:** it is the drawer people already keep beside CTerm
    (the deploy, the README); one drawer of loaded-on-demand files is one
    thing to keep, and the serial line is the connection's protocol much
    as telnet is. The header's own mark ("CTSL") and version keep the
    two kinds apart: the transfer loader never runs it, and the other
    way round. Tested in FS-UAE (verified 2026-10-11, fakemodem.py):
    dial, CONNECT, Zmodem down 2 files 361 KB md5-identical, the board's
    hang-up (NO CARRIER, Return dialled again: the file loaded again),
    +++/ATH0 on closing, BUSY with 2 redials, the file renamed away (the
    message, no crash), null modem both ways (echo). serwin.c (3 KB, the
    line's settings window) stays: it is opened from Settings and the
    phone book, not with a line.
- **Done 2026-10-11 (0.3b10): an ARexx port** (`src/rexx.c`), for scripts
  (BBS logins) and so a later detach/reattach can find a running CTerm.
  - **The port:** "CTERM", then "CTERM.2", "CTERM.3"... (the first free
    name, under Forbid with AddPort); CTermSSH gets one too. Made at the
    start (a port and a name); rexxsyslib.library is opened at the first
    ARexx message or script start. At the end: Forbid, RemPort, every
    message on it answered RC 20, Permit; everything kept (a WAITFOR, a
    queued command) answered RC 20; scripts CTerm started are waited for
    up to 3 s (their message comes back to the port); one still running
    then gets the port left in memory with PA_IGNORE and its signal freed
    (its reply lands in memory nobody frees, never in freed memory).
  - **Commands:** CALL, HANGUP, SEND, WAITFOR, GETLINE, GETSCREEN, CURSOR,
    TAB NEW/SELECT/CLOSE/COUNT/CURRENT/NAME, DOWNLOAD, UPLOAD, LOG,
    SCREEN, WINDOW, PHONEBOOK, VERSION, HELP, QUIT. ReadArgs templates,
    except SEND and WAITFOR, which read their own words: ReadArgs takes
    `*` in quotes as its escape ("Eval 6*7" arrived as "Eval 67").
    Results in RESULT with OPTIONS RESULTS; RC 0/5/10/20.
  - **Which tab:** "ARexx's tab" (rxtab): the one CALL opened, TAB
    NEW/SELECT chose, or a started script's; else the shown one. A login
    script keeps its tab when the person switches. Two scripts at once
    share it (the last to choose wins) - noted, not solved.
  - **Re-entrancy (the rule):** every wait inside CTerm's work (a call,
    a dial, a transfer's loop_round, the Call window, a typed answer)
    calls shells_handle, which now also *reads* the port (its signal is
    in shells_sigmask). Reading answers only what is safe anywhere -
    screen reads, VERSION, TAB COUNT/NAME/CURRENT, SCREEN, WINDOW, and
    registering a WAITFOR or a Zmodem/Hydra DOWNLOAD. Everything that
    changes tabs or connections or waits itself (CALL, SEND, HANGUP, TAB
    NEW/SELECT/CLOSE, Ymodem/Xmodem, UPLOAD, LOG, QUIT) is queued and run
    by rexx_run() from run(), between rounds, where no call or transfer
    is half done; commands that come while one of those runs join the
    queue. Without this, a SEND to a serial line arriving inside the
    modem's dial wait would have restarted the dial's own timer.
  - **WAITFOR without blocking:** the message is kept and answered later
    by ReplyMsg alone (safe anywhere): when its text arrives (fed from
    log_bytes - every host byte passes there - and from the Shell's
    writes, term_take/term_write), when its TIMEOUT runs out (a 0.5 s
    timer.device tick on the same port, only while a limit waits: RC 5),
    when the call ends (RC 10: hung_up, a serial line's end, HANGUP, or
    already ended at the WAITFOR) or the tab goes (RC 10). It looks in the
    tab's text since the last SEND or match (2 KB, escape codes left
    out, UTF-8 read as Latin-1, any case), so a prompt that came before
    the WAITFOR counts but an old one before the SEND does not.
  - **Transfers:** DOWNLOAD with Zmodem/Hydra registers and is answered
    by finish() (rexx_xfer_done), or at once when one ended since the
    last SEND. UPLOAD with Zmodem: the host is often quicker (its ZRINIT
    came before the UPLOAD - seen in FS-UAE: the file requester opened);
    so a tab a script drives gives an UPLOAD 2 s to arrive, and a queued
    UPLOAD's files are taken instead of asking.
  - **Scripts CTerm starts:** an RXCOMM message to REXX with the port as
    host (rm_CommAddr) and the port's name as the argument; its end
    comes back to the port, and an error is said in the tab ("[ARexx
    script x.rexx: error 5, ARexx error 1]"). A name with a space
    ("Ram Disk:x.rexx", which ASL gives for RAM:) cannot be the command's
    first word, and in quotes ARexx runs the quoted text as the program
    (error 41): such a name is called from a one-line program instead
    (RXFF_STRING, `CALL 'name' 'CTERM'`), which keeps the ADDRESS.
    No I/O handles are given (the script's SAY goes where RexxMast's
    output goes).
  - **Phone book:** "script=" per place; the window has a Script field
    (User and Password now share a row: the window stays 542x198 on a
    640x256 screen, measured in FS-UAE).
  - **Not done:** STEM/VAR results (WINDOW HIDE/SHOW: done in 0.3b11,
    below); a requester (ASL, EasyRequest) holds
    the port unread while it is open.
  - **Cost:** CTerm 220,024 -> 237,508 bytes (the port, the commands,
    the book's field). Could be a loaded part like Protocols/Serial if it
    matters: only the port and WAITFOR's feed need to stay.
  - **Tested in FS-UAE** (verified 2026-10-11, tests/rexx/*.rexx with
    rx through wasabi): VERSION/TAB/CURSOR/GETLINE/PHONEBOOK/HELP;
    SEND "Eval 6*7\r" to the Shell, WAITFOR "42" (RC 0), GETSCREEN; a
    2 s WAITFOR timed out (RC 5 after 2.4 s); unknown command and bad
    row RC 10; TAB NEW/SELECT/CLOSE/COUNT/NAME, a WAITFOR in tab 2;
    CALL the telnet board, Zmodem down 2 files md5-identical, UPLOAD 2
    files md5-identical, LOG, HANGUP, SEND after it RC 10; Ymodem down
    (2 files), Xmodem down, Ymodem up (md5-identical); CALL a dial-up
    place through fakemodem.py, Zmodem down md5-identical, the board's
    hang-up ending a WAITFOR (RC 10); info.rexx answered in 0.24 s while
    a CALL redialled a BUSY number; a phone book Script (from the
    window's requester, "Ram Disk:zlogin.rexx") ran after connect with
    ADDRESS CTERM; Project/Run ARexx script...; two CTerms (CTERM,
    CTERM.2, and CTermSSH as CTERM.2); QUIT with a WAITFOR pending (RC 10
    to the script, no hang) and with a script CTerm started still
    waiting (it ended cleanly, no crash).

- **Done 2026-10-11 (0.3b11): detach and reattach** (`src/hide.c`), as
  tmux/screen do it, the Amiga way.
  - **Hiding** (Project/Hide window, the 3.2 iconify gadget - CLOSEWINDOW
    with Code 1 -, ARexx WINDOW HIDE, Exchange's Hide): the window
    closes, and CTerm's own screen with it; the settings, phone book
    and Transfers windows close as for a screen switch. Every tab runs
    on. Not while a Shell's console is raw (Ed, More hold the window's
    pointer): a beep, RC 10. The processes whose requesters went to the
    window (pr_WindowPtr) get NULL (the default screen) and the window
    again when it is back - those whose console is one of CTerm's.
  - **Asked, then done at a round's end:** every way only sets a wish;
    loop_round does it last, where no window message is being read and
    nothing is half drawn. A transfer waits in loop_round, so hiding
    works while one runs (its menu's window_events is on the stack then:
    it now stops reading when win changed, and never touches the menu
    strip after a pick that hid the window).
  - **While hidden:** win, rp and scr are NULL; every drawing entry
    point returns (refresh, redraw_all, draw_row, draw_cursor,
    update_knob, draw_tabbar, layout_and_resize, painter_setup), the
    emulators keep their changes for the redraw. The Shells' writes go
    on through term_take (its 4 KB batch is bounded as before) and are
    answered at once; a flood (list SYS: ALL, 2013 files) ran 29.4 s
    hidden against 38.3 s shown (FS-UAE A1200 with fast RAM). Bells are
    silent: beep() replaces DisplayBeep(scr) everywhere (NULL would
    flash every screen). A program that asks for the window then
    (DISK_INFO) gets none: Ed started while hidden ran, and showed
    right once the window was back. An ssh password question gives up
    (nobody can type it).
  - **Back** (the AppIcon, ARexx WINDOW SHOW, Exchange's Show, starting
    CTerm again): open_display at the place and size it had (hid_l...;
    full screen and the own screen as they were), then tab_show (the
    tab's letters, layout, text, title) and the hosts told the size.
  - **The AppIcon:** only while hidden; workbench.library and
    icon.library opened for it, closed after. CTerm's own icon (CTerm.info
    beside it) or the default tool icon, NO_ICON_POSITION,
    WBAPPICONA_SupportsOpen (its menu's Open). Any message on its port
    shows the window. If it cannot be made and there is no commodity
    broker either, CTerm does not hide (no way back).
  - **The commodity:** a broker for the whole run (commodities.library
    at the start), named after the ARexx port ("CTerm", "CTerm.2"),
    COF_SHOW_HIDE; Exchange's Show, Hide and Remove (quit) work, also
    while hidden.
  - **Starting again:** a start with no words at all (Workbench too) asks
    every CTERM, CTERM.2... port "WINDOW STATE" and the first HIDDEN one
    "WINDOW SHOW", then ends. `SHOW` does the same and else brings the
    first CTerm to the front (WINDOW ACTIVATE). The messages are made by
    rexxsyslib and go straight to the port (no RexxMast); ln_Name must
    be set to "REXX" by hand - CreateRexxMsg left it NULL and IsRexxMsg
    refused them (seen on 3.2). An answer is waited for 5 s; one that
    never comes leaves the port behind (PA_IGNORE), as rexx_close does.
  - **Decided: a start with words opens its own CTerm**, as before
    (HOST, BOOK, SERIAL, FROM, COLS...). It asks for something new; a
    hidden CTerm may be in the middle of other work, and the person may
    want two. Calls into a running CTerm are ARexx's CALL.
  - **Tested in FS-UAE** (A1200 with fast RAM, verified 2026-10-11):
    hide by ARexx, the menu, the iconify gadget and Exchange; show by
    ARexx, a double-click on the AppIcon, Exchange, a second `CTerm` (0.9
    s, then it ends) and `CTerm SHOW` (also when not hidden); the flood
    above, the screen right after showing; Ed started while hidden; HIDE
    refused while Ed ran; a Zmodem download hidden in the middle of it
    (900 KB + 200 KB, md5-identical); memory: 10 hide/show rounds, Avail
    FLUSH the same to the byte; the own screen in full screen, scrolled
    back: pixel-identical before and after; the planar painter's page
    (lookpage.py) pixel-identical too; a stock A1200 (chip only): 6 rounds
    by the menu and the AppIcon, free memory the same (no grab there:
    no room for wasabi's buffer, and rx fails with "Ram Disk is full" -
    CTerm leaves ~100 KB of 750, as 0.3b10 does).
  - **Found on the way:** close_own_screen took a screen for closed when
    CloseScreen failed (a Shell's requester on it - not a visitor, so it
    could be made private): the screen stayed for good. Now it is made
    public again and waited for; hiding never asks, a screen that cannot
    close at once stays and is used again. Seen in FS-UAE with a "Please
    insert volume" requester. Also: a Zmodem batch of 3 MB + 200 KB
    fails on the second file in FS-UAE ("transfer failed" after big.bin)
    with 0.3b10 as well - not this work's; 900 KB + 200 KB is fine. To
    look at.
  - **Cost:** CTerm 237,508 -> 243,252 bytes.

- **Done 2026-10-11 (0.3b12): split panes** (`src/pane.c`), as tmux
  panes and iTerm splits.
  - **A pane shows a tab.** The tabs stay what they were; a pane is a
    rectangle of the window with a tab in it. The panes are the leaves
    of a tree of at most 7 nodes (a split 'h' side by side or 'v' above
    each other, its divider in per mille; four panes at most - since
    0.3b19 as many as there are tabs and room, below). Split:
    the active pane in two, a new Shell tab in the new half (as tmux).
    Close pane: the pane goes, its tab stays in the tab bar. A tab that
    ends: its pane shows a tab in no pane, else closes. Choosing a tab
    shows it in the active pane, or makes active the pane that shows it.
  - **The pane in use:** draw.c works on one pane at a time - ox, oy,
    cols, rows, the fills' edges (fx0, fx1) and the cursor's and rows'
    marks (curx, cury, shown_blank, painted, pgen: now the pane's, by
    macros) are cpane's, T its tab. At rest it is the active pane
    (apane, the shown tab's): keys, the selection, mouse_cell, the scroll
    bar, the title. refresh() draws T's pane if it is in one (taking it
    into use and putting the active one back), refresh_all() every pane
    (the frame timer, a host's bytes); redraw_all() clears the window
    and draws each pane, then the dividers and frames; repair_damage
    goes pane by pane. "Only the shown tab is drawn" became "only tabs
    in panes": term_flush, term_render_soon, the bells, host data.
  - **Sizes:** each tab's emulator has its pane's size; a tab in no pane
    the active pane's. term_cols/term_rows (the Shell's window bounds,
    the line editor), NAWS and the ssh pty are the tab's own now.
    tell_sizes() tells every tab whose size changed since it was last
    told (NAWS, the Shell program's resize report) - after a resize, a
    split, a close, a divider pulled.
  - **Layout:** one pane is exactly the old text area (ox = left border
    + 2, oy = top + tab bar + 1, the same columns and rows: single-pane
    drawing is unchanged). Split, each pane has a 1-pixel frame (in the
    theme's highlight for the active one, else the background) and a
    2-pixel divider between; the text keeps its 2/1-pixel margins inside.
    Each half keeps 10 x 3 letters when split or pulled.
  - **The planar painter (ppaint.s) at pane edges:** pp_cells already
    gave cells whose edge longs leave the layer's cliprect to Text();
    with panes the cliprect is also cut at a pane's inner edges (the
    window's outer edges as before), so the asm never reads and writes
    back a neighbouring pane's pixels. The first column right of a
    divider and the last left of it go to Text(). Measured: a pane's
    test page is pixel-identical to a lone window of the same size but
    for one pixel of a bold B in column 0 of a right-hand pane (Text()'s
    bold smear against the glyph store's, a known difference).
  - **The RTG painters** write rows at the pane's ox, cols*cw wide; the
    row buffer is sized for the widest pane (panes_maxcols). Not tested
    here (no RTG in FS-UAE); read through: blank rows, the backdrop
    test, the cursor (an outline in other panes, cursor_mark shape 3,
    over the written row) and the 8-bit chunky row all take the pane's
    ox/cols. ScrollRaster (planar, and a card without the painter)
    scrolls only the pane's rectangle (fx0..fx1).
  - **The cursor:** solid in the active pane only; the others draw an
    outline (all three drawing ways).
  - **Dividers pulled:** a click within a pixel of one starts a drag;
    the line follows the mouse in COMPLEMENT (drawn twice: gone); let go,
    the split's per mille is set, laid out, drawn, the tabs told.
  - **Letters:** split, every pane wears the window's own (tab_wish
    gives BL_CTERM): cells must be one size to share the window. A PC
    tab gets its VGA font back when alone.
  - **Fullscreen:** the place bar beside the active pane, its column
    cleared top to bottom (panes above each other).
  - **ARexx:** PANE SIDE/UNDER/NEXT/SELECT n/CLOSE/COUNT/CURRENT (queued
    like TAB; the ARexx tab follows the active pane).
  - **No keys** for the pane items at first (the rule: keys must be the
    person's to change); since 0.3b17 every item can be given one in
    CTerm.keys (none by default).
  - **Also fixed:** the title followed T (a transfer ending in a tab not
    shown set its own title); now it is always the shown tab's.
  - **Tested in FS-UAE** (A1200 with fast RAM, verified 2026-10-11):
    split side and under to four, the fifth refused; NAWS heard by
    tests/xfer/naws.py (a telnet host that prints its size): 38x26 in a half,
    38x13 after splitting under it, 38x20 after pulling the divider; a
    long line wrapping at 38 in a Shell pane; clicks moving the active
    pane (frame, solid/outline cursors); Close pane; a Shell ending in a
    pane (another tab shown there; then, none left, the pane closed);
    hide/show with panes; the window resized (panes kept their shares);
    a Zmodem download into a pane while another was active (the panel
    in its pane, md5-identical); the test page in a pane against a lone
    window of the same size: painter (fast RAM) identical but the
    prompt rows, TEXTDRAW (Text()) identical; left and right panes of
    the same page identical with TEXTDRAW.
  - **Speed, one pane** (tests/stock/bench.sh cterm on the stock A1200
    config, a private copy of the drive, 2026-10-11, alternating
    builds): 0.3b11 95.0, 95.9, 96.1, 95.3 s; 0.3b12 90.9, 70.7 s, and
    118.2 s once (a slow group the emulator falls into now and then:
    0.3b10 gave 118.8 s in it too). Not slower. The 30.4 s of 2026-10-10
    could not be had again today with any build, 0.3b10 included (see
    Knowledge/amiga/toolchain-and-testing.md, "conbench in FS-UAE");
    some boots also leave too little chip RAM in one piece to start any
    CTerm (all builds then end with 20).
  - **Also tested:** the chip-only drawing (a -DPPCHIP test build: the
    painter from a row's second colour run, Text() for the rest): left
    and right panes identical, and a pane identical to a lone window;
    own screen, full screen, panes above each other: the place bar
    beside the active pane only; a selection and Copy in a pane, Paste
    into another.
  - **Cost:** CTerm 243,252 -> 251,788 bytes.
  - **Pens on a 16-colour Workbench:** after hiding (the AppIcon's own
    colours) and a second CTerm, some of the test page's 40 colours came
    back as nearer ones (white text on white) - the screen has 16 pens
    for everyone; not the panes'.

- **Done 2026-10-11 (0.3b14): SFTP and FTP, a file browser** (the
  user's spec, 2026-10-11).
  - **The pieces:** `Parts/Files` (src/files.c: the window, transfers,
    SFTP's waits; src/ftp.c: FTP; src/askwin.c: a line asked in a small
    window, also in PhoneBook) is loaded when a place is called and
    unloaded with the last connection (FilesPart in part.h: call, menu,
    events, sigmask, tab_gone, screen, rexx, idle). SFTP is libssh2's
    `libssh2_sftp_*` in CTermSSH's ssh.c, reached through `sftp_ops`
    (a table; NULL in CTerm, so the part is one file for both programs).
    ssh_open's setup is shared (host key, key file, password,
    keyboard-interactive) - `want_sftp` makes it start SFTP instead of a
    pty and shell. CTerm itself keeps: the tab fields (t_fkind, t_fdir,
    t_rf), the stubs in parts.c, `loop_wfd` (loop_round also waits for a
    socket to take bytes), `xfer_show` (a part's transfer shown as a
    protocol's), the phone book's kinds and `dir=`, URLs (`book_url`,
    `call_place`), the `SFTP`/`FTP` start words, the menu item.
  - **A place = a tab + a window.** The tab holds the messages (the FTP
    greeting, ssh's host key question and password prompt - term_ask
    works there as for ssh - each file done); the window lists the
    drawer. Closing the window hangs up and closes the tab (so CTermSSH
    started for an SFTP place ends with it); closing the tab closes the
    window; Return in the tab opens it again. A screen switch or hiding
    closes the windows (files_screen(FALSE)) and opens them again after
    (TRUE); the connection stays.
  - **No freezing:** every wait is `await()`: loop_round with the socket
    (read) or loop_wfd (write), 1 s at a time, giving up on Stop, the tab
    closing or quitting (and after 60 s of silence). A long transfer
    that never waits also runs loop_round(-1, 0) every 0.1 s, so the
    window and Stop work. The browser's own events come back into the
    part while it waits (depth counter): everything but Stop is asleep
    (busy), closing the window while busy stops and closes after.
  - **Transfers** as Zmodem's: T->t_xfer for the panel, the tab name and
    the Transfers window; one at a time in all of CTerm (xfer_busy, also
    Zmodem/Hydra refuse while a browser's runs). Downloads: name.part,
    renamed when whole; a part shorter than the file goes on (FTP REST,
    SFTP seek); a file there is never written over (name.1...); names
    made safe (last part, UTF-8 to Latin-1, ':' '/' '\' and controls to
    '_', at most 100). Uploads: Latin-1 names to UTF-8; a file of the
    name on the host is replaced only after a requester (Replace / Skip
    / Stop; ARexx's PUT replaces).
  - **FTP:** blocking control and data sockets, read only when WaitSelect
    says so (a data socket made non-blocking lost its data in FS-UAE's
    bsdsocket: a download stood still); writes 2 KB at a time when
    writable (the low-water mark). EPSV, else PASV (its address ignored:
    the data goes to the host called, as curl does); MLSD, else LIST
    (Unix ls -l with the year or the time, links, names with spaces;
    DOS/IIS style); TYPE I; REST + RETR; STOR; MKD, RMD, DELE,
    RNFR/RNTO, SIZE, CWD/CDUP/PWD. After a stop, a NOOP's 200 shows
    where the host's 426/226 end. A 421 or a dead line: called again
    (and logged in again: the password typed is kept for that, wiped at
    the end) at the next command, once. Refusals that are expected
    (EPSV, MLSD, SIZE, the 426 after a stop) are not shown.
  - **SFTP:** every call non-blocking (SF_AGAIN: wait the way
    sf_dirs says); 32 KB reads (libssh2 reads ahead 4x the buffer);
    symbolic links followed with stat (the first 32) to tell drawers
    from files; a call given up in the middle (Stop while listing) drops
    the connection, so the next command starts clean; closing a file
    after a stop is done with `tidy` set (a Stop does not stop the
    clean-up). The start drawer: the place's, else realpath(".") (the
    home drawer).
  - **ARexx:** SFTP/FTP place (a new tab, like CALL), LIST, CD, GET
    (TO), PUT, MKDIR, DELETE, RENAME - queued (they wait), on the ARexx
    tab. CALL takes ftp:// and sftp:// too.
  - **The phone book:** kinds "sftp (files)" and "ftp (files)"; Line...
    becomes More... (the start drawer, `dir=` in the file); the window
    stays 542x198.
  - **Found on the way:** loop_round(-1, 0) with SocketBase open did a
    WaitSelect with no sockets and a zero time, which FS-UAE's bsdsocket
    turned into a wait for a signal (a download stood still until the
    mouse moved): now a SetSignal look. parts_free() was never called:
    ARexx's part (18 KB) stayed in memory after every CTerm that a
    script had talked to (~20 KB lost per run, now ~6 KB).
  - **Tested in FS-UAE** (verified 2026-10-11; tests/ftp/ftpd.py, the
    test sshd with `Subsystem sftp internal-sftp`, tests/files/uae.sh:
    a private drive with wasabid on port 1240): FTP on the A1200 + 8 MB
    fast: 3 MB and 8 MB downloads md5-identical (8 MB in 1.6 s), stops
    from the browser, the Transfers window and by closing the tab, each
    resumed with REST and md5-identical (also after the server was
    restarted: called again by itself), names "weird:name.txt" ->
    weird_name.txt and "odd: name ÅÄÖ.txt" (UTF-8 on the host) ->
    "odd_ name ÅÄÖ.txt", existing names numbered (big.bin.1, .2, .3),
    uploads by ARexx (8 MB in 6.7 s, md5) and the requester (Replace
    asked), "up é.txt" Latin-1 -> UTF-8 on the host; New drawer,
    Rename, Delete (asked) from the window; LIST and PASV with a host
    without MLSD and EPSV; a Shell tab typed in during a slow download;
    CTerm FTP user:pass@host, ftp://user:pass@host/sub in the Call
    window; hide/show and a switch from the own screen to Workbench with
    the browser open (it came back); tests/files/t-files.rexx (LIST,
    MKDIR, CD, PUT, RENAME, GET, DELETE, a missing file RC 10). SFTP on
    the A1200 030/50 + 32 MB (AmiSSL wants more than the 8 MB config
    had left): CTermSSH BOOK "Test SFTP" (key login, the host key
    known), 4 MB download md5-identical, a stop and a resume by seek
    md5-identical, PUT 300 KB md5, t-files.rexx all RC 0, a symlinked
    drawer, the odd name; Project/Open SFTP browser from an ssh tab (the
    home drawer). The window on a 640x256 Workbench (608x235) and on a
    1280x512 own screen (728x461).
  - **Speed** on the emulated 030/50: SFTP ~30 KB/s down and up - the
    ssh shell channel itself carried 1 MB in 70 s (~14 KB/s) there, FTP
    8 MB in 6.2 s (1.3 MB/s) on the same machine: encryption on a 50 MHz
    68030 is the limit, not the browser. Not yet tried on the PiStorm
    A1200.
  - **Cost:** CTerm 198,556 -> 202,348 bytes (+3.8 KB; book.c now -Os);
    CTermSSH 379,812 -> 411,464 (libssh2's sftp.o); Parts/Files 32,232;
    PhoneBook 15,052 (+askwin), ARexx 18,220, Call 6,864. Memory: see
    the table below.
  - **Not done:** FTPS (AUTH TLS: AmiSSL is CTermSSH's only - later),
    active FTP, whole drawers, upload resume, local times for UTC dates
    (MLSD, SFTP). (The Amiga's side as a second list: 0.3b15.)

- **Found on the real A1200, 2026-10-11 (0.3b14, against
  ftp.aminet.net; Emu68, lwip bsdsocket):** the browser listed fine and
  downloads arrived whole (RECENT, 10,803 bytes), but (a) the end of an
  FTP download's data was sometimes never reported "readable": the file
  stood at 100 % and the loop went round the wait - now a download whose
  bytes reached the size the host gave waits at most 2 s more, then
  ends; (b) FTP's sockets are non-blocking now (sock_nonblock,
  WOULDBLOCK), so no recv/send can hold CTerm, and the download loop
  sleeps a tick when the wait says "ready" and a look says "not yet"
  (commit c6cb6e8). (c) **Open:** during every download wasabid (the
  remote-control daemon, another program) stopped answering for ~15 s at
  a time, and once (the first time) never came back until a reboot
  (still running per `status`, unreachable). CTerm's task was in "wait",
  not spinning, while wasabid stalled; a double-click elsewhere did not
  stall it. Not reproduced in FS-UAE. Cause not found; suspects: a lock
  both hold (Intuition/layers from the ReAction window during a
  transfer, or something in the DOS writes), or lwip itself. 0.3b15
  removes the ReAction window (below); the A1200 retest will tell.
  Non-blocking sockets now work in FS-UAE too (8 MB down, md5): the
  stall of 0.3b14's first try was the zero-time WaitSelect, not
  FIONBIO.

- **Done 2026-10-11 (0.3b15): the file browser inside the terminal**
  (the user: "Since CTerm is a terminal, I want to stay in the terminal,
  just like ssh, telnet"). The ReAction window is gone; the panels are
  drawn in the place's tab like a program in a terminal (mc's look).
  - **The pieces:** `src/files.c` (the connection, both listings, the
    transfers, ARexx, the part's way in), `src/fileui.c` (the panels,
    drawing, keys, mouse, prompts, command line, key file), `src/ftp.c`
    (unchanged but for its messages and password question going through
    files.c: `fsay`, `ask_tab`). askwin.c is PhoneBook's alone now.
    FilesPart (part.h, PART_VERSION 3): call, menu, key, mouse,
    resized, tab_gone, rexx, idle - the window's events, sigmask and
    screen calls are gone from CTerm (main.c, hide.c, screen.c,
    classes.c).
  - **How keys get there:** a tab with `t_rf` (a connection) gives every
    key (not Right Amiga ones) to the part as a code - a character as
    the keymap makes it, KM_* for the rest (cterm.h, CFile's numbers
    plus PageUp/PageDown/Home/End/Insert/F11/F12 of a PC keyboard) - also
    during a transfer, so the stop key is the person's too. Clicks in
    the text go to the part while `t_fui` (the panels are up); Shift and
    the mouse still mark text. tell_sizes (and net.c's resize during
    waits) calls files_resized: everything is drawn again at the new
    size. The xfer panel over the tab's rows is not drawn for a browser
    tab (it shows its own progress line), and its Stop is not hit-tested.
  - **Drawing:** escape sequences into the tab's emulator (say()): CSI
    H, SGR (the theme's default colours; the bar, the active title and
    the key labels are inverse video - cyan on a 16-colour Workbench
    became grey on grey), box drawing U+2500/2550 (CTerm draws them), the
    tab switched to UTF-8 for it (Latin-1 names written as UTF-8). The
    alternate screen (?1049h) holds the panels; messages wait in a 4 KB
    buffer and are written to the main screen when F9 shows it, when
    the panels go, or when the buffer fills. A moving bar redraws two
    lines, a page turn its panel's list, the rest is drawn whole (a new
    drawer, a resize). The message line is never wider than W-1: a
    character in the last column would wrap and scroll the key bar away
    (seen: a long progress line did).
  - **Layout:** each panel half the width; columns by its inner width
    (name; size from 20 letters; a short date "10-11 07:50" from 34; the
    whole date from 52); a name too long loses its middle (`~`), a path
    its start. Under 24x6 it says so and waits for a bigger window.
  - **Questions in the tab:** y/n (delete, quit, replacing on upload:
    asked once for all the names that are there already), a line (new
    name, new drawer, the command line) with its own keys ([line]). A
    password or a host key question while the panels are up (calling
    again) puts the main screen back for term_ask, then the panels.
  - **Keys:** ENVARC:CTerm/files.keys, CFile's format (KEYS ACTION,
    sections [panels] and [line], `none`), written with the defaults
    when missing; read when the part loads (a change counts at the next
    browser). F1 lists the keys as bound; the key bar shows F1-F10's
    actions as bound.
  - **The command line:** ls/lls (as text over the panels), cd/lcd,
    pwd/lpwd, get/mget, put/mput, mkdir, rm/rmdir, rename, bye, help;
    patterns through ParsePatternNoCase/MatchPatternNoCase (`*` made
    `#?`); Tab completes the command or a name of the side the command
    is about; 8 lines of history.
  - **The Amiga's side:** Lock/ExNext into the same listing as the
    host's (struct Panel: REnt + a name pool, drawers first); NameFromLock
    gives the path; `..` above a volume's root lists the volumes
    (LockDosList); soft links are "Link" and are tried as drawers. Local
    rename (Rename), new drawer (CreateDir) and delete (DeleteFile, after
    y) too, as mc does on the active panel.
  - **A compiler trap (verified, read in the assembly):** bebbo's gcc
    (-Os, also -O1/-O2) compiled `draw_item(c, s, p->cur - p->top)` after
    `cmp.l (a4)+,d3` / `cmp.l (a3)+,a5` as `(a4) - (a3)` with both
    registers already moved on: p->top minus the first long of p->path
    was passed, and the bar was drawn on the border line. `-fbbb=-`
    (bebbo's extra backend pass off) makes it right; Parts/Files is built
    with it (+80 bytes). `-fno-auto-inc-dec`, `-fno-peephole2` did not
    help.
  - **The wheel on OS 3.2:** Intuition turns the NewMouse wheel's press
    ($7A/$7B) into an IDCMP_EXTENDEDMOUSE message (IntuiWheelData) and
    only the release reaches the window as RAWKEY - CTerm's wheel never
    worked on 3.2 (FS-UAE, wasabi's NewMouse keys). The window now asks
    for IDCMP_EXTENDEDMOUSE and makes each notch the old key (and ignores
    the raw keys once such a message came, so a wheel is never counted
    twice).
  - **Also fixed:** ARexx HANGUP on a browser tab said RC 5 (not
    linked); now it hangs up its place (Return calls again).
  - **Tested in FS-UAE** (verified 2026-10-11; tests/ftp/ftpd.py,
    the test sshd, tests/files/uae.sh with wasabid on port 1240): FTP on
    the A1200 + 8 MB fast: the panels at 77x26 (640x256 Workbench), at
    160x63 (a 1280x512 own screen, full screen), in a 38-column pane
    side by side and a 38x12 pane under it, after dragging the window
    smaller (about 58x18: the date column went); Tab, the arrows, Space,
    quick search, Backspace, Return, `*`; F7 new drawer on both sides,
    F6 rename (a Latin-1 é: UTF-8 on the host), F8 delete (asked); F5
    down 3 MB + 6 bytes and up the same, md5-identical; the replace
    question (n: nothing sent); `:lcd`, `:cd ..`, `:ls`, `:mget big8*`
    (two files), `:get big8.bin` - an 8 MB download stopped with Esc
    and resumed by F5 (REST), md5-identical; F9 the messages and back;
    F1 help, the wheel scrolling it and a panel; a click, a double-click
    on `..`, a click on the key bar's Help; a Shell pane typed in while
    a download ran in the browser pane (8 MB, md5); files.keys with
    `<F5> none`, `<F3> copy`, `c copy`: F5 nothing, F3 and c copied, the
    key bar showed 3Copy; files.keys deleted: written again, the
    defaults back; tests/files/t-files.rexx all as expected (LIST,
    MKDIR, CD, PUT, RENAME, GET md5, DELETE, a missing file RC 10). SFTP
    on the 030/50 + 32 MB (1280x512 own screen): the place from the
    phone book, a 4 MB download stopped and resumed (seek),
    md5-identical; 2 files up (md5); t-files.rexx (RC 0 where the
    names are there); ARexx HANGUP, Return calls again, the panels come
    back; F10 y closes the tab and CTermSSH ends. **Not tested:**
    Project/Open SFTP browser from an ssh tab (a blind menu pick missed;
    the code is the old path with the panels), the real A1200 (the
    wasabid stall above), PC-keyboard PageUp/Home/End codes, a
    password asked while the panels are up.
  - **Cost:** CTerm 202,348 -> 203,644 bytes (keys and clicks to the
    part, the wheel message); CTermSSH 411,464 -> 412,468; Parts/Files
    32,716 -> 53,160 (fileui.c is 24 KB: drawing, keys, the command
    line, the key file and its texts). Memory (fast config, Avail FLUSH,
    the same boot): CTerm idle takes 442,032 (0.3b14: 444,824); CTerm
    with an FTP place open takes 485,672 (0.3b14 with its window:
    807,416) - 322 KB less, the window classes no longer loaded. A run
    still leaves ~6.2 KB behind (as 0.3b14).

- **Done 2026-10-11 (0.3b16): pictures in the terminal** (the user's
  spec, 2026-10-11): sixel, iTerm2's inline files, kitty's swallowed.
  - **Stored as cells:** a picture is cut into tiles of a cell; a cell
    showing one holds `VT_PIC_CP(id, row, col)` (bit 31 set, id 13 bits,
    row and column 9 bits each: at most 511 cells a side) instead of a
    letter. So pictures scroll, go into the history, reflow, are erased
    and written over exactly as text is, with no second list to keep in
    step. A row's header cell (`[-2].pen`, free until now) marks a row
    that may hold picture cells (`VT_ROW_PICS`): rows without are drawn
    and walked as before; a whole-row blank clears the mark.
  - **The pixels:** per tab, in the emulator (vt.c, no Amiga headers), a
    table of `VtPic` (id, size, the cell size it was cut for, 24-bit
    RGB), oldest first. The pixel memory comes from the host's hooks
    (`vt_palloc`/`vt_pfree`: CTerm's AllocVec/FreeVec, so it goes back
    to the system at once, never into libnix's heap).
  - **The cap:** `pics_cap()` per tab: a quarter of what is free beyond
    the 512 KB reserve, at most 4 MB; and every allocation also asks
    `vt_room` (the history's reserve rule). Over the cap: first the
    pictures no cell shows any more go (a walk of the marked rows: the
    screens and the history), then the oldest - its cells become blanks
    (also in the history), so a cell never shows another picture that
    got its id. The walk also runs on ED 2/3, RIS, leaving the
    alternate screen and at the 32nd picture.
  - **Sixel** (DCS P1;P2;P3 q): decoded in vt.c as bytes come (a loop of
    its own in vt_write, not the state machine per byte): raster
    attributes (the size, made at once), colour registers (256, RGB and
    DEC's HLS, the VT340's 16 to start), repeat, $ and -. The picture is
    cut at the right edge where it starts (it is never wider than the
    cells left) and grows by halves only without raster attributes.
    Unset pixels: the theme's background. Aspect ratios are ignored
    (1:1, as foot). Placed at the cursor; the screen scrolls for it; the
    cursor goes to the line below, at the picture's left column (DECSET
    8452: right of its last row); DECSET 80 (DECSDM): at the top left,
    cut at the bottom, the cursor stays. CAN/SUB drop it.
  - **iTerm2** (OSC 1337;File=args:base64 BEL/ST): the base64 is decoded
    as it comes into a growing buffer (under the cap); at the end
    `vt_decode` (CTerm: Parts/Pictures) turns the file into ARGB, which
    vt.c scales (nearest pixel, 16.16 steps) to width=/height= (cells,
    px, %; preserveAspectRatio: inside the box) and to the width left.
    The cursor ends right of its last row (as iTerm2; imgcat sends a
    LF). inline=0 (a download) is swallowed.
  - **kitty** (APC G): swallowed whole (APC was already a string to
    ST), so its base64 never shows.
  - **Replies:** DA1 `?62;4;22c` while pictures are on (else as before);
    CSI 14 t (text area in pixels), CSI 16 t (the cell); XTSMGRAPHICS
    `? 1;1;0 S` (256 registers) and `? 2;1;0 S` (the area); DECRQM 80
    and 8452. In the Amiga Shell CSI n t stays the console's own.
  - **Drawing** (draw.c): the RTG painter copies a picture cell's tile
    into its row buffer (the rest of the cell the background), so a row
    of text and picture is one WritePixelArray as before; the 8-bit
    card's painter (paint_row8) and Text()'s path take pens; draw_cells
    writes a run of picture cells straight to the window
    (WritePixelArray on a deep card without the painter,
    WriteChunkyPixels with pens on plain screens - it clips to the
    layer). The planar painter (ppaint.s) is never used on a marked row
    (it would draw glyphs); on a plain screen a picture sets the write
    mask to all planes, so scrolls carry it (until the next full
    redraw). Rows without the mark: one load and a test more per row,
    nothing per cell.
  - **Pens** (Parts/Pictures, pics.c): a colour cube by depth - 6x6x6
    on 256 colours, 4^3 on 128, 3^3 on 64, the 8 pure colours on 32 and
    16 (27 on 16 pens: the first free pens went to the dark entries and
    yellow came out grey) - its pens asked with PRECISION_EXACT
    (PRECISION_IMAGE handed out near ones already given: the cube fell
    into its neighbours, yellow came out yellow-green; measured on a
    256-colour own screen), kept in CTerm's pen cache (`pic_pen`), so
    `release_pens` gives them back; `pen_gen` tells the part to ask
    again. Ordered 4x4 dithering by screen position, with per-channel
    tables (no divide a pixel).
  - **Parts/Pictures** (3.5 KB): decoding iTerm2's files (written to
    `T:CTerm-pic.<task>`, read by picture.datatype V43 as ARGB, alpha
    put over the background, deleted) on a deep stack (`deep_call`), and
    the pens. Loaded when the first file comes or a picture is drawn on
    a screen of 256 colours or fewer; unloaded between rounds when no
    tab holds a picture (`pics_tidy`). It is called while a tab's text
    is parsed: a missing file says nothing (`quiet` slot: say() there
    would re-enter vt_write on the same emulator) - the picture just
    does not show.
  - **ShowPic** (tools/showpic.c, 4.4 KB): a file as iTerm2's code, as
    imgcat. An ARexx version did the same but took ~100 s for a 10 KB PNG
    on the emulated A1200 (base64 in ARexx); ShowPic takes a moment.
  - **The setting:** `pics` (prefs `pictures=0` when off), Settings, Text
    tab; on at first, on every screen (pictures are drawn on all of
    them); off, the codes are swallowed and DA1 says no sixel.
  - **Tested natively** (verified 2026-10-11): tests/pic_test.c in
    `make test` (pictures from tests/pictures/mksixel.py: placement,
    cursor, pixels, scrolling into the history, overwrite, erase and the
    memory back, the right-edge cut, DECSDM, 8452, the cap dropping the
    oldest with no cell left showing it, iTerm2 through a PPM decoder,
    width=, a download and a broken file, kitty swallowed, reflow,
    replies). img2sixel 1.10.5 is installed here but wrote nothing (any
    input): its test is skipped. The fuzzer (fz.c) got sixel, OSC 1337
    and APC fragments, a PPM decoder, pictures on/off and small caps,
    and checks that every picture cell shows a picture there is, inside
    it, on a marked row: seeds 1..846 passed (5 minutes, ASan/UBSan).
  - **Tested in FS-UAE** (verified 2026-10-11; tests/pictures/uae.sh: the
    A1200 + 8 MB fast, a private drive, wasabid on 1250): a 16-colour
    Workbench, an own screen of 256 and of 16 colours (the bars'
    pixels exact on both: FFFFFF FFFF00 00FFFF 00FF00 FF00FF FF0000
    0000FF 000000), `Type` of sixel files in the Shell, ShowPic (PNG
    through datatypes, WIDTH 20, 30, 50%), the scrollback, two panes
    (pictures cut at the pane edge), hide/show (identical), pane close,
    tab switch (the reflowed picture), ESC c and CSI 2J, the setting off
    (swallowed) and on, and a telnet host (tests/pictures/picserve.py):
    the replies `?62;4;22c`, `6;8;8t`, `4;184;608t`, `?1;0;256S`,
    `?2;0;608;184S`, the bars, the PNG (width=30), kitty swallowed, the
    gradient. Memory (Avail FLUSH, same boot): before CTerm 8,938,440,
    CTerm idle takes 450,912 (0.3b15: 442,032), the 282x176 Kickstart
    picture 152,064 more (148,896 its pixels, the rest Parts/Pictures),
    all of it back after a clear (72 bytes off). **Not tested:** RTG
    (FS-UAE has the uaegfx card built in, but the drive has no
    Picasso96, and the P96 package on the PC has no
    cybergraphics.library, which CTerm's RTG painters need): the RTG
    painter's and the 8-bit card's picture paths are read through only;
    the real A1200.
  - **Cost:** CTerm 203,644 -> 215,324 bytes (the emulator's picture
    code ~5.7 KB, compiled -Os; drawing ~1.7 KB; the rest glue,
    replies and relocations) - more than the few KB hoped for; the
    iTerm2 sizing and scaling could move into the part (it would need
    the part to allocate the picture through the hooks). Parts/Pictures
    3,560, Settings 20,264 -> 20,420.
  - **Speed** (tests/stock/bench.sh on the fast config, a private drive,
    alternating builds): first 0.3b15 17.16/17.04/17.20 s against
    17.40/17.48/17.36 - every pair slower, with no picture on screen:
    inlining and layout in the parser changed. Every picture function
    is now cold and noinline (-Os), the clean-up called only when a
    picture exists: 0.3b15 17.24/17.08/17.08, 0.3b16 17.20/17.20/17.08.
  - **Seen once:** a stray cursor block at the end of a wrapped command
    line after ShowPic scrolled the screen; not seen again in four
    tries (wrapped lines, scrolling, sixel and PNG).
  - **Not done:** kitty's pictures shown (they are swallowed); sixel
    aspect ratios; iTerm2's MultipartFile; a picture's own palette on
    screens of few colours (the cube is shared); pictures redrawn at a
    new letter size (a tile keeps its old size, cut or filled).

- Later idea: Hydra's chat with the other side during a transfer
  (HydraCom's CON device channel; AmiExpress has it) - niche, parked.
- Also: an ssh password login tried against a real server,
  dropping files on the window, emoji fallback, more stock-A1200 speed.
- **Done 2026-10-11 (0.3b19): panes the i3 way** (the user's wish:
  drive CTerm as i3/Sway, Left Amiga as the Mod key; then "tab mode"
  and "pane mode").
  - **The tree** (pane.c): the binary tree of 0.3b12 without its limit
    of four: as many panes as tabs (MAXTABS 16 now, MAXPANES the same)
    and room (each at least 10 x 3 letters; a split that would go
    below is refused with a beep and "no room for another pane" in the
    title). Dividers pulled keep each side's whole subtree its letters
    (`need()`: the panes' minimum along the split, dividers added).
    Each pane keeps a direction (`sdir`, 'h' at first): Left Amiga+H/V
    sets it, a pane split off it inherits it (i3's marked container).
  - **pane_do(op, arg)** does it all, for keys ([panes]), the Panes
    menu and ARexx: PO_OPEN (split the active pane its way, a new Shell,
    focus there), PO_KILL (the pane goes - the sibling takes the room -
    and its tab is closed as Close tab does; a transfer in it: a
    requester first, from keys and the menu, not from ARexx), PO_FOCUS /
    PO_MOVE by geometry (`neighbour()`: panes wholly that way that
    overlap along the other axis; the nearest, then the most overlap;
    move swaps the two panes' tabs and directions, focus follows),
    PO_DIR, PO_FULL, PO_SHOW (tab n here; a tab in another pane swaps
    with this one's), PO_TABS, PO_PANES.
  - **Fill the window (PO_FULL):** the tree, each leaf's tab and
    direction and the active pane are kept aside (`znodes`, `ztab`),
    and the window has one pane. The tabs behind keep their size
    (`pane_kept()` in layout_and_resize: no NAWS, no resize report while
    filled - verified with naws.py: only the filled tab was told).
    Again: the layout back; the tab shown meanwhile goes into the pane
    that shows it, else into the active one; a tab that ended meanwhile
    leaves its pane to a tab in none, or the pane goes. A split, focus,
    move or close while filled first brings the layout back.
  - **All tabs to panes (PO_PANES):** k tabs, rounded up to even m; m =
    c x r with r the largest divisor <= sqrt(m) (2: 2x1, 4: 2x2, 6: 3x2,
    8: 4x2, 10: 5x2, 12: 4x3), columns the long side in a wide window,
    rows in a tall one, else turned if only that fits. Odd k: the
    active tab first, spanning column 0's rows 0 and 1; the others in
    tab order row by row. Built as a chain of 'h' splits over columns,
    each a chain of 'v' splits weighted by cells (per mille). When even
    the turned grid does not fit, k-- until it does (the active tab is
    always in); the title says "panes: 6 fit, 2 stay tabs". **All panes
    to tabs (PO_TABS):** one leaf, the active tab.
  - **Left Amiga keys:** keymap.c's `KQ_L` (0x4000), `<L-x>` in the
    file, "Left Amiga+X" in words; keys_code no longer drops Left Amiga.
    A key no action has still goes to the program as before. [panes]
    is checked right after [window], before the file browser and the
    transfer's key rules, so it works in every tab, also during a
    transfer. The file is written again once when it names no [panes]
    action (verified: a custom `<A-n> newtab` kept, [panes] added).
    Settings' Keys tab: a heading "Panes, the i3 way", the list in the
    file's order; [window] and [panes] both "everywhere" (a key in both
    is marked `!`, [window] wins).
  - **What reaches the window (verified on 3.2.3 in FS-UAE with a key
    logger, tests/panes/keylog.c):** Left Amiga + Return, Q, H, V, F, T,
    P, 1..9, B arrive as RAWKEY with qualifier $40. **Left (and Right)
    Amiga + an arrow do not:** Intuition takes the press for its
    keyboard mouse (the pointer moves); only the release arrives. So
    such bindings ([window]/[panes], Amiga + an arrow, any Shift, Ctrl,
    Alt) are commodity filters on CTerm's broker (hide.c `cx_keys`),
    ahead of Intuition (commodities' input handler runs before it):
    CxFilter (IX: the press only; qualifier synonyms need both bits -
    `LSHIFT|RSHIFT` with IXSYM_SHIFT, else Shift never matched), a
    CxSender to the broker's port with the key as id, CxTranslate(NULL)
    to swallow it. On only while CTerm's window is active (ACTIVEWINDOW
    / INACTIVEWINDOW, off when the window closes): elsewhere the keys
    move the pointer as ever (verified: a second window got only the
    releases while CTerm's layout stayed). Remade with the menus (a key
    change in Settings).
  - **Menus:** the Panes menu: New Shell pane, Close pane and tab, New
    panes side by side / one below other (ticks follow the active pane),
    Focus > and Move > (submenus), Fill the window (tick), All panes to
    tabs, All tabs to panes, then the old four. Left Amiga keys are
    written beside the items (NM_COMMANDSTRING). menu_ticks keeps a
    small table of ticked items. The key texts are in one pool now
    (1,200 bytes) instead of 40 bytes for every item.
  - **The 7-Shell wall:** each Shell tab took two signals (its port, its
    helper's end) and a task has 16: the eighth Shell said "CTerm could
    not start a Shell" (0.3b18 too, at MAXTABS 9). The consoles now
    share one port signal and one end signal (console.c `sig_get`), each
    console looking at its own port and a `child_done` flag the helper
    sets before signalling. Verified: 12 Shells in a 4 x 3 grid, endcli
    in one, LA+Q in another, the rest alive.
  - **A compiler trap (verified, not understood to the instruction):**
    `panes` as a pointer (to pane1 or an AllocVec'd block, the plan: no
    memory for panes never used) made every pane blank after a window
    resize - redraw_all and repair_damage ran with the right sizes and
    the painter path, yet nothing showed until the next full redraw
    (Right Amiga+1). With -fbbb=- (bebbo's backend pass off) or a DBG
    build it drew; with HEAD's pane.c changed only to `struct Pane
    *panes = panes_` it failed too. So `panes` is a static array again
    (16 panes, ~8.5 KB). See Knowledge/amiga/toolchain-and-testing.md.
  - **ARexx:** PANE OPEN, KILL, SPLIT H|V, FOCUS dir, MOVE dir, FULL,
    SHOW n, MODE TABS|PANES (template `WHAT/A,ARG`); the old SIDE, UNDER,
    NEXT, SELECT n, CLOSE (the pane only), COUNT, CURRENT as they were.
  - **Tested in FS-UAE** (A1200 + 8 MB fast, 3.2.3, a private drive in
    /tmp/ctpanes, wasabid on 1246, tests/panes/uae.sh; verified
    2026-10-11, screenshots looked at): six panes built with LA+Return,
    LA+H, LA+V; LA+arrows from each (six moves, each to the expected
    pane, checked by an echo typed there); LA+Q in the middle of the
    tree (the sibling grew, the tab went); LA+F and back (the pane
    reflowed full size, then back); LA+Shift+Right and the Move menu
    (swapped); LA+1 (tab 1 swapped into the active pane); LA+T and LA+P
    with 2, 3, 4, 5, 6 tabs (3 and 5: the active one double; 6: 3 x 2)
    and 8 tabs in a small window (6 fit, title said so); 12 Shells as a
    4 x 3 grid; own screen 16 colours (planar painter) with six panes,
    sixel bars and the Kickstart picture in small panes, cut at the
    pane edges; LA+F, LA+T, LA+P there with the pictures; hide and show
    with six panes; the window resized with six panes; the FTP browser
    in a pane (its keys, LA+arrows out of it and back, LA+F on it);
    NAWS from tests/xfer/naws.py: 72x25 alone, 36x12 and 35x25 (the
    active, double) as panes, 72x25 again as tabs; a Zmodem download
    and LA+Q (the requester; Cancel went on; Close stopped it and closed
    the pane and tab); custom keys `<L-j> focusleft`, `<L-x> kill`
    (worked, the old keys then reached the Shell); Settings' Keys tab:
    the [panes] rows, Press a key with Left Amiga+W.
  - **Seen, not this work's:** on the own 16-colour screen, after hide
    and show, the dividers and the active frame came back in other pens
    (the mixes are pens asked again; 0.3b18 not compared). Typing
    quickly into tests/xfer/naws.py's echo shows only the first letter
    of each burst - 0.3b18 the same; the test host's or the emulated
    socket's, not looked into.
  - **Not tested:** RTG and the chunky painter with many panes (FS-UAE
    has no RTG here: read through - nothing pane-specific changed in
    them); the real A1200; ssh tabs as panes.
  - **Cost:** CTerm 226,772 -> 234,448 bytes (pane.c's i3 work ~3 KB at
    -Os, the menus, the hot keys, the key table); CTermSSH 435,228 ->
    443,220; Parts/ARexx 18,256 -> 18,976, Parts/Settings 26,272 ->
    27,096. CTerm idle (fast config, Avail FLUSH, the same boot, 0.3b18
    and 0.3b19 alternating, twice): 0.3b18 takes 467,416 and 463,520,
    0.3b19 479,952 twice - ~14 KB more: the 16 panes' marks (~8.5 KB,
    static, see the compiler trap) and the bigger menus. A run still
    leaves ~6.2 KB behind, as before.
  - **Speed, one pane** (tests/stock/bench.sh CONF=fast on the private
    drive, alternating): 0.3b18 17.12, 17.02, 17.26 s; 0.3b19 17.12,
    16.98, 17.06 s - the same.
- **Done 2026-10-11 (0.3b18): new tabs' characters, one key file, the
  browser's drawing**
  - **New tabs start in the default characters.** vt_set_amiga cleared
    utf8 (and utf8_set), and shell_tab called it after tab_new's
    vt_set_utf8: a new Shell tab read Latin-1 while its t_utf8 (and the
    menu's tick, made once from the prefs) said UTF-8; the first choice
    of the item "turned it off" (no change), the second on. Now
    vt_set_amiga leaves the characters alone (the callers set them;
    tests that want a Latin-1 Shell call vt_set_utf8(v, 0) first, the
    fuzzer's mode 2 too). tab_new sets t_utf8/t_pcmode from saved_utf8/
    saved_pc (PC BBS was never applied to a new tab); call_tab sets
    UTF-8/Latin-1 back when the Call window's PC box is off.
  - **The default is Settings' (Text: Characters).** The chooser shows
    saved_utf8/saved_pc (it showed the shown tab's), Use/Save set them
    (also from a phone book tab now), and the shown tab follows at once
    unless it has its own (a phone book place's, the browser's). The
    menu items UTF-8 and PC BBS change only the shown tab (UTF-8 set
    saved_utf8 too, so a choice there leaked into new tabs and the
    prefs).
  - **The ticks follow the shown tab** (screen.c menu_ticks): the two
    items' MenuItems found once after CreateMenus; the main loop compares
    their CHECKED with shown->t_utf8/t_pcmode every round and only on a
    difference takes the strip off (ClearMenuStrip, flags,
    ResetMenuStrip). Covers tab switches, panes, new tabs, phone book
    places, the browser's forced UTF-8, ARexx.
  - **One key file.** The browser's 36 actions are CTerm's now:
    keys_acts gets KC_FILES ([files], the panels) and KC_FLINE
    ([filesline]) in fileui.c's K_* order (KA_FB_FIRST.. a compile-time
    check in fileui.c), their defaults in defbinds (KQ form: `<C-r>` is
    KQ_C|'r', `<S-BS>` KQ_S|8). Parts/Files keeps only labels and
    descriptions; find() turns its key code (KM_SUP, a Ctrl letter as
    its control character - browser_key unchanged) into the KQ form
    and asks keys_find through PartEnv; F1's list and the key bar come
    from keys_binds. PART_VERSION 6. keys_save writes five sections.
  - **Migration:** keys_load reads CTerm.keys; when it names no
    [files]/[filesline] action and ENVARC:CTerm/files.keys is there, that
    file is read over the defaults (its [panels] and [line] are accepted
    as section names, default section [files]), CTerm.keys is written
    whole and files.keys renamed files.keys.old (never read again). No
    files.keys: CTerm.keys is rewritten once so it lists the browser's
    keys. Verified: a files.keys with an added `c copy` became `c copy`
    under [files], and c copied the marked files.
  - **Settings, Keys tab:** two more headings ("File browser: the
    panels (its own)", "... a line typed"); kother/kclash: a key in two
    sections is marked ! only where one hides the other - [window]
    against [call]/[shell] as before, [window] against a browser section
    unless the window action is a history key (those stand aside in a
    browser tab). Verified: `<Tab> s` for switch typed in the field,
    Saved, s switched panels; Shift+Up (window pageup and browser
    pageup) not marked.
  - **The late panel switch:** not reproduced, in 0.3b17 (a worktree
    build) or 0.3b18, on the A1200+8 MB and the 030/50 configs, FTP and
    SFTP, one pane or split, grabs right after the key or 1.5 s later:
    to_side draws and end() refreshes. To be sure every key and click
    shows at once whatever path the part takes, parts.c files_key and
    files_mouse refresh the tab after the part returns (a no-op when
    nothing changed).
  - **Tested in FS-UAE** (headless, private drive, wasabid 1240,
    tests/ftp/ftpd.py, tests/ssh/test-sshd.sh): default UTF-8: first tab
    and a new Shell tab `type` a UTF-8 file and a Latin-1 file right
    (look-ahead rule); menu ticks per tab after Right Amiga+1/2; the
    Text tab set to Latin-1 (Use): a new Shell tab shows the UTF-8 file
    as Latin-1 bytes and the Latin-1 file and `echo Å` right, unticked;
    Saved, then a new boot: the first tab Latin-1 and unticked; a phone
    book place with chars=latin1 unticked; the FTP browser tab ticked
    UTF-8.
    make test passes.

- **Done 2026-10-11 (0.3b17): every key of CTerm's own configurable**
  (the user's rule for all their programs).
  - **The file:** `ENVARC:CTerm/CTerm.keys`, CFile's/Ahiru's format (KEYS
    ACTION, `[sections]`, `;` notes, `KEY none`), extended with
    qualifiers in the name: `<A-t>` Right Amiga, `<C-r>` Ctrl, `<M-b>`
    Alt, `<S-Up>` Shift (names only; Shift is in a character), combined
    (`<C-S-Tab>`), and `none ACTION` for an action with no key. Written
    whole when missing (every action, `none` lines for the unbound), so
    it documents itself. An action the file names has the file's keys
    only; actions it never names (new ones in a later CTerm) keep the
    built-in keys. Old prefs `keyuser=`/`keypass=` become the first
    file's send keys, then are dropped from the prefs.
  - **Sections:** `[window]` (every menu item - 41, the ones without a
    shortcut too -, tab1..9, nexttab/prevtab, the history: pageup,
    pagedown, lineup, linedown, historytop, historyend), `[call]` (stop:
    a transfer, giving up a call; redial after a call ended), `[shell]`
    (the local Shell's line editor: left right start end wordleft
    wordright back delete wordback killstart killend clear older newer
    search complete accept eof). Order in key(): the file browser tab
    (window keys but the history's, then files.keys), a running
    transfer (stop; non-Amiga keys wait), `[window]`, the Shell's line
    while cooked (`[shell]`, else the keymap's bytes as before), a call
    that ended (`[call]` redial only), then the terminal's own keys to
    the host. What goes to the host (arrows, F-keys, Alt=Meta) is the
    terminal standard's and not in the file.
  - **A key** (keymap.c `keys_code`): the raw key, named keys (KM_*,
    13 27 9 8 127 ' ') with their Shift, else MapRawKey without Ctrl,
    Alt and Amiga - the character with Shift in it, letters lower-cased
    under Ctrl/Amiga - and KQ_* bits. Left Amiga is never CTerm's. One
    linear lookup per key press (49 bindings); no cost on output.
  - **Menus:** a Right Amiga + character binding becomes the item's
    CommKey (Intuition handles it: MENUPICK, no RAWKEY); any other key
    is shown with NM_COMMANDSTRING ("F9", "Ctrl+Shift+F1") and handled
    by key() -> menu_pick(). Remade on Use/Save.
  - **Settings, Keys tab** (Parts/Settings): a listbrowser (action, its
    keys in words; headings per section), a Keys field (typed as in the
    file, Return), Press a key (window.class WINDOW_IDCMPHook with
    IDCMP_RAWKEY: the hook only notes the key, settings_events does the
    work; GADGETUPs while waiting are dropped - a key could hit a
    button's `_S` shortcut), None, Defaults. A key taken from another
    action of its section: said in the title; a `[window]` key that is
    also a `[call]`/`[shell]` key: marked `!`, warned once on Save/Use.
    Save writes the file, Use keeps them until quit, Cancel restores the
    copy taken at open. Messages that name keys ("Esc gives up",
    "Return calls again") name the bound key (say_key, Protocols/Serial
    through SerEnv.keyword, SERMOD_VERSION 2). PART_VERSION 5.
  - **The line editor** (console.c con_edit): new word moves, word
    delete, kill to start/end, start/end on Ctrl+A/E, history search
    (Ctrl+R: the next older line holding what was typed when the search
    began). Ctrl-J still ends a line; Ctrl-C..F stay break keys.
  - **A compiler trap (verified, assembly read):** keys_find's loop over
    `{UWORD key; UBYTE act}` compiled (main program, -O2 -flto) as
    `cmp.w (a0)+,d3` then `move.b 2(a0)` and `addq.l #4,a0`: the wrong
    field and a stride of 6 - no lookup ever matched, wrong actions ran.
    keymap.c is now compiled on its own with `-fbbb=-` (src/keymap.o,
    outside LTO); Parts/Settings too. Same bug as fileui's (see the
    knowledge base).
  - **Not done (done in 0.3b18):** files.keys stays the browser's own file (read by
    Parts/Files when it loads; folding it in would put its defaults in
    CTerm); the Keys tab does not edit it. term_ask's typed answer
    (password, host key yes/no) keeps Return and Backspace.
  - **Tested in FS-UAE** (A1200 + 8 MB fast, private drive, wasabid on
    1243; verified 2026-10-11): the file written on first start with an
    old `keyuser=J` migrated to `<A-j> senduser`; menus show Right
    Amiga letters and `F9`/`Ctrl+Shift+F1` for others; F9 opened a tab,
    F10 moved tabs, Right Amiga+A (given to About) printed it, Right
    Amiga+T unbound typed a t; Ctrl+Tab/Ctrl+Shift+Tab; the Shell line:
    Ctrl+W, Ctrl+R, Ctrl+Left, Ctrl+A/E, Shift+Left + Ctrl+K, Ctrl+U;
    redial bound to F6: "F6 calls again", Return ignored, F6 called;
    Settings: Press a key (Right Amiga+N), taking a key from another
    action, `<A-w> <Return>` typed (marked `!`, warned once), Defaults,
    Save (file rewritten, F8 then opened Call...). The FTP browser's
    keys still move the bar and switch panels.
  - **Cost:** CTerm 215,388 -> 224,600 bytes (keymap.o 5.9 KB, the
    line editor's new actions, the vt lookahead); Parts/Settings 20,420
    -> 25,252; Protocols/Serial 10,140 -> 10,244. CTerm idle (fast
    config, Avail FLUSH, same boot): 0.3b16 451,656 (418,720 the first
    time), 0.3b17 461,720 twice - about 10 KB more (the code, the key
    table, the menus' key texts). conbench CONF=fast (3 runs each):
    0.3b16 17.06, 17.20, 17.10 s; 0.3b17 17.04, 16.98, 17.20 s. A first
    try that checked "a picture just placed" at the top of vt_write's
    byte loop cost 17.34, 17.34, 17.42 s: the check went into the LF
    control code, and the held-9B test under the unfinished-sequence
    branches.
- **0.3b17, three fixes found testing 0.3b16:**
  - **UTF-8 in the Amiga Shell, a lead C2-DF then 9B** (vt.c held()):
    the 9B and what follows are held (across writes too) until it is
    clear: a whole, plausible Amiga CSI (digits, `;`, a leading `>`/`?`,
    one space before p q r v; a final the console uses - with no
    parameters only @ A-H J-M P S T Z m) makes it the Latin-1 letter and
    CSI (Å + CSI K, Ä + CSI 1 D); anything else is the UTF-8 letter
    (Л, ś, ě, Û...) and the held bytes go on as text. Cold code, off the
    hot paths (they skip while a sequence is unfinished). A letter last
    in a write waits for the next byte before it shows. amiga_test has
    the cases; the fuzzer got a fifth mode (Amiga Shell + UTF-8) and
    `D0 9B`, `C5 9B`, `9B >?`, `9B 0 SP p` fragments.
  - **The cursor after an iTerm2 picture:** on the line below at the
    picture's left column (as sixel), not right of its last row - a
    prompt never lands beside it. imgcat and ShowPic write a new line
    after the picture; that one (CR LF too) is taken (pic_nl, in the LF
    control only, not the byte loop) while the cursor is still there and
    nothing was written on its line.
  - **A backdrop behind picture cells** (RTG, paint_row): what a tile
    does not cover (the bottom of a picture's last row when the picture
    is not a whole number of cells high, the right of its last column,
    a picture whose memory went) was the plain background colour, a
    solid band right above the prompt; now the backdrop shows there as
    behind text. Read and fixed, not run: FS-UAE has no RTG here.
  - **Left open:** in the file browser a panel switch (Tab) shows only
    with the next key (seen in 0.3b16 as well, FS-UAE). A new Shell tab
    reads Latin-1 (vt_set_amiga) while Settings/UTF-8 shows ticked from
    the prefs: the first choice of the item turns it off, the second on
    (seen 2026-10-11, older than 0.3b17). Both: see 0.3b18.

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
  in a UTF-8 Shell (0.3b17: it can - the bytes after the 9B decide).

Speed on a stock A1200 (FS-UAE, conbench): 99 s at first, 39.6 s on
2026-10-10, CCON 26.0 s; on the PiStorm A1200 4.45 s against CCON's 4.26.
Notes and what is left: Knowledge/amiga/graphics-and-performance.md.

## Memory (0.3b13, 2026-10-11)

**CTerm needs fast RAM** (decided by the user, 2026-10-11): it needs
AmigaOS 3.2 already, and there are basically no 3.2 installations on
chip-only machines. At the start, with no fast memory or with CTerm's
own code not in it, CTerm says "CTerm needs an Amiga with extra memory
(fast RAM)" (in the Shell, or a requester from Workbench) and ends
(verified on the stock A1200 config: RC 20, the line in the Shell).
Chip-only Amigas are no longer a target: their own code paths (the
painter's pp_all chosen by TypeOfMem, Text() for one-colour rows,
PACE's draw-cost pacing for planar screens) may stay but can be
simplified later.

**What was found** (stock A1200 config, FS-UAE, before the decision):
conbench went from 30.3 s (0.3b7) to 96.4 s (0.3b8) - memory, not code
(with 8 MB fast both were ~17 s). A MEMLOG build that counts failed
allocations (a low-memory handler, AddMemHandler) showed 55,617 failed
16 KB allocations in one run: the history grew until the machine was
full, then every new line asked libnix's malloc for a new 16 KB pool,
failed, and ran exec's low-memory handlers (ramlib's expunge walk) -
once per scrolled line. The same pattern was waiting in term_take
(AllocVec(PENDMAX) on every write when it failed), the host buffer
(on every read) and the pen table (on every new look). Also: libnix
opened asl, diskfont, datatypes, rexxsyslib, commodities, icon and
workbench at the start, because CTerm's library bases were common
symbols and libnix's stubs supplied the definitions (`nm CTerm | grep
" D _.*Base"`); and the Shell's helper process had a 16 KB stack for a
call that uses ~0.5 KB (the Shell itself is a process of its own).

**What was done:**
- the history grows only while a reserve stays free (512 KB with fast
  memory, 64 KB without) and stops for good at the first line it
  cannot have (a clear lets it grow again); free memory is counted
  every 64th new line, not each;
- the Shell's batch buffer, the host buffer and the pen table are asked
  for once: without memory CTerm parses at once / reads 1 KB / uses the
  default look, and does not ask again;
- the library bases are CTerm's own (`= NULL`), so nothing is opened
  that CTerm does not open itself; rexxsyslib is not opened at a start
  unless another CTerm's port exists;
- the prefs buffers (2 x 3.6 KB), the TrueType row buffers (4 KB) and
  the phone book are made when used, not at the start; the Shell
  helper's stack is 4 KB;
- the parts (part.h): the settings window (with the serial line's
  window), the phone book window, ARexx's commands, the Call window,
  the Transfers window and the speed test are files in `Parts/`, loaded
  when wanted and unloaded when their window closes (ARexx's stays once
  loaded: scripts may wait in it). The window classes close with the
  last ReAction window. A part is CTerm's own source built with -DPART;
  it sees CTerm only through PartEnv (partlist.h: V variables, A
  arrays of no size, F calls), the Makefile makes partuse.h from the
  list, and the link fails if libnix gave a part a library base of its
  own (it would be NULL: libstubs' D symbols). Traps: a name in the
  list must not be a struct member or local name in a part (setwin's
  snapshot members became k_*); a part's exported calls are static
  with other names (the cterm.h prototypes are CTerm's stubs);
  const arrays sit in the text hunk, so nm calls them T (functions);
  serwin.c is in both window parts.

**Numbers** (FS-UAE, private drive copies, wasabi on port 1240):

| | 0.3b7 | 0.3b8 | 0.3b12 | 0.3b13 | 0.3b14 |
|---|---|---|---|---|---|
| CTerm (bytes) | 202,960 | 231,176 | 251,788 | 198,556 | 202,348 (0.3b15: 203,644; 0.3b16: 215,324; 0.3b17: 224,600) |
| free, CTerm idle, stock A1200 (2 MB chip) | 85,752 | 60,056 | 39,960 | 119,408 | |
| free, CTerm idle, A1200 + 8 MB fast | | | 8,413,512 | 8,496,696 | |
| CTerm idle takes, A1200 + 8 MB fast (2026-10-11, same boot) | | | | 442,304, 438,448 | 440,968, 440,968 |
| conbench, stock A1200 | 30.3 s | 96.4 s | fails/91-118 s | 30.7 s (now refused) |
| conbench, A1200 + 8 MB fast | 16.78-16.92 (4) | 16.76 | 16.76-17.08 (6) | 17.00-17.40 (10) |
| conbench, A1200 030/50 + 32 MB | | | 21.34, 21.50 | 21.98, 21.34, 21.46 |

(Free memory before CTerm: 549,048 chip / 8,938,984 fast config. The
stock 0.3b13 conbench was measured before the fast-RAM check came in.)
On the fast A1200 0.3b13 is ~0.25 s (1.5 %) slower than 0.3b12 in the
mean, not isolated: the first commit of this work (no parts) shows it,
0.3b12's vt.c in today's tree gave 17.13 s (4 runs) - near the noise;
on the 030/50 they are the same.

Parts and their sizes: Settings 20,264 bytes, ARexx 16,992, PhoneBook
13,292, Call 6,140, SpeedTest 4,476, Transfers 4,416. (0.3b14: Files
32,232, PhoneBook 15,052, ARexx 18,220, Call 6,864.)

0.3b15: CTerm idle 442,032 bytes, with an FTP place's panels 485,672
(0.3b14: 444,824 and 807,416, its window and the window classes).

0.3b14, measured with Avail FLUSH on the fast config (tests/files/uae.sh,
free before CTerm 8,948,840): the file browser open (an FTP place: its
tab, the part, OS 3.2's window classes, the listing) takes 458.8 KB
more (a Shell tab alone takes 92.4 KB), and all of it comes back when
the window closes. What a CTerm run does not give back: 0.3b13 19,952 -
23,872 bytes (ARexx's part), 0.3b14 6,160 - 6,360 (left to find).

**The rule for new features:** measure Avail with CTerm idle (and with
the feature in use) on the fast config before and after; anything not
needed every second (a window, a protocol, a command set, a tool) goes
into a part (`Parts/`, part.h) or a protocol file, loaded when used and
gone when done. Never retry a failed allocation on every write, line or
packet: ask once, fall back, and keep a reserve before any growing
buffer takes the last memory.

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
