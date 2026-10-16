#!/usr/bin/env python3
"""vtprof.py TEST - per-function 68020 instruction counts of the emulator
for one conbench item (vamos trace of vtbench TEST 24 minus TEST 8)."""
import sys, subprocess, re, bisect, collections, os
# Bebbo's m68k-amigaos-gcc: set AMIGA to where it is installed, otherwise it is
# looked for in the usual places (/opt/amiga, then ~/opt/amiga).
AMIGA = os.environ.get('AMIGA') or next((d for d in ('/opt/amiga', os.path.expanduser('~/opt/amiga'))
                                         if os.path.isdir(d)), '/opt/amiga')
t = sys.argv[1]
nm = subprocess.run([f'{AMIGA}/bin/m68k-amigaos-nm', '-n', '/tmp/vtbench'],
                    capture_output=True, text=True).stdout
syms = []
for l in nm.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'tT':
        syms.append((int(p[0], 16), p[2]))
# vt.c's static functions: from the object file, placed by vt_write
vo = subprocess.run([f'{AMIGA}/bin/m68k-amigaos-gcc', '-O2', '-m68020', '-noixemul',
                     '-fomit-frame-pointer', '-Iinclude', '-c', 'src/vt.c', '-o', '/tmp/vt.o'])
onm = subprocess.run([f'{AMIGA}/bin/m68k-amigaos-nm', '-n', '/tmp/vt.o'],
                     capture_output=True, text=True).stdout
osy = [(int(l.split()[0], 16), l.split()[2]) for l in onm.splitlines()
       if len(l.split()) == 3 and l.split()[1] in 'tT']
exe_vw = dict((n, a) for a, n in syms)['_vt_write']
obj_vw = dict((n, a) for a, n in osy)['_vt_write']
names = set(n for _, n in syms)
syms += [(a - obj_vw + exe_vw, n) for a, n in osy if n not in names]
syms.sort()
addrs = [a for a, _ in syms]
def run(n):
    out = subprocess.run(['vamos', '-C', '68020', '-m', '4096', '-I', 'vtbench', t, str(n)],
                         capture_output=True, text=True, cwd='/tmp').stdout + ''
    return out
def count(n):
    txt = subprocess.run(f'vamos -C 68020 -m 4096 -I vtbench {t} {n} 2>&1', shell=True,
                         capture_output=True, text=True, cwd='/tmp').stdout
    pcs = [int(m.group(1), 16) for m in re.finditer(r'N/A\s+([0-9a-f]{6})\s', txt)]
    base = pcs[0]  # first instruction = _start at 0
    c = collections.Counter()
    for pc in pcs:
        off = pc - base
        i = bisect.bisect_right(addrs, off) - 1
        c[syms[i][1] if 0 <= off < 0x100000 and i >= 0 else 'ROM/other'] += 1
    return c
a, b = count(8), count(24)
d = {k: (b[k] - a.get(k, 0)) / 16 for k in b}
for k, v in sorted(d.items(), key=lambda x: -x[1])[:15]:
    if v >= 1:
        print(f'{k:28s} {v:8.0f}')
print('total', sum(d.values()))
