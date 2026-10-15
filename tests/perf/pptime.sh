#!/usr/bin/env bash
# pptime.sh [N] [chip] - the planar painter alone timed on the ctspeed rig
# (tests/perf/pptime.c). PPS=file: another ppaint.s (e.g. an older one);
# PPDEF="-DNOWRITE=1": assemble it with that (a part left out, to measure).
# The rig must be up (tests/stock/bench.sh start).
D=$(cd "$(dirname "$0")/../.." && pwd)
export WASABI_HOST=127.0.0.1 WASABI_PORT=1254
H=$(amiga-test a1200-32-mui5 dir --as ctspeed --port 1254 --label AmigaOS3.2) || exit 1
T=$(mktemp -d /tmp/pptime.XXXX)
vasmm68k_mot -Faout -m68020 -quiet $PPDEF -o "$T/ppaint.o" "${PPS:-$D/src/ppaint.s}" || exit 1
~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer -fbbb=- \
  -Wall -Wno-pointer-sign -I"$D/include" "$D/tests/perf/pptime.c" "$D/src/ppaint.c" \
  "$T/ppaint.o" -o "$H/T/pptime" || exit 1
rm -rf "$T"
wasabi run --max-time 600 "SYS:T/pptime ${1:-100} $2"
