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
