#!/usr/bin/env bash
# uae.sh start|stop - FS-UAE headless (an A1200 with 8 MB fast RAM, the
# emulated bsdsocket for wasabi) whose serial port is a TCP socket on
# 127.0.0.1:5555 (FS-UAE listens; tests/serial/fakemodem.py connects).
# A private copy of the stock drive in /tmp/ctser, this tree's CTerm and
# protocols in SYS:T/CT/. Never stops an emulator it did not start.
D=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/ctser; H="$W/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
case "$1" in
start)
  if pgrep -x fs-uae >/dev/null; then
    echo "uae.sh: an emulator is already running (another session?) - not starting" >&2
    exit 1
  fi
  mkdir -p "$W"
  [ -d "$H" ] || cp -a "$HOME/FS-UAE/Hard Drives/AmigaOS3.2" "$W/"
  sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
      "$D/tests/stock/A1200-Stock-fast.fs-uae" > "$W/ctser.fs-uae"
  echo "serial_port = tcp://127.0.0.1:5555" >> "$W/ctser.fs-uae"
  [ -n "$UAE_EXTRA" ] && echo "$UAE_EXTRA" >> "$W/ctser.fs-uae"
  mkdir -p "$H/T/CT/Protocols"
  cp "$D/CTerm" "$H/T/CT/CTerm"
  cp "$D"/Protocols/* "$H/T/CT/Protocols/"
  (cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen setsid systemd-run --user --scope \
     -p MemoryMax=2G fs-uae "$W/ctser.fs-uae" >"$W/uae.out" 2>&1 &)
  sleep 3; pgrep -n -x fs-uae > "$W/uae.pid"
  for i in $(seq 1 60); do wasabi ping >/dev/null 2>&1 && break; sleep 2; done
  wasabi ping >/dev/null 2>&1 || { echo "uae.sh: no wasabi" >&2; exit 1; }
  echo "up (pid $(cat "$W/uae.pid"))"
  ;;
stop)
  pid=$(cat "$W/uae.pid" 2>/dev/null)
  [ -n "$pid" ] && kill "$pid" 2>/dev/null
  while [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; do sleep 1; done
  rm -f "$W/uae.pid"
  ;;
esac
