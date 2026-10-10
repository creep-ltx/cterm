/*
 * book - the phone book, read and written as text:
 *
 *   name=AmigaNet BBS
 *   host=bbs.example.org
 *   port=23
 *   kind=telnet            (or ssh)
 *   user=...
 *   pass=...
 *   letters=pc             (cterm, amiga, truetype, pc)
 *   chars=pc               (utf8, latin1, pc)
 *   down=Work:Downloads
 *   up=Work:Uploads
 *
 * an empty line between entries. The password is kept as it is: the
 * file is the Amiga's own, as in the BBS dialers of old.
 */
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include "book.h"

#define BOOK_DIR  "ENVARC:CTerm"
#define BOOK_FILE "ENVARC:CTerm/phonebook"

BookEntry *book;
static int bookcap;                     /* entries there is room for */
int nbook;

static const char *const lettername[] = { "cterm", "amiga", "truetype", "pc" };
static const char *const charname[] = { "utf8", "latin1", "pc" };

/* Room for one more entry, eight at a time: a stock A1200 has no 40 KB
 * to keep for 64 entries it may never have. */
int book_need(void)
{
    BookEntry *b;
    int cap;
    if (nbook < bookcap)
        return 1;
    if (nbook >= BOOK_MAX)
        return 0;
    cap = bookcap + 8 > BOOK_MAX ? BOOK_MAX : bookcap + 8;
    b = AllocVec(cap * sizeof *b, MEMF_ANY | MEMF_CLEAR);
    if (!b)
        return 0;
    if (book) {
        CopyMem(book, b, nbook * sizeof *b);
        FreeVec(book);
    }
    book = b;
    bookcap = cap;
    return 1;
}

int book_room(void)
{
    return bookcap;
}

void book_free(void)
{
    if (book)
        FreeVec(book);
    book = NULL;
    bookcap = nbook = 0;
}

void book_blank(BookEntry *e, const char *name)
{
    memset(e, 0, sizeof *e);
    strncpy(e->name, name, sizeof e->name - 1);
}

static void put(char *dst, int max, const char *v)
{
    strncpy(dst, v, max - 1);
    dst[max - 1] = 0;
}

static int pick(const char *const *names, int n, const char *v)
{
    int i;
    for (i = 0; i < n; i++)
        if (!stricmp((char *)names[i], (char *)v))
            return i;
    return 0;
}

void book_load(void)
{
    BPTR f = Open((STRPTR)BOOK_FILE, MODE_OLDFILE);
    char line[300];
    BookEntry *e = NULL;
    nbook = 0;
    if (!f)
        return;

    while (FGets(f, (STRPTR)line, sizeof line)) {
        char *v, *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        if (!line[0]) {
            e = NULL;                   /* the next entry */
            continue;
        }
        v = strchr(line, '=');
        if (!v)
            continue;
        *v++ = 0;
        if (!e) {
            if (!book_need())
                break;
            e = &book[nbook++];
            book_blank(e, "");
        }
        if (!stricmp(line, "name")) put(e->name, sizeof e->name, v);
        else if (!stricmp(line, "host")) put(e->host, sizeof e->host, v);
        else if (!stricmp(line, "user")) put(e->user, sizeof e->user, v);
        else if (!stricmp(line, "pass")) put(e->pass, sizeof e->pass, v);
        else if (!stricmp(line, "down")) put(e->down, sizeof e->down, v);
        else if (!stricmp(line, "up")) put(e->up, sizeof e->up, v);
        else if (!stricmp(line, "port")) e->port = atoi(v);
        else if (!stricmp(line, "kind")) e->ssh = !stricmp(v, "ssh");
        else if (!stricmp(line, "letters")) e->letters = pick(lettername, 4, v);
        else if (!stricmp(line, "chars")) e->chars = pick(charname, 3, v);
    }
    Close(f);
}

static void field(BPTR f, const char *k, const char *v)
{
    if (!v[0])
        return;
    FPuts(f, (STRPTR)k);
    FPutC(f, '=');
    FPuts(f, (STRPTR)v);
    FPutC(f, '\n');
}

BOOL book_save(void)
{
    BPTR f, d;
    int i;
    char num[12];
    d = Lock((STRPTR)BOOK_DIR, ACCESS_READ);
    if (!d)
        d = CreateDir((STRPTR)BOOK_DIR);
    if (d)
        UnLock(d);
    f = Open((STRPTR)BOOK_FILE, MODE_NEWFILE);
    if (!f)
        return FALSE;
    for (i = 0; i < nbook; i++) {
        const BookEntry *e = &book[i];
        if (i)
            FPutC(f, '\n');
        field(f, "name", e->name[0] ? e->name : "?");
        field(f, "host", e->host);
        if (e->port) {
            char *p = num + sizeof num - 1;
            int n = e->port;
            *p = 0;
            do { *--p = '0' + n % 10; n /= 10; } while (n);
            field(f, "port", p);
        }
        field(f, "kind", e->ssh ? "ssh" : "telnet");
        field(f, "user", e->user);
        field(f, "pass", e->pass);
        field(f, "letters", lettername[e->letters & 3]);
        field(f, "chars", charname[e->chars % 3]);
        field(f, "down", e->down);
        field(f, "up", e->up);
    }
    Close(f);
    return TRUE;
}

int book_find(const char *name)
{
    int i;
    for (i = 0; i < nbook; i++)
        if (!stricmp(book[i].name, (char *)name))
            return i;
    return -1;
}

void book_tidy(BookEntry *e)
{
    char *h = e->host, *p;
    if (!strnicmp(h, "telnet://", 9)) {
        e->ssh = 0;
        memmove(h, h + 9, strlen(h + 9) + 1);
    } else if (!strnicmp(h, "ssh://", 6)) {
        e->ssh = 1;
        memmove(h, h + 6, strlen(h + 6) + 1);
    } else {
        /* host:port typed plainly (one colon: not an IPv6 address) */
        if ((p = strchr(h, ':')) && !strchr(p + 1, ':') && p[1] >= '0' &&
            p[1] <= '9') {
            e->port = atoi(p + 1);
            *p = 0;
        }
        return;
    }
    if ((p = strchr(h, '/')))           /* a path: nothing to call */
        *p = 0;
    if ((p = strchr(h, '@'))) {         /* user@ */
        *p = 0;
        if (h[0])
            put(e->user, sizeof e->user, h);
        memmove(h, p + 1, strlen(p + 1) + 1);
    }
    if ((p = strrchr(h, ':')) && p[1] >= '0' && p[1] <= '9') {
        e->port = atoi(p + 1);
        *p = 0;
    }
}

void book_target(const BookEntry *e0, char *out, int max)
{
    char num[12];
    BookEntry c = *e0, *e = &c;
    book_tidy(e);                       /* a URL: its parts */
    out[0] = 0;
    if (e->ssh && e->user[0]) {
        strncat(out, e->user, max - 2);
        strcat(out, "@");
    }
    strncat(out, e->host, max - strlen(out) - 8);
    if (e->port && e->port != (e->ssh ? 22 : 23)) {
        char *p = num + sizeof num - 1;
        int n = e->port;
        *p = 0;
        do { *--p = '0' + n % 10; n /= 10; } while (n);
        strcat(out, ":");
        strcat(out, p);
    }
}
