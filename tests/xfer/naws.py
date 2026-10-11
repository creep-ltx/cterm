#!/usr/bin/env python3
"""naws.py PORT - a telnet host that asks for NAWS, logs every size it is
told (to stdout) and shows it to the caller; anything typed is echoed."""
import socket, sys, threading
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2398
IAC, SB, SE, DO, WILL, NAWS = 255, 250, 240, 253, 251, 31
def session(c, n):
    c.sendall(bytes([IAC, DO, NAWS, IAC, WILL, 1, IAC, WILL, 3]))
    c.sendall(b"naws test host %d\r\n" % n)
    buf = b""; st = 0; sb = bytearray()
    while True:
        d = c.recv(4096)
        if not d:
            return
        out = bytearray()
        for b in d:
            if st == 0:
                if b == IAC: st = 1
                else: out.append(b)
            elif st == 1:
                if b == SB: st = 3; sb = bytearray()
                elif b in (251, 252, 253, 254): st = 2
                elif b == IAC: out.append(IAC); st = 0
                else: st = 0
            elif st == 2: st = 0
            elif st == 3:
                if b == IAC: st = 4
                else: sb.append(b)
            elif st == 4:
                if b == SE:
                    if sb and sb[0] == NAWS and len(sb) >= 5:
                        w = sb[1] * 256 + sb[2]; h = sb[3] * 256 + sb[4]
                        print("conn %d size %dx%d" % (n, w, h), flush=True)
                        c.sendall(b"[size %dx%d]\r\n" % (w, h))
                    st = 0
                elif b == IAC: sb.append(IAC); st = 3
                else: st = 3
        if out:
            c.sendall(bytes(out).replace(b"\r", b"\r\n"))
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", PORT)); s.listen(4); n = 0
print("naws on", PORT, flush=True)
while True:
    c, _ = s.accept(); n += 1
    threading.Thread(target=session, args=(c, n), daemon=True).start()
