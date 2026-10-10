/* libssh2 on AmigaOS 3 (bebbo's gcc, libnix): sockets from
 * bsdsocket.library, crypto from AmiSSL (OpenSSL 3 through the library). */
#ifndef LIBSSH2_CONFIG_AMIGA_H
#define LIBSSH2_CONFIG_AMIGA_H
#define HAVE_UNISTD_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_ARPA_INET_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SELECT 1
#define HAVE_IOCTLSOCKET_CASE 1
#define HAVE_SNPRINTF 1
#define LIBSSH2_OPENSSL 1
#define LIBSSH2_NO_DEPRECATED 1
#endif
