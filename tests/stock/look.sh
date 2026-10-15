#!/usr/bin/env bash
# look.sh [NAME] [CTERM ARGS] - on the ctspeed rig (tests/stock/bench.sh's,
# started if not up): type a test page of colours, styles, box lines and
# blocks (lookpage.py) in this tree's CTerm (BIN=file: another build; ARGS
# e.g. TEXTDRAW), then grab the window; prints the PNG's path
# (/tmp/look-NAME.png). Two builds' grabs must be the same pixels:
#   look.sh a; BIN=other look.sh b; cmp /tmp/look-a.png /tmp/look-b.png
# (or compare them with tests/stock/samepng.py when the files differ).
# SCROLL=1: the page twice more, so the grab is of scrolled rows; SCROLL=2:
# colours flooded off by plain lines, single lines, colours again; SCROLL=3:
# the grab after the single lines (after a flood took the colours off).
# MOVE=n: a 64x24 window dragged n pixels right before the text comes.
D=$(cd "$(dirname "$0")/../.." && pwd)
bash "$D/tests/stock/bench.sh" start >/dev/null || exit 1
export WASABI_HOST=127.0.0.1 WASABI_PORT=1254
H=$(amiga-test a1200-32-mui5 dir --as ctspeed --port 1254 --label AmigaOS3.2) || exit 1
name=${1:-pp}; shift
cp "${BIN:-$D/CTerm}" "$H/T/CTerm"
python3 -I "$D/tests/stock/lookpage.py" > "$H/T/look.txt"
seq 1 40 | sed 's/^/a plain line of text, number /' > "$H/T/plain.txt"
if [ "$SCROLL" = 2 ]; then
  # colours scrolled away by a flood (the plane mask may shrink), single
  # lines scrolled after it, colours again, part of them scrolled off
  { printf 'Prompt "> "\nType SYS:T/look.txt\nType SYS:T/plain.txt\n'
    for i in 1 2 3 4 5; do printf 'Echo "one more line %s"\n' "$i"; done
    printf 'Type SYS:T/look.txt\nEcho "x"\nEcho "y"\nType SYS:T/plain.txt\n'
    printf 'Type SYS:T/look.txt\n'
    for i in 1 2 3 4 5 6; do printf 'Echo "and %s"\n' "$i"; done; } > "$H/S/xlook"
elif [ "$SCROLL" = 3 ]; then
  # colours flooded off, then single lines: the grab after them
  { printf 'Prompt "> "\nType SYS:T/look.txt\nType SYS:T/plain.txt\n'
    for i in 1 2 3 4 5; do printf 'Echo "one more line %s"\n' "$i"; done; } > "$H/S/xlook"
elif [ -n "$SCROLL" ]; then
  printf 'Prompt "> "\nType SYS:T/look.txt\nType SYS:T/look.txt\nType SYS:T/look.txt\n' > "$H/S/xlook"
elif [ -n "$MOVE" ]; then
  # a narrower window dragged MOVE pixels right first: the text off the
  # 32-pixel grid, so the row's edge cells go to Text() and the margin
  # cannot always come with the painter
  printf 'Prompt "> "\nWait 8\nType SYS:T/look.txt\nType SYS:T/plain.txt\nType SYS:T/look.txt\n' > "$H/S/xlook"
else
  printf 'Prompt "> "\nType SYS:T/look.txt\n' > "$H/S/xlook"
fi
size="COLS 77 ROWS 28"; [ -n "$MOVE" ] && size="COLS 64 ROWS 24"
wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm $size FROM S:xlook $*" >/dev/null
wasabi wait window CTerm --timeout 60 >/dev/null
if [ -n "$MOVE" ]; then
  sleep 2                               # the title bar (grab's pixels)
  wasabi mouse down 200 28 >/dev/null; wasabi mouse move $((200 + MOVE)) 30 >/dev/null
  wasabi mouse up >/dev/null
  sleep 25
else
  sleep 12
fi
out=$(wasabi grab --window CTerm)
cp "$out" "/tmp/look-$name.png"; echo "/tmp/look-$name.png"
# quit it: Ctrl-C (CTerm quits on it; ARexx's QUIT needs Parts/ beside it)
for a in $(wasabi ps 2>/dev/null | awk '/SYS:T\/CTerm/ {print $1}'); do
  wasabi kill "$a" >/dev/null 2>&1
done
sleep 3
