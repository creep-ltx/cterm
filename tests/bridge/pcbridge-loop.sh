#!/usr/bin/env bash
# pcbridge-loop.sh AMIGA PORT SERVICE_HOST SERVICE_PORT - pcbridge.py again
# after each session (amibridge serves one pair after another)
while true; do python3 "$(dirname "$0")/pcbridge.py" "$@" || sleep 2; done
