/*
 * zmodem - Zmodem both ways, from Chuck Forsberg's description (1988),
 * with the traps BBX's Go version met against lrzsz written down in
 * Knowledge/go/zmodem-and-telnet.md. Plain C: the Amiga and the PC tests
 * build the same file; the host and the files come through XferIO.
 *
 * Receiving: ZRINIT (32-bit CRCs, full streaming), each ZFILE opened in
 * the download drawer, data written as it comes, ZRPOS at the last good
 * byte on any error or silence. Sending: ZFILE, then the file streamed in
 * 1 KB subpackets (ZCRCQ every 32 KB, at most 256 KB not acknowledged),
 * going back on ZRPOS. Five CANs in a row cancel either way.
 */
#include <string.h>
#include <stdlib.h>
#include "xfer.h"
#ifdef ZDEBUG
#include <stdio.h>
#define ZT(...) fprintf(stderr, __VA_ARGS__)
#else
#define ZT(...) ((void)0)
#endif

#define ZPAD   '*'
#define ZDLE   0x18
#define ZBIN   'A'
#define ZHEX   'B'
#define ZBIN32 'C'

enum { ZRQINIT, ZRINIT, ZSINIT, ZACK, ZFILE, ZSKIP, ZNAK, ZABORT, ZFIN,
       ZRPOS, ZDATA, ZEOF, ZFERR, ZCRC, ZCHALLENGE, ZCOMPL, ZCAN, ZFREECNT,
       ZCOMMAND, ZSTDERR };

#define ZCRCE 'h'                       /* frame ends, header follows */
#define ZCRCG 'i'                       /* frame goes on */
#define ZCRCQ 'j'                       /* goes on, ZACK wanted */
#define ZCRCW 'k'                       /* ends, ZACK wanted */
#define ZRUB0 'l'                       /* 0x7f */
#define ZRUB1 'm'                       /* 0xff */

#define CANFDX  0x01                    /* ZRINIT's flags */
#define CANOVIO 0x02
#define CANFC32 0x20
#define ESCCTL  0x40

#define ZCBIN   1                       /* ZFILE: binary */

/* reading's answers besides a byte */
#define TIMEOUT (-1)
#define GONE    (-2)
#define CANCEL  (-3)
#define BAD     (-4)

#define MAXDATA 8192                    /* lsz -8 sends 8 KB subpackets */
#define BLOCK   1024                    /* what we send */
#define QEVERY  32768                   /* a ZCRCQ every this much */
#define WINDOW  262144                  /* never more not acknowledged */
#define WAIT    10000                   /* ms: a header's time */

struct Z {
    XferIO *io;
    const unsigned char *pre;           /* bytes seen before we began */
    long pren, prep;
    unsigned char in[2048];
    long inn, inp;
    int unget;                          /* a byte read too far, or -1 */
    int cans;
    unsigned char out[4096];
    long outn;
    int last;                           /* the last byte sent: CR after @ */
    int escctl, use32;
    int htype, hcrc32;                  /* the last header read */
    unsigned char hdr[4];
    unsigned char data[MAXDATA + 16];
    long dlen;
};

/* ---- CRCs: CRC-16/XMODEM (0x1021) and CRC-32 (IEEE) ---- */

static unsigned short crc16t[256];
static unsigned long crc32t[256];

static void crc_init(void)
{
    int i, k;
    if (crc32t[1])
        return;
    for (i = 0; i < 256; i++) {
        unsigned short c = (unsigned short)(i << 8);
        unsigned long d = (unsigned long)i;
        for (k = 0; k < 8; k++) {
            c = (c & 0x8000) ? (unsigned short)((c << 1) ^ 0x1021)
                             : (unsigned short)(c << 1);
            d = (d & 1) ? (d >> 1) ^ 0xEDB88320UL : d >> 1;
        }
        crc16t[i] = c;
        crc32t[i] = d;
    }
}

#define CRC16(c, b) ((unsigned short)(((c) << 8) ^ crc16t[(((c) >> 8) ^ (b)) & 255]))
#define CRC32(c, b) (crc32t[((c) ^ (b)) & 255] ^ ((c) >> 8))

/* ---- the host, both ways ---- */

static void flush(struct Z *z)
{
    if (z->outn) {
        z->io->put(z->io->u, z->out, z->outn);
        z->outn = 0;
    }
}

static void obyte(struct Z *z, int c)
{
    if (z->outn >= (long)sizeof z->out)
        flush(z);
    z->out[z->outn++] = (unsigned char)c;
}

static void ostr(struct Z *z, const char *s)
{
    while (*s)
        obyte(z, (unsigned char)*s++);
}

/* One byte from the host within ms. Counts CANs: five in a row is the
 * other side giving up (ZDLE is CAN too, but never twice in a row). */
static int zgetc(struct Z *z, long ms)
{
    int c;
    if (z->unget >= 0) {
        c = z->unget;
        z->unget = -1;
        return c;
    }
    if (z->prep < z->pren)
        c = z->pre[z->prep++];
    else {
        if (z->inp >= z->inn) {
            long k;
            flush(z);                   /* nothing waits on a full buffer */
            if (z->io->stop && z->io->stop(z->io->u))
                return CANCEL;
            k = z->io->get(z->io->u, z->in, sizeof z->in, ms);
            if (k < 0)
                return GONE;
            if (k == 0)
                return TIMEOUT;
            z->inn = k;
            z->inp = 0;
        }
        c = z->in[z->inp++];
    }
    if (c == ZDLE) {
        if (++z->cans >= 5)
            return CANCEL;
    } else
        z->cans = 0;
    return c;
}

/* A ZDLE-escaped byte: the byte, 0x100|end for a subpacket's end, or an
 * error. Bare XON/XOFF are noise (real ones come escaped). */
static int zdl(struct Z *z, long ms)
{
    int c;
    for (;;) {
        c = zgetc(z, ms);
        if (c < 0)
            return c;
        if (c == 0x11 || c == 0x13 || c == 0x91 || c == 0x93)
            continue;
        if (c != ZDLE)
            return c;
        for (;;) {
            c = zgetc(z, ms);
            if (c < 0)
                return c;
            switch (c) {
            case ZCRCE: case ZCRCG: case ZCRCQ: case ZCRCW:
                return 0x100 | c;
            case ZRUB0:
                return 0x7f;
            case ZRUB1:
                return 0xff;
            case 0x11: case 0x13: case 0x91: case 0x93:
                continue;
            default:
                if ((c & 0x60) == 0x40)
                    return c ^ 0x40;
                return BAD;
            }
        }
    }
}

/* ---- headers ---- */

static void pos_hdr(struct Z *z, unsigned long p)
{
    z->hdr[0] = (unsigned char)p;
    z->hdr[1] = (unsigned char)(p >> 8);
    z->hdr[2] = (unsigned char)(p >> 16);
    z->hdr[3] = (unsigned char)(p >> 24);
}

static unsigned long hdr_pos(const struct Z *z)
{
    return (unsigned long)z->hdr[0] | ((unsigned long)z->hdr[1] << 8) |
           ((unsigned long)z->hdr[2] << 16) | ((unsigned long)z->hdr[3] << 24);
}

static void ohex(struct Z *z, int b)
{
    static const char d[] = "0123456789abcdef";
    obyte(z, d[(b >> 4) & 15]);
    obyte(z, d[b & 15]);
}

/* A hex header (what a receiver sends, and ZFIN): CR LF|0x80, and XON
 * after all but ZFIN and ZACK. */
static void send_hex(struct Z *z, int type)
{
    unsigned short crc = 0;
    int i;
    ostr(z, "**\x18" "B");
    ohex(z, type);
    crc = CRC16(crc, type);
    for (i = 0; i < 4; i++) {
        ohex(z, z->hdr[i]);
        crc = CRC16(crc, z->hdr[i]);
    }
    ohex(z, crc >> 8);
    ohex(z, crc & 255);
    obyte(z, '\r');
    obyte(z, 0x8a);
    if (type != ZFIN && type != ZACK)
        obyte(z, 0x11);
    flush(z);
}

/* A byte with what needs escaping escaped: ZDLE, XON/XOFF and DLE (both
 * halves), 0x7f and 0xff (telnet), CR after '@', every control when the
 * receiver asks. */
static void oesc(struct Z *z, int c)
{
    int esc = 0;
    c &= 255;
    switch (c) {
    case ZDLE: case 0x10: case 0x11: case 0x13:
    case 0x90: case 0x91: case 0x93:
        esc = 1;
        break;
    case 0x7f:
        obyte(z, ZDLE);
        obyte(z, ZRUB0);
        z->last = c;
        return;
    case 0xff:
        obyte(z, ZDLE);
        obyte(z, ZRUB1);
        z->last = c;
        return;
    case 0x0d: case 0x8d:
        esc = z->escctl || (z->last & 0x7f) == '@';
        break;
    default:
        esc = z->escctl && (c & 0x60) == 0;
    }
    if (esc) {
        obyte(z, ZDLE);
        obyte(z, c ^ 0x40);
    } else
        obyte(z, c);
    z->last = c;
}

/* A binary header (what a sender sends): CRC-32 when the receiver can. */
static void send_bin(struct Z *z, int type)
{
    int i;
    obyte(z, ZPAD);
    obyte(z, ZDLE);
    if (z->use32) {
        unsigned long crc = 0xFFFFFFFFUL;
        obyte(z, ZBIN32);
        oesc(z, type);
        crc = CRC32(crc, type);
        for (i = 0; i < 4; i++) {
            oesc(z, z->hdr[i]);
            crc = CRC32(crc, z->hdr[i]);
        }
        crc = ~crc;
        for (i = 0; i < 4; i++) {
            oesc(z, (int)(crc & 255));
            crc >>= 8;
        }
    } else {
        unsigned short crc = 0;
        obyte(z, ZBIN);
        oesc(z, type);
        crc = CRC16(crc, type);
        for (i = 0; i < 4; i++) {
            oesc(z, z->hdr[i]);
            crc = CRC16(crc, z->hdr[i]);
        }
        oesc(z, crc >> 8);
        oesc(z, crc & 255);
    }
}

/* A data subpacket ending with end; CRC as the headers'. */
static void send_data(struct Z *z, const unsigned char *b, long n, int end)
{
    long i;
    if (z->use32) {
        unsigned long crc = 0xFFFFFFFFUL;
        for (i = 0; i < n; i++) {
            oesc(z, b[i]);
            crc = CRC32(crc, b[i]);
        }
        obyte(z, ZDLE);
        obyte(z, end);
        crc = ~CRC32(crc, end);
        for (i = 0; i < 4; i++) {
            oesc(z, (int)(crc & 255));
            crc >>= 8;
        }
    } else {
        unsigned short crc = 0;
        for (i = 0; i < n; i++) {
            oesc(z, b[i]);
            crc = CRC16(crc, b[i]);
        }
        obyte(z, ZDLE);
        obyte(z, end);
        crc = CRC16(crc, end);
        oesc(z, crc >> 8);
        oesc(z, crc & 255);
    }
    if (end == ZCRCW) {
        obyte(z, 0x11);
        flush(z);
    }
}

static int hexval(struct Z *z, long ms)
{
    int i, v = 0;
    for (i = 0; i < 2; i++) {
        int c = zgetc(z, ms);
        if (c < 0)
            return c;
        c &= 0x7f;
        if (c >= '0' && c <= '9') v = v * 16 + c - '0';
        else if (c >= 'a' && c <= 'f') v = v * 16 + c - 'a' + 10;
        else return BAD;
    }
    return v;
}

static int read_hex(struct Z *z, long ms)
{
    unsigned short crc = 0;
    int i, t, c, a, b;
    if ((t = hexval(z, ms)) < 0)
        return t;
    crc = CRC16(crc, t);
    for (i = 0; i < 4; i++) {
        if ((c = hexval(z, ms)) < 0)
            return c;
        z->hdr[i] = (unsigned char)c;
        crc = CRC16(crc, c);
    }
    if ((a = hexval(z, ms)) < 0 || (b = hexval(z, ms)) < 0)
        return a < 0 ? a : b;
    if (((a << 8) | b) != crc)
        return BAD;
    /* the trailer: CR, LF|0x80, maybe XON - not data that follows */
    for (i = 0; i < 3; i++) {
        c = zgetc(z, 1000);
        if (c < 0)
            break;
        if ((c & 0x7f) != '\r' && (c & 0x7f) != '\n' && c != 0x11) {
            z->unget = c;
            break;
        }
    }
    z->hcrc32 = 0;
    return t;
}

static int read_bin(struct Z *z, int is32, long ms)
{
    int i, t, c;
    if ((t = zdl(z, ms)) < 0)
        return t;
    if (t & 0x100)
        return BAD;
    for (i = 0; i < 4; i++) {
        if ((c = zdl(z, ms)) < 0)
            return c;
        if (c & 0x100)
            return BAD;
        z->hdr[i] = (unsigned char)c;
    }
    if (is32) {
        unsigned long crc = 0xFFFFFFFFUL, got = 0;
        crc = CRC32(crc, t);
        for (i = 0; i < 4; i++)
            crc = CRC32(crc, z->hdr[i]);
        for (i = 0; i < 4; i++) {
            if ((c = zdl(z, ms)) < 0)
                return c;
            got |= (unsigned long)(c & 255) << (8 * i);
        }
        if (got != (~crc & 0xFFFFFFFFUL))
            return BAD;
    } else {
        unsigned short crc = 0;
        int a, b;
        crc = CRC16(crc, t);
        for (i = 0; i < 4; i++)
            crc = CRC16(crc, z->hdr[i]);
        if ((a = zdl(z, ms)) < 0 || (b = zdl(z, ms)) < 0)
            return a < 0 ? a : b;
        if ((((a & 255) << 8) | (b & 255)) != crc)
            return BAD;
    }
    z->hcrc32 = is32;
    return t;
}

/* The next header: its type, or TIMEOUT/GONE/CANCEL/BAD (BAD also after
 * about 256 KB with no header in it). */
static int get_hdr(struct Z *z, long ms)
{
    long junk = 0;
    for (;;) {
        int c = zgetc(z, ms);
        if (c < 0)
            return c;
        if (c != ZPAD) {
            if (++junk > WINDOW)
                return BAD;
            continue;
        }
        do
            c = zgetc(z, ms);
        while (c == ZPAD);
        if (c < 0)
            return c;
        if (c != ZDLE)
            continue;
        c = zgetc(z, ms);
        if (c < 0)
            return c;
        if (c == ZHEX)
            c = read_hex(z, ms);
        else if (c == ZBIN)
            c = read_bin(z, 0, ms);
        else if (c == ZBIN32)
            c = read_bin(z, 1, ms);
        else
            continue;
        ZT("hdr %d [%02x %02x %02x %02x]\n", c, z->hdr[0], z->hdr[1], z->hdr[2], z->hdr[3]);
        if (c != BAD)
            return z->htype = c;
        if (++junk > WINDOW)
            return BAD;
    }
}

/* A data subpacket into z->data: its end (ZCRCx) or an error. */
static int get_data(struct Z *z, long ms)
{
    unsigned long c32 = 0xFFFFFFFFUL;
    unsigned short c16 = 0;
    int c, i;
    z->dlen = 0;
    for (;;) {
        c = zdl(z, ms);
        if (c < 0)
            return c;
        if (c & 0x100) {
            int end = c & 255;
            if (z->hcrc32) {
                unsigned long got = 0;
                c32 = CRC32(c32, end);
                for (i = 0; i < 4; i++) {
                    if ((c = zdl(z, ms)) < 0)
                        return c;
                    got |= (unsigned long)(c & 255) << (8 * i);
                }
                if (got != (~c32 & 0xFFFFFFFFUL))
                    return BAD;
            } else {
                int a, b;
                c16 = CRC16(c16, end);
                if ((a = zdl(z, ms)) < 0 || (b = zdl(z, ms)) < 0)
                    return a < 0 ? a : b;
                if ((((a & 255) << 8) | (b & 255)) != c16)
                    return BAD;
            }
            return end;
        }
        if (z->dlen >= MAXDATA)
            return BAD;
        z->data[z->dlen++] = (unsigned char)c;
        if (z->hcrc32)
            c32 = CRC32(c32, c);
        else
            c16 = CRC16(c16, c);
    }
}

/* Giving up: CANs the other side knows, and backspaces over them. */
static void send_cancel(struct Z *z)
{
    int i;
    for (i = 0; i < 10; i++)
        obyte(z, 0x18);
    for (i = 0; i < 10; i++)
        obyte(z, 8);
    flush(z);
}

/* At the end: what was read and not used (the host's prompt after "OO")
 * goes back to the caller, for the screen. */
static void z_rest(struct Z *z)
{
    XferIO *io = z->io;
    unsigned char b;
    if (!io->rest)
        return;
    if (z->unget >= 0) {
        b = (unsigned char)z->unget;
        z->unget = -1;
        io->rest(io->u, &b, 1);
    }
    if (z->prep < z->pren)
        io->rest(io->u, z->pre + z->prep, z->pren - z->prep);
    if (z->inp < z->inn)
        io->rest(io->u, z->in + z->inp, z->inn - z->inp);
    z->prep = z->pren;
    z->inp = z->inn;
}

static void z_begin(struct Z *z, XferIO *io, const unsigned char *pre,
                    long n)
{
    memset(z, 0, sizeof *z);
    crc_init();
    z->io = io;
    z->pre = pre;
    z->pren = pre ? n : 0;
    z->unget = -1;
}

static unsigned long num(const char **p, int base)
{
    unsigned long v = 0;
    while (**p == ' ')
        (*p)++;
    while (**p >= '0' && **p < '0' + base)
        v = v * base + (unsigned long)(*(*p)++ - '0');
    return v;
}

/* ---- the start ---- */


static int zm_receive_(struct Z *z, XferIO *io, const unsigned char *pre,
                       long n);
static int zm_send_(struct Z *z, unsigned char *blk, XferIO *io,
                    const char *const *paths, int npaths,
                    const unsigned char *pre, long n);

/* ---- receiving ---- */

static void send_zrinit(struct Z *z)
{
    pos_hdr(z, 0);
    z->hdr[3] = CANFDX | CANOVIO | CANFC32;
    send_hex(z, ZRINIT);
}

static void send_pos(struct Z *z, int type, unsigned long p)
{
    pos_hdr(z, p);
    send_hex(z, type);
}

int zm_receive(XferIO *io, const unsigned char *pre, long n)
{
    struct Z *z = malloc(sizeof *z);    /* 15 KB, only while it runs */
    int rc;
    if (!z)
        return -1;
    rc = zm_receive_(z, io, pre, n);
    free(z);
    return rc;
}

static int zm_receive_(struct Z *z, XferIO *io, const unsigned char *pre,
                       long n)
{
    void *f = 0;
    char name[256];
    unsigned long have = 0, size = 0;
    int files = 0, tries = 0, t, r, i;

    z_begin(z, io, pre, n);
    send_zrinit(z);
    for (;;) {
        t = get_hdr(z, WAIT);
        if (t == CANCEL || t == GONE)
            goto fail;
        if (t == TIMEOUT || t == BAD) {
            if (++tries > 10)
                goto fail;
            if (f)
                send_pos(z, ZRPOS, have);
            else
                send_zrinit(z);
            continue;
        }
        switch (t) {
        case ZRQINIT:
            send_zrinit(z);
            break;
        case ZSINIT:                    /* its attention string: unused */
            r = get_data(z, WAIT);
            pos_hdr(z, 1);
            send_hex(z, r >= 0 ? ZACK : ZNAK);
            break;
        case ZFILE: {
            const char *p, *base;
            r = get_data(z, WAIT);
            if (r < 0) {
                if (r == CANCEL || r == GONE)
                    goto fail;
                pos_hdr(z, 0);
                send_hex(z, ZNAK);
                break;
            }
            z->data[z->dlen < MAXDATA ? z->dlen : MAXDATA] = 0;
            /* the name without any drawers (a host must not choose
             * where a file goes), then "size mtime mode ..." */
            base = (const char *)z->data;
            for (p = base; *p; p++)
                if (*p == '/' || *p == ':' || *p == '\\')
                    base = p + 1;
            for (i = 0; i < (int)sizeof name - 1 && base[i]; i++)
                name[i] = base[i];
            name[i] = 0;
            if (!name[0])
                strcpy(name, "file");
            p = (const char *)z->data + strlen((const char *)z->data) + 1;
            size = (p < (const char *)z->data + z->dlen) ? num(&p, 10) : 0;
            if (io->batch && p < (const char *)z->data + z->dlen) {
                /* mtime (octal), mode, serial, then files and bytes left */
                unsigned long fl, bl;
                (void)num(&p, 8);
                (void)num(&p, 8);
                (void)num(&p, 10);
                fl = num(&p, 10);
                bl = num(&p, 10);
                if (fl || bl)
                    io->batch(io->u, (long)fl, bl);
            }
            if (f) {                    /* the last one never ended */
                io->wclose(io->u, f, 0);
                io->show(io->u, name, have, size, 3);
            }
            have = 0;
            f = io->wopen(io->u, name, size, name, sizeof name, &have);
            if (!f) {
                io->show(io->u, name, 0, size, 3);
                pos_hdr(z, 0);
                send_hex(z, ZSKIP);     /* and nothing more: see the notes */
                break;
            }
            io->show(io->u, name, have, size, 0);
            /* from the start, or where a part from an earlier try ends
             * (crash recovery: the sender seeks there) */
            send_pos(z, ZRPOS, have);
            tries = 0;
            break;
        }
        case ZDATA:
            if (!f) {
                send_zrinit(z);
                break;
            }
            if (hdr_pos(z) != have) {   /* not where we are: back there */
                send_pos(z, ZRPOS, have);
                break;
            }
            for (;;) {
                r = get_data(z, WAIT);
                if (r < 0) {
                    if (r == CANCEL || r == GONE)
                        goto fail;
                    send_pos(z, ZRPOS, have);
                    break;
                }
                if (z->dlen &&
                    io->wwrite(io->u, f, z->data, z->dlen) != z->dlen) {
                    io->show(io->u, name, have, size, 3);
                    goto fail;          /* the disk is full, or gone */
                }
                have += (unsigned long)z->dlen;
                io->show(io->u, name, have, size, 0);
                tries = 0;
                if (r == ZCRCW || r == ZCRCQ)
                    send_pos(z, ZACK, have);
                if (r == ZCRCW || r == ZCRCE)
                    break;
            }
            break;
        case ZEOF:
            if (!f) {
                send_zrinit(z);
                break;
            }
            if (hdr_pos(z) != have)     /* data still to come: ignore */
                break;
            io->wclose(io->u, f, 1);
            f = 0;
            files++;
            io->show(io->u, name, have, size, 1);
            send_zrinit(z);
            break;
        case ZFIN: {
            int c = -1;
            if (f) {                    /* it ended inside a file: a part */
                io->wclose(io->u, f, 0);
                f = 0;
                io->show(io->u, name, have, size, 3);
            }
            pos_hdr(z, 0);
            send_hex(z, ZFIN);
            /* "OO", maybe after what is left of its header's CR LF|0x80
             * and XON; anything else is the host's again (its prompt) */
            for (i = 0; i < 4; i++) {
                c = zgetc(z, 2000);
                if (c < 0)
                    break;
                if (c == 'O') {
                    if ((c = zgetc(z, 500)) == 'O')
                        c = -1;
                    break;
                }
                if ((c & 0x7f) != '\r' && (c & 0x7f) != '\n' && c != 0x11)
                    break;
            }
            if (c >= 0)
                z->unget = c;
            z_rest(z);
            return files;
        }
        case ZCOMMAND:                  /* never: the host runs nothing here */
            pos_hdr(z, 0);
            send_hex(z, ZCOMPL);
            break;
        case ZFREECNT:
            pos_hdr(z, 0x7FFFFFFFUL);
            send_hex(z, ZACK);
            break;
        default:
            break;
        }
    }
fail:
    if (f)
        io->wclose(io->u, f, 0);
    send_cancel(z);
    return -1;
}

/* ---- sending ---- */

/* The next header, answering the side matters on the way. */
static int wait_hdr(struct Z *z, long ms)
{
    return get_hdr(z, ms);
}

int zm_send(XferIO *io, const char *const *paths, int npaths,
            const unsigned char *pre, long n)
{
    struct Z *z = malloc(sizeof *z + BLOCK);
    int rc;
    if (!z)
        return -1;
    rc = zm_send_(z, (unsigned char *)(z + 1), io, paths, npaths, pre, n);
    free(z);
    return rc;
}

static int zm_send_(struct Z *z, unsigned char *blk, XferIO *io,
                    const char *const *paths, int npaths,
                    const unsigned char *pre, long n)
{
    unsigned long rxbuf;
    int files = 0, fi, t, tries;

    z_begin(z, io, pre, n);
    /* the receiver's ZRINIT: in what came already, or ask for it */
    for (tries = 0;; tries++) {
        t = get_hdr(z, WAIT);
        ZT("first t=%d\n", t);
        if (t == ZRINIT)
            break;
        if (t == CANCEL || t == GONE || tries > 6)
            goto fail;
        pos_hdr(z, 0);
        send_hex(z, ZRQINIT);
    }
    rxbuf = (unsigned long)z->hdr[0] | ((unsigned long)z->hdr[1] << 8);
    z->use32 = (z->hdr[3] & CANFC32) != 0;
    z->escctl = (z->hdr[3] & ESCCTL) != 0;

    for (fi = 0; fi < npaths; fi++) {
        unsigned long size = 0, pos = 0, acked = 0, sentq = 0;
        const char *p, *name = paths[fi];
        void *f;
        char info[300];
        int k, done = 0;
        for (p = paths[fi]; *p; p++)
            if (*p == '/' || *p == ':')
                name = p + 1;
        f = io->ropen(io->u, paths[fi], &size);
        if (!f) {
            io->show(io->u, name, 0, 0, 3);
            continue;
        }
        /* "name\0size mtime mode serial files-left bytes-left\0" */
        k = 0;
        while (name[k] && k < 200) {
            info[k] = name[k];
            k++;
        }
        info[k++] = 0;
        {
            char num_[12], *q = num_ + sizeof num_ - 1;
            unsigned long v = size;
            *q = 0;
            do { *--q = (char)('0' + v % 10); v /= 10; } while (v);
            strcpy(info + k, q);
            k += (int)strlen(q);
            strcpy(info + k, " 0 100644 0 1");
            k += (int)strlen(" 0 100644 0 1");
            info[k++] = 0;
        }
        io->show(io->u, name, 0, size, 0);
    zfile:
        for (tries = 0;; tries++) {
            if (tries > 10)
                goto fail_f;
            pos_hdr(z, 0);
            z->hdr[3] = ZCBIN;
            send_bin(z, ZFILE);
            send_data(z, (const unsigned char *)info, k, ZCRCW);
            for (;;) {
                t = wait_hdr(z, WAIT);
                if (t == ZRINIT || t == ZACK)
                    continue;           /* a stray one from before */
                break;
            }
            if (t == CANCEL || t == GONE)
                goto fail_f;
            if (t == ZRPOS) {
                pos = hdr_pos(z);
                break;
            }
            if (t == ZSKIP) {
                io->rclose(io->u, f);
                io->show(io->u, name, 0, size, 2);
                done = 2;
                break;
            }
            /* ZNAK, a timeout, nonsense: offer it again */
        }
        if (done)
            continue;
    data:
        if (pos > size)
            pos = size;
        if (io->rseek(io->u, f, pos))
            goto fail_f;
        acked = sentq = pos;
        pos_hdr(z, pos);
        send_bin(z, ZDATA);
        for (;;) {
            long got = io->rread(io->u, f, blk,
                                 size - pos < BLOCK ? (long)(size - pos)
                                                    : BLOCK);
            int end, c;
            if (got < 0)
                goto fail_f;
            if (got == 0 && pos < size)
                size = pos;             /* it shrank while it went: it ends
                                         * here (ZEOF says where) */
            if (pos + (unsigned long)got >= size)
                end = ZCRCE;
            else if (rxbuf && pos + (unsigned long)got - acked >= rxbuf)
                end = ZCRCW;            /* a receiver with a small buffer */
            else if (pos + (unsigned long)got - sentq >= QEVERY) {
                end = ZCRCQ;
                sentq = pos + (unsigned long)got;
            } else
                end = ZCRCG;
            send_data(z, blk, got, end);
            pos += (unsigned long)got;
            io->show(io->u, name, pos, size, 0);
            if (end == ZCRCE)
                break;
            /* what the receiver says meanwhile: ZRPOS, ZACK - and wait
             * when too much is not acknowledged (or it asked: ZCRCW) */
            for (;;) {
                long ms = (end == ZCRCW || pos - acked > WINDOW) ? WAIT : 0;
                if (ms == 0 && z->prep >= z->pren && z->inp >= z->inn &&
                    z->unget < 0) {
                    flush(z);
                    k = (int)io->get(io->u, z->in, sizeof z->in, 0);
                    if (k < 0)
                        goto fail_f;
                    if (k == 0)
                        break;
                    z->inn = k;
                    z->inp = 0;
                }
                if (io->stop && io->stop(io->u))
                    goto fail_f;
                c = zgetc(z, ms);
                if (c == CANCEL || c == GONE)
                    goto fail_f;
                if (c == TIMEOUT) {
                    pos = acked;        /* silence: from what it has */
                    goto data;
                }
                if (c != ZPAD)
                    continue;
                z->unget = c;
                t = get_hdr(z, ms ? ms : 2000);
                if (t == ZRPOS) {
                    pos = hdr_pos(z);
                    goto data;
                }
                if (t == ZACK) {
                    acked = hdr_pos(z);
                    if (end == ZCRCW)
                        break;
                } else if (t == CANCEL || t == GONE)
                    goto fail_f;
                if (ms == 0 && z->inp >= z->inn)
                    break;
            }
        }
        /* the end: ZEOF until it says it has it all */
        for (tries = 0;; tries++) {
            if (tries > 10)
                goto fail_f;
            pos_hdr(z, size);
            send_bin(z, ZEOF);
            t = wait_hdr(z, WAIT);
            while (t == ZACK)
                t = wait_hdr(z, WAIT);
            if (t == ZRINIT)
                break;
            if (t == ZRPOS) {
                pos = hdr_pos(z);
                goto data;
            }
            if (t == ZSKIP)
                break;
            if (t == CANCEL || t == GONE)
                goto fail_f;
        }
        io->rclose(io->u, f);
        io->show(io->u, name, size, size, 1);
        files++;
        continue;
    fail_f:
        ZT("fail_f t=%d pos=%lu\n", t, pos);
        io->rclose(io->u, f);
        io->show(io->u, name, pos, size, 3);
        goto fail;
        goto zfile;                     /* (not reached) */
    }
    /* all done: ZFIN both ways, then "OO" */
    for (tries = 0; tries < 5; tries++) {
        pos_hdr(z, 0);
        send_hex(z, ZFIN);
        t = wait_hdr(z, 3000);
        if (t == ZFIN)
            break;
        if (t == CANCEL || t == GONE)
            break;
    }
    ostr(z, "OO");
    flush(z);
    z_rest(z);
    return files;
fail:
    send_cancel(z);
    return -1;
}
