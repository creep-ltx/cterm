#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless for the panes tests: as
# tests/files/uae.sh (AmiSSL, the test places; CONF=030 the 68030/50), but
# its own copy and emulator, wasabid on port 1246 (WASABI_PORT=1246).
# The machine and the rest: tests/rig.sh.
RIG=ctpanes PORT=1246 SSL=1 BOOK=1
. "$(dirname "$0")/../rig.sh"
rig "$@"
