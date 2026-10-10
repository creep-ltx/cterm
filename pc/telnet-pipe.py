#!/usr/bin/env python3
"""telnet-pipe.py HOST PORT [TTYPE] - a telnet client with no screen of its
own: it answers the host's option questions (terminal type, window size,
binary) and passes the data both ways raw. Run under the relay
(pc/relay.sh start .venv/bin/python pc/telnet-pipe.py localhost 2323), it
lets CTerm on the Amiga show a BBS that only the PC can reach. The window
size is the terminal's own (the relay's pty follows CTerm's), told again
when it changes."""
import os, select, signal, socket, struct, sys, termios, tty

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
BINARY, ECHO, SGA, TTYPE, NAWS = 0, 1, 3, 24, 31

host, port = sys.argv[1], int(sys.argv[2])
ttype = (sys.argv[3] if len(sys.argv) > 3 else "ANSI").encode()
s = socket.create_connection((host, port))
fd = sys.stdin.fileno()
saved = None
if os.isatty(fd):
    saved = termios.tcgetattr(fd)       # put back as it was, at the end
    tty.setraw(fd)
out = sys.stdout.buffer

# SIGWINCH wakes the select below through a pipe (a signal handler that
# sent the size itself could land in the middle of another send)
wake_r, wake_w = os.pipe()
os.set_blocking(wake_w, False)
signal.set_wakeup_fd(wake_w)
signal.signal(signal.SIGWINCH, lambda *a: None)

def size():
    for f in (sys.stdout.fileno(), fd):
        try:
            cols, rows = os.get_terminal_size(f)
            if cols and rows:
                return min(cols, 65535), min(rows, 65535)
        except OSError:
            pass
    return 80, 25                       # not a terminal: the old guess

# Options on at each end: a request for what already is gets no answer
# (no loops with a host that does the same), DONT/WONT are answered.
us, him = set(), set()

def reply(b):
    s.sendall(bytes(b))

def send_naws():
    if NAWS not in us:
        return                          # the host did not ask for it
    body = bytearray(struct.pack(">HH", *size()))
    reply([IAC, SB, NAWS] + list(body.replace(b"\xff", b"\xff\xff"))
          + [IAC, SE])

def option(cmd, c):
    if cmd == DO:
        if c in us:
            return
        if c in (TTYPE, NAWS, BINARY):
            us.add(c)
            reply([IAC, WILL, c])
            if c == NAWS:
                send_naws()
        else:
            reply([IAC, WONT, c])
    elif cmd == DONT:
        if c in us:
            us.discard(c)
            reply([IAC, WONT, c])
    elif cmd == WILL:
        if c in him:
            return
        if c in (ECHO, SGA, BINARY):
            him.add(c)
            reply([IAC, DO, c])
        else:
            reply([IAC, DONT, c])
    elif cmd == WONT:
        if c in him:
            him.discard(c)
            reply([IAC, DONT, c])

state, cmd, sb = 0, 0, bytearray()

def from_host(data):
    global state, cmd, sb
    plain = bytearray()
    for c in data:
        if state == 0:
            if c == IAC: state = 1
            else: plain.append(c)
        elif state == 1:
            if c == IAC: plain.append(IAC); state = 0
            elif c in (WILL, WONT, DO, DONT): cmd = c; state = 2
            elif c == SB: sb = bytearray(); state = 3
            else: state = 0
        elif state == 2:
            if plain:
                out.write(plain); out.flush(); plain = bytearray()
            option(cmd, c)
            state = 0
        elif state == 3:
            if c == IAC: state = 4
            else: sb.append(c)
        elif state == 4:
            if c == SE:
                if sb[:2] == bytes([TTYPE, 1]):
                    reply([IAC, SB, TTYPE, 0] + list(ttype) + [IAC, SE])
                state = 0
            else:
                sb.append(c); state = 3
    if plain:
        out.write(plain); out.flush()

try:
    while True:
        r, _, _ = select.select([s, fd, wake_r], [], [])
        if wake_r in r:
            os.read(wake_r, 64)
            send_naws()                 # the window may have a new size
        if s in r:
            d = s.recv(4096)
            if not d:
                break
            from_host(d)
        if fd in r:
            d = os.read(fd, 4096)
            if not d:
                break
            s.sendall(d.replace(b"\xff", b"\xff\xff"))
finally:
    if saved is not None:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
