#!/usr/bin/env python3
"""zbbs.py [PORT] - a tiny telnet "BBS" for CTerm's file transfer tests:
'd' sends the files in /tmp/zbbs/files with lsz (Zmodem), 'u' takes
uploads with lrz into /tmp/zbbs/up, 'q' hangs up. Telnet IAC is doubled
and undoubled around lrzsz, as a real board does. lrzsz: see
tests/zmodem_test.c. Listens on 127.0.0.1 (the emulator reaches it)."""
import os, select, socket, subprocess, sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 2397
HERE = os.path.dirname(os.path.abspath(__file__))
LRZSZ = os.path.join(HERE, "../../third_party/src/lrzsz-0.12.20/src/")
IAC = 255

def undouble(data, st):
    out = bytearray()
    for c in data:
        if st[0] == 0:
            if c == IAC: st[0] = 1
            else: out.append(c)
        elif st[0] == 1:
            if c == IAC: out.append(IAC); st[0] = 0
            elif c in (251, 252, 253, 254): st[0] = 2
            elif c == 250: st[0] = 3
            else: st[0] = 0
        elif st[0] == 2: st[0] = 0
        elif st[0] == 3:
            if c == IAC: st[0] = 4
        elif st[0] == 4: st[0] = 0 if c == 240 else 3
    return bytes(out)

def run(conn, argv, cwd=None):
    p = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, cwd=cwd)
    st = [0]
    fo = p.stdout.fileno()
    while True:
        r, _, _ = select.select([conn, fo], [], [], 60)
        if not r:
            break
        if fo in r:
            d = os.read(fo, 65536)
            if not d:
                break
            conn.sendall(d.replace(b"\xff", b"\xff\xff"))
        if conn in r:
            d = conn.recv(65536)
            if not d:
                break
            try:
                p.stdin.write(undouble(d, st)); p.stdin.flush()
            except BrokenPipeError:
                break
    p.wait()
    return p.returncode

def session(conn):
    # binary both ways, as BBSes do for transfers
    conn.sendall(bytes([IAC, 251, 0, IAC, 253, 0]))
    while True:
        conn.sendall(b"\r\n\x1b[1;36mZBBS\x1b[0m  d) download  u) upload  q) quit: ")
        d = conn.recv(100)
        if not d:
            return
        c = undouble(d, [0]).strip().lower()[:1]
        if c == b"d":
            files = sorted(os.path.join("/tmp/zbbs/files", f)
                           for f in os.listdir("/tmp/zbbs/files"))
            conn.sendall(b"\r\nsending %d files...\r\n" % len(files))
            rc = run(conn, [LRZSZ + "lsz", "-b"] + files)
            conn.sendall(b"\r\nlsz ended: %d\r\n" % rc)
        elif c == b"u":
            os.makedirs("/tmp/zbbs/up", exist_ok=True)
            conn.sendall(b"\r\nstart your upload...\r\n")
            rc = run(conn, [LRZSZ + "lrz", "-b"], cwd="/tmp/zbbs/up")
            conn.sendall(b"\r\nlrz ended: %d\r\n" % rc)
        elif c == b"q":
            return

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", PORT))
s.listen(2)
print("zbbs on 127.0.0.1:%d" % PORT, flush=True)
while True:
    c, _ = s.accept()
    try:
        session(c)
    except Exception as e:
        print("session:", e, flush=True)
    c.close()
