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
#include <exec/memory.h>
#include "ssh.h"

extern struct Library *SocketBase;      /* main.c opens it */
struct Library *AmiSSLBase, *AmiSSLExtBase, *AmiSSLMasterBase;

#define KNOWN_DIR "ENVARC:CTerm"
#define KNOWN_FILE "ENVARC:CTerm/known_hosts"

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
    LONG s_fd;
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
static BOOL gave_up, setting_up;

static BOOL wait_sock_for(LONG secs)
{
    int dir = libssh2_session_block_directions(sess), r;
    if (gave_up)
        return FALSE;
    r = net_wait(fd, (dir & LIBSSH2_SESSION_BLOCK_INBOUND) || !dir,
                 (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0, secs);
    if (r > 0)
        return TRUE;
    gave_up = TRUE;
    if (setting_up)
        say(r < 0 ? "\x1b[1;31m[given up]\x1b[0m\r\n"
                  : "\x1b[1;31m[the host stopped answering]\x1b[0m\r\n");
    return FALSE;
}
#define wait_sock() wait_sock_for(30)

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

/* The known hosts: lines "host:port SHA256:fingerprint". */
static int known_check(const char *hostport, const char *fp)
{
    static char buf[4096];
    BPTR f = Open((STRPTR)KNOWN_FILE, MODE_OLDFILE);
    LONG n;
    char *p;
    int hl = strlen(hostport);
    if (!f)
        return 0;                       /* 0: never seen */
    n = Read(f, buf, sizeof buf - 1);
    Close(f);
    buf[n > 0 ? n : 0] = 0;
    for (p = buf; *p; ) {
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        if (!strncmp(p, hostport, hl) && p[hl] == ' ')
            return strcmp(p + hl + 1, fp) ? -1 : 1;   /* -1 changed, 1 ok */
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

static void known_add(const char *hostport, const char *fp)
{
    static char buf[4096], out[4200];
    BPTR f = Open((STRPTR)KNOWN_FILE, MODE_OLDFILE);
    LONG n = 0;
    char *p;
    int hl = strlen(hostport);
    if (f) {
        n = Read(f, buf, sizeof buf - 1);
        Close(f);
    }
    buf[n > 0 ? n : 0] = 0;
    out[0] = 0;
    for (p = buf; *p; ) {               /* every other host's line stays */
        char *e = strchr(p, '\n');
        if (e) *e = 0;
        if (*p && !(!strncmp(p, hostport, hl) && p[hl] == ' ') &&
            strlen(out) + strlen(p) + 2 < sizeof out) {
            strcat(out, p);
            strcat(out, "\n");
        }
        if (!e) break;
        p = e + 1;
    }
    if (strlen(out) + strlen(hostport) + strlen(fp) + 3 < sizeof out) {
        strcat(out, hostport);
        strcat(out, " ");
        strcat(out, fp);
        strcat(out, "\n");
    }
    {
        BPTR d = Lock((STRPTR)KNOWN_DIR, ACCESS_READ);
        if (!d)
            d = CreateDir((STRPTR)KNOWN_DIR);
        if (d)
            UnLock(d);
    }
    f = Open((STRPTR)KNOWN_FILE, MODE_NEWFILE);
    if (f) {
        Write(f, out, strlen(out));
        Close(f);
    }
}

static BOOL yes(const char *q)
{
    char a[8];
    if (!ask_cb(q, a, sizeof a, TRUE))
        return FALSE;
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
        if (!yes("Trust it and remember it? (y/n) "))
            return FALSE;
    } else {
        say("\x1b[1;31m[THE HOST KEY OF ");
        say(hostport);
        say(" HAS CHANGED - someone may be in between]\x1b[0m\r\n"
            "[its key now: ");
        say(fp);
        say("]\r\n");
        if (!yes("Trust the new key anyway? (y/n) "))
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
    (void)name; (void)name_len; (void)abs;
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
        responses[i].text = (char *)malloc(strlen(a) + 1);
        if (responses[i].text) {
            strcpy(responses[i].text, a);
            responses[i].length = strlen(a);
        }
        memset(a, 0, sizeof a);
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
            if (!ask_cb(pr, pw, sizeof pw, FALSE))
                return FALSE;
            while ((rc = libssh2_userauth_password(sess, user, pw)) ==
                   LIBSSH2_ERROR_EAGAIN &&
                   wait_sock())
                ;
            memset(pw, 0, sizeof pw);
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
    setting_up = TRUE;
    if (!ssh_open_(target, cols, rows, ask, sayf)) {
        setting_up = FALSE;
        ssh_close(h);
        return NULL;
    }
    setting_up = FALSE;
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
    sess = libssh2_session_init();
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

void ssh_resize(void *h, int cols, int rows)
{
    cur = h;
    gave_up = FALSE;
    if (cur && chan)
        while (libssh2_channel_request_pty_size(chan, cols, rows) ==
               LIBSSH2_ERROR_EAGAIN &&
               wait_sock())
            ;
}

void ssh_close(void *h)
{
    cur = h;
    if (!cur)
        return;
    /* a goodbye, but a dead host gets 3 s, not for ever */
    gave_up = FALSE;
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
