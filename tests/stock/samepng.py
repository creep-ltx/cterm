#!/usr/bin/env python3
"""samepng.py A.png B.png - the same pixels? Prints how many differ and
the box around them (exit 1 if any). For look.sh's grabs of two builds."""
import sys
from PIL import Image

a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size:
    print(f'sizes differ: {a.size} {b.size}')
    sys.exit(1)
w, h = a.size
pa, pb = a.load(), b.load()
diff = [(x, y) for y in range(h) for x in range(w) if pa[x, y] != pb[x, y]]
if not diff:
    print(f'same: {w}x{h}')
    sys.exit(0)
xs = [x for x, _ in diff]
ys = [y for _, y in diff]
print(f'{len(diff)} pixels differ, x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}')
sys.exit(1)
