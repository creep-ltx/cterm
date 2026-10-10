#!/usr/bin/env python3
"""telnet-pipe.py HOST PORT [TTYPE] - a telnet client with no screen of its
own: it answers the host's option questions (terminal type, window size
80x25, binary) and passes the data both ways raw. Run under the relay
(pc/relay.sh start .venv/bin/python pc/telnet-pipe.py localhost 2323), it
lets CTerm on the Amiga show a BBS that only the PC can reach."""
import os, select, socket, sys, termios, tty

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
BINARY, ECHO, SGA, TTYPE, NAWS = 0, 1, 3, 24, 31

host, port = sys.argv[1], int(sys.argv[2])
ttype = (sys.argv[3] if len(sys.argv) > 3 else "ANSI").encode()
s = socket.create_connection((host, port))
fd = sys.stdin.fileno()
if os.isatty(fd):
    tty.setraw(fd)
out = sys.stdout.buffer

state, cmd, sb = 0, 0, bytearray()
def reply(b):
    s.sendall(bytes(b))

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
            if cmd == DO:
                ok = c in (TTYPE, NAWS, BINARY)
                reply([IAC, WILL if ok else WONT, c])
                if c == NAWS:
                    reply([IAC, SB, NAWS, 0, 80, 0, 25, IAC, SE])
            elif cmd == WILL:
                reply([IAC, DO if c in (ECHO, SGA, BINARY) else DONT, c])
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
        r, _, _ = select.select([s, fd], [], [])
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
    if os.isatty(fd):
        termios.tcsetattr(fd, termios.TCSADRAIN, termios.tcgetattr(fd))
