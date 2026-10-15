#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless whose serial port is a TCP
# socket on 127.0.0.1:5555 (FS-UAE listens; tests/serial/fakemodem.py
# connects), its own copy and emulator, wasabid on port 1248
# (WASABI_PORT=1248). This tree's CTerm and protocols in SYS:T/CT/.
# UAE_EXTRA: more FS-UAE lines. The rest: tests/rig.sh.
RIG=ctser PORT=1248
EXTRA="serial_port = tcp://127.0.0.1:5555${UAE_EXTRA:+
$UAE_EXTRA}"
. "$(dirname "$0")/../rig.sh"
rig "$@"
