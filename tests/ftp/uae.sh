#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless for the FTP extras' tests
# (local times, active FTP, upload resume, whole drawers, FTPS): its own
# rig, wasabid on port 1251 (WASABI_PORT=1251), AmiSSL, the emulated
# bsdsocket (127.0.0.1 is the PC: FTPPORT=2131 tests/ftp/run.sh start).
# put: this tree's CTerm, Parts/ and Protocols/ into SYS:T/CT/ (also done
# by start). The machine and the rest: tests/rig.sh.
RIG=ctftp PORT=1251 SSL=1 BOOK=1
. "$(dirname "$0")/../rig.sh"
rig "$@"
