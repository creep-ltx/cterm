#!/usr/bin/env bash
# run.sh start [ftpd.py options] | stop - tests/ftp/ftpd.py in the
# background on 127.0.0.1:$FTPPORT (default 2121), its pid in
# /tmp/ctftpd[-PORT].pid, its log in /tmp/ftpd[-PORT].log. Defaults:
# --root /tmp/ftproot1 (another port: /tmp/ftproot-PORT) --user tester
# --password secret. Each port its own server: sessions sharing the PC
# use their own ports and never stop each other's.
FP=${FTPPORT:-2121}
if [ "$FP" = 2121 ]; then S=; R=/tmp/ftproot1; else S=-$FP; R=/tmp/ftproot-$FP; fi
P=/tmp/ctftpd$S.pid
case "$1" in
start)
  shift
  [ -f "$P" ] && kill "$(cat "$P")" 2>/dev/null
  mkdir -p "$R"
  python3 -I "$(dirname "$0")/ftpd.py" --root "$R" --port "$FP" --user tester \
    --password secret --log "/tmp/ftpd$S.log" "$@" > "/tmp/ftpd$S.out" 2>&1 &
  echo $! > "$P"
  sleep 0.5; cat "/tmp/ftpd$S.out" ;;
stop)
  [ -f "$P" ] && kill "$(cat "$P")" 2>/dev/null; rm -f "$P" ;;
*) echo "usage: [FTPPORT=n] run.sh start [ftpd.py options] | stop" >&2; exit 2 ;;
esac
