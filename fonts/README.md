# fonts

`PxPlus_IBM_VGA_8x16.ttf`: the IBM VGA 8x16 text-mode font as pixel
outlines, from VileR's Ultimate Oldschool PC Font Pack v2.2
(https://int10h.org/oldschool-pc-fonts/), licensed CC BY-SA 4.0
(LICENSE.txt). CTerm uses it in PC BBS mode, at 16 pixels, not smoothed.
On the Amiga it lives in CTerm's own folder: `PROGDIR:Fonts/`.

`NotoEmoji-CTerm.ttf`: Google's Noto Emoji (monochrome, the Regular weight of
its variable font), made with `tools/emojifont.py` so that ttengine.library
can reach the emoji (copied into the Private Use Area, marked with U+FDD0).
Licensed under the SIL Open Font License 1.1 (`NotoEmoji-OFL.txt`), which
allows a changed copy shipped with it. CTerm uses it as its fallback font
(emoji and symbols the main TrueType font lacks) when it is in
`PROGDIR:Fonts/` and the person has not chosen another fallback font or
None in Settings' Text tab. Not built into CTerm: delete the file and the
fallback is simply off.
