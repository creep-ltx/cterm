#!/usr/bin/env bash
# uae.sh start|stop|put - FS-UAE headless for the pictures' tests: the
# A1200 with 8 MB fast RAM (CONF=030: the 68030/50 with 32 MB), a
# private copy of the drive in /tmp/ctpics-uae whose wasabid listens on
# port 1250 (WASABI_PORT=1250), so other sessions' emulators are never
# touched. put: this tree's CTerm, Parts/, Protocols/ and the test
# pictures into SYS:T/CT/ (also done by start). The emulator runs in a
# systemd scope of its own (2 GB at most); stop ends that scope only.
D=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/ctpics-uae${CONF:+-$CONF}; H="$W/AmigaOS3.2"; U=ctpics-uae${CONF:+-$CONF}
export WASABI_HOST=127.0.0.1 WASABI_PORT=1250
put() {
  mkdir -p "$H/T/CT/Parts" "$H/T/CT/Protocols" "$H/T/CT/Pictures"
  cp "$D/CTerm" "$H/T/CT/"
  cp "$D"/Parts/* "$H/T/CT/Parts/"
  cp "$D"/Protocols/* "$H/T/CT/Protocols/"
  cp "$D"/tests/pictures/*.six "$D"/tests/pictures/*.png \
     "$D"/tests/pictures/*.iterm "$D"/ShowPic "$H/T/CT/Pictures/" 2>/dev/null
}
case "$1" in
start)
  if systemctl --user is-active --quiet $U.scope; then
    echo "uae.sh: ours runs already"; exit 0
  fi
  mkdir -p "$W"
  if [ ! -d "$H" ]; then
    src="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"
    [ "$CONF" = 030 ] && src="$HOME/FS-UAE/Hard Drives/AmigaOS3.2-030"
    cp -a "$src" "$H"
    sed -i -e 's#^  Run >NIL: C:wasabid$#  Run >NIL: C:wasabid 1250#' "$H/S/User-Startup"
    grep -q "wasabid 1250" "$H/S/User-Startup" || { echo "uae.sh: User-Startup not patched" >&2; exit 1; }
  fi
  if [ "$CONF" = 030 ]; then
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$HOME/FS-UAE/Configurations/A1200-030-50.fs-uae" > "$W/ctpics.fs-uae"
    echo "bsdsocket_library = 1" >> "$W/ctpics.fs-uae"
  else
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$D/tests/stock/A1200-Stock-fast.fs-uae" > "$W/ctpics.fs-uae"
  fi
  [ -n "$EXTRA" ] && printf '%s\n' "$EXTRA" >> "$W/ctpics.fs-uae"   # e.g. RTG
  put
  (cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen systemd-run --user --scope --unit=$U \
     -p MemoryMax=2G fs-uae "$W/ctpics.fs-uae" >"$W/uae.out" 2>&1 &)
  for i in $(seq 1 60); do wasabi ping >/dev/null 2>&1 && break; sleep 2; done
  wasabi ping >/dev/null 2>&1 || { echo "uae.sh: no wasabi on 1250" >&2; exit 1; }
  echo "up"
  ;;
put) put ;;
stop)
  systemctl --user stop $U.scope 2>/dev/null
  while systemctl --user is-active --quiet $U.scope; do sleep 1; done
  ;;
esac
