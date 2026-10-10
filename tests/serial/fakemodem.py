#!/usr/bin/env python3
"""fakemodem.py - a Hayes modem on the PC for CTerm's serial tests.

FS-UAE puts the Amiga's serial port on a TCP socket (it listens:
serial_port = tcp://127.0.0.1:PORT); this program connects to it and is
what sits on the other end of the Amiga's serial cable:

  fakemodem.py [--uae HOST:PORT] modem    a modem: AT commands, dials
  fakemodem.py [--uae HOST:PORT] null HOST:PORT
                                          a null modem straight to a
                                          telnet BBS (tests/xfer/zbbs.py)
  fakemodem.py [--uae HOST:PORT] echo     a null modem that echoes

The modem: AT, ATZ, AT&F..., ATE0/1, ATV0/1 (numeric results), ATI,
ATH, ATO, ATA, ATD (T/P/nothing) + a number. The number is looked up in
NUMBERS below, or is host:port (or host*port) itself; it connects there
as a telnet client (IAC taken off and doubled, as a telnet modem does)
and says CONNECT 38400. "BUSY", "NOANSWER", "NOCARRIER", "NODIALTONE"
as numbers give those answers. +++ with a second's quiet each side
goes back to commands (OK); ATH hangs up; the far end closing gives
NO CARRIER. A call into the modem: connect to --ring PORT (default
2399) - it says RING every 2 s until ATA (CONNECT, and that connection
is the other side) or 5 rings. Every command and what was said back
is logged to stderr (and to --log FILE), for the tests to read.
"""
import argparse, select, socket, sys, time

NUMBERS = {"5551234": "127.0.0.1:2397", "5550000": "127.0.0.1:2397"}
IAC = 255

ap = argparse.ArgumentParser()
ap.add_argument("--uae", default="127.0.0.1:5555")
ap.add_argument("--ring", type=int, default=2399)
ap.add_argument("--log")
ap.add_argument("--speed", default="38400")
ap.add_argument("--tap", help="what the Amiga sends online, to this file")
ap.add_argument("mode", choices=["modem", "null", "echo"])
ap.add_argument("target", nargs="?")
a = ap.parse_args()
logf = open(a.log, "a") if a.log else None


def log(*w):
    s = " ".join(str(x) for x in w)
    print(s, file=sys.stderr, flush=True)
    if logf:
        logf.write(time.strftime("%H:%M:%S ") + s + "\n")
        logf.flush()


class Telnet:
    """Telnet off the far end's bytes; 0xFF doubled to it."""
    def __init__(self, sock):
        self.s, self.st = sock, 0

    def decode(self, data):
        out = bytearray()
        for c in data:
            if self.st == 0:
                if c == IAC: self.st = 1
                else: out.append(c)
            elif self.st == 1:
                if c == IAC: out.append(IAC); self.st = 0
                elif c in (251, 252, 253, 254): self.st = 2
                elif c == 250: self.st = 3
                else: self.st = 0
            elif self.st == 2: self.st = 0
            elif self.st == 3:
                if c == IAC: self.st = 4
            elif self.st == 4: self.st = 0 if c == 240 else 3
        return bytes(out)

    def send(self, data):
        self.s.sendall(data.replace(b"\xff", b"\xff\xff"))


def connect_uae():
    host, port = a.uae.rsplit(":", 1)
    while True:
        try:
            s = socket.create_connection((host, int(port)), timeout=5)
            s.settimeout(None)
            log("connected to the emulator's serial port", a.uae)
            return s
        except OSError:
            time.sleep(1)


def relay_null(amiga):
    """null HOST:PORT / echo: the line is the connection."""
    far = tel = None
    if a.mode == "null":
        h, p = a.target.rsplit(":", 1)
        far = socket.create_connection((h, int(p)))
        tel = Telnet(far)
    sent = recvd = 0
    while True:
        socks = [amiga] + ([far] if far else [])
        r, _, _ = select.select(socks, [], [], 60)
        if amiga in r:
            d = amiga.recv(65536)
            if not d:
                log("the emulator closed the line"); return
            recvd += len(d)
            if far: tel.send(d)
            else: amiga.sendall(d)
        if far and far in r:
            d = far.recv(65536)
            if not d:
                log("the far end closed"); far.close(); far = None
                amiga.sendall(b"\r\n[null modem: the far end closed]\r\n")
                continue
            d = tel.decode(d)
            sent += len(d)
            amiga.sendall(d)


class Framed:
    """The Amiga's socket, sending in the parity the modem detected."""
    def __init__(self, sock, modem):
        self.s, self.m = sock, modem

    def sendall(self, b):
        if self.m.par:
            out = bytearray()
            for c in b:
                c &= 0x7F
                odd = bin(c).count("1") % 2
                if (self.m.par == "even") == bool(odd):
                    c |= 0x80
                out.append(c)
            b = bytes(out)
        self.s.sendall(b)

    def recv(self, n):
        return self.s.recv(n)

    def fileno(self):
        return self.s.fileno()


class Modem:
    def __init__(self, amiga):
        self.am = Framed(amiga, self)
        self.echo, self.verbose = True, True
        self.cmd = bytearray()
        self.parity_seen = []
        self.par = None                 # 7E1/7O1 seen in the AT: answer so
        self.far = self.tel = None
        self.online = False
        self.last_in = time.time()      # the Amiga's last byte
        self.plus = 0                   # +++ after quiet
        self.plus_t = 0
        self.ringing = None             # a call coming in
        self.rings = 0
        self.ring_t = 0
        self.rs = socket.socket()
        self.rs.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rs.bind(("127.0.0.1", a.ring))
        self.rs.listen(1)

    CODES = {"OK": 0, "CONNECT": 1, "RING": 2, "NO CARRIER": 3, "ERROR": 4,
             "NO DIALTONE": 6, "BUSY": 7, "NO ANSWER": 8}

    def say(self, word):
        log("modem says:", word)
        if self.verbose:
            self.am.sendall(b"\r\n" + word.encode() + b"\r\n")
        else:
            code = self.CODES.get(word.split(" ")[0], 1 if word.startswith("CONNECT") else 4)
            self.am.sendall(b"%d\r" % code)

    def hangup(self, word="NO CARRIER"):
        if self.far:
            self.far.close()
        self.far = self.tel = None
        if self.online and word:
            self.say(word)
        self.online = False

    def dial(self, num):
        num = num.strip().replace(" ", "").replace("-", "")
        log("dialling", num)
        up = num.upper()
        if up in ("BUSY", "NOANSWER", "NOCARRIER", "NODIALTONE"):
            time.sleep(1)
            self.say({"BUSY": "BUSY", "NOANSWER": "NO ANSWER",
                      "NOCARRIER": "NO CARRIER", "NODIALTONE": "NO DIALTONE"}[up])
            return
        if up == "SILENT":              # rings for ever: CTerm's time limit
            self.dialling_silent = True
            return
        target = NUMBERS.get(num, num.replace("*", ":"))
        try:
            h, p = target.rsplit(":", 1)
            time.sleep(1.5)             # ringing
            self.far = socket.create_connection((h, int(p)), timeout=10)
            self.far.settimeout(None)
        except (OSError, ValueError) as e:
            log("dial failed:", e)
            self.say("NO ANSWER")
            return
        self.tel = Telnet(self.far)
        self.online = True
        self.say("CONNECT " + a.speed)

    def command(self, line):
        s = line.decode("latin-1").strip()
        log("command:", repr(s))
        u = s.upper()
        if not u.startswith("AT"):
            if u:
                self.say("ERROR")
            return
        body = u[2:]
        if body.startswith("D"):
            n = s[3:]
            if n[:1].upper() in ("T", "P"):
                n = n[1:]
            self.dial(n)
            return
        if body.startswith("A"):
            if self.ringing:
                self.far, self.ringing = self.ringing, None
                self.tel = None         # a raw socket, not telnet
                self.online = True
                self.say("CONNECT " + a.speed)
            else:
                self.say("NO CARRIER")
            return
        if body.startswith("O") and self.far:
            self.online = True
            self.say("CONNECT " + a.speed)
            return
        i = 0
        while i < len(body):
            c = body[i]
            arg = ""
            i += 1
            while i < len(body) and body[i].isdigit():
                arg += body[i]; i += 1
            if c == "E": self.echo = arg != "0"
            elif c == "V": self.verbose = arg != "0"
            elif c == "H": self.hangup(None)
            elif c == "Z": self.echo, self.verbose = True, True
            elif c == "I":
                self.am.sendall(b"\r\nCTerm test modem (fakemodem.py)\r\n")
            elif c in "&SQXLMB%\\+":
                # &F1, S0=0, S7=60, ...: skip their argument
                while i < len(body) and body[i] not in "ADEHIOVZ&SQXLMB":
                    i += 1
        self.say("OK")

    def from_amiga(self, d):
        now = time.time()
        out = bytearray()               # to the far end: one send a chunk
        for c in d:
            if self.online:
                # +++: quiet first, three pluses, quiet after
                if c == ord("+") and (self.plus or now - self.last_in > 0.9):
                    self.plus += 1
                    self.plus_t = now
                    continue
                if self.plus:
                    out += b"+" * self.plus
                    self.plus = 0
                out.append(c)
            else:
                # a modem reads the AT's framing: 7 bits and a parity bit
                # come as an 8th bit here (the emulator's TCP sends 8)
                if c & 0x80:
                    self.parity_seen.append(c)
                    ones = bin(c & 0x7F).count("1") + 1
                    self.par = "even" if ones % 2 == 0 else "odd"
                    c &= 0x7F
                if self.echo:
                    self.am.sendall(bytes([c]))
                if c in (13, 10):
                    if self.parity_seen:
                        log("8th bits seen (parity):",
                            " ".join("%02x" % x for x in self.parity_seen))
                        self.parity_seen = []
                    elif self.cmd:
                        self.par = None     # 8N1 again ("T" has no 8th bit)
                    if self.cmd:
                        self.command(bytes(self.cmd))
                    self.cmd = bytearray()
                elif c == 8:
                    self.cmd = self.cmd[:-1]
                else:
                    self.cmd.append(c)
        if out:
            self.far_send(bytes(out))
        self.last_in = now

    def far_send(self, b):
        if not self.far:
            return
        if a.tap:
            with open(a.tap, "ab") as f:
                f.write(b"\n%.3f " % time.time() + b)
        if self.tel: self.tel.send(b)
        else: self.far.sendall(b)

    def tick(self):
        now = time.time()
        if self.plus == 3 and now - self.plus_t > 0.9:
            log("+++: back to commands")
            self.plus = 0
            self.online = False
            self.say("OK")
        elif self.plus and self.plus != 3 and now - self.plus_t > 0.9:
            self.far_send(b"+" * self.plus)
            self.plus = 0
        if self.ringing and now - self.ring_t > 2:
            if self.rings >= 5:
                log("nobody answered")
                self.ringing.close(); self.ringing = None
            else:
                self.rings += 1
                self.ring_t = now
                self.say("RING")

    def run(self):
        while True:
            socks = [self.am, self.rs] + ([self.far] if self.far else [])
            r, _, _ = select.select(socks, [], [], 0.2)
            if self.am in r:
                d = self.am.recv(65536)
                if not d:
                    log("the emulator closed the line")
                    self.hangup(None)
                    return
                self.from_amiga(d)
            if self.far and self.far in r:
                d = self.far.recv(65536)
                if not d:
                    log("the far end hung up")
                    self.hangup("NO CARRIER")
                elif self.online:
                    self.am.sendall(self.tel.decode(d) if self.tel else d)
            if self.rs in r:
                c, _ = self.rs.accept()
                if self.ringing or self.far:
                    c.close()
                else:
                    log("a call comes in")
                    self.ringing, self.rings, self.ring_t = c, 0, 0
            self.tick()


while True:
    am = connect_uae()
    try:
        if a.mode == "modem":
            m = Modem(am)
            try:
                m.run()
            finally:
                m.rs.close()
        else:
            relay_null(am)
    except OSError as e:
        log("line:", e)
    am.close()
    time.sleep(1)
