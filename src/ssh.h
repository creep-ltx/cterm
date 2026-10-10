/*
 * ssh - CTerm's ssh connection: libssh2 on AmiSSL (third_party/).
 * Opens AmiSSL only when a connection is made.
 */
#ifndef SSH_H
#define SSH_H

#include <exec/types.h>

/* What ssh needs from CTerm while it sets up: a line typed in the
 * window (echo FALSE for a password; FALSE: the window closed), and a
 * message shown in it. */
typedef BOOL (*ssh_ask_fn)(const char *prompt, char *buf, int max, BOOL echo);
typedef void (*ssh_say_fn)(const char *text);

/* user@host[:port]: connect, check the host key against ENVARC:CTerm/known_hosts
 * (asking about a new or changed one), log in (key file, then password /
 * keyboard-interactive), open a shell on an xterm-256color pty. */
BOOL ssh_open(const char *target, int cols, int rows,
              ssh_ask_fn ask, ssh_say_fn say);
void ssh_set_key(const char *path);     /* KEY: use this private key */
LONG ssh_socket(void);                  /* for WaitSelect */
LONG ssh_read(UBYTE *buf, LONG max);    /* >0 data, 0 none now, <0 closed */
void ssh_write(const UBYTE *b, LONG n);
void ssh_resize(int cols, int rows);
void ssh_close(void);

#endif
