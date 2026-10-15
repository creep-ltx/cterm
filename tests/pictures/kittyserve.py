#!/usr/bin/env python3
"""kittyserve.py [PORT] - a telnet-ish test host for CTerm's newer
pictures: kitty's graphics (the question programs ask, f=24, f=32
zlib'ed in chunks, f=100 PNG through the Amiga's datatypes, a=t then
a=p, deletes), sixel's aspect (2:1) and iTerm2's MultipartFile. What
CTerm answers is printed here and on its screen. Each key typed in
CTerm's tab starts a part: 1 kitty, 2 sixel aspect + MultipartFile,
3 kitty's deletes, 4 the font test (a picture of each kind), q hangs up.
Port 2141 by default. One call at a time; Ctrl-C ends it."""
import os, socket, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mksixel  # noqa: E402

port = int(sys.argv[1]) if len(sys.argv) > 1 else 2141


def read(name):
    return open(os.path.join(HERE, name), "rb").read()


def replies(c, wait=4.0):
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


def ask(c, label, data):
    """send data, show what came back"""
    c.sendall(label.encode() + b"\r\n" + data)
    r = replies(c)
    print(label, "->", show(r), flush=True)
    c.sendall(b"\r\n  answer: " + show(r).encode() + b"\r\n")


def kitty(keys, fmt, w, h, chunk=4096, z=False):
    import zlib
    data = mksixel.kitty_pixels(fmt, w, h)
    k = "f=%d,s=%d,v=%d" % (fmt, w, h) + ("," + keys if keys else "")
    if z:
        data = zlib.compress(data, 9)
        k += ",o=z"
    return mksixel.kitty(k, data, chunk)


def part1(c):
    ask(c, "kitty: icat's question (a=q) and DA1:",
        b"\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c")
    ask(c, "kitty f=24 i=5, the bars 64x32:", kitty("a=T,i=5", 24, 64, 32))
    c.sendall(b"kitty f=32 zlib'ed, chunks of 64 (the last bar see-through):\r\n" +
              kitty("a=T,q=2", 32, 64, 32, 64, True) + b"\r\n")
    ask(c, "kitty f=100 i=9, the Kickstart PNG 24 letters wide:",
        mksixel.kitty("f=100,a=T,i=9,c=24", read("kickstart.png"), 4096))
    ask(c, "kitty a=t i=6 (kept, not shown), then a=p c=16 r=1:",
        kitty("a=t,i=6,q=1", 24, 64, 32) + b"\x1b_Ga=p,i=6,c=16,r=1\x1b\\")
    c.sendall(b"\r\nend of part 1\r\n")


def part2(c):
    c.sendall(b"sixel P1=0, no raster attributes (2:1: twice as tall):\r\n" +
              mksixel.sixel(mksixel.bars(64, 12), raster=False).encode() +
              b"\r\nthe same, raster 1;1:\r\n" +
              mksixel.sixel(mksixel.bars(64, 12)).encode() + b"\r\n")
    c.sendall(b"iTerm2 MultipartFile, the Kickstart PNG 20 letters wide:\r\n" +
              mksixel.multipart(read("kickstart.png"), "width=20", 1000)
              .replace(b"\x07\n", b"\x07\r\n"))
    c.sendall(b"end of part 2\r\n")


def part3(c):
    c.sendall(b"kitty a=d d=i i=9 (the Kickstart goes), then a=p i=5 C=1:\r\n"
              b"\x1b_Ga=d,d=i,i=9\x1b\\\x1b_Ga=p,i=5,C=1,q=2\x1b\\")
    ask(c, "a=p i=9 again (kept: shown):", b"\x1b_Ga=p,i=9,c=10\x1b\\")
    ask(c, "kitty a=d d=I i=9, then a=p i=9 (ENOENT):",
        b"\x1b_Ga=d,d=I,i=9\x1b\\\x1b_Ga=p,i=9\x1b\\")
    c.sendall(b"end of part 3\r\n")


def part4(c):
    c.sendall(b"\x1b[H\x1b[2J\x1b[3Jsixel:\r\n" + read("bars.six") +
              b"\r\nkitty PNG c=12:\r\n" +
              mksixel.kitty("f=100,a=T,q=2,c=12", read("kickstart.png"), 4096) +
              b"\r\niTerm2 width=12:\r\n" +
              mksixel.iterm(read("kickstart.png"), "width=12")
              .replace(b"\x07\n", b"\x07\r\n") + b"end of part 4\r\n")


s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
s.listen(1)
print("kittyserve on", port, flush=True)
while True:
    c, a = s.accept()
    print("call from", a, flush=True)
    try:
        c.sendall(b"kittyserve: keys 1 2 3 4, q\r\n")
        c.settimeout(None)
        while True:
            d = c.recv(64)
            if not d:
                break
            for ch in d:
                if ch == ord("1"):
                    part1(c)
                elif ch == ord("2"):
                    part2(c)
                elif ch == ord("3"):
                    part3(c)
                elif ch == ord("4"):
                    part4(c)
                elif ch == ord("q"):
                    raise EOFError
            c.settimeout(None)
    except (OSError, EOFError) as e:
        print("call ended:", e, flush=True)
    c.close()
