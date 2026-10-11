/*
 * ssh - CTerm's ssh connection (see ssh.h). libssh2 1.11.1 with its OpenSSL
 * backend on AmiSSL 5; built by third_party/build-libssh2.sh, whose Amiga
 * patch makes the socket really non-blocking (Knowledge/amiga/
 * ssh-libssh2-amissl.md).
 */
#include "../third_party/amiga/prelude.h"
#include <proto/dos.h>
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <amissl/tags.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <exec/memory.h>
#include "ssh.h"

extern struct Library *SocketBase;      /* main.c opens it */
struct Library *AmiSSLBase, *AmiSSLExtBase, *AmiSSLMasterBase;

#define KNOWN_DIR "ENVARC:CTerm"
#define KNOWN_FILE "ENVARC:CTerm/known_hosts"
#define KNOWN_NEW  "ENVARC:CTerm/known_hosts.new"

static char keyfile[256];               /* KEY: this one, not the search */

void ssh_set_key(const char *path)
{
    strncpy(keyfile, path, sizeof keyfile - 1);
    keyfile[sizeof keyfile - 1] = 0;
}

/* One connection; AmiSSL and libssh2 are opened once for them all. */
struct Ssh {
    LIBSSH2_SESSION *s_sess;
    LIBSSH2_CHANNEL *s_chan;
    LIBSSH2_SFTP *s_sftp;               /* SFTP, instead of a shell */
    LONG s_fd;
    int s_busy;                         /* waiting for its socket */
};

static struct Ssh *cur;                 /* the one being set up */
#define sess (cur->s_sess)
#define chan (cur->s_chan)
#define fd   (cur->s_fd)
static int users;                       /* connections open */
static BOOL ssl_up, lib_up;
static ssh_ask_fn ask_cb;
static ssh_say_fn say_cb;

static void say(const char *s)
{
    say_cb(s);
}

static void say_err(const char *what)
{
    char *m = NULL;
    if (sess)
        libssh2_session_last_error(sess, &m, NULL, 0);
    say("\x1b[1;31m[");
    say(what);
    if (m && *m) {
        say(": ");
        say(m);
    }
    say("]\x1b[0m\r\n");
}

static BOOL ssl_open(void)
{
    if (ssl_up)
        return TRUE;
    AmiSSLMasterBase = OpenLibrary((STRPTR)"amisslmaster.library",
                                   AMISSLMASTER_MIN_VERSION);
    if (!AmiSSLMasterBase) {
        say("\x1b[1;31m[ssh needs AmiSSL 5 (amisslmaster.library)]"
            "\x1b[0m\r\n");
        return FALSE;
    }
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno, TAG_DONE)) {
        say("\x1b[1;31m[AmiSSL would not open]\x1b[0m\r\n");
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        return FALSE;
    }
    ssl_up = TRUE;
    return TRUE;
}

/* Wait until the socket can go the way libssh2 wants (non-blocking), at
 * most secs. FALSE: the host stopped answering or the person gave up
 * (Esc, the close gadget) - gave_up stays set and every loop ends. */
static BOOL gave_up, setting_up, wiping, want_sftp;

static BOOL wait_sock_for(LONG secs)
{
    int dir = libssh2_session_block_directions(sess), r;
    if (gave_up)
        return FALSE;
    cur->s_busy++;                      /* a resize meanwhile: not this one */
    r = net_wait(fd, (dir & LIBSSH2_SESSION_BLOCK_INBOUND) || !dir,
                 (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0, secs);
    cur->s_busy--;
    if (r > 0)
        return TRUE;
    gave_up = TRUE;
    if (setting_up)
        say(r < 0 ? "\x1b[1;31m[given up]\x1b[0m\r\n"
                  : "\x1b[1;31m[the host stopped answering]\x1b[0m\r\n");
    return FALSE;
}
#define wait_sock() wait_sock_for(30)

/* libssh2's memory: a length before each block, so that what is freed
 * while a connection is set up (the passwords typed, the login packets
 * that carried them) is wiped first. Later traffic is not: wiping every
 * packet would cost a slow Amiga time for nothing secret. */
static void wipe(void *p, ULONG n)
{
    volatile UBYTE *v = p;              /* a memset the compiler keeps, */
    while (n--)                         /* though the memory dies next */
        *v++ = 0;
}

static LIBSSH2_ALLOC_FUNC(mem_alloc)
{
    ULONG *m = malloc(count + 8);
    (void)abstract;
    if (!m)
        return NULL;
    m[0] = count;
    return m + 2;                       /* 8: as aligned as malloc's own */
}

static LIBSSH2_FREE_FUNC(mem_free)
{
    ULONG *m = (ULONG *)ptr - 2;
    (void)abstract;
    if (!ptr)
        return;
    if (wiping)
        wipe(ptr, m[0]);
    free(m);
}

static LIBSSH2_REALLOC_FUNC(mem_realloc)
{
    void *n;
    if (!ptr)
        return mem_alloc(count, abstract);
    n = mem_alloc(count, abstract);
    if (n) {
        ULONG had = ((ULONG *)ptr)[-2];
        memcpy(n, ptr, had < count ? had : count);
        mem_free(ptr, abstract);
    }
    return n;
}

static void b64(const UBYTE *in, int n, char *out)
{
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, k = 0;
    for (i = 0; i + 2 < n; i += 3) {
        ULONG v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[k++] = t[v >> 18]; out[k++] = t[(v >> 12) & 63];
        out[k++] = t[(v >> 6) & 63]; out[k++] = t[v & 63];
    }
    if (i < n) {
        ULONG v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0);
        out[k++] = t[v >> 18]; out[k++] = t[(v >> 12) & 63];
        if (i + 1 < n) out[k++] = t[(v >> 6) & 63];
    }
    out[k] = 0;                         /* unpadded, as OpenSSH shows it */
}

/* The known hosts: lines "host:port SHA256:fingerprint", a line at a time
 * (dos.library buffers it), however long the file grows. A line longer
 * than the buffer comes in pieces; only a piece that starts a line is a
 * host's. */
#define KNOWN_LINE 256

static int known_line(char *line, BOOL *whole)
{
    int n = strlen(line);
    *whole = n && line[n - 1] == '\n';
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = 0;
    return n;
}

static BOOL known_is(const char *line, const char *hostport, int hl)
{
    return !strncmp(line, hostport, hl) && line[hl] == ' ';
}

static int known_check(const char *hostport, const char *fp)
{
    char line[KNOWN_LINE];
    BPTR f = Open((STRPTR)KNOWN_FILE, MODE_OLDFILE);
    int hl = strlen(hostport), r = 0;   /* 0: never seen */
    BOOL start = TRUE, whole;
    if (!f)
        return 0;
    while (FGets(f, (STRPTR)line, sizeof line)) {
        known_line(line, &whole);
        if (start && known_is(line, hostport, hl)) {
            r = strcmp(line + hl + 1, fp) ? -1 : 1;   /* -1 changed, 1 ok */
            break;
        }
        start = whole;
    }
    Close(f);
    return r;
}

/* Every other host's line copied to a new file, this host's line last;
 * the old file is replaced only when the new one was written whole. */
static void known_add(const char *hostport, const char *fp)
{
    char line[KNOWN_LINE];
    BPTR in, out;
    int hl = strlen(hostport);
    BOOL start = TRUE, skip = FALSE, whole, ok = TRUE;
    {
        BPTR d = Lock((STRPTR)KNOWN_DIR, ACCESS_READ);
        if (!d)
            d = CreateDir((STRPTR)KNOWN_DIR);
        if (d)
            UnLock(d);
    }
    out = Open((STRPTR)KNOWN_NEW, MODE_NEWFILE);
    if (!out)
        return;
    in = Open((STRPTR)KNOWN_FILE, MODE_OLDFILE);
    if (in) {
        while (ok && FGets(in, (STRPTR)line, sizeof line)) {
            int n = known_line(line, &whole);
            if (start)
                skip = known_is(line, hostport, hl);
            if (!skip && (n || !start || !whole))
                ok = FPuts(out, (STRPTR)line) == 0 &&
                     (!whole || FPutC(out, '\n') >= 0);
            start = whole;
        }
        if (!start && !skip)            /* a last line with no newline */
            ok = ok && FPutC(out, '\n') >= 0;
        Close(in);
    }
    ok = ok && FPuts(out, (STRPTR)hostport) == 0 && FPutC(out, ' ') >= 0 &&
         FPuts(out, (STRPTR)fp) == 0 && FPutC(out, '\n') >= 0;
    if (!Close(out) || !ok) {
        DeleteFile((STRPTR)KNOWN_NEW);  /* the old file stays as it was */
        say("\x1b[1;31m[the key could not be remembered]\x1b[0m\r\n");
        return;
    }
    DeleteFile((STRPTR)KNOWN_FILE);
    Rename((STRPTR)KNOWN_NEW, (STRPTR)KNOWN_FILE);
}

/* An answer to a question: "y" will do, or (full) only "yes" itself. */
static BOOL yes(const char *q, BOOL full)
{
    char a[8];
    if (!ask_cb(q, a, sizeof a, TRUE))
        return FALSE;
    if (full)
        return (a[0] == 'y' || a[0] == 'Y') && (a[1] == 'e' || a[1] == 'E')
               && (a[2] == 's' || a[2] == 'S') && !a[3];
    return a[0] == 'y' || a[0] == 'Y';
}

static BOOL check_host(const char *hostport)
{
    const UBYTE *h = (const UBYTE *)libssh2_hostkey_hash(
        sess, LIBSSH2_HOSTKEY_HASH_SHA256);
    char fp[64];
    int k;
    if (!h) {
        say_err("no host key");
        return FALSE;
    }
    strcpy(fp, "SHA256:");
    b64(h, 32, fp + 7);
    k = known_check(hostport, fp);
    if (k == 1)
        return TRUE;
    if (k == 0) {
        say("\x1b[1m[a host not met before: ");
        say(hostport);
        say("]\x1b[0m\r\n[its key: ");
        say(fp);
        say("]\r\n");
        if (!yes("Trust it and remember it? (y/n) ", FALSE))
            return FALSE;
    } else {
        say("\x1b[1;31m[THE HOST KEY OF ");
        say(hostport);
        say(" HAS CHANGED - someone may be in between]\x1b[0m\r\n"
            "[its key now: ");
        say(fp);
        say("]\r\n[Unless you know the host was given a new key (it was "
            "reinstalled, or its owner says so), someone may be reading "
            "and changing all you send it, passwords too. Then answer no.]"
            "\r\n");
        if (!yes("Type yes in full to trust the new key: ", TRUE))
            return FALSE;
    }
    known_add(hostport, fp);
    return TRUE;
}

/* A private key from ENVARC:CTerm/ (as ssh-keygen writes it, no
 * passphrase), read into memory: no stdio of ours. */
static BOOL key_login(const char *user)
{
    static const char *const names[] = {
        "ENVARC:CTerm/id_ed25519", "ENVARC:CTerm/id_ecdsa",
        "ENVARC:CTerm/id_rsa", NULL
    };
    static char key[16384];
    const char *one[2];
    const char *const *list = names;
    int i;
    if (keyfile[0]) {
        one[0] = keyfile;
        one[1] = NULL;
        list = one;
    }
    for (i = 0; list[i]; i++) {
        BPTR f = Open((STRPTR)list[i], MODE_OLDFILE);
        LONG n;
        int rc;
        if (!f)
            continue;
        n = Read(f, key, sizeof key - 1);
        Close(f);
        if (n <= 0)
            continue;
        key[n] = 0;
        while ((rc = libssh2_userauth_publickey_frommemory(
                    sess, user, strlen(user), NULL, 0, key, n, NULL)) ==
               LIBSSH2_ERROR_EAGAIN &&
               wait_sock())
            ;
        memset(key, 0, sizeof key);
        if (!rc) {
            say("\x1b[2m[logged in with ");
            say(list[i]);
            say("]\x1b[0m\r\n");
            return TRUE;
        }
    }
    return FALSE;
}

/* keyboard-interactive: every prompt asked in the window */
static void kbd_cb(const char *name, int name_len, const char *instr,
                   int instr_len, int num,
                   const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                   LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abs)
{
    int i;
    (void)name; (void)name_len;
    if (instr_len > 0) {
        char t[200];
        int n = instr_len < 199 ? instr_len : 199;
        memcpy(t, instr, n);
        t[n] = 0;
        say(t);
        say("\r\n");
    }
    for (i = 0; i < num; i++) {
        char p[200], a[200];
        int n = prompts[i].length < 199 ? (int)prompts[i].length : 199;
        memcpy(p, prompts[i].text, n);
        p[n] = 0;
        a[0] = 0;
        if (!gave_up && !ask_cb(p, a, sizeof a, prompts[i].echo != 0))
            gave_up = TRUE;             /* the window was closed */
        /* libssh2 frees it with our mem_free, which wipes it */
        responses[i].text = (char *)mem_alloc(strlen(a) + 1, abs);
        if (responses[i].text) {
            strcpy(responses[i].text, a);
            responses[i].length = strlen(a);
        }
        wipe(a, sizeof a);
    }
}

static BOOL password_login(const char *user, const char *methods)
{
    int tries;
    for (tries = 0; tries < 3 && !gave_up; tries++) {
        int rc;
        if (methods && strstr(methods, "keyboard-interactive")) {
            while ((rc = libssh2_userauth_keyboard_interactive(sess, user,
                        kbd_cb)) == LIBSSH2_ERROR_EAGAIN &&
                   wait_sock())
                ;
        } else if (methods && strstr(methods, "password")) {
            char pw[200], pr[160];
            strcpy(pr, user);
            strcat(pr, "'s password: ");
            if (!ask_cb(pr, pw, sizeof pw, FALSE)) {
                wipe(pw, sizeof pw);    /* what was typed before the close */
                return FALSE;
            }
            while ((rc = libssh2_userauth_password(sess, user, pw)) ==
                   LIBSSH2_ERROR_EAGAIN &&
                   wait_sock())
                ;
            wipe(pw, sizeof pw);
        } else
            return FALSE;
        if (!rc)
            return TRUE;
        say("\x1b[1;31m[that was not it]\x1b[0m\r\n");
    }
    return FALSE;
}

static BOOL ssh_open_(const char *target, int cols, int rows,
                      ssh_ask_fn ask, ssh_say_fn sayf);

void *ssh_open(const char *target, int cols, int rows,
               ssh_ask_fn ask, ssh_say_fn sayf)
{
    struct Ssh *h = AllocVec(sizeof *h, MEMF_ANY | MEMF_CLEAR);
    if (!h)
        return NULL;
    h->s_fd = -1;
    cur = h;
    users++;
    gave_up = FALSE;
    setting_up = wiping = TRUE;
    if (!ssh_open_(target, cols, rows, ask, sayf)) {
        setting_up = FALSE;
        ssh_close(h);                   /* still wiping what it frees */
        wiping = FALSE;
        return NULL;
    }
    setting_up = wiping = FALSE;
    return h;
}

static BOOL ssh_open_(const char *target, int cols, int rows,
                      ssh_ask_fn ask, ssh_say_fn sayf)
{
    char user[64], host[128], hostport[160], *at, *colon, *methods;
    LONG port = 22;
    int rc;
    ask_cb = ask;
    say_cb = sayf;
    at = strchr(target, '@');
    if (!at || at == target || strlen(target) >= sizeof host) {
        say("\x1b[1;31m[ssh wants user@host]\x1b[0m\r\n");
        return FALSE;
    }
    memcpy(user, target, at - target < 63 ? at - target : 63);
    user[at - target < 63 ? at - target : 63] = 0;
    strcpy(host, at + 1);
    colon = strchr(host, ':');
    if (colon) {
        *colon = 0;
        port = atol(colon + 1);
    }
    strcpy(hostport, host);
    strcat(hostport, ":");
    {
        char n[12], d[12];
        int k = 0, j = 0;
        LONG v = port;
        do { n[k++] = '0' + v % 10; v /= 10; } while (v);
        while (k) d[j++] = n[--k];
        d[j] = 0;
        strcat(hostport, d);
    }

    SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (ULONG)&errno,
                   TAG_DONE);
    if (!ssl_open())
        return FALSE;
    if (!lib_up) {
        if (libssh2_init(0)) {
            say_err("libssh2 would not start");
            return FALSE;
        }
        lib_up = TRUE;
    }

    fd = net_connect(host, port, say);
    if (fd < 0)
        return FALSE;
    {
        LONG one = 1;
        setsockopt(fd, IPPROTO_TCP, 1 /* TCP_NODELAY */, &one, sizeof one);
    }
    sess = libssh2_session_init_ex(mem_alloc, mem_free, mem_realloc, NULL);
    if (!sess)
        return FALSE;
    libssh2_session_set_blocking(sess, 0);
    while ((rc = libssh2_session_handshake(sess, fd)) == LIBSSH2_ERROR_EAGAIN &&
           wait_sock())
        ;
    if (rc) {
        say_err("the ssh handshake failed");
        return FALSE;
    }
    if (!check_host(hostport)) {
        say("[not connected]\r\n");
        return FALSE;
    }
    while (!(methods = libssh2_userauth_list(sess, user, strlen(user))) &&
           libssh2_session_last_errno(sess) == LIBSSH2_ERROR_EAGAIN &&
           wait_sock())
        ;
    if (gave_up)
        return FALSE;
    if (!libssh2_userauth_authenticated(sess) &&
        !key_login(user) && !password_login(user, methods)) {
        say("\x1b[1;31m[not logged in");
        if (methods && !strstr(methods, "password") &&
            !strstr(methods, "keyboard-interactive")) {
            /* nothing to type a password into: say what would work */
            say(" - this host takes ");
            say(methods);
            say(" only]\x1b[0m\r\n[a key: ENVARC:CTerm/id_ed25519 or "
                "id_rsa, made by ssh-keygen without a passphrase, its .pub "
                "in the host's ~/.ssh/authorized_keys]\r\n");
        } else
            say("]\x1b[0m\r\n");
        return FALSE;
    }
    if (want_sftp) {                    /* the file browser's: no shell */
        while (!(cur->s_sftp = libssh2_sftp_init(sess)) &&
               libssh2_session_last_errno(sess) == LIBSSH2_ERROR_EAGAIN &&
               wait_sock())
            ;
        if (!cur->s_sftp) {
            say_err("no SFTP on the host");
            return FALSE;
        }
        return TRUE;
    }
    while (!(chan = libssh2_channel_open_session(sess)) &&
           libssh2_session_last_errno(sess) == LIBSSH2_ERROR_EAGAIN &&
           wait_sock())
        ;
    if (!chan) {
        say_err("no session channel");
        return FALSE;
    }
    while ((rc = libssh2_channel_request_pty_ex(chan, "xterm-256color", 14,
                NULL, 0, cols, rows, 0, 0)) == LIBSSH2_ERROR_EAGAIN &&
           wait_sock())
        ;
    if (rc) {
        say_err("no terminal on the host");
        return FALSE;
    }
    while ((rc = libssh2_channel_shell(chan)) == LIBSSH2_ERROR_EAGAIN &&
           wait_sock())
        ;
    if (rc) {
        say_err("no shell on the host");
        return FALSE;
    }
    return TRUE;
}

LONG ssh_socket(void *h)
{
    return h ? ((struct Ssh *)h)->s_fd : -1;
}

LONG ssh_read(void *h, UBYTE *buf, LONG max)
{
    ssize_t n;
    cur = h;
    if (!cur || !chan)
        return -1;
    n = libssh2_channel_read(chan, (char *)buf, max);
    if (n == LIBSSH2_ERROR_EAGAIN) {
        n = libssh2_channel_read_stderr(chan, (char *)buf, max);
        if (n == LIBSSH2_ERROR_EAGAIN)
            return libssh2_channel_eof(chan) ? -1 : 0;
    }
    if (n == 0 && libssh2_channel_eof(chan))
        return -1;
    return n < 0 ? -1 : (LONG)n;
}

void ssh_write(void *h, const UBYTE *b, LONG n)
{
    cur = h;
    if (!cur)
        return;
    gave_up = FALSE;
    while (chan && n > 0) {
        ssize_t k = libssh2_channel_write(chan, (const char *)b, n);
        if (k == LIBSSH2_ERROR_EAGAIN) {
            if (!wait_sock())           /* not read for 30 s, or Esc */
                return;
            continue;
        }
        if (k < 0)
            return;
        b += k;
        n -= k;
    }
}

/* Also called while another connection is set up or waits (the window
 * resized meanwhile): that one's state is kept, and a connection that is
 * itself waiting for its socket is left alone (libssh2 wants its pending
 * call finished first). */
void ssh_resize(void *h, int cols, int rows)
{
    struct Ssh *keep = cur;
    BOOL kept = gave_up;
    cur = h;
    gave_up = FALSE;
    if (cur && chan && !cur->s_busy)
        while (libssh2_channel_request_pty_size(chan, cols, rows) ==
               LIBSSH2_ERROR_EAGAIN &&
               wait_sock())
            ;
    cur = keep;
    gave_up = kept;
}

void ssh_close(void *h)
{
    cur = h;
    if (!cur)
        return;
    /* a goodbye, but a dead host gets 3 s, not for ever */
    gave_up = FALSE;
    if (cur->s_sftp) {
        while (libssh2_sftp_shutdown(cur->s_sftp) == LIBSSH2_ERROR_EAGAIN &&
               wait_sock_for(3))
            ;
        cur->s_sftp = NULL;
    }
    if (chan) {
        while (libssh2_channel_free(chan) == LIBSSH2_ERROR_EAGAIN &&
               wait_sock_for(3))
            ;
        chan = NULL;
    }
    if (sess) {
        while (libssh2_session_disconnect(sess, "bye") ==
               LIBSSH2_ERROR_EAGAIN && wait_sock_for(3))
            ;
        libssh2_session_free(sess);
        sess = NULL;
    }
    if (fd >= 0) {
        CloseSocket(fd);
        fd = -1;
    }
    FreeVec(cur);
    cur = NULL;
    if (--users > 0)
        return;                         /* the others still need them */
    if (lib_up) {
        libssh2_exit();
        lib_up = FALSE;
    }
    if (ssl_up) {
        CloseAmiSSL();
        ssl_up = FALSE;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
}

/* ---- SFTP, for the file browser (ssh.h: sftp_ops) ----
 * Its own connection, set up as an ssh tab's (the host key, the login),
 * then everything without blocking: the browser waits for the socket in
 * CTerm's own round, so the window and the other tabs go on. */

#define SH ((struct Ssh *)h)

static void *sf_open(const char *target, ssh_ask_fn ask, ssh_say_fn sayf)
{
    void *h;
    want_sftp = TRUE;
    h = ssh_open(target, 80, 24, ask, sayf);
    want_sftp = FALSE;
    return h;
}

static LONG sf_socket(void *h)
{
    return SH->s_fd;
}

static int sf_dirs(void *h)
{
    int d = libssh2_session_block_directions(SH->s_sess);
    return (d & LIBSSH2_SESSION_BLOCK_INBOUND ? SF_IN : 0) |
           (d & LIBSSH2_SESSION_BLOCK_OUTBOUND ? SF_OUT : 0);
}

static LONG sf_err(void *h)
{
    return libssh2_session_last_errno(SH->s_sess);
}

static LONG sf_error(void *h)
{
    LONG rc = sf_err(h);
    return rc == LIBSSH2_ERROR_SFTP_PROTOCOL
           ? (LONG)libssh2_sftp_last_error(SH->s_sftp) : rc;
}

static void attr_take(const LIBSSH2_SFTP_ATTRIBUTES *at, SfAttr *a)
{
    memset(a, 0, sizeof *a);
    if (at->flags & LIBSSH2_SFTP_ATTR_SIZE)
        a->size = at->filesize > 0xffffffffULL ? 0xffffffffUL
                                               : (ULONG)at->filesize;
    if (at->flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
        a->mtime = at->mtime;
    if (at->flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        a->dir = LIBSSH2_SFTP_S_ISDIR(at->permissions);
        a->link = LIBSSH2_SFTP_S_ISLNK(at->permissions);
    }
}

static LONG sf_opendir(void *h, const char *path, void **dh)
{
    LIBSSH2_SFTP_HANDLE *d = libssh2_sftp_open_ex(SH->s_sftp, path,
        strlen(path), 0, 0, LIBSSH2_SFTP_OPENDIR);
    *dh = d;
    return d ? 0 : sf_err(h);
}

static LONG sf_readdir(void *dh, char *name, int max, SfAttr *a)
{
    LIBSSH2_SFTP_ATTRIBUTES at;
    int rc = libssh2_sftp_readdir_ex(dh, name, max - 1, NULL, 0, &at);
    if (rc >= 0) {
        name[rc] = 0;
        attr_take(&at, a);
    }
    return rc;
}

static LONG sf_closedir(void *dh)
{
    return libssh2_sftp_close_handle(dh);
}

static LONG sf_realpath(void *h, const char *path, char *out, int max)
{
    int rc = libssh2_sftp_symlink_ex(SH->s_sftp, path, strlen(path), out,
                                     max - 1, LIBSSH2_SFTP_REALPATH);
    if (rc >= 0)
        out[rc] = 0;
    return rc;
}

static LONG sf_stat(void *h, const char *path, SfAttr *a)
{
    LIBSSH2_SFTP_ATTRIBUTES at;
    int rc = libssh2_sftp_stat_ex(SH->s_sftp, path, strlen(path),
                                  LIBSSH2_SFTP_STAT, &at);
    if (!rc)
        attr_take(&at, a);
    return rc;
}

static LONG sf_fopen(void *h, const char *path, int how, void **fh)
{
    static const unsigned long flags[3] = {
        LIBSSH2_FXF_READ,
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_APPEND };
    LIBSSH2_SFTP_HANDLE *f = libssh2_sftp_open_ex(SH->s_sftp, path,
        strlen(path), flags[how % 3], 0644, LIBSSH2_SFTP_OPENFILE);
    *fh = f;
    return f ? 0 : sf_err(h);
}

static LONG sf_read(void *fh, UBYTE *b, LONG n)
{
    return libssh2_sftp_read(fh, (char *)b, n);
}

static LONG sf_write(void *fh, const UBYTE *b, LONG n)
{
    return libssh2_sftp_write(fh, (const char *)b, n);
}

static void sf_seek(void *fh, ULONG pos)
{
    libssh2_sftp_seek64(fh, pos);
}

static LONG sf_mkdir(void *h, const char *path)
{
    return libssh2_sftp_mkdir_ex(SH->s_sftp, path, strlen(path), 0755);
}

static LONG sf_rmdir(void *h, const char *path)
{
    return libssh2_sftp_rmdir_ex(SH->s_sftp, path, strlen(path));
}

static LONG sf_unlink(void *h, const char *path)
{
    return libssh2_sftp_unlink_ex(SH->s_sftp, path, strlen(path));
}

/* never over a file there (no OVERWRITE) */
static LONG sf_rename(void *h, const char *from, const char *to)
{
    return libssh2_sftp_rename_ex(SH->s_sftp, from, strlen(from), to,
        strlen(to), LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE);
}

static const struct SftpOps sfops = {
    sf_open, ssh_close, sf_socket, sf_dirs, sf_opendir, sf_readdir,
    sf_closedir, sf_realpath, sf_stat, sf_fopen, sf_read, sf_write,
    sf_seek, sf_closedir, sf_mkdir, sf_rmdir, sf_unlink, sf_rename,
    sf_error
};
const struct SftpOps *sftp_ops = &sfops;
