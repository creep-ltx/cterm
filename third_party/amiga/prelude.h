/* Forced in front of every libssh2 file (-include): the Amiga libraries'
 * call macros, so socket and OpenSSL calls go through the bases. The C
 * library's headers come first: bsdsocket's macros (gethostid...) would
 * otherwise rewrite their declarations. */
#ifndef LIBSSH2_AMIGA_PRELUDE_H
#define LIBSSH2_AMIGA_PRELUDE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/bsdsocket.h>
#include <proto/amissl.h>
/* BSD's mbuf field macros: libssh2 has parameters by these names */
#undef m_len
#undef m_data
#undef m_next
#undef m_type
#undef m_flags
#undef m_nextpkt
#undef m_act
extern struct Library *SocketBase, *AmiSSLBase;
#define select(n, r, w, e, t) WaitSelect(n, r, w, e, t, NULL)
#ifndef FIONBIO
#define FIONBIO 0x8004667EUL      /* _IOW('f', 126, long) */
#endif
#endif
