/*
 * book - CTerm's phone book: the places called, each with how to call it
 * (telnet or ssh, the port), who to log in as, and how to show it (the
 * letters, the characters). Kept in ENVARC:CTerm/phonebook as text.
 */
#ifndef BOOK_H
#define BOOK_H

#include <exec/types.h>

#define BOOK_MAX 64

enum { BL_CTERM, BL_AMIGA, BL_TT, BL_PC };     /* letters: CTerm's own... */
enum { BC_UTF8, BC_LATIN1, BC_PC };            /* characters */

typedef struct {
    char name[48];
    char host[128];         /* a name, an IP number or a telnet:// ssh:// URL */
    char user[64], pass[64];
    char down[160], up[160];        /* drawers; empty: the global ones */
    int port;               /* 0: the usual one (23 telnet, 22 ssh) */
    int ssh;                /* 1: ssh, 0: telnet */
    int letters, chars;
} BookEntry;

extern BookEntry *book;              /* BOOK_MAX of them, or NULL */
extern int nbook;

int book_need(void);                    /* room for one more entry */
int book_room(void);                    /* the room there is */
void book_free(void);
void book_load(void);
BOOL book_save(void);
int book_find(const char *name);        /* -1: none */
/* A URL in host (telnet://, ssh://user@host:port/) into its fields. */
void book_tidy(BookEntry *e);
/* What to dial: "host:port" for telnet, "user@host:port" for ssh. */
void book_target(const BookEntry *e, char *out, int max);
void book_blank(BookEntry *e, const char *name);

#endif
