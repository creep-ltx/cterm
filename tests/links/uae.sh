#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless for the links, drag and
# drop, fallback font and PANE tests (tests/links/): wasabid on port 1253
# (WASABI_PORT=1253); PC test servers on 2151-2159. The machine and the
# rest: tests/rig.sh.
RIG=ctlinks PORT=1253
. "$(dirname "$0")/../rig.sh"
rig "$@"
