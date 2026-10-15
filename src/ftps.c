/*
 * ftps - TLS for FTP on AmiSSL 5 (ftps.h): OpenSSL 3 as AmiSSL's shared
 * library, opened as in Knowledge/amiga/ssh-libssh2-amissl.md (and as
 * CUtils' wget does). Part of Parts/Files. Every socket is non-blocking:
 * a step that has to wait says which way (TLS_WR, TLS_WW) and ftp.c
 * waits in CTerm's round, as for plain FTP.
 *
 * Which certificates are good: one an authority in AmiSSL's store signed
 * for the name called (tls_trusted), else the one the person said yes to
 * before (ftp.c keeps them as ssh keeps host keys).
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <string.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <amissl/tags.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/evp.h>
#include "ftps.h"
#pragma GCC optimize("Os")

struct Library *AmiSSLBase, *AmiSSLExtBase, *AmiSSLMasterBase;
static SSL_CTX *ctx;
static int tls_errno;                   /* AmiSSL's errno */
static char why[120];

static void copy(char *to, const char *s, int n)
{
    int k = strlen(s);
    if (k > n - 1)
        k = n - 1;
    memcpy(to, s, k);
    to[k] = 0;
}

const char *tls_why(void)
{
    return why;
}

const char *tls_open(struct Library *socketbase, BOOL store, BOOL user)
{
    if (ctx)
        return NULL;
    if (!AmiSSLMasterBase) {
        if (!(AmiSSLMasterBase = OpenLibrary((STRPTR)"amisslmaster.library",
                                             AMISSLMASTER_MIN_VERSION)))
            return "FTPS needs AmiSSL 5 (amisslmaster.library)";
        if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                           AmiSSL_UsesOpenSSLStructs, FALSE,
                           AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                           AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                           AmiSSL_SocketBase, (ULONG)socketbase,
                           AmiSSL_ErrNoPtr, (ULONG)&tls_errno, TAG_DONE)) {
            CloseLibrary(AmiSSLMasterBase);
            AmiSSLMasterBase = NULL;
            return "AmiSSL would not open (too old, or not enough memory: "
                   "it wants about 4 MB)";
        }
    }
    if (!(ctx = SSL_CTX_new(TLS_client_method()))) {
        tls_close();
        return "AmiSSL could not set up TLS";
    }
    /* the classic key exchanges: OpenSSL 3.5+ offers X25519MLKEM768
     * first, seconds more per call on a 68030 */
    SSL_CTX_set1_groups_list(ctx, "X25519:P-256:P-384");
    /* ChaCha20 first: no AES in the 68k's hardware, and ChaCha20-Poly1305
     * is the quicker one in software (the host may still choose) */
    SSL_CTX_set_ciphersuites(ctx, "TLS_CHACHA20_POLY1305_SHA256:"
                             "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384");
    SSL_CTX_set_cipher_list(ctx, "ECDHE+CHACHA20:ECDHE+AESGCM:ECDHE+AES:"
                            "HIGH:!aNULL:!MD5:!RC4");
    /* a write may be taken in part, and retried from another place */
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE |
                          SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    /* checked, not enforced: tls_trusted reads the result, and a
     * certificate no authority signed can still be said yes to */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    if (store)
        SSL_CTX_set_default_verify_paths(ctx);     /* AmiSSL:Certs */
    if (user)
        SSL_CTX_load_verify_locations(ctx, NULL, "AmiSSL:UserCerts");
    ERR_clear_error();
    return NULL;
}

void tls_close(void)
{
    if (ctx)
        SSL_CTX_free(ctx);
    ctx = NULL;
    if (AmiSSLMasterBase) {
        CloseAmiSSL();
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        AmiSSLBase = AmiSSLExtBase = NULL;
    }
}

static BOOL is_ip(const char *h)
{
    for (; *h; h++)
        if (!((*h >= '0' && *h <= '9') || *h == '.'))
            return FALSE;
    return TRUE;
}

void *tls_new(LONG fd, const char *host, void *reuse)
{
    SSL *s;
    if (!ctx || !(s = SSL_new(ctx)))
        return NULL;
    SSL_set_fd(s, fd);
    if (is_ip(host))
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(s), (char *)host);
    else {
        SSL_set_tlsext_host_name(s, (char *)host);
        SSL_set1_host(s, (char *)host);
    }
    if (reuse) {
        SSL_SESSION *se = SSL_get1_session((SSL *)reuse);
        if (se) {
            SSL_set_session(s, se);
            SSL_SESSION_free(se);
        }
    }
    SSL_set_connect_state(s);
    return s;
}

/* why it failed, in words: OpenSSL's reason, else what kind of failure
 * (SSL_get_error's number) and the socket's errno */
static void keep(int e)
{
    unsigned long err = ERR_peek_error();
    const char *w = err ? ERR_reason_error_string(err) : NULL;
    if (w && *w)
        copy(why, w, sizeof why);
    else {
        char *p;
        copy(why, e == SSL_ERROR_SYSCALL ? "the connection broke"
                                         : "TLS failed", sizeof why - 24);
        p = why + strlen(why);
        strcpy(p, " (0, 0)");           /* (the error, errno) */
        p[2] = '0' + e % 10;
        p[5] = '0' + tls_errno % 10;
        if (tls_errno >= 10) {
            memmove(p + 6, p + 5, 3);
            p[5] = '0' + tls_errno / 10 % 10;
        }
    }
    ERR_clear_error();
}

int tls_shake(void *t)
{
    int r = SSL_do_handshake((SSL *)t), e;
    if (r == 1)
        return 1;
    e = SSL_get_error((SSL *)t, r);
    if (e == SSL_ERROR_WANT_READ)
        return TLS_WR;
    if (e == SSL_ERROR_WANT_WRITE)
        return TLS_WW;
    keep(e);
    return -1;
}

LONG tls_read(void *t, UBYTE *b, LONG n)
{
    int r = SSL_read((SSL *)t, b, n), e;
    if (r > 0)
        return r;
    e = SSL_get_error((SSL *)t, r);
    if (e == SSL_ERROR_WANT_READ)
        return TLS_WR;
    if (e == SSL_ERROR_WANT_WRITE)
        return TLS_WW;
    if (e == SSL_ERROR_ZERO_RETURN)
        return 0;                       /* close_notify: the end */
    keep(e);
    /* a host that closes without close_notify (many do after the last
     * byte): the end, not an error - the size tells */
    return e == SSL_ERROR_SYSCALL ? 0 : -1;
}

LONG tls_write(void *t, const UBYTE *b, LONG n)
{
    int r = SSL_write((SSL *)t, b, n), e;
    if (r > 0)
        return r;
    e = SSL_get_error((SSL *)t, r);
    if (e == SSL_ERROR_WANT_READ)
        return TLS_WR;
    if (e == SSL_ERROR_WANT_WRITE)
        return TLS_WW;
    keep(e);
    return -1;
}

BOOL tls_pending(void *t)
{
    return t && SSL_pending((SSL *)t) > 0;
}

void tls_end(void *t, BOOL notify)
{
    if (!t)
        return;
    if (notify)
        SSL_shutdown((SSL *)t);         /* once: not waited for */
    ERR_clear_error();
    SSL_free((SSL *)t);
}

BOOL tls_trusted(void *t)
{
    X509 *x = SSL_get1_peer_certificate((SSL *)t);
    BOOL ok = x && SSL_get_verify_result((SSL *)t) == X509_V_OK;
    if (x)
        X509_free(x);
    return ok;
}

void tls_print(void *t, char *fp)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    X509 *x = SSL_get1_peer_certificate((SSL *)t);
    unsigned char md[64];
    unsigned int n = 0, i;
    char *o = fp;
    fp[0] = 0;
    if (!x)
        return;
    if (!X509_digest(x, EVP_sha256(), md, &n) || n != 32) {
        X509_free(x);
        return;
    }
    X509_free(x);
    strcpy(o, "SHA256:");               /* as ssh shows keys: base64, no = */
    o += 7;
    for (i = 0; i < n; i += 3) {
        ULONG v = (ULONG)md[i] << 16 | (i + 1 < n ? md[i + 1] << 8 : 0) |
                  (i + 2 < n ? md[i + 2] : 0);
        *o++ = b64[v >> 18 & 63];
        *o++ = b64[v >> 12 & 63];
        if (i + 1 < n)
            *o++ = b64[v >> 6 & 63];
        if (i + 2 < n)
            *o++ = b64[v & 63];
    }
    *o = 0;
}

const char *tls_version(void *t)
{
    return SSL_get_version((SSL *)t);
}
