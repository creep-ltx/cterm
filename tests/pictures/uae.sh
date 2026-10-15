#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless for the pictures' tests:
# its own copy and emulator, wasabid on port 1250 (WASABI_PORT=1250).
# put: this tree's CTerm, Parts/, Protocols/ and the test pictures with
# ShowPic into SYS:T/CT/ (also done by start). CONF=030: the 68030/50 with
# 32 MB more; EXTRA: more FS-UAE lines (e.g. RTG). The rest: tests/rig.sh.
RIG=ctpics PORT=1250 PICS=1
. "$(dirname "$0")/../rig.sh"
rig "$@"
