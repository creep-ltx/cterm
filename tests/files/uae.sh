#!/usr/bin/env bash
# uae.sh start|stop|put|reset - FS-UAE headless for the file browser's tests
# (and the shared rig other sessions use): wasabid on port 1240
# (WASABI_PORT=1240), AmiSSL, the emulated bsdsocket (127.0.0.1 is the PC:
# tests/ftp/ftpd.py on 2121, tests/ssh/test-sshd.sh on 2299 - start it
# first), the test places "Test FTP" and "Test SFTP" (/tmp/sftptest1) and
# the test sshd's keys. CONF=030: the 68030/50 with 32 MB more. put: this
# tree's CTerm, CTermSSH, Parts/ and Protocols/ into SYS:T/CT/ (also done
# by start). The machine and the rest: tests/rig.sh.
RIG=ctfiles PORT=1240 SSL=1 BOOK=1
. "$(dirname "$0")/../rig.sh"
rig "$@"
