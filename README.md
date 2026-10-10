# CTerm

A terminal for AmigaOS 3.2, in C: the Amiga Shell in a window of its own,
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
| `PC` | a PC BBS: the PC character set (CP437), PC colours |
| `LATIN1` | Latin-1 instead of UTF-8 (Amiga BBSes) |
| `COLS n` / `ROWS n` | the window's size in letters |
| `FROM script` | the Shell runs this script first |
| `LISTEN` / `PORT n` | wait for the PC's relay (pc/cterm-relay.py) |

Project/Call... (Right Amiga+K) asks for a host and remembers the last
eight. A call opens in a window of its own; when it ends, Return calls
again.

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

Mark text with the left mouse button. Right Amiga+C copies it, Right
Amiga+V types the clipboard in.

## Settings

The Settings menu: Amiga font, TrueType font, bigger/smaller text,
Workbench window or own screen, full screen, scroll bar, UTF-8, PC BBS.
They are kept in `ENVARC:CTerm.prefs`, with the window's place and size.

## Building

`make` (bebbo's m68k-amigaos-gcc in ~/opt/amiga) builds CTerm and CTermSSH. The first build
downloads the AmiSSL SDK and libssh2 (third_party/fetch.sh) and builds
libssh2. `make test` checks the emulator core on Linux.
