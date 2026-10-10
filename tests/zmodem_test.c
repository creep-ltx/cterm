/*
 * zmodem_test - src/zmodem.c on the PC against lrzsz (the reference):
 * lsz sends to us, we send to lrz, over a socketpair, with small and big
 * files, every byte value, 8 KB subpackets and escaped controls, and a
 * line that loses and garbles bytes. Every file is compared byte for byte.
 *
 *   make tests/zmodem_test && tests/zmodem_test
 * (needs lrzsz built in third_party/src: see Knowledge/go/zmodem-and-telnet.md)
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "../src/xfer.h"

#define LRZSZ "third_party/src/lrzsz-0.12.20/src/"
#define DIR   "/tmp/zmodem_test"

static int fails;
#define CHECK(c, what) do { if (c) printf("ok   %s\n", what); \
    else { printf("FAIL %s\n", what); fails++; } } while (0)

struct Line {
    int fd;
    unsigned noise;                     /* 1 in this many bytes spoiled */
    unsigned onoise;                    /* the same for what we send */
    unsigned seed;
    char down[256];
};

static long get(void *u, unsigned char *b, long max, long ms)
{
    struct Line *l = u;
    struct pollfd p = { l->fd, POLLIN, 0 };
    long k;
    if (poll(&p, 1, (int)ms) <= 0)
        return 0;
    k = read(l->fd, b, max);
    if (k <= 0)
        return -1;
    if (l->noise) {                     /* a bad line: flip and drop */
        long i, j = 0;
        for (i = 0; i < k; i++) {
            l->seed = l->seed * 1103515245u + 12345u;
            if ((l->seed >> 8) % l->noise == 0) {
                if (l->seed & 0x10000)
                    continue;           /* lost */
                b[i] ^= 0x5a;           /* garbled */
            }
            b[j++] = b[i];
        }
        k = j ? j : 0;
    }
    return k;
}

static void put(void *u, const unsigned char *b0, long n0)
{
    struct Line *l = u;
    static unsigned char tmp[65536];
    const unsigned char *b = b0;
    long n = n0;
    if (l->onoise) {                    /* spoil what we send, too */
        long i, j = 0;
        for (i = 0; i < n0 && j < (long)sizeof tmp; i++) {
            l->seed = l->seed * 1103515245u + 12345u;
            if ((l->seed >> 8) % l->onoise == 0) {
                if (l->seed & 0x10000)
                    continue;
                tmp[j++] = b0[i] ^ 0x5a;
            } else
                tmp[j++] = b0[i];
        }
        b = tmp;
        n = j;
    }
    while (n > 0) {
        long k = write(l->fd, b, n);
        if (k <= 0)
            return;
        b += k;
        n -= k;
    }
}

static int stop(void *u) { (void)u; return 0; }

static void show(void *u, const char *name, unsigned long done,
                 unsigned long size, int what)
{
    (void)u; (void)done; (void)size;
    if (what)
        printf("     %s %s\n", name, what == 1 ? "done" : what == 2 ? "skipped" : "failed");
}

static void *wopen(void *u, const char *name, unsigned long size, char *made,
                   int madelen)
{
    struct Line *l = u;
    char path[512];
    (void)size;
    snprintf(path, sizeof path, "%s/%s", l->down, name);
    snprintf(made, madelen, "%s", name);
    return fopen(path, "wb");
}

static long wwrite(void *u, void *f, const unsigned char *b, long n)
{
    (void)u;
    return (long)fwrite(b, 1, n, f);
}

static void wclose(void *u, void *f, int ok) { (void)u; (void)ok; fclose(f); }

static void *ropen(void *u, const char *path, unsigned long *size)
{
    FILE *f = fopen(path, "rb");
    (void)u;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = (unsigned long)ftell(f);
    fseek(f, 0, SEEK_SET);
    return f;
}

static long rread(void *u, void *f, unsigned char *b, long n)
{
    (void)u;
    return (long)fread(b, 1, n, f);
}

static int rseek(void *u, void *f, unsigned long pos)
{
    (void)u;
    return fseek(f, (long)pos, SEEK_SET);
}

static void rclose(void *u, void *f) { (void)u; fclose(f); }

/* lsz or lrz on the far end of a socketpair; the near end is ours */
static pid_t child(char *argv[], const char *cwd, int *fd)
{
    int sv[2];
    pid_t p;
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    static char abs_[4096];
    if (realpath(argv[0], abs_))        /* before the chdir below */
        argv[0] = abs_;
    p = fork();
    if (p == 0) {
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        close(sv[0]);
        if (cwd && chdir(cwd)) _exit(99);
        execv(argv[0], argv);
        _exit(98);
    }
    close(sv[1]);
    *fd = sv[0];
    return p;
}

static int same(const char *a, const char *b)
{
    FILE *f = fopen(a, "rb"), *g = fopen(b, "rb");
    int c, d, ok = f && g;
    while (ok && (c = fgetc(f)) == (d = fgetc(g)))
        if (c == EOF)
            break;
    if (ok && c != d)
        ok = 0;
    if (f) fclose(f);
    if (g) fclose(g);
    return ok;
}

static void make(const char *name, long n, int kind)
{
    char path[256];
    FILE *f;
    long i;
    unsigned s = 7;
    snprintf(path, sizeof path, DIR "/src/%s", name);
    f = fopen(path, "wb");
    for (i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        fputc(kind == 0 ? "Zmodem test line\r\n"[i % 18]
              : kind == 1 ? (int)(i & 255) : (int)((s >> 16) & 255), f);
    }
    fclose(f);
}

static const char *names[] = { "empty", "text.txt", "bytes.bin", "random.bin" };

static XferIO io_for(struct Line *l)
{
    XferIO io;
    memset(&io, 0, sizeof io);
    io.u = l;
    io.get = get; io.put = put; io.stop = stop; io.show = show;
    io.wopen = wopen; io.wwrite = wwrite; io.wclose = wclose;
    io.ropen = ropen; io.rread = rread; io.rseek = rseek; io.rclose = rclose;
    return io;
}

/* lsz FLAGS files -> zm_receive */
static void download(const char *what, const char *flag, unsigned noise,
                     unsigned onoise)
{
    struct Line l;
    XferIO io;
    char *argv[12];
    unsigned char buf[4096];
    long n = 0, at = 0;
    int a = 0, i, st, got, k;
    pid_t p;
    char label[200];
    memset(&l, 0, sizeof l);
    l.noise = noise;
    l.onoise = onoise;
    l.seed = 1;
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down && mkdir -p " DIR "/down");
    argv[a++] = LRZSZ "lsz";
    argv[a++] = "-b";
    argv[a++] = "-q";
    if (flag) argv[a++] = (char *)flag;
    for (i = 0; i < 4; i++) {
        static char paths[4][128];
        snprintf(paths[i], sizeof paths[i], DIR "/src/%s", names[i]);
        argv[a++] = paths[i];
    }
    argv[a] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    /* the start header first, as CTerm sees it in what the host sends */
    for (k = 0; k < 50 && !zm_detect(buf, n, &at); k++) {
        long g = get(&l, buf + n, sizeof buf - n, 200);
        if (g > 0) n += g;
    }
    got = zm_detect(buf, n, &at) == 1
        ? zm_receive(&io, buf + at, n - at) : -2;
    waitpid(p, &st, 0);
    close(l.fd);
    snprintf(label, sizeof label, "%s: 4 files received", what);
    CHECK(got == 4, label);
    for (i = 0; i < 4; i++) {
        char x[256], y[256];
        snprintf(x, sizeof x, DIR "/src/%s", names[i]);
        snprintf(y, sizeof y, DIR "/down/%s", names[i]);
        snprintf(label, sizeof label, "%s: %s the same", what, names[i]);
        CHECK(same(x, y), label);
    }
    snprintf(label, sizeof label, "%s: lsz ended well", what);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, label);
}

/* zm_send -> lrz FLAGS */
static void upload(const char *what, const char *flag, unsigned noise,
                   unsigned onoise)
{
    struct Line l;
    XferIO io;
    char *argv[8];
    const char *paths[4];
    static char pbuf[4][128];
    int a = 0, i, st, got;
    pid_t p;
    char label[200];
    memset(&l, 0, sizeof l);
    l.noise = noise;
    l.onoise = onoise;
    l.seed = 3;
    system("rm -rf " DIR "/up && mkdir -p " DIR "/up");
    argv[a++] = LRZSZ "lrz";
    argv[a++] = "-b";
    argv[a++] = "-q";
    if (flag) argv[a++] = (char *)flag;
    argv[a] = NULL;
    p = child(argv, DIR "/up", &l.fd);
    io = io_for(&l);
    for (i = 0; i < 4; i++) {
        snprintf(pbuf[i], sizeof pbuf[i], DIR "/src/%s", names[i]);
        paths[i] = pbuf[i];
    }
    got = zm_send(&io, paths, 4, NULL, 0);
    waitpid(p, &st, 0);
    close(l.fd);
    snprintf(label, sizeof label, "%s: 4 files sent", what);
    CHECK(got == 4, label);
    for (i = 0; i < 4; i++) {
        char y[256];
        snprintf(y, sizeof y, DIR "/up/%s", names[i]);
        snprintf(label, sizeof label, "%s: %s the same", what, names[i]);
        CHECK(same(pbuf[i], y), label);
    }
    snprintf(label, sizeof label, "%s: lrz ended well", what);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, label);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    if (access(LRZSZ "lsz", X_OK)) {
        printf("no lrzsz in " LRZSZ " - see the comment at the top\n");
        return 2;
    }
    system("rm -rf " DIR " && mkdir -p " DIR "/src");
    make("empty", 0, 0);
    make("text.txt", 3000, 0);
    make("bytes.bin", 256 * 4, 1);
    make("random.bin", 1000000, 2);
    download("lsz", NULL, 0, 0);
    download("lsz -8 (8 KB subpackets)", "-8", 0, 0);
    download("lsz -e (controls escaped)", "-e", 0, 0);
    download("lsz, a bad line both ways", NULL, 3000, 500);
    upload("lrz", NULL, 0, 0);
    upload("lrz -e (asks for controls escaped)", "-e", 0, 0);
    upload("lrz, a bad line both ways", NULL, 500, 3000);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
