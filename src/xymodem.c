/*
 * xymodem - Xmodem (128-byte blocks, checksum or CRC; 1K) and Ymodem
 * (batch: names and sizes in block 0, 1 KB blocks, CRC), both ways, from
 * Ward Christensen's and Chuck Forsberg's descriptions. Plain C, like
 * zmodem.c: the host and the files come through XferIO.
 *
 * Neither has a start a terminal can notice, so the person starts them:
 * the receiver asks with 'C' (CRC) or NAK (checksum) and the sender
 * answers block by block, each acknowledged before the next.
 */
#include <string.h>
#include <stdlib.h>
#include "xfer.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define SUB 0x1a                        /* the padding of a last block */

#define TIMEOUT (-1)
#define GONE    (-2)
#define CANCEL  (-3)
#define BAD     (-4)

struct X {
    XferIO *io;
    unsigned char in[1024];
    long inn, inp;
    unsigned char blk[1024 + 8];
    unsigned short crctab[256];
};

static void crc_init(struct X *x)
{
    int i, k;
    for (i = 0; i < 256; i++) {
        unsigned short c = (unsigned short)(i << 8);
        for (k = 0; k < 8; k++)
            c = (c & 0x8000) ? (unsigned short)((c << 1) ^ 0x1021)
                             : (unsigned short)(c << 1);
        x->crctab[i] = c;
    }
}

static unsigned short crc16(struct X *x, const unsigned char *b, long n)
{
    unsigned short c = 0;
    while (n--)
        c = (unsigned short)((c << 8) ^ x->crctab[((c >> 8) ^ *b++) & 255]);
    return c;
}

static int getb(struct X *x, long ms)
{
    if (x->inp >= x->inn) {
        long k;
        if (x->io->stop && x->io->stop(x->io->u))
            return CANCEL;
        k = x->io->get(x->io->u, x->in, sizeof x->in, ms);
        if (k < 0)
            return GONE;
        if (k == 0)
            return x->io->stop && x->io->stop(x->io->u) ? CANCEL : TIMEOUT;
        x->inn = k;
        x->inp = 0;
    }
    return x->in[x->inp++];
}

static void putb(struct X *x, int c)
{
    unsigned char b = (unsigned char)c;
    x->io->put(x->io->u, &b, 1);
}

/* Whatever is still coming (after an error): until a second of quiet. */
static void purge(struct X *x)
{
    int c;
    x->inp = x->inn;
    while ((c = getb(x, 1000)) >= 0)
        ;
}

static void cancel(struct X *x)
{
    static const unsigned char c[] = { CAN, CAN, CAN, CAN, CAN, CAN, CAN, CAN,
                                       8, 8, 8, 8, 8, 8, 8, 8 };
    x->io->put(x->io->u, c, sizeof c);
}

/* ---- receiving ---- */

/* One block: its number and length, EOT, or an error. */
static int rx_block(struct X *x, int crc, int *num, int *len, long ms)
{
    int c, n, nc, i;
    for (;;) {
        c = getb(x, ms);
        if (c < 0)
            return c;
        if (c == EOT)
            return EOT;
        if (c == CAN) {
            if (getb(x, 1000) == CAN)
                return CANCEL;
            continue;
        }
        if (c == SOH || c == STX)
            break;
        /* noise before a block: look on */
    }
    *len = c == SOH ? 128 : 1024;
    if ((n = getb(x, 1000)) < 0 || (nc = getb(x, 1000)) < 0)
        return n < 0 ? n : nc;
    for (i = 0; i < *len + (crc ? 2 : 1); i++) {
        if ((c = getb(x, 1000)) < 0)
            return c;
        x->blk[i] = (unsigned char)c;
    }
    if (((n + nc) & 255) != 255)
        return BAD;
    if (crc) {
        unsigned short got = (unsigned short)((x->blk[*len] << 8) |
                                              x->blk[*len + 1]);
        if (got != crc16(x, x->blk, *len))
            return BAD;
    } else {
        unsigned char sum = 0;
        for (i = 0; i < *len; i++)
            sum = (unsigned char)(sum + x->blk[i]);
        if (sum != x->blk[*len])
            return BAD;
    }
    *num = n;
    return SOH;
}

/* Ask for the first block: 'C' (CRC), after four silences NAK
 * (checksum) when Xmodem allows it. *crc says which worked. */
static int rx_start(struct X *x, int ymodem, int *crc, int *num, int *len)
{
    int tries, r;
    for (tries = 0; tries < 16; tries++) {
        *crc = ymodem || tries < 4;
        putb(x, *crc ? 'C' : NAK);
        r = rx_block(x, *crc, num, len, 3000);
        if (r == TIMEOUT)
            continue;
        if (r == BAD) {
            purge(x);
            continue;
        }
        return r;
    }
    return TIMEOUT;
}

/* The data of one file: blocks 1, 2, ... until EOT. left: the size still
 * to come when known (Ymodem), else -1. First: a block already read. */
static int rx_file(struct X *x, int crc, void *f, const char *name,
                   long left, unsigned long size, int first, int fnum,
                   int flen)
{
    XferIO *io = x->io;
    int expect = 1, r = first, num = fnum, len = flen, errors = 0;
    unsigned long done = 0;
    for (;;) {
        if (r == EOT) {
            putb(x, ACK);
            return 0;
        }
        if (r == CANCEL || r == GONE)
            return r;
        if (r == SOH && num == (expect & 255)) {
            long n = len;
            if (left >= 0 && n > left)
                n = left;               /* Ymodem knows where it ends */
            if (n > 0 && io->wwrite(io->u, f, x->blk, n) != n)
                return BAD;
            if (left >= 0)
                left -= n;
            done += (unsigned long)n;
            io->show(io->u, name, done, size, 0);
            expect++;
            errors = 0;
            putb(x, ACK);
        } else if (r == SOH && num == ((expect - 1) & 255)) {
            putb(x, ACK);               /* our ACK was lost: again */
        } else if (r == SOH) {
            return BAD;                 /* out of step: hopeless */
        } else {
            if (++errors > 10)
                return BAD;
            purge(x);
            putb(x, NAK);
        }
        r = rx_block(x, crc, &num, &len, 10000);
    }
}

static unsigned long num10(const char **p)
{
    unsigned long v = 0;
    while (**p == ' ')
        (*p)++;
    while (**p >= '0' && **p <= '9')
        v = v * 10 + (unsigned long)(*(*p)++ - '0');
    return v;
}

int xy_receive(XferIO *io, int ymodem, const char *xname)
{
    struct X *x = malloc(sizeof *x);
    int files = 0, crc, num, len, r;
    if (!x)
        return -1;
    memset(x, 0, sizeof *x);
    crc_init(x);
    x->io = io;
    for (;;) {
        char name[256];
        const char *p, *base;
        unsigned long size = 0;
        long left = -1;
        void *f;
        int i;
        r = rx_start(x, ymodem, &crc, &num, &len);
        if (r != SOH && r != EOT)
            goto fail;
        if (ymodem) {
            if (r != SOH || num != 0)
                goto fail;
            if (!x->blk[0]) {           /* an empty name: the batch ends */
                putb(x, ACK);
                break;
            }
            base = (const char *)x->blk;
            for (p = base; *p; p++)
                if (*p == '/' || *p == ':' || *p == '\\')
                    base = p + 1;
            for (i = 0; i < 200 && base[i]; i++)
                name[i] = base[i];
            name[i] = 0;
            p = (const char *)x->blk + strlen((const char *)x->blk) + 1;
            if (p < (const char *)x->blk + len && *p)
                left = (long)(size = num10(&p));
            f = io->wopen(io->u, name, size, name, sizeof name);
            if (!f) {
                cancel(x);
                io->show(io->u, name, 0, size, 3);
                goto fail_;
            }
            io->show(io->u, name, 0, size, 0);
            putb(x, ACK);
            /* and 'C' again: the data come */
            r = rx_start(x, 1, &crc, &num, &len);
        } else {
            strncpy(name, xname ? xname : "xmodem.bin", sizeof name - 1);
            name[sizeof name - 1] = 0;
            f = io->wopen(io->u, name, 0, name, sizeof name);
            if (!f) {
                cancel(x);
                goto fail_;
            }
            io->show(io->u, name, 0, 0, 0);
        }
        r = rx_file(x, crc, f, name, left, size, r, num, len);
        io->wclose(io->u, f, r == 0);
        if (r != 0) {
            io->show(io->u, name, 0, size, 3);
            goto fail;
        }
        io->show(io->u, name, size, size, 1);
        files++;
        if (!ymodem)
            break;
    }
    free(x);
    return files;
fail:
    cancel(x);
fail_:
    free(x);
    return -1;
}

/* ---- sending ---- */

/* The receiver's start: 'C' for CRC, NAK for a checksum. */
static int tx_start(struct X *x, int *crc)
{
    int c, cans = 0;
    for (;;) {
        c = getb(x, 60000);
        if (c < 0)
            return c;
        if (c == 'C' || c == NAK) {
            *crc = c == 'C';
            return 0;
        }
        if (c == CAN && ++cans >= 2)
            return CANCEL;
    }
}

/* A block, until the receiver takes it. */
static int tx_block(struct X *x, int crc, int num, const unsigned char *d,
                    int len)
{
    int tries, c, cans = 0;
    unsigned char *b = x->blk;
    b[0] = len == 1024 ? STX : SOH;
    b[1] = (unsigned char)num;
    b[2] = (unsigned char)~num;
    memcpy(b + 3, d, len);
    if (crc) {
        unsigned short s = crc16(x, d, len);
        b[3 + len] = (unsigned char)(s >> 8);
        b[4 + len] = (unsigned char)s;
    } else {
        unsigned char sum = 0;
        int i;
        for (i = 0; i < len; i++)
            sum = (unsigned char)(sum + d[i]);
        b[3 + len] = sum;
    }
    for (tries = 0; tries < 10; tries++) {
        x->io->put(x->io->u, b, 3 + len + (crc ? 2 : 1));
        for (;;) {
            c = getb(x, 10000);
            if (c == ACK)
                return 0;
            if (c == CANCEL || c == GONE)
                return c;
            if (c == CAN && ++cans >= 2)
                return CANCEL;
            if (c == NAK || c == TIMEOUT || c == 'C')
                break;                  /* again */
        }
    }
    return BAD;
}

static int tx_eot(struct X *x)
{
    int tries, c;
    for (tries = 0; tries < 10; tries++) {
        putb(x, EOT);
        c = getb(x, 10000);
        if (c == ACK)
            return 0;
        if (c == CANCEL || c == GONE)
            return c;
    }
    return BAD;
}

int xy_send(XferIO *io, int ymodem, int onek, const char *const *paths,
            int npaths)
{
    struct X *x = malloc(sizeof *x);
    static unsigned char data[1024];
    int files = 0, fi, crc = 1, r;
    if (!x)
        return -1;
    memset(x, 0, sizeof *x);
    crc_init(x);
    x->io = io;
    for (fi = 0; fi < npaths; fi++) {
        const char *p, *name = paths[fi];
        unsigned long size = 0, pos = 0;
        int num = 1, bs = (ymodem || onek) ? 1024 : 128;
        void *f;
        for (p = paths[fi]; *p; p++)
            if (*p == '/' || *p == ':')
                name = p + 1;
        f = io->ropen(io->u, paths[fi], &size);
        if (!f) {
            io->show(io->u, name, 0, 0, 3);
            continue;
        }
        if ((r = tx_start(x, &crc)) < 0)
            goto fail_f;
        if (ymodem) {                   /* block 0: "name\0size" */
            char num_[12], *q = num_ + sizeof num_ - 1;
            unsigned long v = size;
            int k = (int)strlen(name);
            memset(data, 0, 128);
            if (k > 100)
                k = 100;
            memcpy(data, name, k);
            *q = 0;
            do { *--q = (char)('0' + v % 10); v /= 10; } while (v);
            strcpy((char *)data + k + 1, q);
            if ((r = tx_block(x, crc, 0, data, 128)) < 0)
                goto fail_f;
            if ((r = tx_start(x, &crc)) < 0)
                goto fail_f;
        }
        io->show(io->u, name, 0, size, 0);
        for (;;) {
            long got = io->rread(io->u, f, data, bs);
            int len = bs;
            if (got < 0)
                goto fail_f;
            if (got == 0)
                break;
            if (got <= 128 && bs == 1024 && ymodem)
                len = 128;              /* a short end in a short block */
            if (got < len)
                memset(data + got, SUB, len - got);
            if ((r = tx_block(x, crc, num & 255, data, len)) < 0)
                goto fail_f;
            num++;
            pos += (unsigned long)got;
            io->show(io->u, name, pos, size, 0);
            if (got < bs)
                break;
        }
        if ((r = tx_eot(x)) < 0)
            goto fail_f;
        io->rclose(io->u, f);
        io->show(io->u, name, size, size, 1);
        files++;
        continue;
    fail_f:
        io->rclose(io->u, f);
        io->show(io->u, name, pos, size, 3);
        goto fail;
    }
    if (ymodem) {                       /* the end of the batch */
        memset(data, 0, 128);
        if (tx_start(x, &crc) < 0 || tx_block(x, crc, 0, data, 128) < 0)
            goto fail;
    }
    free(x);
    return files;
fail:
    cancel(x);
    free(x);
    return -1;
}
