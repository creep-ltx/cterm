#!/usr/bin/env bash
# run.sh start [ftpd.py options] | stop - tests/ftp/ftpd.py in the
# background on 127.0.0.1:2121, its pid in /tmp/ctftpd.pid, its log in
# /tmp/ftpd.log. Defaults: --root /tmp/ftproot1 --user tester
# --password secret.
P=/tmp/ctftpd.pid
case "$1" in
start)
  shift
  [ -f $P ] && kill "$(cat $P)" 2>/dev/null
  python3 "$(dirname "$0")/ftpd.py" --root /tmp/ftproot1 --user tester \
    --password secret --log /tmp/ftpd.log "$@" > /tmp/ftpd.out 2>&1 &
  echo $! > $P
  sleep 0.5; cat /tmp/ftpd.out ;;
stop)
  [ -f $P ] && kill "$(cat $P)" 2>/dev/null; rm -f $P ;;
esac
