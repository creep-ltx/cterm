#!/usr/bin/env python3
"""mkstress.py DIR - test streams for tests/perf/vtsame.sh: conbench's shapes
(plain, wrapping, colour runs and colour per letter, cursor moves, frames,
erase, insert/delete, runs of new lines, form feeds) and seeded random mixes
of letters, controls, CSI/ESC sequences, UTF-8, Latin-1 and 0x9B - what the
parser's fast paths take and what they must hand back to the state machine."""
import os, random, sys

E = '\x1b['
d = sys.argv[1]
os.makedirs(d, exist_ok=True)


def save(name, s):
    with open(os.path.join(d, name), 'wb') as f:
        f.write(s if isinstance(s, bytes) else s.encode('latin-1'))


blk = ''.join(chr(33 + i % 94) for i in range(78)) + '\n'
longl = ''.join(chr(33 + i % 94) for i in range(300))
o = []
for i in range(60):
    o.append(blk)
for i in range(30):
    o.append(''.join(f'{E}3{k}m12345678' for k in range(8)) + '\n')
for i in range(30):
    o.append(''.join(f'{E}3{k % 8}m{chr(65 + (k + i) % 26)}' for k in range(40)) + '\n')
for i in range(80):
    o.append(f'{E}{i % 20 + 1};{i % 60 + 1}H-cursor-')
for i in range(60):
    o.append(f'{E}{i % 28 + 1};1H{E}K' + longl[(i * 7) % 240:][:60])
for i in range(40):
    o.append(f'\rworking: {i}%{E}K')
o.append('\n')
for i in range(10):
    o.append(longl)
for i in range(20):
    o.append('\n' * 10)
for i in range(20):
    o.append(f'{E}10;5Habcdefgh{E}10;5H{E}4@{E}4P{E}12;1H{E}L{E}M')
o.append('\x0c' + blk * 20)
o.append(f'{E}1;31;44mbold red on blue{E}0m plain {E}7minverse{E}27m{E}38;5;200mx{E}48;2;1;2;3my{E}m\n')
save('conbench.bin', ''.join(o))

toks = [
    lambda r: ''.join(chr(r.randrange(32, 127)) for _ in range(r.randrange(1, 90))),
    lambda r: '\n', lambda r: '\r', lambda r: '\r\n', lambda r: '\n' * r.randrange(2, 40),
    lambda r: '\b', lambda r: '\t', lambda r: '\x07', lambda r: '\x0b', lambda r: '\x0c',
    lambda r: '\x7f', lambda r: '\x0e', lambda r: '\x0f',
    lambda r: f'{E}{r.randrange(0, 108)}m',
    lambda r: f'{E}{r.randrange(0, 8)};{r.randrange(30, 48)}m',
    lambda r: f'{E}38;5;{r.randrange(256)}m', lambda r: f'{E}48;2;{r.randrange(256)};{r.randrange(256)};{r.randrange(256)}m',
    lambda r: f'{E}m',
    lambda r: f'{E}{r.randrange(0, 40)};{r.randrange(0, 100)}H',
    lambda r: f'{E}{r.randrange(0, 3)}K', lambda r: f'{E}{r.randrange(0, 3)}J',
    lambda r: f'{E}{r.randrange(0, 9)}{r.choice("@PLMXSTABCDEFGd")}',
    lambda r: f'{E}{r.randrange(1, 20)};{r.randrange(2, 30)}r',
    lambda r: f'{E}?{r.choice(["7", "25", "6", "1049", "47"])}{r.choice("hl")}',
    lambda r: f'{E}4{r.choice("hl")}',
    lambda r: '\x1b' + r.choice('78DEMc=>'), lambda r: '\x1b(0' + 'lqkxjmtuvwn' + '\x1b(B',
    lambda r: '\x9b' + str(r.randrange(0, 40)) + r.choice('mHKJABCD'),
    lambda r: chr(r.randrange(0xa0, 0x100)), lambda r: chr(r.randrange(0x80, 0xa0)),
    lambda r: 'é漢字'.encode('utf-8').decode('latin-1'),
    lambda r: '\U0001F600'.encode('utf-8').decode('latin-1'),
    lambda r: f'{E}{r.randrange(0, 9)}',          # a CSI cut short by the next token
    lambda r: '\x1b',
    lambda r: f'{E}"q', lambda r: f'{E}>m',
]
for seed in range(1, 7):
    r = random.Random(seed)
    save(f'random{seed}.bin', ''.join(r.choice(toks)(r) for _ in range(6000)))
