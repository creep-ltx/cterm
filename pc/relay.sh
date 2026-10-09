#!/usr/bin/env bash
# relay.sh start [COMMAND...] | stop - the relay in the background, for tests.
# Stops by the process number the relay wrote, never by name.
here=$(cd "$(dirname "$0")/.." && pwd)
pidfile="${XDG_RUNTIME_DIR:-/tmp}/cterm-relay.pid"
case "$1" in
stop)
    [ -f "$pidfile" ] && kill "$(cat "$pidfile")" 2>/dev/null
    rm -f "$pidfile"
    ;;
start)
    shift
    "$0" stop
    nohup "$here/.venv/bin/python" "$here/pc/cterm-relay.py" -- "${@:-bash}" \
        > /tmp/relay.log 2>&1 &
    ;;
*) echo "usage: relay.sh start [COMMAND...] | stop" >&2; exit 2 ;;
esac
