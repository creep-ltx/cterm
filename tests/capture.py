#!/usr/bin/env python3
"""capture.py OUT COLS ROWS KEYS -- COMMAND...

Run COMMAND in a pty of COLS x ROWS (TERM=xterm-256color), type KEYS
(Python string escapes; '~' waits a second), keep everything it prints
in OUT. Test streams for the emulator.
"""
import os, pty, select, sys, time, struct, fcntl, termios

out, cols, rows, keys = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
cmd = sys.argv[sys.argv.index("--") + 1:]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm-256color"
    os.environ["COLUMNS"], os.environ["LINES"] = str(cols), str(rows)
    os.execvp(cmd[0], cmd)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
data = bytearray()

def pump(secs):
    end = time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                b = os.read(fd, 65536)
            except OSError:
                return False
            if not b:
                return False
            data.extend(b)
    return True

pump(1.5)
for k in keys.encode().decode("unicode_escape"):
    if k == "~":
        pump(1.0)
    else:
        os.write(fd, k.encode("latin-1"))
        pump(0.05)
pump(2)
try:
    os.kill(pid, 9)
except OSError:
    pass
open(out, "wb").write(data)
print(out, len(data), "bytes")
