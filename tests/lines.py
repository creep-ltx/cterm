#!/usr/bin/env python3
"""Line operations (IL, DL, SU, SD, scroll regions) checked against a
model written from the VT100/xterm rules, not against pyte: pyte gets
some of these wrong (its rows are shared after an insert). The streams in
tests/lines use only CUP, DECSTBM, IL, DL, SU, SD, CR, LF and ASCII text.
Names end in -COLSxROWS.bin."""
import glob, os, re, subprocess, sys

def model(data, cols, rows):
    scr = [""] * rows
    x = y = 0
    top, bot = 0, rows - 1
    pending = False
    def scroll_up(t, b, n):
        n = min(n, b - t + 1)
        del scr[t:t + n]
        for _ in range(n):
            scr.insert(b - n + 1, "")
    def scroll_down(t, b, n):
        n = min(n, b - t + 1)
        del scr[b - n + 1:b + 1]
        for _ in range(n):
            scr.insert(t, "")
    i = 0
    while i < len(data):
        c = data[i]
        if c == 0x1b:
            m = re.match(rb"\x1b\[([0-9;]*)([A-Za-z])", data[i:])
            p = [int(v) if v else 0 for v in m.group(1).split(b";")] if m.group(1) else []
            f = m.group(2)
            n = max(p[0], 1) if p else 1
            if f == b"H":
                y = min(max(p[0] if p else 1, 1), rows) - 1
                x = min(max(p[1] if len(p) > 1 else 1, 1), cols) - 1
            elif f == b"r":
                t = (p[0] if p and p[0] else 1) - 1
                b = (p[1] if len(p) > 1 and p[1] else rows) - 1
                if t < b <= rows - 1:
                    top, bot = t, b
                x = y = 0
            elif f == b"M" and top <= y <= bot:
                scroll_up(y, bot, n); x = 0
            elif f == b"L" and top <= y <= bot:
                scroll_down(y, bot, n); x = 0
            elif f == b"S":
                scroll_up(top, bot, n)
            elif f == b"T":
                scroll_down(top, bot, n)
            pending = False
            i += m.end()
            continue
        if c == 13:
            x = 0; pending = False
        elif c == 10:
            pending = False
            if y == bot:
                scroll_up(top, bot, 1)
            elif y < rows - 1:
                y += 1
        else:
            if pending:
                x = 0; pending = False
                if y == bot:
                    scroll_up(top, bot, 1)
                elif y < rows - 1:
                    y += 1
            line = scr[y].ljust(x)
            scr[y] = line[:x] + chr(c) + line[x + 1:]
            if x == cols - 1:
                pending = True
            else:
                x += 1
        i += 1
    return [l.rstrip() for l in scr]

bad = 0
for path in sorted(glob.glob(os.path.join(os.path.dirname(__file__), "lines", "*.bin"))):
    m = re.search(r"-(\d+)x(\d+)\.bin$", path)
    cols, rows = int(m.group(1)), int(m.group(2))
    whole = open(path, "rb").read()
    # cut only between sequences, so the model sees whole ones
    cuts = [len(whole)] + [len(whole) * k // 16 for k in range(1, 16)]
    cuts = sorted(set(whole.rfind(b"\n", 0, c) + 1 or c for c in cuts))
    worst = None
    for cut in cuts:
        open("/tmp/vtlines.bin", "wb").write(whole[:cut])
        ours = subprocess.run(["tests/vtdump", str(cols), str(rows), "/tmp/vtlines.bin"],
                              capture_output=True).stdout.decode("utf-8", "replace").split("\n")[:rows]
        ref = model(whole[:cut], cols, rows)
        diffs = [(i, a, b) for i, (a, b) in enumerate(zip(ours, ref)) if a.rstrip() != b]
        if diffs:
            worst = (cut, diffs)
            break
    print("%-24s %d checkpoints: %s" % (os.path.basename(path), len(cuts),
          "all same" if not worst else "differs at byte %d" % worst[0]))
    if worst:
        for i, a, b in worst[1][:5]:
            print("   row %2d ours: %r\n         model: %r" % (i, a[:90], b[:90]))
    bad += bool(worst)
sys.exit(1 if bad else 0)
