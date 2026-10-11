#!/usr/bin/env bash
# uae.sh start|stop|put - FS-UAE headless for the panes tests: as
# tests/files/uae.sh (an A1200 with 8 MB fast RAM, CONF=030 the 030/50,
# bsdsocket on), but a private drive in /tmp/ctpanes whose wasabid
# listens on port 1246 (WASABI_PORT=1246), its own systemd scope.
D=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/ctpanes${CONF:+-$CONF}; H="$W/AmigaOS3.2"; U=ctpanes-uae
export WASABI_HOST=127.0.0.1 WASABI_PORT=1246
put() {
  mkdir -p "$H/T/CT/Parts" "$H/T/CT/Protocols"
  cp "$D/CTerm" "$D/CTermSSH" "$H/T/CT/"
  cp "$D"/Parts/* "$H/T/CT/Parts/"
  cp "$D"/Protocols/* "$H/T/CT/Protocols/"
}
seed() {
  E="$H/Prefs/Env-Archive/CTerm"; mkdir -p "$E"
  if [ -f /tmp/cterm-sshd/id_ed25519 ]; then
    cp /tmp/cterm-sshd/id_ed25519 "$E/id_ed25519"
    echo "127.0.0.1:2299 $(ssh-keygen -lf /tmp/cterm-sshd/host_ed25519.pub | cut -d' ' -f2)" > "$E/known_hosts"
  fi
  grep -q "name=Test FTP" "$E/phonebook" 2>/dev/null || cat >> "$E/phonebook" <<'BOOK'

name=Test FTP
host=127.0.0.1
port=2121
kind=ftp
user=tester
pass=secret

name=Test SFTP
host=127.0.0.1
port=2299
kind=sftp
user=creep
dir=/tmp/sftptest1
BOOK
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
    sed -i -e 's#^  Run >NIL: C:wasabid$#  Run >NIL: C:wasabid 1246#' "$H/S/User-Startup"
    grep -q "wasabid 1246" "$H/S/User-Startup" || { echo "uae.sh: User-Startup not patched" >&2; exit 1; }
    printf 'Assign LIBS: SYS:T/amissl ADD\n' >> "$H/S/User-Startup"
    seed
  fi
  if [ "$CONF" = 030 ]; then
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$HOME/FS-UAE/Configurations/A1200-030-50.fs-uae" > "$W/ctpanes.fs-uae"
    echo "bsdsocket_library = 1" >> "$W/ctpanes.fs-uae"
  else
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$D/tests/stock/A1200-Stock-fast.fs-uae" > "$W/ctpanes.fs-uae"
  fi
  put
  (cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen systemd-run --user --scope --unit=$U \
     -p MemoryMax=2G fs-uae "$W/ctpanes.fs-uae" >"$W/uae.out" 2>&1 &)
  for i in $(seq 1 60); do wasabi ping >/dev/null 2>&1 && break; sleep 2; done
  wasabi ping >/dev/null 2>&1 || { echo "uae.sh: no wasabi on 1240" >&2; exit 1; }
  echo "up"
  ;;
put) put ;;
stop)
  systemctl --user stop $U.scope 2>/dev/null
  while systemctl --user is-active --quiet $U.scope; do sleep 1; done
  ;;
esac
