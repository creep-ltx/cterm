#!/usr/bin/env bash
# test-sshd.sh start|stop - a private sshd for CTerm's ssh tests: your own
# user, 127.0.0.1:2299, throwaway keys in /tmp/cterm-sshd (client key:
# id_ed25519 there), key logins only; SFTP too. Recipe: Knowledge/go/tui-and-sockets.md.
# For the real A1200: SSHD_ADDR=<the PC's LAN address> SSHD_PORT=2390 (the firewall lets
# only the A1200 in on 2390-2399).
d=/tmp/cterm-sshd
port=${SSHD_PORT:-2299}
addr=${SSHD_ADDR:-127.0.0.1}
case "$1" in
stop) [ -f $d/pid ] && kill "$(cat $d/pid)" 2>/dev/null; rm -f $d/pid ;;
start)
  mkdir -p $d && chmod 700 $d
  [ -f $d/host_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -f $d/host_ed25519
  [ -f $d/id_ed25519 ] || ssh-keygen -q -t ed25519 -N '' -C cterm-test -f $d/id_ed25519
  [ -f $d/id_rsa ] || ssh-keygen -q -t rsa -b 2048 -m PEM -N '' -C cterm-test -f $d/id_rsa
  cat $d/id_ed25519.pub $d/id_rsa.pub > $d/authorized_keys
  cat > $d/sshd_config <<CFG
Port $port
ListenAddress $addr
HostKey $d/host_ed25519
PidFile $d/pid
AuthorizedKeysFile $d/authorized_keys
UsePAM no
StrictModes no
PasswordAuthentication no
KbdInteractiveAuthentication no
LogLevel VERBOSE
Subsystem sftp internal-sftp
CFG
  /usr/bin/sshd -f $d/sshd_config -E $d/log && echo "sshd on $addr:$port as $USER" ;;
*) echo "usage: test-sshd.sh start|stop" >&2; exit 2 ;;
esac
