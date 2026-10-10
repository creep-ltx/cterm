/* amibridge LOCAL/N PC/N - testing aid (serves one pair after another): lets a program on the Amiga reach
 * a service only the PC can offer (the PC's firewall drops calls from
 * the LAN). Listens on PC (any address) for the PC's pcbridge.py and on
 * LOCAL (127.0.0.1) for the Amiga program; once both are in, bytes go
 * both ways until either side closes. Ctrl-C ends it. No stdio. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <string.h>

struct Library *SocketBase;

static LONG listen_at(LONG port, ULONG addr)
{
    struct sockaddr_in sa;
    LONG one = 1, fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = addr;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 1) < 0) {
        CloseSocket(fd);
        return -1;
    }
    return fd;
}

static LONG take(LONG lfd)
{
    for (;;) {
        fd_set rd;
        ULONG sigs = SIGBREAKF_CTRL_C;
        FD_ZERO(&rd);
        FD_SET(lfd, &rd);
        if (WaitSelect(lfd + 1, &rd, NULL, NULL, NULL, &sigs) < 0 ||
            (sigs & SIGBREAKF_CTRL_C))
            return -1;
        if (FD_ISSET(lfd, &rd))
            return accept(lfd, NULL, NULL);
    }
}

int main(void)
{
    LONG a[2] = { 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"LOCAL/N/A,PC/N/A", a, NULL);
    LONG lp, pp, lf = -1, pf = -1, ls = -1, ps = -1;
    static char buf[8192];
    if (!rd)
        return 20;
    lp = *(LONG *)a[0];
    pp = *(LONG *)a[1];
    FreeArgs(rd);
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4)))
        return 20;
    pf = listen_at(pp, INADDR_ANY);
    lf = listen_at(lp, htonl(0x7F000001UL));
    if (pf < 0 || lf < 0) {
        PutStr("amibridge: cannot listen\n");
        goto out;
    }
    for (;;) {                          /* one pair after another */
        BOOL stop = FALSE;
        PutStr("amibridge: waiting for the PC\n");
        Flush(Output());
        if ((ps = take(pf)) < 0) break;
        PutStr("amibridge: the PC is in; waiting for the local program\n");
        Flush(Output());
        if ((ls = take(lf)) < 0) break;
        PutStr("amibridge: joined\n");
        Flush(Output());
        for (;;) {
            fd_set r;
            ULONG sigs = SIGBREAKF_CTRL_C;
            LONG n, m = (ls > ps ? ls : ps) + 1;
            FD_ZERO(&r);
            FD_SET(ls, &r);
            FD_SET(ps, &r);
            if (WaitSelect(m, &r, NULL, NULL, NULL, &sigs) < 0 ||
                (sigs & SIGBREAKF_CTRL_C)) {
                stop = TRUE;
                break;
            }
            if (FD_ISSET(ls, &r)) {
                n = recv(ls, buf, sizeof buf, 0);
                if (n <= 0 || send(ps, buf, n, 0) != n) break;
            }
            if (FD_ISSET(ps, &r)) {
                n = recv(ps, buf, sizeof buf, 0);
                if (n <= 0 || send(ls, buf, n, 0) != n) break;
            }
        }
        CloseSocket(ls);
        CloseSocket(ps);
        ls = ps = -1;
        if (stop) break;
    }
    PutStr("amibridge: done\n");
out:
    if (ls >= 0) CloseSocket(ls);
    if (ps >= 0) CloseSocket(ps);
    if (lf >= 0) CloseSocket(lf);
    if (pf >= 0) CloseSocket(pf);
    CloseLibrary(SocketBase);
    return 0;
}
