#!/usr/bin/env bash
# look.sh [ARGS] - cold-start FS-UAE A1200-Stock-net headless, type a test
# page of colours, styles, box lines and blocks in this tree's CTerm (with
# ARGS, e.g. TEXTDRAW), grab the window: prints the PNG's path.
# (Planar, as stock, but with fast RAM for the grab.)
H="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
D=$(cd "$(dirname "$0")/../.." && pwd)
if pgrep -x fs-uae >/dev/null; then
  echo "look.sh: an emulator is already running (another session?) - not starting" >&2
  exit 1
fi
cp "$D/CTerm" "$H/T/CTerm"
python3 "$D/tests/stock/lookpage.py" > "$H/T/look.txt"
printf 'Type SYS:T/look.txt\n' > "$H/S/xlook"
# 8 MB of fast RAM: a stock machine has no room for the grab
(cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen setsid fs-uae "$D/tests/stock/A1200-Stock-fast.fs-uae" >/dev/null 2>&1 &)
sleep 2; pid=$(pgrep -n -x fs-uae)
sleep 20
until wasabi ping >/dev/null 2>&1; do sleep 2; done
wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm COLS 77 ROWS 28 FROM S:xlook $*" >/dev/null
wasabi wait window CTerm --timeout 60 >/dev/null
sleep 15
out=$(wasabi grab --window CTerm)
cp "$out" "/tmp/look-${1:-pp}.png"; echo "/tmp/look-${1:-pp}.png"
kill "$pid" 2>/dev/null
