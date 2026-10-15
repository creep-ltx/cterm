/*
 * ftps - TLS for FTP (FTPS, explicit: AUTH TLS) on AmiSSL 5, for ftp.c
 * (Parts/Files). AmiSSL is opened when the first FTPS place calls and
 * closed when the last one is gone. A file of its own, so that the
 * OpenSSL headers stay out of CTerm's.
 */
#ifndef FTPS_H
#define FTPS_H

#define TLS_WR (-2)                     /* wait until the socket can be read */
#define TLS_WW (-3)                     /* ... written */

/* AmiSSL opened, one client context (store: AmiSSL's authorities are
 * there - AmiSSL:Certs, and AmiSSL:UserCerts when user). NULL: ready;
 * else why not. */
const char *tls_open(struct Library *socketbase, BOOL store, BOOL user);
void tls_close(void);                   /* AmiSSL closed */
/* A TLS connection on the (non-blocking) socket fd to host (its name:
 * SNI and the certificate's check). reuse: the control connection, whose
 * session a data connection takes up (as hosts that insist want). NULL:
 * no memory. */
void *tls_new(LONG fd, const char *host, void *reuse);
/* The handshake, a step: 1 done, TLS_WR / TLS_WW wait, -1 failed (why:
 * tls_why) */
int tls_shake(void *t);
LONG tls_read(void *t, UBYTE *b, LONG n);       /* >0, 0 the end, -1, TLS_W* */
LONG tls_write(void *t, const UBYTE *b, LONG n);
BOOL tls_pending(void *t);              /* bytes read, not yet taken */
void tls_end(void *t, BOOL notify);     /* close_notify sent (notify); freed */
/* The host's certificate: signed by an authority AmiSSL knows, for the
 * name called */
BOOL tls_trusted(void *t);
void tls_print(void *t, char *fp);      /* "SHA256:..." (60 bytes), "" none */
const char *tls_why(void);              /* what the last failure was */
const char *tls_version(void *t);       /* "TLSv1.3" */

#endif
