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
| Serial lines: the settings, the tabs (CTerm's side) | `src/serial.c`, `src/serial.h` | Yes |
| The line's code, loaded on demand (`Protocols/Serial`): the device, dialling, the modem's words | `src/sermod.c`, `src/sermod.h` | Yes |
| A serial line's settings window (phone book, Settings) | `src/serwin.c` | Yes |
| The ARexx port: commands, WAITFOR, starting scripts | `src/rexx.c` | Yes |
| Hiding the window: the AppIcon, the commodity broker | `src/hide.c` | Yes |
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

- Later idea: Hydra's chat with the other side during a transfer
  (HydraCom's CON device channel; AmiExpress has it) - niche, parked.
- Also: an ssh password login tried against a real server,
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
