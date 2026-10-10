#!/usr/bin/env bash
# prof.sh TEST [N] - cold-start FS-UAE stock headless, a PROFILE_ECLOCK
# build of this tree's CTerm runs cbone TEST N; prints CTerm's counters
# (E clock ticks, 709379 a second) from SYS:T/cterm.prof
H="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
D=$(cd "$(dirname "$0")/../.." && pwd)
(cd "$D" && touch src/main.c && make CTerm PROFILE=1 PROFILE_ECLOCK=1 >/dev/null) || exit 1
~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer "$D/tests/stock/cbone.c" -o "$H/T/cbone" || exit 1
pkill -x fs-uae; sleep 2
cp "$D/CTerm" "$H/T/CTerm"
printf 'FailAt 30\nStack 20000\nSYS:T/cbone %s %s\nEndCLI\n' "$1" "${2:-1}" > "$H/S/xcb"
rm -f "$H/T/cterm.prof"
(cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen setsid fs-uae Configurations/A1200-Stock-net.fs-uae >/dev/null 2>&1 &)
sleep 20
until wasabi ping >/dev/null 2>&1; do sleep 2; done
wasabi run --max-time 5 'Run >NIL: SYS:T/CTerm COLS 77 ROWS 30 FROM S:xcb' >/dev/null
for i in $(seq 1 200); do [ -s "$H/T/cterm.prof" ] && break; sleep 3; done
sleep 1
python3 - "$H/T/cterm.prof" <<'PY'
import sys,re
s=open(sys.argv[1]).read()
d=dict((k,int(v)) for k,v in re.findall(r'(\w+) (-?\d+)',s))
for k in ['wait','packets','vt','draw','scroll','text','fill','cursor','knob','rows','draws','writes']:
    v=d.get(k,0)
    print(f"{k:8s} {v/709379:8.2f} s" if k not in ('draws','writes') else f"{k:8s} {v}")
PY
(cd "$D" && touch src/main.c && make CTerm >/dev/null)
