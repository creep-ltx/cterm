#!/usr/bin/env bash
# prof.sh TEST [N] [CTERM ARGS] - cold-start FS-UAE stock headless, a PROFILE_ECLOCK
# build of this tree's CTerm runs cbone TEST N; prints CTerm's counters
# (E clock ticks, 709379 a second) from SYS:T/cterm.prof
H="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
D=$(cd "$(dirname "$0")/../.." && pwd)
(cd "$D" && touch src/main.c && make CTerm PROFILE=1 PROFILE_ECLOCK=1 >/dev/null) || exit 1
~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer "$D/tests/stock/cbone.c" -o "$H/T/cbone" || exit 1
# another session's emulator holds wasabi's port (127.0.0.1:1234): wait,
# never stop it
if pgrep -x fs-uae >/dev/null; then
  echo "$0: an emulator is already running (another session?) - not starting" >&2
  exit 1
fi
cp "$D/CTerm" "$H/T/CTerm"
printf 'FailAt 30\nStack 20000\nSYS:T/cbone %s %s\nEndCLI\n' "$1" "${2:-1}" > "$H/S/xcb"
rm -f "$H/T/cterm.prof"
# CONF=fast: the same A1200 with 8 MB of fast RAM (an accelerated machine)
cf=Configurations/A1200-Stock-net.fs-uae
[ "$CONF" = fast ] && cf="$(cd "$(dirname "$0")" && pwd)/A1200-Stock-fast.fs-uae"
(cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen setsid fs-uae "$cf" >/dev/null 2>&1 &)
sleep 2; pgrep -n -x fs-uae > /tmp/cterm-bench-uae.pid   # the newest: ours (none ran before)
sleep 20
until wasabi ping >/dev/null 2>&1; do sleep 2; done
extra="${*:3}"
wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm COLS 77 ROWS 30 FROM S:xcb $extra" >/dev/null
for i in $(seq 1 200); do [ -s "$H/T/cterm.prof" ] && break; sleep 3; done
sleep 1
python3 - "$H/T/cterm.prof" <<'PY'
import sys,re
s=open(sys.argv[1]).read()
d=dict((k,int(v)) for k,v in re.findall(r'(\w+) (-?\d+)',s))
for k in ['wait','packets','vt','draw','scroll','text','fill','cursor','knob','rows','nrows','nall','nscroll','calib1000','draws','writes']:
    v=d.get(k,0)
    print(f"{k:8s} {v/709379:8.2f} s" if k not in ('draws','writes','nrows','nall','nscroll') else f"{k:8s} {v}")
PY
(cd "$D" && touch src/main.c && make CTerm >/dev/null)
pid=$(cat /tmp/cterm-bench-uae.pid); kill "$pid" 2>/dev/null   # our own emulator only
while kill -0 "$pid" 2>/dev/null; do sleep 1; done   # gone before the next run starts
