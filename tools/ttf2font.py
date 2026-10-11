#!/usr/bin/env python3
"""ttf2font - a fixed-width TrueType font as an Amiga bitmap font, so every
program (Font prefs, the Shell, Workbench, Ed) can use it, not only the ones
that read .ttf files themselves (CTerm through ttengine).

    python3 tools/ttf2font.py FONT.ttf NAME OUTDIR SIZE [SIZE...]

writes OUTDIR/NAME.font and OUTDIR/NAME/SIZE for each size: copy both into
FONTS:. SIZE is the Amiga size, the line height in pixels (as Arial/13 is 13
rows); the glyphs are FreeType's own black-and-white rendering (hinted) at
the largest size whose Latin-1 letters (Å and g included) fit those rows.
Characters 32-255 (ISO-8859-1, the Amiga's); 127-159 are the empty box.

The file layout is MicroKnight/8's (a 3.2 machine's FONTS:): a hunk file,
"moveq #-1,d0; rts", a DiskFontHeader, its TextFont, the bits, the CharLoc
table, four relocations. Read back with ByteBandit's tools/amigafont.py.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

LO, HI = 32, 255
NT_FONT = 12
FPF_DISKFONT, FPF_DESIGNED = 0x02, 0x40
DFH_ID, FCH_ID = 0x0F80, 0x0F00
HUNK_HEADER, HUNK_CODE, HUNK_RELOC32, HUNK_END = 0x3F3, 0x3E9, 0x3EC, 0x3F2


def latin1():
    return [chr(c) for c in range(LO, 0x7F)] + [chr(c) for c in range(0xA0, HI + 1)]


def extents(font):
    """rows above the baseline and at/below it of the Latin-1 letters"""
    top = bot = 0
    for ch in latin1():
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls', mode='1')
        if x1 > x0:
            top, bot = max(top, -y0), max(bot, y1)
    return top, bot


def pick(path, size):
    """the largest em size whose Latin-1 letters fit `size` rows"""
    for em in range(size + 4, 3, -1):
        f = ImageFont.truetype(path, em)
        top, bot = extents(f)
        if top + bot <= size:
            return f, em, top, bot
    raise SystemExit(f'ttf2font: nothing fits {size} rows')


def glyphs(font, size, top, bot):
    """(width, baseline row, [rows of 0/1 lists per glyph incl. the default])"""
    width = round(font.getlength('0', mode='1'))
    above = top + (size - top - bot) // 2       # spare rows shared out
    out = []
    for c in range(LO, HI + 2):
        img = Image.new('1', (width, size), 0)
        if c <= HI and not 0x7F <= c <= 0x9F:
            d = ImageDraw.Draw(img)
            d.fontmode = '1'
            d.text((0, above), chr(c), font=font, fill=1, anchor='ls')
        else:                                   # the empty box
            d = ImageDraw.Draw(img)
            d.rectangle((1, above - top + 1, width - 2, above + bot - 2), outline=1)
        px = img.load()
        out.append([[px[x, y] for x in range(width)] for y in range(size)])
    return width, above - 1, out


def size_file(name, size, width, baseline, gl):
    n = len(gl)
    modulo = (n * width + 15) // 16 * 2         # bytes a row, even
    bits = bytearray(modulo * size)
    for i, g in enumerate(gl):
        for y in range(size):
            for x in range(width):
                if g[y][x]:
                    b = i * width + x
                    bits[y * modulo + (b >> 3)] |= 0x80 >> (b & 7)
    cloc = b''.join(struct.pack('>HH', i * width, width) for i in range(n))

    dfh = 4                                     # after moveq/rts
    nameoff = dfh + 22
    tf = dfh + 54
    cdata = tf + 52
    clocoff = cdata + len(bits)
    end = clocoff + len(cloc)
    data = bytearray(end + (-end) % 4)
    data[0:4] = b'\x70\xff\x4e\x75'
    # the DiskFontHeader's Node, its id, revision, segment, name
    struct.pack_into('>IIBBI', data, dfh, 0, 0, NT_FONT, 0, nameoff)
    struct.pack_into('>HHI', data, dfh + 14, DFH_ID, 1, 0)
    data[nameoff:nameoff + 31] = name.encode('latin-1')[:31].ljust(31, b'\0')
    # the TextFont: its Message (Node, reply port, length), then the font
    struct.pack_into('>IIBBIIH', data, tf, 0, 0, NT_FONT, 0, nameoff, 0, end - cdata)
    struct.pack_into('>HBBHHHHBBIHIII', data, tf + 20, size, 0, FPF_DESIGNED,
                     width, baseline, 1, 0, LO, HI, cdata, modulo, clocoff, 0, 0)
    data[cdata:clocoff] = bits
    data[clocoff:end] = cloc
    relocs = [dfh + 10, tf + 10, tf + 34, tf + 40]
    longs = len(data) // 4
    h = struct.pack('>IIIII', HUNK_HEADER, 0, 1, 0, 0) + struct.pack('>I', longs)
    h += struct.pack('>II', HUNK_CODE, longs) + bytes(data)
    h += struct.pack('>III', HUNK_RELOC32, len(relocs), 0)
    h += b''.join(struct.pack('>I', r) for r in relocs) + struct.pack('>I', 0)
    return h + struct.pack('>I', HUNK_END)


def contents_file(name, sizes):
    out = struct.pack('>HH', FCH_ID, len(sizes))
    for s in sizes:
        fn = f'{name}/{s}'.encode('latin-1')
        out += fn.ljust(256, b'\0') + struct.pack('>HBB', s, 0, FPF_DESIGNED | FPF_DISKFONT)
    return out


def main():
    if len(sys.argv) < 5:
        raise SystemExit(__doc__)
    path, name, outdir = sys.argv[1:4]
    sizes = sorted(int(s) for s in sys.argv[4:])
    os.makedirs(os.path.join(outdir, name), exist_ok=True)
    for s in sizes:
        font, em, top, bot = pick(path, s)
        width, baseline, gl = glyphs(font, s, top, bot)
        with open(os.path.join(outdir, name, str(s)), 'wb') as f:
            f.write(size_file(name, s, width, baseline, gl))
        print(f'{name}/{s}: {width} x {s}, baseline {baseline}, drawn at {em} px')
    with open(os.path.join(outdir, name + '.font'), 'wb') as f:
        f.write(contents_file(name, sizes))


if __name__ == '__main__':
    main()
