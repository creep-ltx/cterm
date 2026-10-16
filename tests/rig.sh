# rig.sh - sourced by the tests' uae.sh scripts: a private FS-UAE on the
# shared clean test Amigas (~/UAE/TestMachines, `amiga-test`, README there).
# The caller sets RIG (the instance name), PORT (its wasabid) and may set
# CONF=030 (the 68030/50 with 32 MB more), SSL=1 (AmiSSL 5 in SYS:T/amissl,
# Assign LIBS: SYS:T/amissl ADD), BOOK=1 (the test FTP/SFTP places and the
# test sshd's keys), PICS=1 (the test pictures and ShowPic in SYS:T/CT/
# Pictures), EXTRA (FS-UAE lines, one per line). The machine is Workbench
# 3.2.1 + MUI 5 on an A1200 with 8 MB fast RAM (68EC020), its volume named
# AmigaOS3.2 as the tests expect. This tree's CTerm, CTermSSH, Parts/ and
# Protocols/ go into SYS:T/CT/ (put), csh as the system Shell (L:csh + useshell) when ~/Projects/CShell/csh
# is built (NOCSH=1: the ROM Shell only, on a fresh copy). Each rig its own emulator in its own systemd scope (2 GB at
# most); stop ends that one only.
D=$(cd "$(dirname "$0")/../.." && pwd)
M=a1200-32-mui5
[ "$CONF" = 030 ] && RIG=$RIG-030
A=(--as "$RIG" --port "$PORT" --label AmigaOS3.2)
X=(); [ "$CONF" = 030 ] && X=(--cpu 030)
while IFS= read -r l; do [ -n "$l" ] && X+=(--config "$l"); done <<< "${EXTRA:-}"
export WASABI_HOST=127.0.0.1 WASABI_PORT=$PORT
AMISSL="$HOME/FS-UAE/Hard Drives/AmigaOS3.2/T/amissl"

put() {
  H=$(amiga-test $M dir "${A[@]}") || exit 1
  mkdir -p "$H/T/CT/Parts" "$H/T/CT/Protocols"
  cp "$D/CTerm" "$H/T/CT/"
  [ -f "$D/CTermSSH" ] && cp "$D/CTermSSH" "$H/T/CT/"
  cp "$D"/Parts/* "$H/T/CT/Parts/"
  cp "$D"/Protocols/* "$H/T/CT/Protocols/"
  # csh 0.1b11+ is the system Shell only ("one csh"): L:csh with no e bit
  # (started as a command it waits for ever, or worse), `useshell L:csh`
  # at the top of S:User-Startup points "shell" and the BootShell at it -
  # so CTerm's tabs are csh themselves and no SHELL=csh is wanted
  rm -f "$H/C/csh" "$H/C/csh.uaem"
  if [ -z "$NOCSH" ] && [ -f "$HOME/Projects/CShell/csh" ] && [ -f "$HOME/Projects/CShell/useshell" ]; then
    mkdir -p "$H/L"
    cp "$HOME/Projects/CShell/csh" "$H/L/csh"
    printf -- '--p-rw-d %s 00:00:00.00 \n' "$(date +%F)" > "$H/L/csh.uaem"
    cp "$HOME/Projects/CShell/useshell" "$H/C/useshell"
    grep -q '^C:useshell L:csh' "$H/S/User-Startup" ||
      sed -i '1i C:useshell L:csh' "$H/S/User-Startup"
  fi
  if [ -n "$PICS" ]; then
    mkdir -p "$H/T/CT/Pictures"
    cp "$D"/tests/pictures/*.six "$D"/tests/pictures/*.png \
       "$D"/tests/pictures/*.iterm "$D"/tests/pictures/*.kitty "$D"/ShowPic \
       "$H/T/CT/Pictures/" 2>/dev/null
  fi
}
seed() {
  H=$(amiga-test $M dir "${A[@]}") || exit 1
  if [ -n "$SSL" ] && [ ! -d "$H/T/amissl" ]; then
    cp -a "$AMISSL" "$H/T/amissl"
    printf 'Assign LIBS: SYS:T/amissl ADD\n' >> "$H/S/User-Startup"
  fi
  [ -n "$BOOK" ] || return 0
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
rig() {
  case "$1" in
  start) seed; put; amiga-test $M start "${A[@]}" "${X[@]}" && echo up ;;
  put)   put ;;
  stop)  amiga-test $M stop "${A[@]}" ;;
  reset) amiga-test $M reset "${A[@]}" ;;
  *) echo "usage: uae.sh start|stop|put|reset" >&2; exit 2 ;;
  esac
}
