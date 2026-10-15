#!/usr/bin/env python3
"""mksixel.py - test pictures for CTerm's pictures in the terminal, made
without img2sixel: a sixel encoder for small RGB pictures, and the test
patterns (colour bars, a gradient). Also writes iTerm2 inline files.

  mksixel.py bars W H [N]       N colour bars (default 8), as sixel
  mksixel.py gradient W H       a red-green gradient over blue, 256 colours
  mksixel.py ppm W H            the colour bars as a PPM (for tests)
  mksixel.py iterm FILE [ARGS]  FILE as an iTerm2 inline picture
                                (ARGS: width=..;height=..)
  mksixel.py aspect W H P1 A     the bars, sixel P1 and raster aspect A
                                ("2;1"; none: no raster attributes)
  mksixel.py multipart FILE ARGS N  FILE as iTerm2's MultipartFile, N
                                base64 bytes a FilePart
  mksixel.py kitty KEYS F W H [N] [z]  the bars as kitty's f=F (24, 32),
                                chunks of N base64 bytes, z: zlib'ed
  mksixel.py kittyfile KEYS FILE [N]  FILE as kitty's f=100
  mksixel.py kittycmd KEYS      kitty's APC with no data (a=d, a=p...)
  mksixel.py picture FILE [W]   a picture file (PNG...) W pixels wide, at
                                most 256 colours, as sixel (needs PIL)
Output on stdout.
"""
import sys, base64

BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]


def bars(w, h, n=8):
    """n vertical bars, the colours of a test card"""
    return [[BARS[(x * n // w) % len(BARS)] for x in range(w)]
            for _ in range(h)]


def gradient(w, h):
    """red across, green down, blue 128: quantised to 16 x 16 colours"""
    return [[((x * 16 // w) * 17, (y * 16 // h) * 17, 128) for x in range(w)]
            for y in range(h)]


def sixel(px, raster=True, p2=1, p1=0, aspect="1;1"):
    """px: rows of (r, g, b); at most 256 colours. raster: the raster
    attributes "aspect;w;h (aspect "Pan;Pad"); else none (P1's aspect)"""
    h, w = len(px), len(px[0])
    cols = []
    idx = {}
    for row in px:
        for c in row:
            if c not in idx:
                idx[c] = len(cols)
                cols.append(c)
    assert len(cols) <= 256, "too many colours"
    out = ["\x1bP%d;%d;0q" % (p1, p2)]
    if raster:
        out.append('"%s;%d;%d' % (aspect, w, h))
    for i, (r, g, b) in enumerate(cols):
        out.append("#%d;2;%d;%d;%d" % (i, round(r * 100 / 255),
                                        round(g * 100 / 255),
                                        round(b * 100 / 255)))
    for band in range(0, h, 6):
        first = True
        for ci in range(len(cols)):
            bits = []
            for x in range(w):
                v = 0
                for k in range(6):
                    y = band + k
                    if y < h and idx[px[y][x]] == ci:
                        v |= 1 << k
                bits.append(v)
            if not any(bits):
                continue
            if not first:
                out.append("$")
            first = False
            out.append("#%d" % ci)
            x = 0
            while x < w:                # run-length: !n
                n = 1
                while x + n < w and bits[x + n] == bits[x]:
                    n += 1
                ch = chr(63 + bits[x])
                out.append("!%d%s" % (n, ch) if n > 3 else ch * n)
                x += n
        out.append("-")
    out.append("\x1b\\")
    return "".join(out)


def ppm(px):
    h, w = len(px), len(px[0])
    return b"P6\n%d %d\n255\n" % (w, h) + bytes(c for row in px for p in row
                                               for c in p)


def iterm(data, args=""):
    a = "inline=1;size=%d" % len(data) + (";" + args if args else "")
    return b"\x1b]1337;File=" + a.encode() + b":" + \
        base64.b64encode(data) + b"\x07\n"   # imgcat ends so


def multipart(data, args="", piece=200):
    """iTerm2's MultipartFile: the base64 in FilePart pieces, FileEnd"""
    a = "inline=1;size=%d" % len(data) + (";" + args if args else "")
    b = base64.b64encode(data)
    out = b"\x1b]1337;MultipartFile=" + a.encode() + b"\x07"
    for i in range(0, len(b), piece):
        out += b"\x1b]1337;FilePart=" + b[i:i + piece] + b"\x07"
    return out + b"\x1b]1337;FileEnd\x07\n"


def kitty(keys, data, chunk=4096):
    """kitty's APC G: the keys on the first chunk, m=1 on all but the
    last (each chunk's base64 a multiple of 4, as kitty asks)"""
    b = base64.b64encode(data)
    if not b:
        return ("\x1b_G%s\x1b\\" % keys).encode()
    out = b""
    for i in range(0, len(b), chunk):
        more = 1 if i + chunk < len(b) else 0
        k = (keys + "," if i == 0 and keys else "") + "m=%d" % more
        out += b"\x1b_G" + k.encode() + b";" + b[i:i + chunk] + b"\x1b\\"
    return out


def kitty_pixels(fmt, w, h):
    """the colour bars as kitty's f=24 (RGB) or f=32 (RGBA: the last bar
    see-through: kitty blends it over the background)"""
    px = bars(w, h)
    if fmt == 24:
        return bytes(c for row in px for p in row for c in p)
    out = bytearray()
    for row in px:
        for x, p in enumerate(row):
            a = 0 if p == (0, 0, 0) else 255
            out += bytes(p) + bytes([a])
    return bytes(out)


def main(a):
    out = sys.stdout.buffer
    if a[0] == "aspect":                # aspect W H P1 Pan;Pad|none
        r = a[4] != "none"
        out.write(sixel(bars(int(a[1]), int(a[2])), raster=r, p1=int(a[3]),
                        aspect=a[4] if r else "1;1").encode())
    elif a[0] == "multipart":           # multipart FILE ARGS PIECE
        out.write(multipart(open(a[1], "rb").read(), a[2], int(a[3])))
    elif a[0] == "kitty":               # kitty KEYS FMT W H [CHUNK] [z]
        import zlib
        fmt, w, h = int(a[2]), int(a[3]), int(a[4])
        data = kitty_pixels(fmt, w, h)
        keys = "f=%d,s=%d,v=%d" % (fmt, w, h) + ("," + a[1] if a[1] else "")
        if len(a) > 6 and a[6] == "z":
            data = zlib.compress(data, 9)
            keys += ",o=z"
        out.write(kitty(keys, data, int(a[5]) if len(a) > 5 else 4096))
    elif a[0] == "kittyfile":           # kittyfile KEYS FILE [CHUNK]: f=100
        keys = "f=100" + ("," + a[1] if a[1] else "")
        out.write(kitty(keys, open(a[2], "rb").read(),
                        int(a[3]) if len(a) > 3 else 4096))
    elif a[0] == "kittycmd":            # kittycmd KEYS: no data (a=d, a=p)
        out.write(kitty(a[1], b""))
    elif a[0] == "bars":
        out.write(sixel(bars(int(a[1]), int(a[2]),
                             int(a[3]) if len(a) > 3 else 8)).encode())
    elif a[0] == "gradient":
        out.write(sixel(gradient(int(a[1]), int(a[2]))).encode())
    elif a[0] == "ppm":
        out.write(ppm(bars(int(a[1]), int(a[2]))))
    elif a[0] == "picture":
        from PIL import Image
        im = Image.open(a[1]).convert("RGB")
        if len(a) > 2:
            w = int(a[2])
            im = im.resize((w, im.height * w // im.width), Image.LANCZOS)
        im = im.quantize(256).convert("RGB")
        px = [[im.getpixel((x, y)) for x in range(im.width)]
              for y in range(im.height)]
        out.write(sixel(px).encode())
    elif a[0] == "iterm":
        out.write(iterm(open(a[1], "rb").read(), a[2] if len(a) > 2 else ""))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:] or ["?"])
