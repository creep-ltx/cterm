# CTerm

A terminal for AmigaOS 3.2 and up, in C: the Amiga Shell in a window of its own,
and a terminal for other computers (telnet, ssh, a serial line or a modem,
BBSes, programs on a PC), in one program. VT100/xterm emulation, Amiga and TrueType fonts, 2 to 16
million colours, Latin-1, UTF-8 and the PC's character set.

## Starting it

`CTerm` on its own opens the Amiga Shell in a CTerm window.

| Option | What it does |
|---|---|
| `HOST name[:port]` | call a telnet host (port 23 if none is given) |
| `SSH user@host[:port]` | call an ssh host (port 22) |
| `KEY file` | with SSH: log in with this private key |
| `PC` | a PC BBS: the PC character set (CP437), the PC's colours and its own font (IBM VGA 8x16, from `Fonts/` beside CTerm) |
| `LATIN1` | Latin-1 instead of UTF-8 (Amiga BBSes) |
| `COLS n` / `ROWS n` | the window's size in letters |
| `FROM script` | the Shell runs this script first |
| `BOOK name` | call a phone book entry |
| `SERIAL` | a serial terminal on the Settings' line (Modem tab) |
| `DEVICE name` / `UNIT n` / `BAUD n` | with SERIAL: another device, unit or speed |
| `LISTEN` / `PORT n` | wait for the PC's relay (pc/cterm-relay.py) |

LISTEN lets in only the PC named in `ENVARC:CTerm/relay_from`. The first
time, CTerm asks whether to trust the PC that calls and remembers it;
delete that file to choose another PC.

Project/Call... (Right Amiga+K) asks for a host and remembers the last
eight; the phone book's places are listed first. A call opens in a new
tab (or a window of its own); when it ends, Return calls again.

## Serial lines and modems

CTerm calls over a serial line as well as over the network: a null-modem
cable to another computer, or a modem dialling a BBS. Any device that
works as serial.device does (a multi-serial card's own too), any unit.

- **Serial terminal** (Project/Serial terminal, the Call window's Serial
  terminal button, or `CTerm SERIAL`): the line opens in a tab and what
  you type goes to it. With a modem on it you type the AT commands
  yourself, as with the terminals of old; CTerm notices CONNECT (the call
  is up) and NO CARRIER (it ended). The line is set on the Settings'
  Modem tab (Line).
- **A phone book place on a serial line** (Connection: serial): its
  Phone is the number a modem dials; with no number the line itself is
  the connection (a null modem, there at once). Line... sets the device,
  unit, speed, data bits, parity, stop bits, flow control, the device's
  buffer, and whether the speed is locked, and the place's own init
  string and dial prefix.
- **Dialling:** the modem's init strings (Settings, Modem: two, then the
  place's own), each waiting for OK, then the dial prefix (Tone ATDT,
  Pulse ATDP or your own), the number and the suffix (^M is Return). The
  modem's own words show in the tab, CTerm's in dim brackets. CONNECT
  means connected; BUSY and NO ANSWER call again if Settings says so (how
  many times, the pause between; Esc stops); NO CARRIER, NO DIALTONE and
  ERROR end the call. Numeric results (ATV0) are understood too. Esc
  while dialling stops the modem and hangs up.
- **Speeds:** every usual one from 300 to 460800 baud, or any other the
  device takes. Without flow control a fast line can outrun the Amiga;
  when bytes are lost CTerm says so (Zmodem and Hydra send them again). "Baud locked" (on
  by default) keeps the line's speed; off, the line follows the speed in
  "CONNECT 2400".
- **Flow control:** RTS/CTS (the hardware's, the default, right for a
  modem), XON/XOFF, or none (a three-wire null-modem cable). If the line
  holds back what is sent (no CTS: the modem off, the cable out), CTerm
  says so instead of waiting for ever.
- **Hanging up:** closing the tab hangs up: "+++" with a second's quiet
  each side, then ATH0 (or your own command), then the line closes,
  which drops DTR (a modem set to AT&D2 hangs up on that alone; Settings
  can say DTR only). When the far end hangs up (NO CARRIER, or the
  carrier detect line going away) a phone book call ends: Return calls
  again.
- **Answering:** Project/Answer call (ATA) picks up; with "Answer when it
  rings" on, CTerm answers a RING by itself.
- File transfers (Zmodem, Ymodem, Xmodem, Hydra) work over a serial
  line as over telnet, with Zmodem and Hydra starting by themselves.

## The tour

`CTermTour` (built by `make tour`), run in a CTerm window, shows what CTerm
does, page by page: styles, colours, lines and blocks, characters, cursor
shapes, a Boing ball in 24-bit colour, and a race against the Amiga's own
CON: window (R on its page). Space: next page, B: back, Esc: the end;
`SECS n` turns the pages by themselves. On the A1200 (PiStorm, 32-bit
Workbench): 1500 coloured lines, a line a write, CON: 9.90 s, CTerm 0.14 s.

Programs can switch the window to UTF-8 and back (ESC % G, ESC % @), and
in the Amiga Shell `CSI 1..6 SP q` sets the cursor's shape as on xterm
(0 is still the Amiga's window bounds request).

## The phone book

Project/Phone book... (Right Amiga+B) opens the phone book, a window of
its own: the places on the left (New, Copy, Delete), the one chosen on
the right. What you type counts at once and is saved when the window
closes. Call (or a double-click on a place) calls it in a new tab and
closes the book. Each place has

- a name, the address (a host name, an IP number, or a `telnet://` or
  `ssh://user@host:port` link, which is taken apart) and the port;
- telnet, ssh or serial (a serial line: the phone number and the line's
  settings, see Serial lines and modems);
- your user name and password. At the host's prompt, Project/Send user
  name (Right Amiga+U) and Send password (Right Amiga+P) type them with
  Return; the keys can be changed on the Keys tab. An ssh login is given
  the saved password by itself, once;
- the letters (CTerm's setting, an Amiga font, TrueType, or the PC's VGA
  font) and the characters (UTF-8, Latin-1, PC BBS);
- its own download and upload drawers (empty: the Files tab's).

A place opens in a new tab dressed the way the entry says (the Call
window lists the places too) (ssh in a CTermSSH window). `CTerm BOOK "name"`
calls one from the Shell. The book is kept in `ENVARC:CTerm/phonebook`
as plain text, passwords too, as the BBS dialers of old did.

## For programs on other computers

In a telnet, ssh or relay tab CTerm behaves like a modern xterm:

- Alt is Meta (Alt+B, Alt+F in bash; emacs), and Shift/Alt/Ctrl with
  the arrows and Del send xterm's codes. The local Shell keeps the
  Amiga's Alt characters. (Settings: Alt key.)
- Programs can use the mouse (vim, htop, mc, Claude Code): clicks, the
  wheel and movement go to them. Shift+drag still marks text, and the
  right button is still the menu button.
- Flicker-free screens (programs that draw a whole frame at once),
  focus reports, and the cursor shape a program asks for (block,
  underline, bar).
- When the window gets wider or narrower, the text (and the
  scrollback) flows again to the new width.

## Tabs

Several terminals in one window. Project/New Shell tab (Right Amiga+T)
opens another Amiga Shell; Project/Call... opens a call in a new tab (or,
unticked, a window of its own). A row of tab names appears above the
text: click one, or press Right Amiga+1 to 9. Project/Close tab (Right
Amiga+W) ends the shown tab's Shell or hangs up its host. Tabs you are
not looking at keep running.

## ssh

- ssh is in a second program, **CTermSSH**, kept beside CTerm: CTerm
  starts it when ssh is asked for, so a small Amiga never loads it.
- Needs AmiSSL 5 (amisslmaster.library and LIBS:AmiSSL/) and fast memory.
- A key file is used if there is one: `ENVARC:CTerm/id_ed25519`,
  `id_ecdsa` or `id_rsa` (made with ssh-keygen, no passphrase; put the
  `.pub` in the host's `~/.ssh/authorized_keys`). Otherwise CTerm asks
  for the password in the window.
- A host met for the first time shows its key's fingerprint and asks;
  the answer is kept in `ENVARC:CTerm/known_hosts`. If a host's key ever
  changes, CTerm warns in red and does not connect unless told to.

## Copy and paste

Mark text with the left mouse button: drag, double-click a word (a file
name or a path is one word) or triple-click a whole line. Right Amiga+C
copies it, Right Amiga+V types the clipboard in.

Programs on the other computer can copy too: what you copy in vim, tmux
and the like (the OSC 52 code) lands on the Amiga's clipboard. They
cannot read it.

## File transfers

- **Zmodem starts by itself.** When the host begins one (a BBS
  download, or `sz file` in a shell), CTerm receives the files into the
  phone book entry's download drawer, else the Files tab's, else
  `PROGDIR:Downloads`; a file never goes over one that is there (a
  number is added). When the host waits for files (a BBS upload, or `rz`),
  a requester asks which; several can be chosen at once.
- **Hydra starts by itself** too (the host's START packet, as
  HydraCom's terminal did), and goes both ways at once: what the host sends
  comes in while files you queued with Transfer/Send with Hydra... go out.
  CTerm runs HydraCom's own code: the HYDRA file transfer protocol was
  designed by Arjen G. Lentz and Joaquim H. Homrighausen, and HydraCom
  written by Arjen G. Lentz, LENTZ SOFTWARE-DEVELOPMENT; Hydra and HydraCom
  are free (see `src/hydra.c` and HydraCom's `license.doc`).
- **Ymodem and Xmodem** are started by you, after starting the other side
  on the host: Transfer/Receive with Ymodem (names come with the files),
  Receive with Xmodem... (asks the name), Send with Ymodem... and Send
  with Xmodem... Xmodem pads a file's end to a whole block, as it must.
- **CTerm goes on while files move**: switch tabs, use a Shell, call
  another board. A panel over the transferring tab's last rows shows the
  file and its drawer, a bar for the file and one for the whole batch,
  the speed and the time left, and a Stop button (Esc stops too); the
  tab's name shows the per cent (v down, ^ up). Transfer/Show
  transfers... opens a window listing every transfer with its bars and a
  Stop for each; it never opens by itself.
- The protocols are files of their own in `Protocols/` beside CTerm
  (Zmodem 10 KB, XYmodem 6 KB, Hydra 31 KB): loaded when a transfer starts,
  gone when it ends, so what is not used takes no memory. Keep the drawer
  with CTerm.
- Tested against lrzsz, the reference (`make ztest`): every file the same,
  also over a line that loses and garbles bytes (Zmodem).

## Logging

Project/Start log... (Right Amiga+L) saves everything that comes into
the tab to a file, added to the end if the file is there; the title says
"(logging)" until Project/Stop log or the tab closes. The bytes are kept
as they came, colour codes and all, so a BBS screen plays back from it.

## Settings

Settings/Settings... (Right Amiga+S) opens one window for everything,
in six tabs (it is built with OS 3.2's own window classes, so it looks
like the system's settings programs):

- **Screen:** a Workbench window or CTerm's own screen (any screen mode
  and number of colours), full screen, scroll bar. In full screen a thin
  bar on the right edge shows where you are in the history; pull it with
  the mouse to scroll back.
- **Text:** an Amiga font or TrueType, the TrueType size, line spacing,
  the characters (UTF-8, Latin-1, PC BBS), and whether PC BBS mode uses
  the PC's own font.
- **Colours:** a theme (Nord, Black, Workbench, Amber, Green, Light), and
  your own text and background colour on top of it.
- **Keys:** what the Alt key does (Meta for programs on other
  computers, or the Amiga's own Alt characters), and the letters for
  sending the phone book's user name and password.
- **Files:** where downloads go and where the upload requester opens.
- **Modem:** the modem every dial-up place uses (init strings, dialling,
  hanging up, how long a call may take, redialling, answering) and the
  serial terminal's line.
- **Picture** (on the Colours tab): a picture behind the text (any format the Amiga's
  datatypes read: PNG, JPEG, IFF...), and how much of it shows through
  the background, 0 to 100 %. It covers the window without stretching,
  stays still while the text scrolls over it, and costs no speed. It
  needs a screen of 16 bits of colour or more (a graphics card).

Letters and colours change at once, so you see them; the screen changes
on Use or Save. Save keeps them (ENVARC:CTerm.prefs), Use until the next
reboot, Cancel puts everything back. The window's place and size are
kept as well. The Settings menu still has the quick items (fonts, text
size, screen, full screen, scroll bar).

## Building

`make` (bebbo's m68k-amigaos-gcc in ~/opt/amiga) builds CTerm and CTermSSH. The first build
downloads the AmiSSL SDK and libssh2 (third_party/fetch.sh) and builds
libssh2. `make test` checks the emulator core on Linux.
`tests/serial/` tests serial calls in FS-UAE (its serial port on a TCP
socket) against a pretend Hayes modem on the PC: see `fakemodem.py`.
