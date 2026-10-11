#!/usr/bin/env python3
"""picserve.py [PORT] - a telnet-ish test host for CTerm's pictures: on a
call it asks what programs ask (DA1, the cell and window in pixels,
XTSMGRAPHICS), shows the answers, then sends the colour bars as sixel,
the Kickstart picture as an iTerm2 PNG, the gradient, a kitty picture
(which must not show as text) and some text. The replies are printed
here too. Port 2424 by default. One call at a time; Ctrl-C ends it."""
import os, socket, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
port = int(sys.argv[1]) if len(sys.argv) > 1 else 2424


def read(name):
    return open(os.path.join(HERE, name), "rb").read()


def replies(c, wait=2.0):
    c.settimeout(0.3)
    got, end = b"", time.time() + wait
    while time.time() < end:
        try:
            d = c.recv(4096)
            if not d:
                break
            got += d
        except socket.timeout:
            pass
    return got


def show(b):
    return "".join("ESC" if ch == 27 else chr(ch) if 32 <= ch < 127
                   else "<%02X>" % ch for ch in b
                   if ch not in (0xFF, 0xFB, 0xFD, 0xFC, 0xFE))


s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
s.listen(1)
print("picserve on", port, flush=True)
while True:
    c, a = s.accept()
    print("call from", a, flush=True)
    try:
        c.sendall(b"picserve: pictures for CTerm\r\n")
        c.sendall(b"\x1b[c\x1b[16t\x1b[14t\x1b[?1;1;0S\x1b[?2;1;0S")
        r = replies(c)
        print("replies:", show(r), flush=True)
        c.sendall(("replies: " + show(r) + "\r\n").encode())
        c.sendall(b"sixel, the colour bars:\r\n" + read("bars.six") + b"\r\n")
        c.sendall(b"iTerm2, a PNG 30 letters wide:\r\n" +
                  read("kickstart.iterm").replace(b"inline=1;",
                                                  b"inline=1;width=30;")
                  .replace(b"\x07\n", b"\x07\r\n"))
        c.sendall(b"kitty's picture (must not show): "
                  b"\x1b_Gf=100,a=T;iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB\x1b\\"
                  b"[nothing between]\r\n")
        c.sendall(b"sixel, the gradient:\r\n" + read("gradient.six") +
                  b"\r\nend of the pictures\r\n")
        replies(c, 600)
    except OSError as e:
        print("call ended:", e, flush=True)
    c.close()
