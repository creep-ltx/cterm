# CTerm

A terminal for AmigaOS 3.2 and up, in C: the Amiga Shell in a window of its own,
and a terminal for other computers (telnet, ssh, BBSes, programs on a PC),
in one program. VT100/xterm emulation, Amiga and TrueType fonts, 2 to 16
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
| `LISTEN` / `PORT n` | wait for the PC's relay (pc/cterm-relay.py) |

Project/Call... (Right Amiga+K) asks for a host and remembers the last
eight; the phone book's places are listed first. A call opens in a new
tab (or a window of its own); when it ends, Return calls again.

## The phone book

Settings, Phone book tab: the places you call, each with

- a name, the address (a host name, an IP number, or a `telnet://` or
  `ssh://user@host:port` link, which is taken apart) and the port;
- telnet or ssh;
- your user name and password. At the host's prompt, Project/Send user
  name (Right Amiga+U) and Send password (Right Amiga+P) type them with
  Return; the keys can be changed on the Keys tab. An ssh login is given
  the saved password by itself, once;
- the letters (CTerm's setting, an Amiga font, TrueType, or the PC's VGA
  font) and the characters (UTF-8, Latin-1, PC BBS);
- its own download and upload drawers (empty: the Files tab's).

Choose a place in the Call window and Call: it opens in a new tab dressed
the way the entry says (ssh in a CTermSSH window). `CTerm BOOK "name"`
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
- **Ymodem and Xmodem** are started by you, after starting the other side
  on the host: Transfer/Receive with Ymodem (names come with the files),
  Receive with Xmodem... (asks the name), Send with Ymodem... and Send
  with Xmodem... Xmodem pads a file's end to a whole block, as it must.
- The window title shows the file, how far it is and the speed; **Esc
  stops** a transfer. The other tabs' Shells go on meanwhile.
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
- **Phone book:** see above.
- **Files:** where downloads go and where the upload requester opens.
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
