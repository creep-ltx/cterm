/*
 * detect - the start of a transfer in what a host sends: all of the
 * protocols CTerm keeps in itself (the protocols are files of their own,
 * loaded when one begins: xfermod.h).
 */
#include <string.h>
#include "xfer.h"

#define ZDLE 0x18

/* Zmodem: "**" ZDLE "B0" and the type - 1 the host sends (ZRQINIT), 2 it
 * waits for files (ZRINIT). */
int zm_detect(const unsigned char *b, long n, long *at)
{
    long i;
    for (i = 0; i + 6 <= n; i++)
        if (b[i] == '*' && b[i + 1] == '*' && b[i + 2] == ZDLE &&
            b[i + 3] == 'B' && b[i + 4] == '0' &&
            (b[i + 5] == '0' || b[i + 5] == '1')) {
            if (at)
                *at = i;
            return b[i + 5] == '0' ? 1 : 2;
        }
    return 0;
}

/* Hydra: HydraCom's autostart - the START packet as a sender sends it. */
int hy_detect(const unsigned char *b, long n, long *at)
{
    /* HydraCom's autostart: the START packet as a sender sends it */
    static const unsigned char start[] = "\030cA\\f5\\a3\030a";
    long i;
    for (i = 0; i + 11 <= n; i++)
        if (!memcmp(b + i, start, 11)) {
            if (at)
                *at = i;
            return 1;
        }
    return 0;
}

/* A start split across two reads. tail: what the caller keeps per tab
 * between reads, SPLIT_TAIL + 1 bytes, zeroed at first (tail[0]: how many
 * follow). Only a start that begins in the read before and ends in b is
 * found here (one wholly in b is the two above's); then its kind is
 * returned (1, 2: Zmodem as zm_detect; 3: Hydra) and *had says how many
 * of its bytes the read before had - already on the screen. Either way
 * the tail takes b's last bytes. A few byte compares per read. */
#define SPLIT_TAIL 10                   /* Hydra's 11-byte start less one */

int split_detect(unsigned char *tail, const unsigned char *b, long n,
                 int *had)
{
    unsigned char w[2 * SPLIT_TAIL];
    int tn = tail[0], k = n < SPLIT_TAIL ? (int)n : SPLIT_TAIL, kind = 0;
    int z0 = tn > 5 ? tn - 5 : 0;       /* Zmodem's 6: 5 of them before */
    long at = 0;
    memcpy(w, tail + 1, tn);
    memcpy(w + tn, b, k);
    if (tn && (kind = zm_detect(w + z0, tn - z0 + k, &at)) &&
        (at += z0) >= tn)
        kind = 0;                       /* wholly in b */
    {
        long hat;
        if (tn && hy_detect(w, tn + k, &hat) && hat < tn &&
            (!kind || hat < at)) {
            kind = 3;
            at = hat;
        }
    }
    if (kind) {
        if (had)
            *had = tn - (int)at;
        tail[0] = 0;                    /* taken: not found twice */
        return kind;
    }
    if (n >= SPLIT_TAIL) {
        memcpy(tail + 1, b + n - SPLIT_TAIL, SPLIT_TAIL);
        tail[0] = SPLIT_TAIL;
    } else {
        int keep = tn + n > SPLIT_TAIL ? SPLIT_TAIL - (int)n : tn;
        memmove(tail + 1, tail + 1 + tn - keep, keep);
        memcpy(tail + 1 + keep, b, n);
        tail[0] = keep + n;
    }
    return 0;
}
