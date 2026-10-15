#!/usr/bin/env python3
"""emojifont.py IN.ttf OUT.ttf - an emoji font CTerm's fallback font can use.

ttengine.library (TrueType on the Amiga) reaches only the first 65536 code
points (the BMP): most emoji are past it (U+1F000-1FAFF), and come out as
an empty box whatever the font holds (tested: UTF-32, UTF-16 surrogates
and UTF-8 alike, ttengine 7.1). This makes a copy of a monochrome emoji
font (Noto Emoji, from Google Fonts; Symbola) where each of those is ALSO
at U+E000 + (code point - U+1F000), in the Private Use Area, which
ttengine reaches; U+FDD0 (a code point no font uses) marks the font as
made so. CTerm, given such a font as its fallback font (fallbackfont= in
ENV:CTerm.prefs, Settings' Text tab), draws U+1Fxxx from there.

A variable font (Noto Emoji's [wght] file) becomes its Regular weight,
which also makes it smaller. Needs fontTools (pip install fonttools).
Example: python3 tools/emojifont.py "NotoEmoji[wght].ttf" NotoEmoji-CTerm.ttf
"""
import sys

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable

FIRST, LAST, PUA = 0x1F000, 0x1FAFF, 0xE000
MARK = 0xFDD0


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    font = TTFont(sys.argv[1])
    if "fvar" in font:
        from fontTools.varLib import instancer
        axes = {a.axisTag: a.defaultValue for a in font["fvar"].axes}
        if "wght" in axes:
            axes["wght"] = 400
        font = instancer.instantiateVariableFont(font, axes)
    best = font.getBestCmap()
    add = {}
    for cp, glyph in best.items():
        if FIRST <= cp <= LAST:
            add[PUA + cp - FIRST] = glyph
    if not add:
        print("no emoji in U+1F000-1FAFF in", sys.argv[1])
        return 1
    clash = [hex(c) for c in add if c in best and best[c] != add[c]]
    if clash:
        print("the font has its own letters at", clash[:5], "...: not changed")
        return 1
    add[MARK] = add.get(PUA + 0x600, next(iter(add.values())))
    cmap = font["cmap"]
    have4 = False
    for t in cmap.tables:
        if not t.isUnicode():
            continue
        for cp, glyph in add.items():
            t.cmap[cp] = glyph
        have4 |= t.format == 4
    if not have4:                       # a BMP table for ttengine to find
        t = CmapSubtable.newSubtable(4)
        t.platformID, t.platEncID, t.language = 3, 1, 0
        t.cmap = {cp: g for cp, g in best.items() if cp <= 0xFFFF}
        t.cmap.update(add)
        cmap.tables.append(t)
    font.save(sys.argv[2])
    print(f"{sys.argv[2]}: {len(add) - 1} emoji also at U+E000-")
    return 0


if __name__ == "__main__":
    sys.exit(main())
