"""cmap_cmp.py OURS FC - two charsets (hex ranges "a-b" or "a", spaces
between) compared as sets of code points; the controls (below 0x20 and
0x7F-0x9F) left out, as fontconfig leaves them. Exit 0: the same."""
import sys


def points(path):
    s = set()
    for w in open(path).read().split():
        a, _, b = w.partition("-")
        a = int(a, 16)
        b = int(b, 16) if b else a
        s.update(range(a, b + 1))
    return {c for c in s if c >= 0x20 and not 0x7F <= c <= 0x9F}


ours, fc = points(sys.argv[1]), points(sys.argv[2])
if ours == fc:
    print(f"  {len(ours)} characters")
    sys.exit(0)
print("  only ours:", sorted(hex(c) for c in ours - fc)[:20], len(ours - fc))
print("  only fontconfig's:", sorted(hex(c) for c in fc - ours)[:20], len(fc - ours))
sys.exit(1)
