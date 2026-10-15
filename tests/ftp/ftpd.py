#!/usr/bin/env python3
"""ftpd.py - a small FTP server for CTerm's tests (Python stdlib only).

Serves one directory to 127.0.0.1: USER/PASS (one user, or anonymous),
PWD, CWD, CDUP, TYPE, PASV, EPSV, PORT, EPRT, LIST, MLSD, NLST, RETR and
STOR (with REST), APPE, MKD, RMD, DELE, RNFR/RNTO, SIZE, MDTM, FEAT,
SYST, NOOP, ABOR, QUIT.

  ftpd.py --root DIR [--port 2121] [--user U --password P] [--rate KB]
          [--nomlsd] [--noepsv] [--nopasv] [--norest] [--log FILE]

--rate slows RETR and STOR down (KB a second) so a test can stop a
transfer in the middle; --nomlsd and --noepsv answer 500 to those, to
test LIST parsing and PASV; --nopasv refuses PASV and EPSV (active FTP
only: PORT); --norest refuses REST (an upload goes on with APPE).
Never give it a directory you care about: clients may delete in it.
"""
import argparse
import os
import socket
import socketserver
import stat
import sys
import threading
import time

ARGS = None
LOG = None
LOCK = threading.Lock()


def log(*a):
    if LOG:
        with LOCK:
            LOG.write(time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a) + "\n")
            LOG.flush()


MONTHS = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec".split()


def ls_line(name, st):
    mode = st.st_mode
    kind = "d" if stat.S_ISDIR(mode) else "l" if stat.S_ISLNK(mode) else "-"
    perms = ""
    for who in (6, 3, 0):
        bits = (mode >> who) & 7
        perms += ("r" if bits & 4 else "-") + ("w" if bits & 2 else "-") + ("x" if bits & 1 else "-")
    t = time.localtime(st.st_mtime)
    if time.time() - st.st_mtime > 180 * 86400:
        when = "%s %2d  %d" % (MONTHS[t.tm_mon - 1], t.tm_mday, t.tm_year)
    else:
        when = "%s %2d %02d:%02d" % (MONTHS[t.tm_mon - 1], t.tm_mday, t.tm_hour, t.tm_min)
    return "%s%s %3d %-8s %-8s %10d %s %s" % (kind, perms, st.st_nlink, "user", "group",
                                           st.st_size, when, name)


def mlsd_line(name, st):
    t = time.gmtime(st.st_mtime)
    kind = "dir" if stat.S_ISDIR(st.st_mode) else "file"
    return "type=%s;size=%d;modify=%04d%02d%02d%02d%02d%02d; %s" % (
        kind, st.st_size, t.tm_year, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, name)


class Session(socketserver.StreamRequestHandler):
    def reply(self, line):
        log("<", line)
        self.wfile.write((line + "\r\n").encode("utf-8", "surrogateescape"))
        self.wfile.flush()

    def real(self, path):
        """A path the client gave, inside the root: (virtual, real) or None."""
        v = path if path.startswith("/") else os.path.join(self.cwd, path)
        v = os.path.normpath("/" + v)
        if v.startswith("//"):
            v = v[1:]
        r = os.path.realpath(os.path.join(self.root, v.lstrip("/")))
        if r != self.root and not r.startswith(self.root + os.sep):
            return None
        return v, r

    def data_conn(self):
        if self.active:
            addr, self.active = self.active, None
            try:
                d = socket.create_connection(addr, timeout=20)
            except OSError as e:
                log("! PORT connect", addr, repr(e))
                self.reply("425 Cannot open the data connection")
                return None
            log("connected to", addr)
            return d
        if not self.pasv:
            self.reply("425 Use PASV or EPSV first")
            return None
        self.pasv.settimeout(20)
        try:
            d, _ = self.pasv.accept()
        except OSError:
            self.reply("425 No data connection")
            return None
        finally:
            self.pasv.close()
            self.pasv = None
        return d

    def handle(self):
        self.root = os.path.realpath(ARGS.root)
        self.cwd = "/"
        self.user = None
        self.logged = False
        self.pasv = None
        self.active = None
        self.rest = 0
        self.rnfr = None
        self.reply("220 CTerm test FTP server ready")
        while True:
            raw = self.rfile.readline()
            if not raw:
                break
            line = raw.decode("utf-8", "surrogateescape").rstrip("\r\n")
            cmd, _, arg = line.partition(" ")
            cmd = cmd.upper()
            log(">", "PASS ****" if cmd == "PASS" else line)
            try:
                if not self.command(cmd, arg):
                    break
            except (BrokenPipeError, ConnectionResetError):
                break
            except Exception as e:  # keep serving: report and go on
                log("!", repr(e))
                self.reply("451 Local error: %s" % e)
        if self.pasv:
            self.pasv.close()

    def command(self, cmd, arg):
        if cmd == "QUIT":
            self.reply("221 Goodbye")
            return False
        if cmd == "USER":
            self.user = arg
            self.reply("331 Password, please")
            return True
        if cmd == "PASS":
            if (ARGS.user is None and self.user in ("anonymous", "ftp")) or \
               (self.user == ARGS.user and arg == ARGS.password):
                self.logged = True
                self.reply("230-Welcome to the test server.")
                self.reply("230 Logged in")
            else:
                self.reply("530 Login incorrect")
            return True
        if cmd in ("SYST",):
            self.reply("215 UNIX Type: L8")
            return True
        if cmd == "FEAT":
            self.reply("211-Features:")
            for f in (["MLSD"] if not ARGS.nomlsd else []) + ["SIZE", "MDTM", "REST STREAM", "UTF8"] + \
                     (["EPSV"] if not ARGS.noepsv and not ARGS.nopasv else []):
                self.reply(" " + f)
            self.reply("211 End")
            return True
        if cmd == "NOOP":
            self.reply("200 OK")
            return True
        if not self.logged:
            self.reply("530 Log in first")
            return True
        if cmd == "PWD":
            self.reply('257 "%s" is the current directory' % self.cwd.replace('"', '""'))
        elif cmd in ("CWD", "CDUP", "XCUP"):
            r = self.real(".." if cmd != "CWD" else arg)
            if r and os.path.isdir(r[1]):
                self.cwd = r[0]
                self.reply("250 OK")
            else:
                self.reply("550 No such directory")
        elif cmd == "TYPE":
            self.reply("200 Type set")
        elif cmd == "PORT":
            try:
                h = [int(x) for x in arg.split(",")]
                assert len(h) == 6
            except (ValueError, AssertionError):
                self.reply("501 Bad PORT")
                return True
            self.active = ("%d.%d.%d.%d" % tuple(h[:4]), h[4] * 256 + h[5])
            self.reply("200 PORT command successful")
        elif cmd == "EPRT":
            f = arg.split(arg[:1]) if arg else []
            if len(f) < 4 or f[1] != "1":
                self.reply("522 Use (1)")
                return True
            self.active = (f[2], int(f[3]))
            self.reply("200 EPRT command successful")
        elif cmd in ("PASV", "EPSV"):
            if ARGS.nopasv:
                self.reply("502 Passive mode not allowed here")
                return True
            if (cmd == "EPSV" and ARGS.noepsv):
                self.reply("500 EPSV not understood")
                return True
            if self.pasv:
                self.pasv.close()
            self.pasv = socket.socket()
            self.pasv.bind(("127.0.0.1", 0))
            self.pasv.listen(1)
            p = self.pasv.getsockname()[1]
            if cmd == "PASV":
                self.reply("227 Entering Passive Mode (127,0,0,1,%d,%d)" % (p >> 8, p & 255))
            else:
                self.reply("229 Entering Extended Passive Mode (|||%d|)" % p)
        elif cmd in ("LIST", "NLST", "MLSD"):
            if cmd == "MLSD" and ARGS.nomlsd:
                self.reply("500 MLSD not understood")
                return True
            path = arg if arg and not arg.startswith("-") else "."
            r = self.real(path)
            if not r or not os.path.isdir(r[1]):
                self.reply("550 No such directory")
                return True
            d = self.data_conn()
            if not d:
                return True
            self.reply("150 Here comes the listing")
            out = []
            for name in sorted(os.listdir(r[1])):
                st = os.lstat(os.path.join(r[1], name))
                out.append(name if cmd == "NLST" else
                           mlsd_line(name, st) if cmd == "MLSD" else ls_line(name, st))
            if cmd == "LIST":
                out.insert(0, "total %d" % len(out))
            d.sendall(("\r\n".join(out) + ("\r\n" if out else "")).encode("utf-8", "surrogateescape"))
            d.close()
            self.reply("226 Listing sent")
        elif cmd == "REST":
            if ARGS.norest:
                self.reply("502 REST not implemented")
                return True
            self.rest = int(arg)
            self.reply("350 Restarting at %d" % self.rest)
        elif cmd == "RETR":
            r = self.real(arg)
            if not r or not os.path.isfile(r[1]):
                self.reply("550 No such file")
                self.rest = 0
                return True
            d = self.data_conn()
            if not d:
                return True
            start, self.rest = self.rest, 0
            self.reply("150 Opening BINARY connection for %s (%d bytes)" % (arg, os.path.getsize(r[1])))
            ok = True
            with open(r[1], "rb") as f:
                f.seek(start)
                t0 = time.time()
                sent = 0
                while True:
                    b = f.read(8192)
                    if not b:
                        break
                    try:
                        d.sendall(b)
                    except OSError:
                        ok = False
                        break
                    sent += len(b)
                    if ARGS.rate:
                        ahead = sent / (ARGS.rate * 1024) - (time.time() - t0)
                        if ahead > 0:
                            time.sleep(ahead)
            d.close()
            log("RETR", arg, "from", start, "sent", sent, "ok" if ok else "aborted")
            self.reply("226 Transfer complete" if ok else "426 Connection closed; transfer aborted")
        elif cmd in ("STOR", "APPE"):
            r = self.real(arg)
            if not r or os.path.isdir(r[1]):
                self.reply("553 Not a file name")
                return True
            d = self.data_conn()
            if not d:
                return True
            start, self.rest = self.rest, 0
            self.reply("150 Ok to send data")
            got = 0
            mode = "ab" if cmd == "APPE" else "r+b" if start and os.path.isfile(r[1]) else "wb"
            t0 = time.time()
            with open(r[1], mode) as f:
                if mode == "r+b":
                    f.seek(start)
                    f.truncate()
                while True:
                    try:
                        b = d.recv(65536)
                    except OSError:
                        break
                    if not b:
                        break
                    f.write(b)
                    f.flush()
                    got += len(b)
                    if ARGS.rate:
                        ahead = got / (ARGS.rate * 1024) - (time.time() - t0)
                        if ahead > 0:
                            time.sleep(ahead)
            d.close()
            log(cmd, arg, "from", start if mode == "r+b" else "end" if cmd == "APPE" else 0, "got", got)
            self.reply("226 Transfer complete")
        elif cmd in ("MKD", "XMKD"):
            r = self.real(arg)
            if not r or os.path.exists(r[1]):
                self.reply("550 Cannot make it")
            else:
                os.mkdir(r[1])
                self.reply('257 "%s" created' % r[0].replace('"', '""'))
        elif cmd in ("RMD", "XRMD"):
            r = self.real(arg)
            try:
                os.rmdir(r[1])
                self.reply("250 Removed")
            except (OSError, TypeError):
                self.reply("550 Cannot remove it")
        elif cmd == "DELE":
            r = self.real(arg)
            if r and os.path.isfile(r[1]):
                os.remove(r[1])
                self.reply("250 Deleted")
            else:
                self.reply("550 No such file")
        elif cmd == "RNFR":
            r = self.real(arg)
            if r and os.path.exists(r[1]):
                self.rnfr = r[1]
                self.reply("350 Ready for RNTO")
            else:
                self.reply("550 No such file")
        elif cmd == "RNTO":
            r = self.real(arg)
            if not self.rnfr or not r or os.path.exists(r[1]):
                self.reply("550 Cannot rename")
            else:
                os.rename(self.rnfr, r[1])
                self.reply("250 Renamed")
            self.rnfr = None
        elif cmd == "SIZE":
            r = self.real(arg)
            if r and os.path.isfile(r[1]):
                self.reply("213 %d" % os.path.getsize(r[1]))
            else:
                self.reply("550 No such file")
        elif cmd == "MDTM":
            r = self.real(arg)
            if r and os.path.exists(r[1]):
                self.reply("213 " + time.strftime("%Y%m%d%H%M%S", time.gmtime(os.path.getmtime(r[1]))))
            else:
                self.reply("550 No such file")
        elif cmd == "ABOR":
            self.reply("226 Nothing to abort")
        else:
            self.reply("502 %s not implemented" % cmd)
        return True


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    global ARGS, LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=2121)
    ap.add_argument("--user")
    ap.add_argument("--password")
    ap.add_argument("--rate", type=float, default=0)
    ap.add_argument("--nomlsd", action="store_true")
    ap.add_argument("--noepsv", action="store_true")
    ap.add_argument("--nopasv", action="store_true")
    ap.add_argument("--norest", action="store_true")
    ap.add_argument("--log")
    ARGS = ap.parse_args()
    if ARGS.log:
        LOG = open(ARGS.log, "a")
    srv = Server(("127.0.0.1", ARGS.port), Session)
    print("ftpd.py: serving %s on 127.0.0.1:%d" % (ARGS.root, ARGS.port), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
