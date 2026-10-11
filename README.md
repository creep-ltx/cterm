# CTerm

A terminal for AmigaOS 3.2 and up, in C: the Amiga Shell in a window of its own,
and a terminal for other computers (telnet, ssh, a serial line or a modem,
BBSes, programs on a PC), in one program, with a file browser for SFTP and
FTP. VT100/xterm emulation, Amiga and TrueType fonts, 2 to 16
million colours, Latin-1, UTF-8 and the PC's character set.

## What it needs

- **AmigaOS 3.2** or later.
- **Extra memory (fast RAM)**: an accelerator card or a memory card. On
  an Amiga with only its 2 MB of chip memory CTerm says "CTerm needs an
  Amiga with extra memory (fast RAM)" and does not start.
- For ssh also AmiSSL 5 (see ssh below).
- **The files beside CTerm**, as they come in its drawer:

| File or drawer | What is in it | Without it |
|---|---|---|
| `CTermSSH` | ssh and SFTP | ssh and SFTP calls do not start |
| `Protocols/` | Zmodem, XYmodem, Hydra; Serial (serial lines and modems) | that transfer, or serial lines, say they are missing |
| `Parts/` | Settings, PhoneBook, ARexx, Call, Transfers, SpeedTest, Files (the SFTP and FTP browser) | that window or feature says it is missing |
| `Fonts/` | the PC's VGA font | PC BBSes use the Amiga font |
| `Rexx/` | example ARexx scripts | |

Everything in `Protocols/` and `Parts/` is loaded only when it is used
and leaves memory when it is done (ARexx's part stays once a script has
used it), so a feature never used costs no memory.

## Starting it

`CTerm` on its own opens the Amiga Shell in a CTerm window.

| Option | What it does |
|---|---|
| `HOST name[:port]` | call a telnet host (port 23 if none is given) |
| `SSH user@host[:port]` | call an ssh host (port 22) |
| `KEY file` | with SSH: log in with this private key |
| `SFTP user@host[:port][/drawer]` | open the file browser on an ssh host (SFTP) |
| `FTP [user[:password]@]host[:port][/drawer]` | open the file browser on an FTP host (port 21; no user: anonymous) |
| `PC` | a PC BBS: the PC character set (CP437), the PC's colours and its own font (IBM VGA 8x16, from `Fonts/` beside CTerm) |
| `LATIN1` | Latin-1 instead of UTF-8 (Amiga BBSes) |
| `COLS n` / `ROWS n` | the window's size in letters |
| `FROM script` | the Shell runs this script first |
| `BOOK name` | call a phone book entry |
| `SERIAL` | a serial terminal on the Settings' line (Modem tab) |
| `DEVICE name` / `UNIT n` / `BAUD n` | with SERIAL: another device, unit or speed |
| `LISTEN` / `PORT n` | wait for the PC's relay (pc/cterm-relay.py) |
| `SHOW` | bring back a CTerm that is already running (hidden or not) instead of opening another |

LISTEN lets in only the PC named in `ENVARC:CTerm/relay_from`. The first
time, CTerm asks whether to trust the PC that calls and remembers it;
delete that file to choose another PC.

Project/Call... (Right Amiga+K) asks for a host and remembers the last
eight; the phone book's places are listed first. A call opens in a new
tab (or a window of its own); when it ends, Return calls again.

## Files on other computers: SFTP and FTP

CTerm has a file browser for copying files to and from other computers:
SFTP (files over ssh, as safe as ssh) and FTP (the old way: the password
goes over the network as it is - use it only where that does not matter,
or on your own network).

- **Opening it:** a phone book place of the kind `sftp` or `ftp`
  (Call, a double-click, `CTerm BOOK "name"`), `ftp://` or `sftp://` typed
  in the Call window (`ftp://user:password@host/drawer`), `CTerm FTP ...`
  or `CTerm SFTP ...` from the Shell, or in an ssh tab Project/Open SFTP
  browser (the same host and login, a connection of its own). SFTP is
  CTermSSH's: CTerm starts it for an SFTP place, as it does for ssh.
- **Its tab:** each place gets a small tab of its own where its messages
  go: the host's greeting, a host key question, a password asked for,
  each file sent or received. The browser window and the tab are one
  thing: closing the window hangs up and closes the tab, closing the tab
  closes the window. Return in the tab opens the window again.
- **The window** lists the drawer on the other computer: drawers first,
  then files, with their sizes and dates. A double-click enters a
  drawer (`..` goes up) or downloads a file. Parent goes up, the path
  field takes a drawer typed in (Return), Refresh lists it again. Shift
  and a click choose several.
- **Download** fetches the chosen files into the place's download
  drawer, else the Files tab's, else `PROGDIR:Downloads` (the status line
  says which). A file that is there already is never written over: a
  number is added (`name.1`). A name the Amiga cannot take (with `:` or
  `/` in it) gets `_` instead.
- **Upload...** sends files chosen in a requester (several at once) into
  the drawer shown; a file of that name there is replaced only if you
  say so.
- **New drawer...**, **Rename...** and **Delete** (it asks first; a drawer
  must be empty) work on the other computer.
- **CTerm goes on while files move:** the other tabs, the Shell, a second
  browser. The browser shows how far it is (bytes, per cent, speed), and
  the tab's panel and Transfer/Show transfers... show it as for Zmodem.
  Stop (in the browser, the panel or the Transfers window, or Esc in the
  tab) stops it. A stopped download keeps `name.part`; downloading the
  same file again goes on from where it stopped. One transfer at a time
  in all of CTerm.
- **Logins:** SFTP checks the host's key and logs in as ssh does (a key
  file in `ENVARC:CTerm/`, else the phone book's password, else it asks
  in the tab). FTP uses the phone book's user and password, asks in the
  tab for a password it has not got, or logs in as anonymous with no
  user. An FTP host that hung up on a quiet line is called again by
  itself at the next click.
- **The phone book** has the kinds `sftp (files)` and `ftp (files)`;
  their More... button (where Line... is for a serial line) sets the
  drawer on the other computer the browser starts in.
- The browser is a file of its own, `Parts/Files` (32 KB): loaded when a
  place is called, gone when the last one hangs up.
- Dates: FTP hosts that list the old way show their own clock; SFTP and
  the newer FTP listing (MLSD) give UTC, which is shown as it is.
- Not there yet: FTPS (FTP with TLS), active FTP (passive only, which is
  what works through home routers), downloading whole drawers, a list
  of the Amiga's side in the same window.

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
- The serial code is a file of its own, `Protocols/Serial` beside
  CTerm (10 KB), as the transfer protocols are: it is loaded when a
  serial tab opens its line and leaves memory when the last one closes,
  so it costs nothing until you use it. Without the file a serial tab
  says "serial support missing" and nothing else happens.

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

- a name, the address (a host name, an IP number, or a `telnet://`,
  `ssh://user@host:port`, `sftp://` or `ftp://` link, which is taken
  apart) and the port;
- telnet, ssh or serial (a serial line: the phone number and the line's
  settings, see Serial lines and modems), or sftp and ftp, which open
  the file browser (see Files on other computers);
- your user name and password. At the host's prompt, Project/Send user
  name (Right Amiga+U) and Send password (Right Amiga+P) type them with
  Return; the keys can be changed on the Keys tab. An ssh login is given
  the saved password by itself, once;
- the letters (CTerm's setting, an Amiga font, TrueType, or the PC's VGA
  font) and the characters (UTF-8, Latin-1, PC BBS);
- its own download and upload drawers (empty: the Files tab's);
- a script: an ARexx script run when the call is up, to log in by
  itself (see ARexx below).

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

## Panes: several terminals in sight at once

The Panes menu splits the window, as tmux and iTerm do: Split
left/right puts a new Amiga Shell beside the pane you are in, Split
top/bottom puts one below it. Each half can be split again, up to four
panes.

- **Every pane shows a tab.** The tab bar still lists them all: the one
  you type in is the brightest, the others in panes a little less.
  Choosing a tab (a click in the tab bar, Right Amiga+1 to 9) shows it
  in the pane you are in; a tab that is in another pane already makes
  that pane the one you are in.
- **Click in a pane** to type there. It gets a thin frame and the solid
  cursor; the other panes show their cursor as an outline. Panes/Next
  pane goes round them.
- **Pull the line between two panes** with the mouse to give one more
  room (each keeps at least 10 letters by 3 lines).
- **Each pane has its own size:** the Shell or the host in it is told
  its pane's size (as when the window is resized), and the text flows
  to it again. Each keeps its own history; the scroll bar follows the
  pane you are in.
- **Panes/Close pane** closes the pane, not its tab: the tab goes on in
  the tab bar. When a tab ends, its pane shows another one, or closes
  if there is none left to show.
- While the window is split, every pane wears the window's own letters
  (Settings): they must be one size to share the window. A tab's own
  (the PC's font for a PC BBS, a phone book place's) come back when it
  is alone in the window again.
- File transfers show their panel in their own pane, and go on whichever
  pane you are in.

## Hiding the window

Project/Hide window (or the window's iconify gadget, the small one left
of the two depth gadgets) puts CTerm away: its window closes (and its
own screen, if it has one), but everything in it goes on - the Shells
and what runs in them, calls, file transfers, logs and ARexx scripts.
A CTerm icon appears on Workbench while it is hidden. To bring it back:

- double-click the icon;
- start CTerm again, from Workbench or as `CTerm` in a Shell with
  nothing after it: the hidden CTerm comes back and no second one
  opens. `CTerm SHOW` also brings a CTerm that is not hidden to the
  front;
- Exchange (SYS:Tools/Commodities): CTerm, Show Interface. Hide
  Interface hides it, Remove quits it;
- ARexx: `WINDOW SHOW`.

It comes back as it was: the same screen, place and size, every tab,
how far each is scrolled back, the settings. Nothing is drawn while it
is hidden and bells keep quiet; what came meanwhile is there to read.
While a program has the window to itself (Ed, More) it cannot hide
(you hear a beep).

A start with words (`HOST`, `BOOK`, `SERIAL`, `FROM`, `COLS`...) still
opens a CTerm of its own, as always: it asks for something new, and a
hidden CTerm is left as it is. To call from a hidden one, use ARexx's
`CALL`.

## ssh

- ssh is in a second program, **CTermSSH**, kept beside CTerm: CTerm
  starts it when ssh is asked for, so a small Amiga never loads it.
- Needs AmiSSL 5 (amisslmaster.library and LIBS:AmiSSL/).
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
  (Zmodem 10 KB, XYmodem 6 KB, Hydra 31 KB; the serial line's code,
  Serial, is there too): loaded when a transfer starts,
  gone when it ends, so what is not used takes no memory. Keep the drawer
  with CTerm.
- Tested against lrzsz, the reference (`make ztest`): every file the same,
  also over a line that loses and garbles bytes (Zmodem).

## Logging

Project/Start log... (Right Amiga+L) saves everything that comes into
the tab to a file, added to the end if the file is there; the title says
"(logging)" until Project/Stop log or the tab closes. The bytes are kept
as they came, colour codes and all, so a BBS screen plays back from it.

## ARexx: scripts drive CTerm

CTerm has an ARexx port, so a script (or another program) can call a
board, wait for its questions, type the answers, fetch files and read
the screen. The first CTerm's port is `CTERM`, a second one's
`CTERM.2`, and so on (About in the Project menu says which).
RexxMast must be running (it is on a standard Workbench).

- **Project/Run ARexx script...** runs a script from `REXX:`.
- **A phone book place's Script** (the phone book window, Script) runs
  when the call to that place is up: the automatic BBS login. The
  script is given the port's name, and its commands go to CTerm and to
  that place's tab. `rexx/bbs-login.rexx` is one to start from.
- From the Shell: `rx myscript.rexx`, with `ADDRESS CTERM` in it.

The commands (any case; `OPTIONS RESULTS` in the script to get answers
in RESULT; RC is 0 when all went well, 5 for a warning such as a time
limit, 10 for an error, 20 for a failure):

| Command | What it does |
|---|---|
| `CALL name` | calls a phone book place, `host:port`, or `SERIAL`, in a new tab; RC 0 when it is up. `NOSCRIPT`: without the place's script |
| `HANGUP` | hangs up the tab's call (the tab stays) |
| `SEND "text"` | types the text to the host, or to the Shell: `\r` is Return, `\n` a new line, `\e` Escape, `\t` Tab, `\xHH` any byte. `SEND RAW "text"`: as it is |
| `WAITFOR "text" ["other"...] [TIMEOUT secs]` | waits until the host sends one of the texts (any case); RESULT is the one that came, RC 5 when the time ran out, 10 when the call ended. It sees what came since the last SEND, so a question that came a moment early still counts |
| `GETLINE [row]` | the text of a row of the screen (the cursor's row if none) |
| `GETSCREEN` | the whole screen, a line each row |
| `CURSOR` | where the cursor is: "row column" |
| `TAB NEW`, `TAB SELECT n`, `TAB CLOSE [n]`, `TAB COUNT`, `TAB CURRENT`, `TAB NAME [n]` | the tabs |
| `DOWNLOAD [file] [PROTOCOL zmodem/ymodem/xmodem/hydra] [TIMEOUT secs]` | Zmodem and Hydra: waits for the host's transfer (start it with SEND first) and answers when it has ended; Ymodem and Xmodem start receiving now (Xmodem saves as `file`). RESULT: the files |
| `UPLOAD file... [PROTOCOL ...]` | sends the files (Hydra: with the host's next Hydra session) |
| `LOG file` / `LOG OFF` | logs the tab to a file, or stops |
| `SCREEN TOFRONT` / `TOBACK` | CTerm's screen to the front or back |
| `WINDOW TOFRONT` / `ACTIVATE` / `TOBACK` | the window |
| `WINDOW HIDE` / `SHOW` | hides the window (everything goes on) and brings it back; RC 10 when it cannot hide (Ed has the window) |
| `WINDOW STATE` | RESULT is `HIDDEN` or `SHOWN` |
| `PANE SIDE` / `UNDER` | splits the pane you are in: a new Shell beside it or below it (RESULT: how many panes) |
| `PANE NEXT`, `PANE SELECT n`, `PANE CLOSE`, `PANE COUNT`, `PANE CURRENT` | the panes; the commands that follow go to the tab of the pane chosen |
| `PHONEBOOK` | the places' names, each in quotes |
| `SFTP place`, `FTP place` | opens the file browser: a phone book place, or `[user@]host[:port][/drawer]` (or a URL), in a new tab; RC 0 when it is connected. The commands below go to that tab |
| `LIST` | the files in the browser's drawer, each in quotes, drawers with a `/` at the end |
| `CD drawer` | goes to that drawer (`..` up); RESULT: where it is now |
| `GET name... [TO drawer]` | downloads files from the drawer (TO: into that drawer on the Amiga); RC 5 when some failed |
| `PUT file...` | uploads Amiga files into the drawer (a file of that name there is replaced) |
| `MKDIR name`, `DELETE name`, `RENAME name TO newname` | on the other computer (DELETE: a file, or an empty drawer) |
| `VERSION`, `HELP [command]`, `QUIT` | |

Commands go to the tab the script last called, opened or chose
(`TAB SELECT`), else to the tab you are looking at: a login script
keeps working in its own tab even when you switch to another.

In a script, put each command in quotes as a whole, with the text in
double quotes inside: `'SEND "guest\r"'`. ARexx turns words outside
quotes into capitals.

`rexx/` (the `Rexx` drawer beside CTerm) has examples: `bbs-login.rexx`
(a board's login, for the phone book's Script) and `info.rexx` (what
CTerm says about itself).

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

`make` (bebbo's m68k-amigaos-gcc in ~/opt/amiga) builds CTerm, CTermSSH,
`Protocols/` and `Parts/` (`make parts`: the parts alone). The first build
downloads the AmiSSL SDK and libssh2 (third_party/fetch.sh) and builds
libssh2. `make test` checks the emulator core on Linux.
`tests/serial/` tests serial calls in FS-UAE (its serial port on a TCP
socket) against a pretend Hayes modem on the PC: see `fakemodem.py`.
