#!/usr/bin/env bash
# vtcount.sh [tests...] - 68020 instructions per conbench item in the
# emulator alone (vamos trace, N=24 minus N=8, so start-up cancels out)
cd "$(dirname "$0")/../.."
~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer -Iinclude \
  tests/perf/vtbench.c src/vt.c -o /tmp/vtbench || exit 1
cd /tmp
for t in ${@:-plain sgr perchar cursor frame nl eol}; do
  a=$(timeout 300 vamos -C 68020 -m 4096 -I vtbench $t 8 2>&1 | grep -c 'instr:')
  b=$(timeout 300 vamos -C 68020 -m 4096 -I vtbench $t 24 2>&1 | grep -c 'instr:')
  echo "$t $(( (b - a) / 16 ))"
done
