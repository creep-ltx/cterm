/*
 * book - the phone book, read and written as text:
 *
 *   name=AmigaNet BBS
 *   host=bbs.example.org
 *   port=23
 *   kind=telnet            (or ssh, or serial)
 *   user=...
 *   pass=...
 *   letters=pc             (cterm, amiga, truetype, pc)
 *   chars=pc               (utf8, latin1, pc)
 *   down=Work:Downloads
 *   up=Work:Uploads
 *
 * and for kind=serial the line and the modem's number:
 *
 *   device=serial.device
 *   unit=0
 *   baud=38400
 *   bits=8                 (7)
 *   parity=N               (E, O, M, S)
 *   stop=1                 (2)
 *   flow=rtscts            (none, xonxoff)
 *   buffer=16              (KB)
 *   locked=1               (0: follow CONNECT's speed)
 *   phone=5551234          (none: a null modem, connected at once)
 *   init=AT&F1             (after the modem's own, Settings)
 *   dial=ATDP              (none: the modem's)
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
static const char *const flowname[] = { "none", "rtscts", "xonxoff" };
static const char *const parname[] = { "N", "E", "O", "M", "S" };

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
    e->line = serdef;                   /* the settings' line to start */
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
        int n;
        if (nl)
            *nl = 0;
        else {
            /* longer than any field: its start is kept (put cuts it
             * anyway), the rest of the line is not read as a new one */
            LONG ch;
            while ((ch = FGetC(f)) >= 0 && ch != '\n')
                ;
        }
        n = strlen(line);
        if (n > 0 && line[n - 1] == '\r')
            line[--n] = 0;              /* written on a PC: CRLF */
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
        else if (!stricmp(line, "kind")) {
            e->ssh = !stricmp(v, "ssh");
            e->serial = !stricmp(v, "serial");
        }
        else if (!stricmp(line, "device")) put(e->line.device, sizeof e->line.device, v);
        else if (!stricmp(line, "unit")) e->line.unit = atol(v);
        else if (!stricmp(line, "baud")) e->line.baud = atol(v) > 0 ? atol(v) : 38400;
        else if (!stricmp(line, "bits")) e->line.bits = atol(v) == 7 ? 7 : 8;
        else if (!stricmp(line, "parity")) e->line.parity = pick(parname, 5, v);
        else if (!stricmp(line, "stop")) e->line.stop = atol(v) == 2 ? 2 : 1;
        else if (!stricmp(line, "flow")) e->line.flow = pick(flowname, 3, v);
        else if (!stricmp(line, "rtscts"))      /* an older way to say it */
            e->line.flow = v[0] == '1' ? SF_RTSCTS : SF_NONE;
        else if (!stricmp(line, "buffer")) e->line.bufkb = atol(v) > 0 ? atol(v) : 16;
        else if (!stricmp(line, "locked")) e->line.locked = v[0] != '0';
        else if (!stricmp(line, "phone")) put(e->phone, sizeof e->phone, v);
        else if (!stricmp(line, "init")) put(e->init, sizeof e->init, v);
        else if (!stricmp(line, "dial")) put(e->dial, sizeof e->dial, v);
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

static void number(BPTR f, const char *k, LONG n)
{
    char num[12], *p = num + sizeof num - 1;
    *p = 0;
    if (n < 0)
        n = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    field(f, k, p);
}

BOOL book_save(void)
{
    BPTR f, d;
    int i;
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
        if (e->port)
            number(f, "port", e->port);
        field(f, "kind", e->serial ? "serial" : e->ssh ? "ssh" : "telnet");
        if (e->serial) {
            field(f, "device", e->line.device);
            number(f, "unit", e->line.unit);
            number(f, "baud", e->line.baud);
            number(f, "bits", e->line.bits);
            field(f, "parity", parname[e->line.parity % 5]);
            number(f, "stop", e->line.stop);
            field(f, "flow", flowname[e->line.flow % 3]);
            number(f, "buffer", e->line.bufkb);
            number(f, "locked", e->line.locked);
            field(f, "phone", e->phone);
            field(f, "init", e->init);
            field(f, "dial", e->dial);
        }
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
    out[0] = 0;
    if (e->serial) {
        if (e->phone[0])
            strncat(out, e->phone, max - 1);
        else {
            char *p = num + sizeof num - 1;
            LONG n = e->line.unit;
            *p = 0;
            do { *--p = '0' + n % 10; n /= 10; } while (n);
            strncat(out, e->line.device, max - 14);
            strcat(out, " ");
            strcat(out, p);
        }
        return;
    }
    book_tidy(e);                       /* a URL: its parts */
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
