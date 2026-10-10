/*
 * zmodem_test - src/zmodem.c on the PC against lrzsz (the reference):
 * lsz sends to us, we send to lrz, over a socketpair, with small and big
 * files, every byte value, 8 KB subpackets and escaped controls, and a
 * line that loses and garbles bytes. Every file is compared byte for byte.
 * Also: a download that goes on from a part (crash recovery), a file that
 * shrinks while it is sent, the host's prompt after the session, a
 * session that ends inside a file (a script of frames), and Xmodem's
 * block size against receivers that want 128 bytes.
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
/* zmodem.c itself, for its frame writers (a host's script, below) */
#include "../src/zmodem.c"

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
    int together;                       /* reads wait for a quiet moment */
    unsigned long grow;                 /* ropen says the file is bigger */
    char part[610], whole[600];         /* the download open now */
    long opens, closes, okcloses, written;
    unsigned char rest[4096];           /* what came back after the end */
    long nrest;
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
    while (l->together && k < max && poll(&p, 1, 100) > 0) {
        long j = read(l->fd, b + k, max - k);   /* the host's next bytes */
        if (j <= 0)                             /* in the same read */
            break;
        k += j;
    }
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

/* as CTerm's x_wopen: written as name.part, renamed when whole; a part
 * shorter than the file is gone on from when the protocol can */
static void *wopen(void *u, const char *name, unsigned long size, char *made,
                   int madelen, unsigned long *have)
{
    struct Line *l = u;
    struct stat st;
    FILE *f;
    snprintf(l->whole, sizeof l->whole, "%s/%s", l->down, name);
    snprintf(l->part, sizeof l->part, "%s.part", l->whole);
    snprintf(made, madelen, "%s", name);
    l->opens++;
    if (have) {
        *have = 0;
        if (!stat(l->part, &st) && st.st_size > 0 &&
            (unsigned long)st.st_size < size) {
            *have = (unsigned long)st.st_size;
            return fopen(l->part, "ab");
        }
    }
    f = fopen(l->part, "wb");
    return f;
}

static long wwrite(void *u, void *f, const unsigned char *b, long n)
{
    struct Line *l = u;
    l->written += n;
    return (long)fwrite(b, 1, n, f);
}

static void wclose(void *u, void *f, int ok)
{
    struct Line *l = u;
    fclose(f);
    l->closes++;
    if (ok) {
        l->okcloses++;
        rename(l->part, l->whole);
    }
}

static void rest(void *u, const unsigned char *b, long n)
{
    struct Line *l = u;
    if (l->nrest + n <= (long)sizeof l->rest) {
        memcpy(l->rest + l->nrest, b, n);
        l->nrest += n;
    }
}

static void *ropen(void *u, const char *path, unsigned long *size)
{
    struct Line *l = u;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = (unsigned long)ftell(f) + l->grow;
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
    io.rest = rest;
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

/* Ymodem/Xmodem: lsz --ymodem / -X -> xy_receive */
static void xy_down(const char *what, int ymodem, const char *flag)
{
    struct Line l;
    XferIO io;
    char *argv[12];
    int a = 0, i, st, got, n = ymodem ? 4 : 1;
    pid_t p;
    char label[200];
    static char paths[4][128];
    memset(&l, 0, sizeof l);
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down && mkdir -p " DIR "/down");
    argv[a++] = LRZSZ "lsz";
    argv[a++] = "-q";
    argv[a++] = (char *)flag;
    if (!ymodem) argv[a++] = "-k";
    for (i = 0; i < n; i++) {
        snprintf(paths[i], sizeof paths[i], DIR "/src/%s",
                 ymodem ? names[i] : "random.bin");
        argv[a++] = paths[i];
    }
    argv[a] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    got = xy_receive(&io, ymodem, "random.bin");
    waitpid(p, &st, 0);
    close(l.fd);
    snprintf(label, sizeof label, "%s: %d files received", what, n);
    CHECK(got == n, label);
    for (i = 0; i < n; i++) {
        char y[256];
        const char *nm = ymodem ? names[i] : "random.bin";
        snprintf(y, sizeof y, DIR "/down/%s", nm);
        snprintf(label, sizeof label, "%s: %s the same%s", what, nm,
                 ymodem ? "" : " (Xmodem pads the end)");
        if (ymodem)
            CHECK(same(paths[i], y), label);
        else {                          /* the original, then SUBs */
            FILE *f = fopen(paths[i], "rb"), *g = fopen(y, "rb");
            int c, d, ok = f && g;
            while (ok && (c = fgetc(f)) != EOF)
                ok = (d = fgetc(g)) == c;
            while (ok && (d = fgetc(g)) != EOF)
                ok = d == 0x1a;
            if (f) fclose(f);
            if (g) fclose(g);
            CHECK(ok, label);
        }
    }
    snprintf(label, sizeof label, "%s: lsz ended well", what);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, label);
}

/* xy_send -> lrz --ymodem / -X */
static void xy_up(const char *what, int ymodem)
{
    struct Line l;
    XferIO io;
    char *argv[8];
    const char *paths[4];
    static char pbuf[4][128];
    int a = 0, i, st, got, n = ymodem ? 4 : 1;
    pid_t p;
    char label[200];
    memset(&l, 0, sizeof l);
    system("rm -rf " DIR "/up && mkdir -p " DIR "/up");
    argv[a++] = LRZSZ "lrz";
    argv[a++] = "-q";
    if (ymodem) argv[a++] = "--ymodem";
    else { argv[a++] = "-X"; argv[a++] = "-c"; argv[a++] = "random.bin"; }
    argv[a] = NULL;
    p = child(argv, DIR "/up", &l.fd);
    io = io_for(&l);
    for (i = 0; i < n; i++) {
        snprintf(pbuf[i], sizeof pbuf[i], DIR "/src/%s",
                 ymodem ? names[i] : "random.bin");
        paths[i] = pbuf[i];
    }
    got = xy_send(&io, ymodem, 1, paths, n);
    waitpid(p, &st, 0);
    close(l.fd);
    snprintf(label, sizeof label, "%s: %d files sent", what, n);
    CHECK(got == n, label);
    for (i = 0; ymodem && i < n; i++) {
        char y[256];
        snprintf(y, sizeof y, DIR "/up/%s", names[i]);
        snprintf(label, sizeof label, "%s: %s the same", what, names[i]);
        CHECK(same(pbuf[i], y), label);
    }
    if (!ymodem) {
        FILE *f = fopen(pbuf[0], "rb"), *g = fopen(DIR "/up/random.bin", "rb");
        int c, d, ok = f && g;
        while (ok && (c = fgetc(f)) != EOF)
            ok = (d = fgetc(g)) == c;
        while (ok && (d = fgetc(g)) != EOF)
            ok = d == 0x1a;
        if (f) fclose(f);
        if (g) fclose(g);
        snprintf(label, sizeof label, "%s: random.bin the same (padded)", what);
        CHECK(ok, label);
    }
    snprintf(label, sizeof label, "%s: lrz ended well", what);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, label);
}

/* ---- what the audit of 2026-10-10 found ---- */

/* lsz sends random.bin while a part of it (300000 bytes) is here from an
 * earlier try: only the rest comes, and the file is whole */
static void resume(void)
{
    struct Line l;
    XferIO io;
    char *argv[6];
    unsigned char buf[4096];
    long n = 0, at = 0;
    int st, got, k;
    pid_t p;
    memset(&l, 0, sizeof l);
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down && mkdir -p " DIR "/down && head -c 300000 "
           DIR "/src/random.bin > " DIR "/down/random.bin.part");
    argv[0] = LRZSZ "lsz";
    argv[1] = "-b";
    argv[2] = "-q";
    argv[3] = DIR "/src/random.bin";
    argv[4] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    for (k = 0; k < 50 && !zm_detect(buf, n, &at); k++) {
        long g = get(&l, buf + n, sizeof buf - n, 200);
        if (g > 0) n += g;
    }
    got = zm_detect(buf, n, &at) == 1
        ? zm_receive(&io, buf + at, n - at) : -2;
    waitpid(p, &st, 0);
    close(l.fd);
    CHECK(got == 1, "resume: 1 file received");
    CHECK(l.written == 1000000 - 300000, "resume: only the 700000 missing bytes came");
    CHECK(same(DIR "/src/random.bin", DIR "/down/random.bin"), "resume: random.bin the same");
    CHECK(access(DIR "/down/random.bin.part", F_OK) != 0, "resume: no part left");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "resume: lsz ended well");
}

/* The host's prompt right after the session (in the same read as "OO")
 * comes back for the screen, not lost with the protocol's buffer */
static void prompt_after(void)
{
    struct Line l;
    XferIO io;
    char *argv[4];
    unsigned char buf[4096];
    long n = 0, at = 0;
    int st, got, k;
    pid_t p;
    memset(&l, 0, sizeof l);
    l.together = 1;
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down && mkdir -p " DIR "/down");
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = LRZSZ "lsz -b -q " DIR "/src/text.txt; printf 'BBS prompt> '";
    argv[3] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    for (k = 0; k < 50 && !zm_detect(buf, n, &at); k++) {
        long g = get(&l, buf + n, sizeof buf - n, 200);
        if (g > 0) n += g;
    }
    got = zm_detect(buf, n, &at) == 1
        ? zm_receive(&io, buf + at, n - at) : -2;
    /* and what CTerm reads itself afterwards */
    while ((k = (int)get(&l, l.rest + l.nrest, sizeof l.rest - l.nrest, 2000)) > 0)
        l.nrest += k;
    waitpid(p, &st, 0);
    close(l.fd);
    CHECK(got == 1, "prompt after Zmodem: 1 file received");
    CHECK(same(DIR "/src/text.txt", DIR "/down/text.txt"), "prompt after Zmodem: text.txt the same");
    CHECK(l.nrest == 12 && !memcmp(l.rest, "BBS prompt> ", 12),
          "prompt after Zmodem: the prompt reaches the screen");
}

/* A host's own script of frames, written with zmodem.c's writers */
static unsigned char script[16384];
static long nscript;
static void sput(void *u, const unsigned char *b, long n)
{
    (void)u;
    memcpy(script + nscript, b, n);
    nscript += n;
}
static long gone_(void *u, unsigned char *b, long max, long ms)
{
    (void)u; (void)b; (void)max; (void)ms;
    return -1;                          /* the script is all there is */
}
static void drop(void *u, const unsigned char *b, long n)
{
    (void)u; (void)b; (void)n;
}

/* ZFILE, maybe some data, then ZFIN: the file is closed (a part), the
 * session ends well, what follows "OO" comes back */
static void ends_inside(const char *what, long data)
{
    static struct Z zs;
    struct Line l;
    XferIO sio, io;
    unsigned char d[300];
    char label[200];
    int got;
    long i;
    memset(&sio, 0, sizeof sio);
    sio.put = sput;
    nscript = 0;
    z_begin(&zs, &sio, NULL, 0);
    zs.use32 = 1;
    pos_hdr(&zs, 0);
    zs.hdr[3] = ZCBIN;
    send_bin(&zs, ZFILE);
    send_data(&zs, (const unsigned char *)"cut.bin\0" "5000 0 100644 0 1", 25,
              ZCRCW);
    if (data) {
        for (i = 0; i < data; i++)
            d[i] = (unsigned char)i;
        pos_hdr(&zs, 0);
        send_bin(&zs, ZDATA);
        send_data(&zs, d, data, ZCRCE);
    }
    pos_hdr(&zs, 0);
    send_hex(&zs, ZFIN);
    ostr(&zs, "OOafter\r\n");
    flush(&zs);

    memset(&l, 0, sizeof l);
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down && mkdir -p " DIR "/down");
    io = io_for(&l);
    io.get = gone_;
    io.put = drop;
    got = zm_receive(&io, script, nscript);
    snprintf(label, sizeof label, "%s: no file, no failure", what);
    CHECK(got == 0, label);
    snprintf(label, sizeof label, "%s: the file was closed, as a part", what);
    CHECK(l.opens == 1 && l.closes == 1 && l.okcloses == 0, label);
    snprintf(label, sizeof label, "%s: the part holds what came", what);
    {
        struct stat st;
        CHECK(!stat(DIR "/down/cut.bin.part", &st) && st.st_size == data,
              label);
    }
    snprintf(label, sizeof label, "%s: what followed \"OO\" comes back", what);
    CHECK(l.nrest == 7 && !memcmp(l.rest, "after\r\n", 7), label);
}

/* The file is 5000 bytes shorter than its size said (it shrank while it
 * went): it ends where it ends, lrz has it all */
static void shrinks(void)
{
    struct Line l;
    XferIO io;
    char *argv[4];
    const char *paths[1] = { DIR "/src/text.txt" };
    int st, got;
    pid_t p;
    memset(&l, 0, sizeof l);
    l.grow = 5000;
    system("rm -rf " DIR "/up && mkdir -p " DIR "/up");
    argv[0] = LRZSZ "lrz";
    argv[1] = "-b";
    argv[2] = "-q";
    argv[3] = NULL;
    p = child(argv, DIR "/up", &l.fd);
    io = io_for(&l);
    alarm(60);                          /* (it used to go on for ever) */
    got = zm_send(&io, paths, 1, NULL, 0);
    alarm(0);
    waitpid(p, &st, 0);
    close(l.fd);
    CHECK(got == 1, "shrinks while sent: 1 file sent");
    CHECK(same(paths[0], DIR "/up/text.txt"), "shrinks while sent: text.txt the same");
}

/* Xmodem to a receiver in this process: it asks with NAK or 'C' and may
 * refuse 1 KB blocks, as a classic one does */
struct XRx {
    int start;                          /* NAK or 'C' */
    int take1k;
    int q[64], nq;                      /* its answers, to come */
    unsigned char got[300000];
    long ngot;
    int stx, soh, next;
};
static struct XRx xr;
static long xget(void *u, unsigned char *b, long max, long ms)
{
    (void)u; (void)max; (void)ms;
    if (!xr.nq)
        return 0;
    b[0] = (unsigned char)xr.q[0];
    memmove(xr.q, xr.q + 1, --xr.nq * sizeof xr.q[0]);
    return 1;
}
static void xput(void *u, const unsigned char *b, long n)
{
    (void)u;
    if (b[0] == 0x04 && n == 1) {               /* EOT */
        xr.q[xr.nq++] = 0x06;
        return;
    }
    if (b[0] == 0x02 && !xr.take1k) {           /* noise to it */
        xr.stx++;
        xr.q[xr.nq++] = 0x15;
        return;
    }
    if (b[0] == 0x02 || b[0] == 0x01) {
        long len = b[0] == 0x02 ? 1024 : 128;
        if (b[0] == 0x02) xr.stx++; else xr.soh++;
        if (b[1] == (unsigned char)xr.next) {
            memcpy(xr.got + xr.ngot, b + 3, len);
            xr.ngot += len;
            xr.next++;
        }
        xr.q[xr.nq++] = 0x06;
    }
}

static void xm_blocks(const char *what, int start, int take1k, int want1k)
{
    struct Line l;
    XferIO io;
    const char *paths[1] = { DIR "/src/text.txt" };
    char label[200];
    int got, ok;
    FILE *f;
    long i;
    memset(&l, 0, sizeof l);
    memset(&xr, 0, sizeof xr);
    xr.start = start;
    xr.take1k = take1k;
    xr.next = 1;
    xr.q[xr.nq++] = start;
    io = io_for(&l);
    io.get = xget;
    io.put = xput;
    got = xy_send(&io, 0, 1, paths, 1);
    snprintf(label, sizeof label, "%s: sent", what);
    CHECK(got == 1, label);
    snprintf(label, sizeof label, "%s: %s blocks", what,
             want1k ? "1 KB" : "128-byte");
    CHECK(want1k ? xr.stx > 0 && xr.soh <= 1
                 : xr.soh > 0 && (take1k || xr.stx <= 2), label);
    f = fopen(paths[0], "rb");
    ok = f != NULL && xr.ngot >= 3000;
    for (i = 0; ok && i < xr.ngot; i++) {
        int c = fgetc(f);
        ok = c == EOF ? xr.got[i] == 0x1a : xr.got[i] == c;
    }
    if (f) fclose(f);
    snprintf(label, sizeof label, "%s: text.txt the same (padded)", what);
    CHECK(ok, label);
}

int main(int argc, char **argv)
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
    if (argc > 1 && !strcmp(argv[1], "xy"))
        goto xy;
    if (argc > 1 && !strcmp(argv[1], "audit"))
        goto audit;
    download("lsz", NULL, 0, 0);
    download("lsz -8 (8 KB subpackets)", "-8", 0, 0);
    download("lsz -e (controls escaped)", "-e", 0, 0);
    download("lsz, a bad line both ways", NULL, 3000, 500);
    upload("lrz", NULL, 0, 0);
    upload("lrz -e (asks for controls escaped)", "-e", 0, 0);
    upload("lrz, a bad line both ways", NULL, 500, 3000);
xy:
    xy_down("lsz --ymodem", 1, "--ymodem");
    xy_down("lsz -X -k (Xmodem 1K)", 0, "-X");
    xy_up("lrz --ymodem", 1);
    xy_up("lrz -X -c (Xmodem CRC)", 0);
audit:
    resume();
    prompt_after();
    ends_inside("ZFILE, data, ZFIN", 200);
    ends_inside("ZFILE, ZFIN", 0);
    shrinks();
    xm_blocks("Xmodem to a NAK (checksum) receiver", 0x15, 1, 0);
    xm_blocks("Xmodem to a 'C' receiver that takes 1K", 'C', 1, 1);
    xm_blocks("Xmodem to a 'C' receiver that takes 128 only", 'C', 0, 0);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
