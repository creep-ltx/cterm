/*
 * hydraenv.h - what HydraCom's hydra.c (the HYDRA protocol by Arjen G.
 * Lentz and Joaquim H. Homrighausen) finds around it in CTerm, in place
 * of HydraCom's MS-DOS hydracom.h: the types, the line and the files
 * (hydraenv.c, on XferIO), and nothing for its screen.
 */
#ifndef HYDRAENV_H
#define HYDRAENV_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* 2types.h, as on MS-DOS: a word is 16 bits, a dword and a long 32 (the
 * PC test builds 32-bit for that) */
typedef unsigned char  byte;
typedef unsigned short word;
typedef unsigned long  dword;
typedef int            boolean;
#ifndef false
#define false 0
#define true  1
#endif

/* Hydra's numbers travel low byte first (the PC's order): swapped on a
 * 68k. (inteli: 16 bits, intell: 32.) */
#if defined(__m68k__) || defined(mc68000) || defined(__mc68000__)
#define inteli(x) ((word) ((((word) (x)) >> 8) | (((word) (x)) << 8)))
#define intell(x) ((long) ((((dword) (x)) >> 24) | \
                           ((((dword) (x)) >> 8) & 0xff00UL) | \
                           ((((dword) (x)) << 8) & 0xff0000UL) | \
                           (((dword) (x)) << 24)))
#else
#define inteli(x) (x)
#define intell(x) (x)
#endif

#define HC_OS   "Amiga"
#define PRGNAME "CTerm"
#define VERSION "0.3"
#define PATHLEN 260
#define WIN_AGL 0

#include "hydra.h"

/* HydraCom's settings that hydra.c reads */
extern word    cur_speed, com_speed;
extern char   *download;
extern char    single_file[20];
extern boolean single_done, noresume, nostamp, nooriginator, hdxsession,
               mailer, parity;
extern dword   hydra_options;
extern long    hydra_txwindow, hydra_rxwindow;
extern word    chatfill;
extern long    chattimer, lasttimer;
extern int     loglevel;
extern boolean hy_nomem;                /* hydra_init() found no memory */

/* its screen: CTerm shows progress in its own window title */
void  message      (int level, char *fmt, ...);
void  hydra_gotoxy (int x, int y);
void  hydra_printf (char *fmt, ...);
void  hydra_clreol (void);
#define cprintf(...)       ((void)0)
#define gotoxy(x, y)       ((void)0)
#define window(a, b, c, d) ((void)0)
#define textattr(a)        ((void)0)
#define clrscr()           ((void)0)
#define clreol()           ((void)0)
#define putch(c)           ((void)0)
struct text_info { unsigned char winleft, wintop, winright, winbottom,
    attribute, normattr, currmode, screenheight, screenwidth, curx, cury; };
#define gettextinfo(t)     ((t)->screenheight = 25, (t)->screenwidth = 80, \
                            (t)->curx = (t)->cury = 1, (t)->attribute = 7)
#define gettext(a, b, c, d, e) 1
#define puttext(a, b, c, d, e) 1

/* its machine */
int   keyabort     (void);
int   fexist       (char *filename);
int   carrier      (void);
void  rem_chat     (byte *data, word len);
char *h_revdate    (long revstamp);
long  freespace    (char *drivepath);
void  resultlog    (boolean xmit, char *fname, long bytes, long xfertime);
void  sys_idle     (void);
void  com_putblock (byte *s, word len);
void  com_putbyte  (byte c);
void  com_flush    (void);
void  com_purge    (void);
void  com_dump     (void);
int   com_outfull  (void);
int   com_getbyte  (void);

/* its files: handles into CTerm's (XferIO's) files */
int   dos_sopen    (char *pathname, byte create);
int   dos_close    (int handle);
long  dos_seek     (int handle, long offset, int fromwhere);
long  dos_tell     (int handle);
int   dos_read     (int handle, void *buf, word len);
int   dos_write    (int handle, void *buf, word len);
int   hy_stat      (char *pathname, long *size, long *ftime);
char *xfer_init    (char *fname, long fsize, long ftime);
boolean xfer_bad   (void);
char *xfer_okay    (void);

#define strupr hy_strupr
#define strlwr hy_strlwr
#define strnicmp hy_strnicmp
char *hy_strupr    (char *s);
char *hy_strlwr    (char *s);
int   hy_strnicmp  (const char *a, const char *b, int n);

#endif
