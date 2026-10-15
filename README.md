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
| `Parts/` | Settings, PhoneBook, ARexx, Call, Transfers, SpeedTest, Files (the SFTP and FTP file browser), Pictures (pictures in the terminal) | that window or feature says it is missing (Pictures: PNG files and pictures on screens of 256 colours or fewer do not show) |
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

CTerm copies files to and from other computers with SFTP (files over
ssh, as safe as ssh) and FTP (the old way: the password goes over the
network as it is - use it only where that does not matter, or on your
own network). It stays in the terminal, as ssh and telnet do: the files
show in the tab itself, in two panels, the way Midnight Commander shows
them on Linux.

- **Opening it:** a phone book place of the kind `sftp` or `ftp`
  (Call, a double-click, `CTerm BOOK "name"`), `ftp://` or `sftp://` typed
  in the Call window (`ftp://user:password@host/drawer`), `CTerm FTP ...`
  or `CTerm SFTP ...` from the Shell, or in an ssh tab Project/Open SFTP
  browser (the same host and login, a tab and a connection of its own).
  SFTP is CTermSSH's: CTerm starts it for an SFTP place, as it does for
  ssh.
- **Calling:** the tab shows the call as for ssh: the host's greeting, a
  host key question, a password asked for. Once connected the panels
  come up in the tab.
- **The panels:** on the left the Amiga (it starts in the place's
  download drawer, else the Files tab's, else `PROGDIR:Downloads`), on
  the right the other computer. Each lists its drawer: `..` (the drawer
  above) first, then the drawers, then the files, with their sizes and
  dates; the drawer's path is in the panel's top line, what it holds (or
  what is marked) in its bottom line. The panel you are in has the
  double frame and the bar. Above the Amiga's volumes, `..` lists the
  volumes. Under the panels is the message line (what happened last,
  how a transfer goes), and at the bottom the function keys.
- **The keys** (as Midnight Commander has them):

| Key | What it does |
|---|---|
| Up, Down | the bar up and down |
| Shift+Up, Shift+Down (PageUp, PageDown) | a page up, a page down |
| Ctrl+Up, Ctrl+Down (Home, End) | the first line, the last |
| Tab | the other panel (Left and Right: the left one, the right one) |
| Return | into the drawer under the bar (on a file: its size and date) |
| Backspace | the drawer above |
| Space (Insert) | mark the file under the bar (or unmark it), then down |
| `*` | turn the marks of all the files over |
| letters | find the first name that starts so, as you type it |
| F1 (Help) | every key, and the commands |
| F2 (Ctrl+R) | read both drawers again |
| F5 | copy the marked files - or the one under the bar - to the other side |
| F6 | rename (a new name is asked on the message line) |
| F7 | a new drawer (its name is asked) |
| F8 (Del) | delete the marked, or the one under the bar (asks y/n first; a drawer must be empty) |
| F9 | the messages: the tab's own text (the greeting, each file copied); any key goes back |
| F10 | hang up and close the tab (asks y/n) |
| Esc | stop a transfer or a listing; forget a name being looked for |
| `:` | the command line |

- **The mouse:** a click chooses a line (and the panel), a double-click
  goes into a drawer, the wheel scrolls the panel under the pointer, a
  click on a function key's name does what it says. Shift and the mouse
  still mark text.
- **Every key can be changed**, in Settings (the Keys tab) or in
  CTerm's one key file, `ENVARC:CTerm/CTerm.keys`: the sections
  `[files]` (the panels) and `[filesline]` (a line typed: a name, the
  command line). See Keys below. F1 lists them as they are. Only yes and
  no are always `y` and `n`.
- **The command line** (`:`) takes the commands of Linux's ftp and sftp,
  on the message line; names with a space go in "quotes", `#?` and `*`
  match any letters, `?` one, and Tab completes a name or a command:

| Command | What it does |
|---|---|
| `ls [pattern]`, `lls [pattern]` | the other computer's drawer, the Amiga's, as a list over the panels (any key goes back) |
| `cd drawer`, `lcd drawer` | into a drawer there, on the Amiga (`cd ..`, `lcd Work:`, `lcd ..`) |
| `pwd`, `lpwd` | where each side is |
| `get name...`, `mget pattern...` | copy files from there into the Amiga's drawer |
| `put name...`, `mput pattern...` | copy files from the Amiga's drawer (or `Work:path`) there |
| `mkdir name` | a new drawer there |
| `rm name...`, `rmdir name` | delete there (asks y/n first) |
| `rename old new` | rename there |
| `bye` | hang up and close the tab |
| `help` | the keys and these commands |

- **Copying:** a file that is on the Amiga already is never written
  over: a number is added (`name.1`). A name the Amiga cannot take
  (with `:` or `/` in it) gets `_` instead. Sending files that are on
  the other computer already asks first: `y` replaces them, `n` leaves
  them out. Drawers are not copied (mark their files).
- **CTerm goes on while files move:** the other tabs, the Shell, other
  panes, a second place. The message line shows the file, how much of
  it, per cent, the speed and how many files are left; the tab's name
  shows the per cent, and Transfer/Show transfers... lists it as it
  does Zmodem. Esc (or Stop there) stops it. A stopped download keeps
  `name.part`; copying the same file again goes on from where it
  stopped. One transfer at a time in all of CTerm.
- **Logins:** SFTP checks the host's key and logs in as ssh does (a key
  file in `ENVARC:CTerm/`, else the phone book's password, else it asks
  in the tab). FTP uses the phone book's user and password, asks in the
  tab for a password it has not got, or logs in as anonymous with no
  user. An FTP host that hung up on a quiet line is called again by
  itself at the next command.
- **The phone book** has the kinds `sftp (files)` and `ftp (files)`;
  their More... button (where Line... is for a serial line) sets the
  drawer on the other computer the browser starts in.
- **ARexx** (LIST, CD, GET, PUT, MKDIR, DELETE, RENAME, below) works on
  the tab's panels; HANGUP hangs up (Return calls again).
- The browser is a file of its own, `Parts/Files` (52 KB): loaded when a
  place is called, gone when the last one hangs up. It uses no windows
  of its own, so OS 3.2's window classes are not loaded for it.
- Dates: FTP hosts that list the old way show their own clock; SFTP and
  the newer FTP listing (MLSD) give UTC, which is shown as it is.
- Not there yet: FTPS (FTP with TLS), active FTP (passive only, which is
  what works through home routers), copying whole drawers.

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
  Return; the keys can be changed (see Keys). An ssh login is given
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

## Pictures in the terminal

Programs can show pictures inside the text, as modern terminals on
Linux do: a picture appears where the cursor is, takes up a block of
letters' places, and the text goes on below it.

- **Sixel**, the classic way: `img2sixel`, `chafa`, `lsix`, `gnuplot`
  (`set terminal sixelgd`), `mpv --vo=sixel`, `viu`, BBS art tools, and
  Amiga programs that write sixel to the Shell. A program asking whether
  the terminal can do it is told yes (the answer to "what are you"
  includes sixel), and is told the size of a letter and of the window
  in pixels, so it can size its picture to fit.
- **iTerm2's pictures** (a picture file sent whole: PNG, JPEG, GIF):
  `imgcat`, and the many tools that use it. CTerm opens the file with
  the Amiga's own datatypes, so any picture format your Amiga reads
  works. A width or height asked for (in letters, pixels or per cent of
  the window) is kept, in shape; a picture wider than the window is
  made smaller to fit.
- **kitty's pictures** are not shown, but they are swallowed cleanly:
  you never see their code as text.

How they behave:

- A picture scrolls with the text and goes into the history above, so
  you can scroll back to it. Text written over it, or a clear, removes
  that part. It survives everything that redraws the window: a tab
  switch, panes, hiding the window, another window passing over it.
- A picture wider than the window (or a pane) is cut at its right edge.
- **Memory:** each tab keeps its pictures in fast memory, up to 4 MB (a
  quarter of what is free on a smaller Amiga), and never takes the last
  512 KB. When a new picture does not fit, the oldest goes - also from
  the history: you then see an empty place where it was. Pictures that
  have left the screen and the history give their memory back at once.
- **On a graphics card** with thousands or millions of colours (15 bits
  and up) pictures show in their own colours. **With 256 colours or
  fewer** (an 8-bit card, AGA, a Workbench of 16 colours) CTerm picks a
  set of colours that fits beside the text's (216 on 256 colours, the 8
  pure colours on 16 or 32) and mixes them in a fine pattern
  (dithering). On a 16-colour Workbench whose colours are all taken,
  the nearest ones are used.
- In the **Amiga Shell** tab pictures work too: a program that writes
  sixel or iTerm2's code to the Shell shows its picture.

**The setting:** Settings, Text tab, "Pictures in the terminal (sixel,
iTerm2)", on at first. Off, the pictures' codes are swallowed, nothing
is shown, and CTerm does not tell programs it can show pictures.

**Trying it:** the `Pictures` drawer beside CTerm has test pictures.
In CTerm's Shell, first `CD` to CTerm's drawer, then:

    Type Pictures/bars.six          colour bars (sixel)
    Type Pictures/kickstart.six     the Kickstart hand (sixel)
    Type Pictures/gradient.six      a colour gradient (sixel)
    Pictures/ShowPic Pictures/kickstart.png
    Pictures/ShowPic Pictures/kickstart.png WIDTH 30

`ShowPic FILE [WIDTH w] [HEIGHT h]` shows any picture file the Amiga
can read, as `imgcat` does on Linux (w and h: letters, `300px`, or
`50%` of the window). On a Linux host, over ssh or telnet:
`img2sixel picture.png`, `chafa -f sixel picture.png`, or
`imgcat picture.png`.

PNG and JPEG files, and pictures on screens of 256 colours or fewer,
need `Parts/Pictures` (3.5 KB, loaded when the first picture needs it,
gone when no tab holds a picture any more).

## Tabs

Several terminals in one window. Project/New Shell tab (Right Amiga+T)
opens another Amiga Shell; Project/Call... opens a call in a new tab (or,
unticked, a window of its own). A row of tab names appears above the
text: click one, or press Right Amiga+1 to 9 (Ctrl+Tab: the next one,
Ctrl+Shift+Tab: the one before). Project/Close tab (Right Amiga+W) ends
the shown tab's Shell or hangs up its host. Tabs you are not looking at
keep running. Up to 16 tabs (Right Amiga+1 to 9 reach the first nine;
the tab bar and Ctrl+Tab all of them).

## Panes: several terminals in sight at once

The Panes menu splits the window, as tmux and iTerm do: Split
left/right puts a new Amiga Shell beside the pane you are in, Split
top/bottom puts one below it. Each half can be split again, as long as
every pane keeps at least 10 letters by 3 lines (up to 16 panes, as
many as there can be tabs). With the keyboard it goes as on i3 and
Sway: see the next section.

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

## Panes, the i3 way

**Tabs or panes - one pair of keys:** Left Amiga+Return opens a new Shell
and Left Amiga+Q closes it. Where it goes is the Panes menu's choice:
**New Shells in panes** (as on i3) or **New Shells in tabs**. The CTerm
menu's New Shell and Close Shell do the same; Right Amiga+T and W stay
plain tab keys.

If you use i3 or Sway on Linux, CTerm's panes work the same way, with
the **Left Amiga** key where i3 has its Mod key. Every one of these
keys can be changed (Settings, the Keys tab, or the `[panes]` part of
`ENVARC:CTerm/CTerm.keys`); the Panes menu has them all too.

| Key | What it does |
|---|---|
| Left Amiga+Return | a new Amiga Shell in a new pane, next to the pane you are in; you are then in the new one. With **Panes/New Shells in tabs** ticked: a new tab instead |
| Left Amiga+Q | closes the pane you are in and its tab (the Shell ends, a call hangs up). A file transfer running there: CTerm asks first. The panes beside it take the room. The last pane: the same as Close tab. With **New Shells in tabs**: closes the tab |
| Left Amiga+H | from now on this pane splits side by side (the new pane on the right) |
| Left Amiga+V | from now on this pane splits one above the other (the new pane below) |
| Left Amiga+D | dwindle on or off: new panes split by shape (below) |
| Left Amiga+arrows | to the pane on the left, right, above or below |
| Left Amiga+Shift+arrows | the pane changes places with the one on that side |
| Left Amiga+F | the pane fills the window; the others go on behind it. Again: back as it was |
| Left Amiga+1 to 9 | workspace 1 to 9 (below); an empty one opens with a new Shell |
| Left Amiga+Shift+1 to 9 | the tab you are in moves to that workspace (the keys follow your keymap: Shift+3 is £ on some) |
| Left Amiga+Ctrl+1 to 9 | tab 1 to 9 of this workspace comes into the pane you are in (the one there goes behind it) |
| Left Amiga+R | resize mode: the arrows move the edge of the pane you are in (Shift+arrows: a tenth of the room at a time, as i3's `10 ppt`); on the scratchpad the arrows size it and Shift+arrows move it; Return or Esc ends |
| Left Amiga+- | the scratchpad (below): shown, given the keys, hidden |
| Left Amiga+Shift+- | the tab you are in becomes the scratchpad |
| Left Amiga+T | all panes become tabs: the whole window shows the tab you were in, the others run on behind it, the tab bar lists them |
| Left Amiga+P | all tabs become panes, laid out by themselves (below) |

- **Workspaces** (as i3's): nine, each with its own panes and tabs. The
  bar at the top shows the ones in use (click one to go there); the tab
  bar and Right Amiga+1-9 count the shown workspace's tabs. Every tab
  keeps running whichever workspace is shown.
- **The scratchpad** (as i3's): one Shell that floats over the panes,
  shown and hidden with Left Amiga+-. Click a pane to work there while it
  stays on show; Left Amiga+- again gives it the keys, again hides it.
  Left Amiga+R sizes and moves it.
- **Saved layouts** (Panes/Save layout..., Open layout..., in
  `ENVARC:CTerm/Layouts`): every workspace's panes, where their dividers
  are, their tabs and the scratchpad, each tab as what starts it again - a
  Shell in its drawer with the program that ran in it, or a host. Panes/
  Open this layout at start: CTerm opens with it (its first Shell takes
  the first pane).
- **Gaps** (Settings, Screen tab, "Gap between panes", 0 to 32
  pixels; seen at once): every pane has a thin border, the one you are in
  lit; the gap is the room between two panes' borders, all of it, showing
  the window's background (or the picture behind the text). 0: the
  borders touch.
- **Outer gaps** (Settings, Screen tab, on unless unticked): the same gap
  between the panes and the window's edges, so 5 means 5 pixels all
  round and 5 between two panes. On a full screen the position bar sits
  in the right gap (narrower in a gap under 8). Untick both Outer gaps
  and Scroll bar for panes that reach every edge.
- **Each pane its own scroll bar** (Scroll bar on): a thin bar along the
  pane's right edge, inside its border, shows that pane's history and
  where you are in it (the knob in the border's colour; no bar while a
  pane has no history). Click or pull it: that pane becomes the one you
  are in and scrolls. One pane alone: the window's bar, as before.
- **Pane border** (Settings, Screen tab, 0 to 8 pixels) and its colours
  (Colours tab, "Colour of": Active pane's border, Other panes' borders;
  the theme's until set, "Back to the theme's colours" returns to them).
- **Dwindle** (Left Amiga+D, or Panes/Automatic split; kept in the
  settings): as Hyprland's dwindle layout, a new pane splits the one you
  are in along its longer side - side by side when it is wider than
  high, one below the other when it is higher. On a wide screen the
  panes spiral inwards: beside, below, beside... With dwindle on, Left
  Amiga+H or V chooses the way for the next pane only.
- **Which way a new pane goes:** each pane remembers its own way (side
  by side at first). Left Amiga+H or V changes it for the pane you are
  in, and a pane split off it takes the same way. The window's title
  says it for a moment, and the Panes menu shows a tick by "New panes
  side by side" or "New panes one below other".
- **All tabs to panes** (Left Amiga+P): 1 tab fills the window; 2 are
  halves side by side; 4 a grid of 2 x 2; 6 a grid of 3 x 2 (or 2 x 3
  in a tall window); 8 a grid of 4 x 2. With an odd number the tab you
  are in gets twice the room: **3 tabs** make a 2 x 2 grid where yours
  has the whole left half and the other two are stacked on the right,
  a quarter each; 5 tabs a 3 x 2 grid where yours has the left column.
  The tabs go in their order, row by row. When the window has no room
  for all of them (each pane at least 10 letters by 3 lines), as many
  as fit become panes (yours always) and the rest stay tabs - the
  title says how many. After that it is an ordinary layout: every key
  above works on it.
- Going back and forth between tabs and panes keeps everything in
  every tab (the Shell and what runs in it, a call, a transfer, the
  history, the file browser); each Shell or host is told its new size.
- Filling the window (Left Amiga+F) does not change the size of the
  panes behind it: their programs are not disturbed.
- Left Amiga+N and M stay the Amiga's (screens to the front and back),
  as do Left Amiga+V and B while a requester is open. Left Amiga and
  the arrows usually move the mouse pointer: in CTerm's window CTerm
  takes them (only while its window is the active one; elsewhere they
  move the pointer as always). In the Settings' Keys tab, "Press a
  key" cannot catch Left Amiga + an arrow (the pointer moves): type it
  in the field instead, as `<L-Left>`.
- Nothing is lost by closing a pane with the Panes menu's Close pane:
  that closes only the pane, its tab goes on in the tab bar.

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

## Finding in the history

Edit/Find in history... (Right Amiga+H) searches the tab's history and
screen for what you type, from the newest line back; capitals or not does
not matter. The title shows what you typed; the match is marked and
brought into view.

| Key | What it does |
|---|---|
| a letter, Backspace | the text to find, found as you type |
| Return, Up | the next match, older |
| Shift+Return, Down | the next match, newer |
| Right Amiga+H | the next older; right at the start: the text you looked for last time |
| Esc | done: the view stays there, the match marked (Right Amiga+C copies it) |

## Links

Addresses in the text (`https://`, `http://`, `www.`, `ftp://`,
`sftp://`, `ssh://`, `telnet://`, `gopher://`, `gemini://`) can be
opened: **Ctrl+click** one, or mark it (or a part of it) and choose
Edit/Open link (Right Amiga+O; with nothing marked: the first address on
the cursor's line).

- `telnet://`, `ssh://`, `ftp://` and `sftp://` open in a new CTerm tab
  (the file browser for FTP and SFTP).
- Web addresses go to your browser through `openurl.library` when it is
  installed, else through the `browser=` line in `ENV:CTerm.prefs`: a
  command that gets the address, at `%s` or at the end, for example
  `browser=Work:IBrowse/IBrowse "%s"`. With neither, the address goes onto
  the clipboard and the title says "link copied".

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
| `HANGUP` | hangs up the tab's call (the tab stays; also a file browser's) |
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
| `PANE NEXT`, `PANE SELECT n`, `PANE CLOSE`, `PANE COUNT`, `PANE CURRENT` | the panes; the commands that follow go to the tab of the pane chosen (CLOSE: the pane only, its tab stays) |
| `PANE OPEN`, `PANE KILL` | the i3 way: a new Shell in a new pane (split the way the pane is set to), the pane closed with its tab (no question asked, a transfer is stopped) |
| `PANE SPLIT H` / `V` | new panes from this one side by side, or one below the other |
| `PANE FOCUS LEFT` / `RIGHT` / `UP` / `DOWN`, `PANE MOVE ...` | to the pane that way; the pane changes places with that one. RC 10: no pane there |
| `PANE FULL`, `PANE SHOW n`, `PANE MODE TABS` / `PANES` | fill the window (again: back); tab n in this pane; all panes to tabs, all tabs to panes (RESULT: how many panes) |
| `PHONEBOOK` | the places' names, each in quotes |
| `SFTP place`, `FTP place` | opens the file browser: a phone book place, or `[user@]host[:port][/drawer]` (or a URL), in a new tab; RC 0 when it is connected. The commands below go to that tab |
| `LIST` | the files in the browser's drawer, each in quotes, drawers with a `/` at the end |
| `CD drawer` | goes to that drawer (`..` up); RESULT: where it is now |
| `GET name... [TO drawer]` | downloads files from the drawer into the Amiga panel's drawer (TO: into that drawer); RC 5 when some failed |
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

## Keys

Every key that makes CTerm itself do something can be changed: the
menus' shortcuts (and items that have none can get one), the tabs, the
history, the Shell's line editing, giving up a call, calling again, and
the file browser's keys.

**Two kinds of keys.** CTerm's own keys do something in CTerm. All the
other keys go to the program in the tab, as on any terminal: the arrows,
the function keys, Alt as Meta and the rest are sent the way the
VT100/xterm standard (or, in the Amiga Shell, the Amiga console) says,
so bash, vim, mc and Ed get what they expect. Those are not CTerm's to
change. A key you give to CTerm no longer reaches the program; a key you
take away from CTerm goes to the program again.

**Where they are kept:** all in one file, `ENVARC:CTerm/CTerm.keys`, a
text file written with the keys below the first time CTerm starts (as
CFile's and Ahiru's keys files are). It has six sections: `[window]`,
`[panes]`, `[call]`, `[shell]`, `[files]` and `[filesline]`. (A file
from before 0.3b19 is written again once, with the `[panes]` keys in
it; your own keys stay.) Each section's keys
are its own, so one key can do one thing in the Shell's line and
another in the file browser. Before 0.3b18 the file browser's keys were
in a file of their own, `files.keys`: CTerm reads it into `CTerm.keys`
once, then renames it `files.keys.old` (it is not read again; delete it
when you like).

**Changing them in Settings** (Settings..., the Keys tab): the list shows
everything CTerm does on a key, and its keys. Choose a line, then

- **Press a key**, then the key you want (it replaces the line's keys);
- or type the keys in the Keys field as the file writes them
  (`<A-n> <F9>`) and press Return - one action can have several;
- **None** takes the line's keys away; **Defaults** puts every key back
  as CTerm comes.

The list has a heading for each section; the file browser's two
sections are its own. A key that another line of the same section had
is taken from it (the title says so). A key that is CTerm's in every
tab and also the Shell's, the call's or the file browser's (say Return
for Close tab) is marked `!`: the every-tab one wins; Save or Use says
so once, and a second press keeps it. (The history's keys stand aside in
a file browser, so Shift+Up is the panel's page up there.) Save writes the file, Use
keeps the keys until CTerm quits, Cancel puts them back. The menus show
the new keys at once: Right Amiga + a letter as the Amiga does, any
other key written beside the item (`F9`, `Ctrl+Shift+F1`).

**Changing them in the file:** one key a line, `KEYS ACTION`. A key is a
character, or a name in `<>`: `<Up> <Down> <Left> <Right> <Return>
<Esc> <Tab> <Space> <Del> <BS> <Help> <F1>`..`<F12> <PgUp> <PgDn> <Home>
<End> <Ins>`, with any of `A-` (Right Amiga), `L-` (Left Amiga), `C-`
(Ctrl), `M-` (Alt), `S-` (Shift, with the names) in front: `<A-t>`,
`<L-q>`, `<C-r>`, `<S-Up>`, `<C-S-Tab>`, `<L-S-Left>`. `none ACTION` gives an action no key; `KEY none` takes a
key away from CTerm. Lines starting with `;` are notes. A change in the
file counts the next time CTerm starts.

The keys CTerm comes with:

| Key | Action | What it does |
|---|---|---|
| **In every tab** `[window]` | | |
| Right Amiga+T | `newtab` | Project/New Shell tab |
| Right Amiga+K | `call` | Project/Call... |
| Right Amiga+B | `phonebook` | Project/Phone book... |
| Right Amiga+W | `closetab` | Project/Close tab |
| Right Amiga+L | `log` | Project/Start log... |
| Right Amiga+U, P | `senduser`, `sendpass` | Project/Send user name, Send password |
| Right Amiga+Q | `quit` | Project/Quit |
| Right Amiga+C, V | `copy`, `paste` | Edit/Copy, Paste |
| Right Amiga+H | `find` | Edit/Find in history... |
| Right Amiga+O | `openlink` | Edit/Open link |
| Right Amiga+S | `settings` | Settings/Settings... |
| Right Amiga + `+`, `-` | `bigger`, `smaller` | Settings/Bigger text, Smaller text |
| Right Amiga+F | `fullscreen` | Settings/Full screen |
| Right Amiga+1 to 9 | `tab1` .. `tab9` | that tab |
| Ctrl+Tab, Ctrl+Shift+Tab | `nexttab`, `prevtab` | the next tab, the one before |
| Shift+Up, Shift+Down | `pageup`, `pagedown` | the history a page up, down |
| (none) | `serial`, `answer`, `sftp`, `logstop`, `rexx`, `hide`, `about`, `speedtest`, `yrecv`, `xrecv`, `ysend`, `xsend`, `hsend`, `transfers`, `splitside`, `splitunder`, `nextpane`, `closepane`, `font`, `ttfont`, `workbench`, `screen16`, `screen256`, `screendeep`, `scrollbar`, `utf8`, `pcbbs` | the other menu items, in the menus' order |
| (none) | `lineup`, `linedown`, `historytop`, `historyend` | the history a line up, down; its top; back to the end |
| **The panes, the i3 way** `[panes]` (in every tab, after `[window]`) | | |
| Left Amiga+Return | `open` | a new Shell: in a new pane, or a new tab (Panes/New Shells in tabs) |
| Left Amiga+Q | `kill` | close the pane and its tab |
| Left Amiga+Left, Right, Up, Down | `focusleft`, `focusright`, `focusup`, `focusdown` | the pane that way |
| Left Amiga+Shift+Left, Right, Up, Down | `moveleft`, `moveright`, `moveup`, `movedown` | change places with the pane that way |
| Left Amiga+H, V | `splith`, `splitv` | new panes side by side, one below the other |
| Left Amiga+F | `full` | the pane fills the window (again: back) |
| Left Amiga+D | `dwindle` | dwindle on or off: split by shape |
| Left Amiga+T, P | `tabs`, `panes` | all panes to tabs, all tabs to panes |
| Left Amiga+Ctrl+1 to 9 | `show1` .. `show9` | that tab in this pane |
| **Calling and transfers** `[call]` | | |
| Esc | `stop` | stops a transfer; gives up a call being made |
| Return | `redial` | calls again, after a call ended |
| **The Amiga Shell's line, as you type it** `[shell]` | | |
| Left, Right | `left`, `right` | a letter left, right |
| Shift+Left, Ctrl+A | `start` | to the start |
| Shift+Right, Ctrl+E | `end` | to the end |
| Ctrl+Left, Ctrl+Right | `wordleft`, `wordright` | a word left, right |
| Backspace, Del | `back`, `delete` | delete the letter before, under the cursor |
| Ctrl+W | `wordback` | delete the word before |
| Ctrl+U, Ctrl+K | `killstart`, `killend` | delete to the start, to the end |
| Ctrl+X | `clear` | clear the line |
| Up, Down | `older`, `newer` | the lines typed before (the history) |
| Ctrl+R | `search` | the line before that holds what is typed (again: the one before that) |
| Tab | `complete` | complete a file name |
| Return | `accept` | enter the line |
| Ctrl+\ | `eof` | end of input (on an empty line) |
| **The file browser's panels** `[files]` | | |
| Up, Down | `up`, `down` | the bar up and down |
| Shift+Up, PageUp / Shift+Down, PageDown | `pageup`, `pagedown` | a page up, a page down |
| Ctrl+Up, Home / Ctrl+Down, End | `top`, `bottom` | the first line, the last |
| Left, Right | `left`, `right` | the Amiga's panel, the host's |
| Tab | `switch` | the other panel |
| Return | `open` | into the drawer under the bar |
| Backspace | `parent` | the drawer above |
| Space, Insert | `mark` | mark or unmark, then down |
| `*` | `invert` | the files' marks turned over |
| F1, Help | `help` | the keys and the commands |
| F2, Ctrl+R | `refresh` | both drawers read again |
| F5 | `copy` | copy the marked (or the bar's) across |
| F6 | `rename` | rename the one under the bar |
| F7 | `mkdir` | a new drawer |
| F8, Del | `delete` | delete the marked (asks first) |
| F9 | `log` | the messages |
| F10 | `quit` | hang up and close the tab (asks first) |
| Esc | `stop` | stop a transfer or a listing; forget a search |
| `:` | `command` | the command line |
| Ctrl+L | `redraw` | draw everything again |
| **A line typed in the file browser** `[filesline]` | | |
| Left, Right | `left`, `right` | a letter left, right |
| Shift+Left, Ctrl+A, Home | `start` | to the start |
| Shift+Right, Ctrl+E, End | `end` | to the end |
| Backspace, Del | `back`, `delete` | delete the letter before, under the cursor |
| Shift+Backspace, Ctrl+U | `kill` | delete to the start |
| Return, Esc | `accept`, `cancel` | done; nothing done |
| Tab | `complete` | complete a name or a command |
| Up, Down | `older`, `newer` | the command lines before, after |

The Shell's line keys count while the Shell (or a program) reads a line;
a program that takes the keys itself (Ed, More) gets them as they are.
In a file browser tab CTerm's keys come first, but for the history's
(the panels page themselves), then the browser's own; Right Amiga and
Alt keys do not reach the browser (Alt types its character there). Ctrl+C to Ctrl+F
stay the Amiga's break keys, and yes and no in questions are y and n.


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
  the PC's own font. The characters are what every new tab starts in
  (a phone book place's own characters win for its tab); the shown tab
  follows them at once. Settings/UTF-8 and Settings/PC BBS in the menu
  change only the shown tab, and their ticks always show what the shown
  tab reads.
- **Colours:** a theme (Nord, Black, Workbench, Amber, Green, Light), and
  your own text and background colour on top of it.
- **Keys:** what the Alt key does (Meta for programs on other
  computers, or the Amiga's own Alt characters), and every key of
  CTerm's own (see Keys above).
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
`Protocols/` and `Parts/` (`make parts`: the parts alone); `make showpic`
builds ShowPic. The first build
downloads the AmiSSL SDK and libssh2 (third_party/fetch.sh) and builds
libssh2. `make test` checks the emulator core on Linux.
`tests/serial/` tests serial calls in FS-UAE (its serial port on a TCP
socket) against a pretend Hayes modem on the PC: see `fakemodem.py`.
