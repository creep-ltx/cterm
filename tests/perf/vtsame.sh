#!/usr/bin/env bash
# vtsame.sh [REF] - the parser of this tree against REF's (default HEAD),
# both built for the 68020 (the asm fast paths) and run under vamos:
# every cell's look, the cursor and the changes to draw must be the same
# over the test streams and tests/perf/mkstress.py's, fed whole and in
# chunks of 1, 7 and 64 bytes, in UTF-8, the Amiga Shell's Latin-1 and
# code page 437. Also this tree's 68020 build against its own PC build
# (the C fallback of the asm). Prints the differing runs; exit 1 if any.
# QUICK=1: chunks of 7 and whole only.
D=$(cd "$(dirname "$0")/../.." && pwd)
ref=${1:-HEAD}
T=$(mktemp -d /tmp/vtsame.XXXX)
mkdir -p "$T/old/src" "$T/s"
(cd "$D" && git show "$ref:src/vt.c") > "$T/old/src/vt.c" || exit 1
(cd "$D" && git show "$ref:src/vt.h") > "$T/old/src/vt.h" || exit 1
GCC=~/opt/amiga/bin/m68k-amigaos-gcc
F="-O2 -m68020 -noixemul -fomit-frame-pointer -fbbb=- -w -flto"
$GCC $F -I"$T/old/src" "$D/tests/vtlooks.c" "$T/old/src/vt.c" -o "$T/old.68k" || exit 1
$GCC $F -I"$D/src" "$D/tests/vtlooks.c" "$D/src/vt.c" -o "$T/new.68k" || exit 1
gcc -O2 -w -o "$T/new.pc" "$D/tests/vtlooks.c" "$D/src/vt.c" || exit 1
python3 -I "$D/tests/perf/mkstress.py" "$T/s"
cp "$D"/tests/streams/*.bin "$D"/tests/lines/*.bin "$T/s/"
chunks="0 1 7 64"; [ -n "$QUICK" ] && chunks="0 7"
bad=0 runs=0
cd "$T/s" || exit 1          # vamos: a path with a leading / is the parent dir
for f in "$T"/s/*.bin; do
  b=$(basename "$f" .bin); geo=${b##*-}
  [[ $geo =~ ^[0-9]+x[0-9]+$ ]] || geo=76x28
  c=${geo%x*} r=${geo#*x}
  for mode in utf8 amiga cp437; do
    for chunk in $chunks; do
      check=5; [ "$chunk" = 0 ] && check=1; [ "$chunk" = 1 ] && check=23
      args=("$c" "$r" "$b.bin" "$chunk" "$mode" "$check")
      run() { vamos -C 68020 -m 8000 -s 64 "$1" "${args[@]}" 2>/dev/null; }
      run "$T/old.68k" > "$T/o"; run "$T/new.68k" > "$T/n"
      # vamos runs at the same time (another test) can fail one: again
      cmp -s "$T/o" "$T/n" || { run "$T/old.68k" > "$T/o"; run "$T/new.68k" > "$T/n"; }
      "$T/new.pc" "${args[@]}" > "$T/p" 2>&1
      runs=$((runs + 1))
      if ! cmp -s "$T/o" "$T/n"; then
        echo "DIFF 68k old/new: $b $mode chunk $chunk"; bad=1
        diff "$T/o" "$T/n" | head -5
      fi
      if ! cmp -s "$T/n" "$T/p"; then
        echo "DIFF 68k/PC: $b $mode chunk $chunk"; bad=1
        diff "$T/n" "$T/p" | head -5
      fi
    done
  done
done
echo "vtsame: $runs runs against $ref: $([ $bad = 0 ] && echo 'all the same' || echo 'DIFFERENCES')"
rm -rf "$T"
exit $bad
