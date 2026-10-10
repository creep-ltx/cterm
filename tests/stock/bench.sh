#!/usr/bin/env bash
# bench.sh cterm|ccon NAME - cold-start FS-UAE A1200-Stock-net headless and
# run conbench REPS 3 SCALE 1 SYNC in a 77x30 window (the CCON race setup):
# in CTerm (tests this tree's CTerm) or in a CCON: window (the control).
H="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
kind=$1; name=${2:-$1}
# another session's emulator holds wasabi's port (127.0.0.1:1234): wait,
# never stop it
if pgrep -x fs-uae >/dev/null; then
  echo "bench.sh: an emulator is already running (another session?) - not starting" >&2
  exit 1
fi
cp "$(dirname "$0")/../../CTerm" "$H/T/CTerm"
printf 'FailAt 30\nStack 20000\nC:conbench %s TO SYS:T/cb-%s.txt REPS 3 SCALE 1 SYNC\nEndCLI\n' \
  "$kind" "$name" > "$H/S/xcb"; rm -f "$H/T/cb-$name.txt"
(cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen setsid fs-uae Configurations/A1200-Stock-net.fs-uae >/dev/null 2>&1 &)
sleep 2; pgrep -n -x fs-uae > /tmp/cterm-bench-uae.pid   # the newest: ours (none ran before)
sleep 20
until wasabi ping >/dev/null 2>&1; do sleep 2; done
if [ "$kind" = cterm ]; then
  wasabi run --max-time 5 'Run >NIL: SYS:T/CTerm COLS 77 ROWS 30 FROM S:xcb' >/dev/null
else
  wasabi run --max-time 5 'Run >NIL: NewShell "CCON:0/0/640/256/CCON/DEFAULTS" FROM S:xcb' >/dev/null
fi
for i in $(seq 1 200); do grep -q TOTAL "$H/T/cb-$name.txt" 2>/dev/null && break; sleep 5; done
sed -n '/window/p;/test /,$p' "$H/T/cb-$name.txt"
kill "$(cat /tmp/cterm-bench-uae.pid)" 2>/dev/null   # our own emulator only
