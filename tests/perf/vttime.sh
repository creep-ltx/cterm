#!/usr/bin/env bash
# vttime.sh [N] [TEST...] - vt.c alone timed on the ctspeed rig (the
# A1200 + 8 MB fast, cycle-exact: the numbers repeat to the tick).
# VT=file: another vt.c (e.g. an older one) instead of this tree's.
# The rig must be up (tests/stock/bench.sh start).
D=$(cd "$(dirname "$0")/../.." && pwd)
export WASABI_HOST=127.0.0.1 WASABI_PORT=1254
H=$(amiga-test a1200-32-mui5 dir --as ctspeed --port 1254 --label AmigaOS3.2) || exit 1
vt=${VT:-$D/src/vt.c}
~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer -fbbb=- \
  -Wall -Wno-pointer-sign -I"$(dirname "$vt")/../include" -I"$D/include" -flto \
  "$D/tests/perf/vttime.c" "$vt" -o "$H/T/vttime" || exit 1
wasabi run --max-time 600 "SYS:T/vttime ${1:-200} ${*:2}"
