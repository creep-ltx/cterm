#!/usr/bin/env python3
"""Feed each stream in tests/streams through CTerm's emulator (vtdump)
and through pyte, and show where the final screens differ.
Stream names end in -COLSxROWS.bin."""
import glob, os, re, subprocess, sys
import pyte

class AltScreen(pyte.Screen):
    """pyte without the alternate screen would draw full-screen programs
    over the shell; xterm's ?1049 / ?47 / ?1047, added."""
    def set_mode(self, *modes, **kw):
        if kw.get("private") and any(m in (47, 1047, 1049) for m in modes):
            self._saved_main = ([dict(self.buffer[y]) for y in range(self.lines)],
                                (self.cursor.x, self.cursor.y))
            self.erase_in_display(2)
            modes = [m for m in modes if m not in (47, 1047, 1049)]
        super().set_mode(*modes, **kw)
    def select_graphic_rendition(self, *a, **kw):
        if not kw.get("private"):   # "CSI > 4;2 m" (xterm key modes)
            super().select_graphic_rendition(*a)
    def reset_mode(self, *modes, **kw):
        if kw.get("private") and any(m in (47, 1047, 1049) for m in modes) \
                and getattr(self, "_saved_main", None):
            lines, (x, y) = self._saved_main
            self.erase_in_display(2)
            for yy, line in enumerate(lines):
                self.buffer[yy].update(line)
            self.cursor.x, self.cursor.y = x, y
            self._saved_main = None
            self.dirty.update(range(self.lines))
            modes = [m for m in modes if m not in (47, 1047, 1049)]
        super().reset_mode(*modes, **kw)

def pyte_screen(data, cols, rows):
    s = AltScreen(cols, rows)
    st = pyte.ByteStream(s)
    st.feed(data)
    return [l.rstrip() for l in s.display]

bad = 0
for path in sorted(glob.glob(os.path.join(os.path.dirname(__file__), "streams", "*.bin"))):
    m = re.search(r"-(\d+)x(\d+)\.bin$", path)
    cols, rows = int(m.group(1)), int(m.group(2))
    whole = open(path, "rb").read()
    # pyte's own gaps, not ours: it has no DEC line drawing, and it prints
    # the final letter of private CSIs it does not know (kitty's CSI < u)
    known = re.compile(rb"\x1b\[[<=>][0-9;]*u|\x1b\(0|\x1b\(B")
    cuts = sorted(set([len(whole)] + [len(whole) * k // 8 for k in range(1, 8)]))
    worst = []
    for cut in cuts:
        data = whole[:cut]
        if known.search(data):
            data_p = known.sub(b"", data)
        else:
            data_p = data
        open("/tmp/vtcut.bin", "wb").write(data)
        ours = subprocess.run(["tests/vtdump", str(cols), str(rows), "/tmp/vtcut.bin"],
                              capture_output=True).stdout.decode("utf-8", "replace").split("\n")[:rows]
        ref = pyte_screen(data_p, cols, rows)
        diffs = [(i, a, b) for i, (a, b) in enumerate(zip(ours, ref)) if a.rstrip() != b.rstrip()]
        if b"\x1b(0" in data:          # line drawing: pyte cannot
            diffs = [d for d in diffs if not any(ch in d[1] for ch in "\u2500\u2502\u250c\u2510\u2514\u2518")]
        if diffs and not worst:
            worst = [(cut, d) for d in diffs]
    print("%-24s %d checkpoints: %s" % (os.path.basename(path), len(cuts),
          "all same" if not worst else "differs at byte %d" % worst[0][0]))
    for cut, (i, a, b) in worst[:5]:
        print("   row %2d ours: %r\n          pyte: %r" % (i, a[:90], b[:90]))
    bad += bool(worst)
sys.exit(1 if bad else 0)
