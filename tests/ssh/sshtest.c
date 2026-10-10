/* sshtest HOST PORT USER KEY CMD - libssh2 + AmiSSL on the Amiga: log in
 * with a private key file, run one command, print what it says. */
#include "../../third_party/amiga/prelude.h"
#include <proto/dos.h>
#include <proto/amisslmaster.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <amissl/tags.h>
#include <libssh2.h>

struct Library *SocketBase, *AmiSSLBase, *AmiSSLExtBase, *AmiSSLMasterBase;
static BOOL ssl_up;

static void say(const char *s) { PutStr((STRPTR)s); }
static struct DateStamp t0;
static void tick(const char *what)
{
    struct DateStamp t; char b[16]; long ms;
    DateStamp(&t);
    ms = ((t.ds_Minute - t0.ds_Minute) * 3000 + (t.ds_Tick - t0.ds_Tick)) * 20;
    PutStr((STRPTR)what); PutStr((STRPTR)" at ");
    { long v = ms; int n = 0, k = 0; char tt[12]; do { tt[n++] = '0' + v % 10; v /= 10; } while (v); while (n) b[k++] = tt[--n]; b[k] = 0; }
    PutStr((STRPTR)b); PutStr((STRPTR)" ms\n");
}
static void num(char *d, long v){char t[12];int n=0,k=0; if(v<0){d[k++]='-';v=-v;} do{t[n++]='0'+v%10;v/=10;}while(v); while(n)d[k++]=t[--n]; d[k]=0;}

static BOOL ssl_open(void)
{
    if (!(AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION)))
        return say("no amisslmaster.library\n"), FALSE;
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno, TAG_DONE))
        return say("OpenAmiSSLTags failed\n"), FALSE;
    ssl_up = TRUE;
    return TRUE;
}

static void ssl_close(void)
{
    if (ssl_up) CloseAmiSSL();
    if (AmiSSLMasterBase) CloseLibrary(AmiSSLMasterBase);
}

int main(void)
{
    LONG a[5] = {0,0,0,0,0};
    struct RDArgs *rd = ReadArgs((STRPTR)"HOST/A,PORT/N/A,USER/A,KEY/A,CMD/A", a, NULL);
    struct sockaddr_in sa;
    LONG sock = -1, kl;
    LIBSSH2_SESSION *s = NULL;
    LIBSSH2_CHANNEL *ch = NULL;
    static char key[8192], buf[4096], n[16];
    BPTR f;
    int rc;
    if (!rd) return 20;
    DateStamp(&t0);
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4))) return 20;
    SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (ULONG)&errno, TAG_DONE);
    if (!ssl_open()) goto out;
    f = Open((STRPTR)a[3], MODE_OLDFILE);
    if (!f) { say("no key file\n"); goto out; }
    kl = Read(f, key, sizeof key - 1); Close(f);
    key[kl > 0 ? kl : 0] = 0;
    if ((rc = libssh2_init(0))) { say("libssh2_init failed\n"); goto out; }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(*(LONG *)a[1]);
    sa.sin_addr.s_addr = inet_addr((STRPTR)a[0]);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0) { say("connect failed\n"); goto out; }
    tick("connected");
    s = libssh2_session_init();
    libssh2_session_set_blocking(s, 1);
    if ((rc = libssh2_session_handshake(s, sock))) {
        char *m; libssh2_session_last_error(s, &m, NULL, 0);
        say("handshake failed: "); say(m); say("\n"); goto out;
    }
    tick("handshake");
    say("handshake ok, kex "); say(libssh2_session_methods(s, LIBSSH2_METHOD_KEX) ? libssh2_session_methods(s, LIBSSH2_METHOD_KEX) : "?");
    say(", host key "); say(libssh2_session_methods(s, LIBSSH2_METHOD_HOSTKEY) ? libssh2_session_methods(s, LIBSSH2_METHOD_HOSTKEY) : "?");
    say(", cipher "); say(libssh2_session_methods(s, LIBSSH2_METHOD_CRYPT_CS) ? libssh2_session_methods(s, LIBSSH2_METHOD_CRYPT_CS) : "?");
    say("\n");
    if ((rc = libssh2_userauth_publickey_frommemory(s, (char *)a[2], strlen((char *)a[2]),
                                                    NULL, 0, key, kl, NULL))) {
        char *m; libssh2_session_last_error(s, &m, NULL, 0);
        num(n, rc); say("auth failed "); say(n); say(": "); say(m); say("\n"); goto out;
    }
    tick("logged in");
    ch = libssh2_channel_open_session(s);
    if (!ch || libssh2_channel_exec(ch, (char *)a[4])) { say("exec failed\n"); goto out; }
    for (;;) {
        ssize_t k = libssh2_channel_read(ch, buf, sizeof buf - 1);
        if (k <= 0) break;
        buf[k] = 0; say(buf);
    }
    say("done\n");
out:
    say("[free channel]\n");
    if (ch) libssh2_channel_free(ch);
    say("[disconnect]\n");
    if (s) { libssh2_session_disconnect(s, "bye"); say("[session free]\n"); libssh2_session_free(s); }
    say("[close socket]\n");
    if (sock >= 0) CloseSocket(sock);
    say("[libssh2_exit]\n");
    libssh2_exit();
    say("[close amissl]\n");
    ssl_close();
    say("[close bsdsocket]\n");
    CloseLibrary(SocketBase);
    say("[end]\n");
    FreeArgs(rd);
    return 0;
}
