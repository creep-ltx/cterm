/*
 * serial.h - a serial line (a device and its settings) and a modem's
 * ways: what the phone book, the settings and the tabs share.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <exec/types.h>

enum { SP_NONE, SP_EVEN, SP_ODD, SP_MARK, SP_SPACE };   /* parity */
enum { SF_NONE, SF_RTSCTS, SF_XONXOFF };                /* flow control */
enum { SH_BOTH, SH_DTR, SH_COMMAND };                   /* how to hang up */

/* A line: which device, how fast, how each byte is framed. */
typedef struct {
    char device[48];                    /* serial.device, a card's own */
    LONG unit;
    LONG baud;                          /* any rate the device takes */
    UBYTE bits, parity, stop, flow;     /* 7/8; SP_*; 1/2; SF_* */
    UBYTE locked;                       /* the speed stays whatever
                                           CONNECT says */
    UBYTE pad;
    LONG bufkb;                         /* the device's buffer, KB */
} SerLine;

/* The modem: how every dial-up place is called (Settings, Modem). */
typedef struct {
    char init[64], init2[64];           /* sent first, each waits for OK */
    char dial[24], suffix[16];          /* ATDT, then the number, then ^M */
    char hangcmd[24];                   /* ATH0, after +++ */
    LONG hangup;                        /* SH_* */
    LONG wait;                          /* a call's answer: seconds */
    LONG redial, pause;                 /* BUSY, NO ANSWER: again, after */
    LONG answer;                        /* RING: answer it (ATA) */
} Modem;

extern SerLine serdef;                  /* the serial terminal's line */
extern Modem modem;

extern const LONG ser_rates[];          /* the usual speeds, 0 at the end */
extern const char *const ser_parname[]; /* "N", "E", "O", "M", "S" */

void ser_line_default(SerLine *l);
/* "serial.device 0, 38400 8N1, RTS/CTS" */
void ser_line_text(const SerLine *l, char *out, int max);
/* the settings' keys (serdev=...): TRUE if key was one */
BOOL ser_pref(const char *key, const char *val);
void ser_prefs_save(char *buf);

#endif
