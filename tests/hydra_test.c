/*
 * hydra_test - src/hydra.c (HydraCom's, in CTerm's environment) on the PC
 * against HydraCom 1.00 itself built for Linux (tests/hydra/hcref, see
 * tests/hydra/build.sh): downloads, uploads, and both at once, over a
 * socketpair, every file compared. Built 32-bit like HydraCom (-m32).
 * Also: going on from a part, the BBS's prompt after the session, no
 * memory for the session's buffers (malloc wrapped: -Wl,--wrap=malloc),
 * names and sizes in FINFO.
 *
 * (Harness from tests/zmodem_test.c:
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
#include <ctype.h>
#include "../src/xfer.h"

#define HCREF "tests/hydra/hcref"
#define DIR   "/tmp/hydra_test"

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
    char part[610], whole[600];         /* the download open now */
    long opens, closes, okcloses, written;
    unsigned char rest[4096];           /* what came back after the end */
    long nrest;
};

/* malloc and free, counted (and made to fail) for the session's own */
void *__real_malloc(size_t n);
void __real_free(void *p);
static long live, mallocs, failat;
void *__wrap_malloc(size_t n)
{
    void *p;
    if (failat && ++mallocs == failat)
        return NULL;
    p = __real_malloc(n);
    if (p)
        live++;
    return p;
}
void __wrap_free(void *p)
{
    if (p)
        live--;
    __real_free(p);
}

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
    return fopen(l->part, "wb");
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

/* HydraCom on the far end of a socketpair; the near end is ours */
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

/* a file that may have come with HydraCom's upper case name */
static int same_ci(const char *orig, const char *dir, const char *name)
{
    char p[512], up[256];
    int i;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    if (access(p, F_OK) != 0) {
        for (i = 0; name[i] && i < 255; i++)
            up[i] = (char)toupper((unsigned char)name[i]);
        up[i] = 0;
        snprintf(p, sizeof p, "%s/%s", dir, up);
    }
    return same(orig, p);
}

static const char *names[] = { "text.txt", "bytes.bin", "random.bin" };

/* HydraCom (not the originator: a BBS) sends hc[], we send us[] */
static void session(const char *what, int nhc, int nus)
{
    struct Line l;
    XferIO io;
    char *argv[12];
    static char hcp[3][128], usp[3][128];
    const char *paths[3];
    int a = 0, i, st, got;
    pid_t p;
    char label[200];
    memset(&l, 0, sizeof l);
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down " DIR "/hcdown && mkdir -p " DIR "/down " DIR "/hcdown");
    argv[a++] = HCREF;
    argv[a++] = "-d";
    argv[a++] = DIR "/hcdown/";
    for (i = 0; i < nhc; i++) {
        snprintf(hcp[i], sizeof hcp[i], DIR "/src/%s", names[i]);
        argv[a++] = hcp[i];
    }
    argv[a] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    for (i = 0; i < nus; i++) {
        snprintf(usp[i], sizeof usp[i], DIR "/src/%s", names[i]);
        paths[i] = usp[i];
    }
    got = hy_session(&io, paths, nus, NULL, 0, 1);
    waitpid(p, &st, 0);
    close(l.fd);
    snprintf(label, sizeof label, "%s: %d files went", what, nhc + nus);
    CHECK(got == nhc + nus, label);
    for (i = 0; i < nhc; i++) {
        snprintf(label, sizeof label, "%s: %s came the same", what, names[i]);
        CHECK(same_ci(hcp[i], DIR "/down", names[i]), label);
    }
    for (i = 0; i < nus; i++) {
        snprintf(label, sizeof label, "%s: %s went the same", what, names[i]);
        CHECK(same_ci(usp[i], DIR "/hcdown", names[i]), label);
    }
    snprintf(label, sizeof label, "%s: HydraCom ended well", what);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, label);
}

/* a host must not choose the drawer: FINFO names lose theirs */
static void drawers(void)
{
    static const char *in[][2] = {
        { "SYS:WBStartup/Evil", "Evil" }, { "/Evil", "Evil" },
        { "DEVS:x", "x" }, { "a\\b\\c.zip", "c.zip" },
        { "plain.lha", "plain.lha" }, { "dir/", "" },
    };
    char s[200], name[64];
    long t;
    unsigned long sz;
    int i;
    for (i = 0; i < (int)(sizeof in / sizeof in[0]); i++) {
        strcpy(s, "0000000000000400000000000000000000000000");
        strcat(s, in[i][0]);
        hy_finfo(s, &t, &sz, name, sizeof name);
        CHECK(!strcmp(name, in[i][1]) && sz == 0x400, in[i][0]);
    }
    /* sizes past 2 GB stay what they are (no negative long) */
    hy_finfo("00000000c0000000000000000000000000000000big.iso", &t, &sz,
             name, sizeof name);
    CHECK(sz == 0xc0000000UL && !strcmp(name, "big.iso"), "FINFO size 3 GB");
}

/* HydraCom sends random.bin while a part of it (100000 bytes) is here
 * from an earlier try: only the rest comes */
static void resume(void)
{
    struct Line l;
    XferIO io;
    char *argv[6];
    int st, got;
    pid_t p;
    memset(&l, 0, sizeof l);
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down " DIR "/hcdown && mkdir -p " DIR "/down "
           DIR "/hcdown && head -c 100000 " DIR "/src/random.bin > "
           DIR "/down/random.bin.part");
    argv[0] = HCREF;
    argv[1] = "-d";
    argv[2] = DIR "/hcdown/";
    argv[3] = DIR "/src/random.bin";
    argv[4] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    got = hy_session(&io, NULL, 0, NULL, 0, 1);
    waitpid(p, &st, 0);
    close(l.fd);
    CHECK(got == 1, "resume: 1 file went");
    CHECK(l.written == 300000 - 100000, "resume: only the 200000 missing bytes came");
    CHECK(same(DIR "/src/random.bin", DIR "/down/random.bin"), "resume: random.bin came the same");
    CHECK(access(DIR "/down/random.bin.part", F_OK) != 0, "resume: no part left");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "resume: HydraCom ended well");
}

/* The BBS's prompt right after the session comes back for the screen -
 * and nothing of Hydra's own packets with it */
static void prompt_after(void)
{
    struct Line l;
    XferIO io;
    char *argv[4];
    int st, got, k;
    long i, dle = 0;
    pid_t p;
    memset(&l, 0, sizeof l);
    l.together = 1;
    strcpy(l.down, DIR "/down");
    system("rm -rf " DIR "/down " DIR "/hcdown && mkdir -p " DIR "/down " DIR "/hcdown");
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = HCREF " -d " DIR "/hcdown/ " DIR "/src/text.txt; printf 'BBS prompt> '";
    argv[3] = NULL;
    p = child(argv, NULL, &l.fd);
    io = io_for(&l);
    got = hy_session(&io, NULL, 0, NULL, 0, 1);
    while ((k = (int)get(&l, l.rest + l.nrest, sizeof l.rest - l.nrest, 2000)) > 0)
        l.nrest += k;
    waitpid(p, &st, 0);
    close(l.fd);
    for (i = 0; i < l.nrest; i++)
        dle += l.rest[i] == 0x18;
    CHECK(got == 1, "prompt after Hydra: 1 file went");
    CHECK(l.nrest >= 12 && !memcmp(l.rest + l.nrest - 12, "BBS prompt> ", 12),
          "prompt after Hydra: the prompt reaches the screen");
    CHECK(dle == 0, "prompt after Hydra: no packet bytes with it");
}

/* No memory for one of hydra_init's buffers: the session says so and
 * gives back all it did get */
static void nomem(void)
{
    struct Line l;
    XferIO io;
    int i, got, ok = 1;
    memset(&l, 0, sizeof l);
    io = io_for(&l);
    for (i = 1; i <= 6; i++) {          /* ib, ob, then hydra_init's four */
        mallocs = 0;
        failat = i;
        live = 0;
        got = hy_session(&io, NULL, 0, NULL, 0, 1);
        if (got != -1 || live != 0) {
            printf("     malloc %d failing: %d, %ld left\n", i, got, live);
            ok = 0;
        }
    }
    failat = 0;
    CHECK(ok, "no memory: -1, and nothing kept");
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    if (access(HCREF, X_OK)) {
        printf("no " HCREF " - run tests/hydra/build.sh\n");
        return 2;
    }
    drawers();
    nomem();
    system("rm -rf " DIR " && mkdir -p " DIR "/src");
    make("text.txt", 3000, 0);
    make("bytes.bin", 256 * 4, 1);
    make("random.bin", 300000, 2);
    session("download (the BBS sends 3)", 3, 0);
    session("upload (we send 3)", 0, 3);
    session("both ways at once", 3, 3);
    resume();
    prompt_after();
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
