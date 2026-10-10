#!/usr/bin/env python3
"""cterm-relay: a program on this PC, shown in CTerm on the Amiga.

Knocks on CTerm's port on the Amiga (CTerm listens; this PC's firewall
drops calls from the LAN, so the PC calls the Amiga). When CTerm answers,
runs COMMAND in a pty and passes bytes both ways as telnet: CTerm tells
the window size with NAWS, and the pty follows it.

The relay gives a shell to whoever answers on that port, so it is careful
about who that is: without --host it asks wasabi again where the Amiga is
when no CTerm has answered for a minute (an address can pass to another
machine), and it starts COMMAND only once the other end has spoken as
CTerm does (IAC WILL NAWS). CTerm on its side lets in
only this PC's address (ENVARC:CTerm/relay_from, asked the first time).

    cterm-relay.py [--host AMIGA] [--port 7478] [--once] [-- COMMAND ...]

COMMAND defaults to the user's login shell. Standard library only.
"""

import argparse
import fcntl
import os
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import termios
import time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
BINARY, ECHO, SGA, NAWS, TTYPE = 0, 1, 3, 31, 24


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


def find_amiga():
    try:
        out = subprocess.run(["wasabi", "discover"], capture_output=True,
                             text=True, timeout=15).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    m = re.match(r"(\d+\.\d+\.\d+\.\d+)", out)
    return m.group(1) if m else None


class Telnet:
    """Just enough telnet: options are answered, NAWS is read, 0xFF is
    doubled. Bytes that are data come back from feed()."""

    def __init__(self, sock):
        self.sock = sock
        self.state = 0
        self.opt_cmd = 0
        self.sb = bytearray()
        self.size = None                # (cols, rows) when CTerm says
        self.cterm = False              # it offered NAWS, as CTerm does

    def start(self):
        # we will echo and suppress go-ahead (a "character mode" server),
        # binary both ways, and we want the window size
        self.sock.sendall(bytes([IAC, WILL, ECHO, IAC, WILL, SGA,
                                 IAC, WILL, BINARY, IAC, DO, BINARY,
                                 IAC, DO, NAWS]))

    def send(self, data):
        self.sock.sendall(data.replace(b"\xff", b"\xff\xff"))

    def feed(self, data):
        out = bytearray()
        for b in data:
            if self.state == 0:
                if b == IAC:
                    self.state = 1
                else:
                    out.append(b)
            elif self.state == 1:               # after IAC
                if b == IAC:
                    out.append(IAC)
                    self.state = 0
                elif b in (DO, DONT, WILL, WONT):
                    self.opt_cmd = b
                    self.state = 2
                elif b == SB:
                    self.sb = bytearray()
                    self.state = 3
                else:
                    self.state = 0
            elif self.state == 2:               # the option
                if self.opt_cmd == WILL and b == NAWS:
                    self.cterm = True           # we asked DO NAWS already
                self.state = 0                  # nothing more to say
            elif self.state == 3:               # subnegotiation
                if b == IAC:
                    self.state = 4
                else:
                    self.sb.append(b)
            elif self.state == 4:
                if b == SE:
                    self.state = 0
                    if len(self.sb) >= 5 and self.sb[0] == NAWS:
                        w, h = struct.unpack(">HH", bytes(self.sb[1:5]))
                        self.size = (w, h)
                else:
                    self.sb.append(b)
                    self.state = 3
        return bytes(out)


def set_size(fd, cols, rows):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def session(sock, command):
    """Run COMMAND for CTerm on sock. False: it was not CTerm, or CTerm
    did not let us in."""
    tn = Telnet(sock)
    tn.start()
    # CTerm first: it offers NAWS at once, or after its user has said yes
    # to this PC (the first time); anything else gets no shell
    sock.settimeout(60)
    first = b""
    try:
        while not tn.cterm:
            chunk = sock.recv(4096)
            if not chunk:
                return False            # refused, or not CTerm
            first += tn.feed(chunk)
    except socket.timeout:
        log("no CTerm answered there")
        return False
    except OSError:
        return False                    # refused, or not CTerm
    # then wait a moment for the size, so the program starts at it
    sock.settimeout(1.5)
    try:
        while tn.size is None:
            chunk = sock.recv(4096)
            if not chunk:
                return False
            first += tn.feed(chunk)
    except socket.timeout:
        pass
    sock.settimeout(None)
    cols, rows = tn.size or (80, 24)
    log("CTerm is %dx%d; running %s" % (cols, rows, " ".join(command)))

    pid, fd = pty.fork()
    if pid == 0:
        # A clean start: a relay started from inside a Claude Code session
        # would hand its markers on, and a Claude Code run in CTerm would
        # think it is that session's child (it showed "auto mode on").
        for k in list(os.environ):
            if k.startswith("CLAUDE"):
                del os.environ[k]
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLORTERM"] = "truecolor"
        os.environ.pop("COLUMNS", None)
        os.environ.pop("LINES", None)
        os.execvp(command[0], command)
    set_size(fd, cols, rows)
    if first:
        os.write(fd, first)
    shown = tn.size
    try:
        while True:
            r, _, _ = select.select([sock, fd], [], [])
            if sock in r:
                data = sock.recv(65536)
                if not data:
                    break
                data = tn.feed(data)
                if tn.size != shown and tn.size:
                    shown = tn.size
                    set_size(fd, *shown)
                    os.kill(pid, signal.SIGWINCH)
                if data:
                    os.write(fd, data)
            if fd in r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    break                       # the program ended
                if not data:
                    break
                tn.send(data)
    finally:
        try:
            os.kill(pid, signal.SIGHUP)
            os.waitpid(pid, 0)
        except OSError:
            pass
        os.close(fd)
        log("session ended")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host")
    ap.add_argument("--port", type=int, default=7478)
    ap.add_argument("--once", action="store_true", help="one session only")
    ap.add_argument("command", nargs="*")
    a = ap.parse_args()
    command = a.command or [os.environ.get("SHELL", "/bin/bash"), "-l"]
    # Our process number, so the relay can be stopped by it: "pkill -f"
    # with its name also hits any shell whose command line mentions it.
    pidfile = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/tmp"),
                           "cterm-relay.pid")
    with open(pidfile, "w") as fh:
        fh.write(str(os.getpid()))
    host = a.host
    misses = 0                          # knocks in a row with no CTerm
    log("cterm-relay, knocking on port %d" % a.port)
    while True:
        if not host:
            host = find_amiga()
            if not host:
                time.sleep(30)
                continue
            log("the Amiga is at", host)
        ran = False
        try:
            sock = socket.create_connection((host, a.port), timeout=2)
        except OSError:
            sock = None
        if sock:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try:
                ran = session(sock, command)
            except OSError as exc:
                log("connection lost:", exc)
                ran = True
            finally:
                sock.close()
        if a.once and ran:
            return
        misses = 0 if ran else misses + 1
        # a minute of knocks with no CTerm: where is the Amiga now? (never
        # with --host; if wasabi cannot say, the old address is kept)
        if not a.host and misses >= 30:
            misses = 0
            now = find_amiga()
            if now and now != host:
                log("the Amiga is now at", now)
                host = now
        time.sleep(2)


if __name__ == "__main__":
    main()
