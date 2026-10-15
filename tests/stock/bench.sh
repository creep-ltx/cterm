#!/usr/bin/env bash
# bench.sh - conbench REPS 3 SCALE 1 SYNC in a 77x30 CTerm window (the CCON
# race set-up) on a private test Amiga: rig ctspeed, wasabid on port 1254,
# the shared clean a1200-32-mui5 (A1200, 68EC020 14 MHz, 2 MB chip + 8 MB
# fast, cycle-exact; CONF=030: the 68030/50 with 32 MB more, rig
# ctspeed-030) through tests/rig.sh. The rig stays up between runs, so
# builds can be run alternately in one boot (the only fair comparison:
# boots differ by up to 3%).
#   bench.sh start | stop | reset       the rig (start seeds conbench, CCON:
#                                       and a 16-colour PAL Hires Workbench)
#   bench.sh run cterm|ccon|nil NAME [CTERM ARGS]
#                                       one run (BIN=file: another CTerm build);
#                                       prints the result, the TOTAL last
#   bench.sh ab N BIN... [-- CTERM ARGS] N rounds, the builds in turn each
#                                       round; a table of TOTALs and the means
#   bench.sh tests N BIN...             the same, the mean per test per build
#   bench.sh prof TEST N [CTERM ARGS]   a PROFILE_ECLOCK build (BIN=, else made)
#                                       runs cbone TEST N: its time counters
# Starts the rig itself when it is not up. Stop it when done.
RIG=ctspeed PORT=1254
EXTRA="accuracy = 1"   # cycle-exact, as the old A1200-Stock configs
. "$(dirname "$0")/../rig.sh"
OLD="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"   # conbench and CCON come from here

seed_bench() {
  H=$(amiga-test $M dir "${A[@]}") || exit 1
  cp "$OLD/C/conbench" "$H/C/conbench"
  # CCON 1.2.8 (the yardstick), mounted as CCON: only
  if [ -f "$OLD/L/ccon-handler" ] && [ ! -f "$H/L/ccon-handler" ]; then
    cp "$OLD/L/ccon-handler" "$H/L/"; cp "$OLD/Devs/CCON-mountlist" "$H/Devs/"
    printf 'Mount CCON: from DEVS:CCON-mountlist\n' >> "$H/S/User-Startup"
  fi
  # the Workbench of the old stock runs: PAL Hires, 16 colours (4 planes)
  cp "$OLD/Prefs/Env-Archive/Sys/ScreenMode.prefs" "$H/Prefs/Env-Archive/Sys/"
}
up() {
  systemctl --user is-active --quiet "amiga-test-$RIG.scope" && return 0
  seed_bench; rig start >/dev/null || exit 1
  sleep 10   # Workbench settles (LoadWB, IPrefs)
}

# one run: prints conbench's lines from "window" and the table
run1() {
  local kind=$1 name=$2; shift 2; local extra="$*"
  H=$(amiga-test $M dir "${A[@]}") || exit 1
  cp "${BIN:-$D/CTerm}" "$H/T/CTerm"
  printf 'FailAt 30\nStack 20000\nC:conbench %s TO SYS:T/cb-%s.txt REPS 3 SCALE 1 SYNC\nEndCLI\n' \
    "$kind" "$name" > "$H/S/xcb"
  rm -f "$H/T/cb-$name.txt"
  case $kind in
  nil) wasabi run --max-time 900 "Stack 20000" >/dev/null
       wasabi run --max-time 900 "C:conbench nil TO SYS:T/cb-$name.txt REPS 3 SCALE 1 SYNC FORCE >NIL:" >/dev/null ;;
  cterm) wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm COLS 77 ROWS 30 FROM S:xcb $extra" >/dev/null ;;
  *) wasabi run --max-time 5 'Run >NIL: NewShell "CCON:0/0/640/256/CCON/DEFAULTS" FROM S:xcb' >/dev/null ;;
  esac
  local i
  for i in $(seq 1 300); do grep -q TOTAL "$H/T/cb-$name.txt" 2>/dev/null && break; sleep 3; done
  # the window gone (EndCLI ends CTerm's last tab) before the next run
  for i in $(seq 1 30); do
    wasabi windows 2>/dev/null | grep -q -E 'CTerm|CCON' || break; sleep 1
  done
  sleep 2
  sed -n '/window/p;/test /,$p' "$H/T/cb-$name.txt"
}

# prof TEST N: a PROFILE_ECLOCK build (BIN, else made here) runs cbone TEST N;
# CTerm's counters (E clock ticks, 709379 a second) from SYS:T/cterm.prof
prof() {
  H=$(amiga-test $M dir "${A[@]}") || exit 1
  if [ -z "$BIN" ]; then
    (cd "$D" && touch src/main.c && make CTerm PROFILE=1 PROFILE_ECLOCK=1 >/dev/null 2>&1) || exit 1
    cp "$D/CTerm" "$H/T/CTerm"; (cd "$D" && touch src/main.c && make CTerm >/dev/null 2>&1)
  else cp "$BIN" "$H/T/CTerm"; fi
  ~/opt/amiga/bin/m68k-amigaos-gcc -O2 -m68020 -noixemul -fomit-frame-pointer \
    "$D/tests/stock/cbone.c" -o "$H/T/cbone" || exit 1
  printf 'FailAt 30\nStack 20000\nSYS:T/cbone %s %s\nEndCLI\n' "$1" "${2:-1}" > "$H/S/xcb"
  rm -f "$H/T/cterm.prof"
  wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm COLS 77 ROWS 30 FROM S:xcb ${*:3}" >/dev/null
  local i
  for i in $(seq 1 300); do [ -s "$H/T/cterm.prof" ] && break; sleep 2; done
  sleep 2
  python3 -I - "$H/T/cterm.prof" <<'PY'
import sys, re
s = open(sys.argv[1]).read()
d = dict((k, int(v)) for k, v in re.findall(r'(\w+) (-?\d+)', s))
for k in ['all', 'wait', 'packets', 'vt', 'draw', 'scroll', 'text', 'fill', 'cursor', 'knob', 'rows',
          'ppset', 'ppasm', 'nrows', 'nall', 'nscroll', 'calib1000', 'draws', 'writes', 'ppnum']:
    v = d.get(k, 0)
    print(f"{k:9s} {v / 709379:8.3f} s" if k not in ('draws', 'writes', 'nrows', 'nall', 'nscroll', 'ppnum')
          else f"{k:9s} {v}")
print('(each clock read ~%.0f us; a painted row: set-up %.2f ms, asm %.2f ms)' % (
    d.get('calib1000', 0) / 709.379, d.get('ppset', 0) / 709.379 / max(1, d.get('ppnum', 1)),
    d.get('ppasm', 0) / 709.379 / max(1, d.get('ppnum', 1))))
PY
}

case "$1" in
start) up; echo up ;;
prof) up; shift; prof "$@" ;;
stop|reset) rig "$1" ;;
run) up; shift; run1 "$@" ;;
ab|tests)
  mode=$1 n=$2; shift 2; bins=()
  while [ $# -gt 0 ] && [ "$1" != -- ]; do bins+=("$1"); shift; done
  [ "$1" = -- ] && shift; extra="$*"
  up; out=$(mktemp -d /tmp/ctspeed-ab.XXXX)
  for r in $(seq 1 "$n"); do
    for b in "${bins[@]}"; do
      t=$(basename "$b"); BIN=$b run1 cterm "ab" $extra > "$out/$t.$r"
      printf '%-20s round %d: %s\n' "$t" "$r" "$(awk '/TOTAL/{print $3}' "$out/$t.$r")"
    done
  done
  python3 -I - "$mode" "$out" "$n" "${bins[@]}" <<'PY'
import sys, os, statistics as st
mode, out, n, bins = sys.argv[1], sys.argv[2], int(sys.argv[3]), [os.path.basename(b) for b in sys.argv[4:]]
def table(b, r):
    d = {}
    for l in open(f"{out}/{b}.{r}"):
        f = l.split()
        if len(f) >= 3 and f[0] != 'test' and not f[0].startswith('-'):
            try: d[f[0]] = float(f[2].rstrip('?'))
            except ValueError: pass
    return d
runs = {b: [table(b, r) for r in range(1, n + 1)] for b in bins}
if mode == 'ab':
    for b in bins:
        t = [x.get('TOTAL', 0) for x in runs[b]]
        print(f"{b:20s} " + " ".join(f"{v:6.2f}" for v in t) +
              f"   mean {st.mean(t):6.2f}  spread {max(t)-min(t):.2f}")
else:
    keys = list(runs[bins[0]][0].keys())
    print(f"{'test':14s}" + "".join(f"{b[:12]:>13s}" for b in bins))
    for k in keys:
        print(f"{k:14s}" + "".join(f"{st.mean(x.get(k, 0) for x in runs[b]):13.2f}" for b in bins))
PY
  ;;
*) sed -n '2,17p' "$0"; exit 2 ;;
esac
