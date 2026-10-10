#!/usr/bin/env python3
"""pcbridge.py AMIGA_HOST AMIGA_PORT SERVICE_HOST SERVICE_PORT - the PC half
of amibridge: calls the Amiga (allowed out through the PC's firewall) and
a service on the PC (once amibridge says an Amiga program is there), and joins them."""
import select, socket, sys
a = socket.create_connection((sys.argv[1], int(sys.argv[2])))
# wait for amibridge's "go" (an Amiga program has come), then call the
# service: an idle early call is dropped by sshd after LoginGraceTime
if a.recv(1) != b"\x01":
    sys.exit(1)
s = socket.create_connection((sys.argv[3], int(sys.argv[4])))
print("pcbridge: joined", flush=True)
while True:
    r, _, _ = select.select([a, s], [], [])
    for x in r:
        d = x.recv(65536)
        if not d:
            sys.exit(0)
        (s if x is a else a).sendall(d)
