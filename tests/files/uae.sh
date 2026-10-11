#!/usr/bin/env bash
# uae.sh start|stop|put - FS-UAE headless for the file browser's tests: an
# A1200 with 8 MB fast RAM (CONF=030: the 68030/50 with 32 MB), the
# emulated bsdsocket (127.0.0.1 is the PC: tests/ftp/ftpd.py on 2121,
# tests/ssh/test-sshd.sh on 2299), a private copy of the drive in
# /tmp/ctfiles whose wasabid listens on port 1240 (WASABI_PORT=1240), so
# other sessions' emulators on 1234 are never touched. AmiSSL from
# SYS:T/amissl. put: this tree's CTerm, CTermSSH, Parts/ and Protocols/
# into SYS:T/CT/ (also done by start). The drive is seeded with the test
# places "Test FTP" and "Test SFTP" (/tmp/sftptest1 on the PC), the test
# sshd's client key and host key (tests/ssh/test-sshd.sh start first).
# The emulator runs in a systemd scope of its own (2 GB at most); stop
# ends that scope only.
D=$(cd "$(dirname "$0")/../.." && pwd)
W=/tmp/ctfiles${CONF:+-$CONF}; H="$W/AmigaOS3.2"; U=ctfiles-uae
export WASABI_HOST=127.0.0.1 WASABI_PORT=1240
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
    sed -i -e 's#^  Run >NIL: C:wasabid$#  Run >NIL: C:wasabid 1240#' "$H/S/User-Startup"
    grep -q "wasabid 1240" "$H/S/User-Startup" || { echo "uae.sh: User-Startup not patched" >&2; exit 1; }
    printf 'Assign LIBS: SYS:T/amissl ADD\n' >> "$H/S/User-Startup"
    seed
  fi
  if [ "$CONF" = 030 ]; then
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$HOME/FS-UAE/Configurations/A1200-030-50.fs-uae" > "$W/ctfiles.fs-uae"
    echo "bsdsocket_library = 1" >> "$W/ctfiles.fs-uae"
  else
    sed -e "s#^hard_drive_0 = .*#hard_drive_0 = $H#" -e '/^hard_drive_1/d' \
        "$D/tests/stock/A1200-Stock-fast.fs-uae" > "$W/ctfiles.fs-uae"
  fi
  put
  (cd ~/FS-UAE && SDL_VIDEODRIVER=offscreen systemd-run --user --scope --unit=$U \
     -p MemoryMax=2G fs-uae "$W/ctfiles.fs-uae" >"$W/uae.out" 2>&1 &)
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
