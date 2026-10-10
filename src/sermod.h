/*
 * sermod - the serial line's code as a file of its own
 * (PROGDIR:Protocols/Serial), loaded with LoadSeg when a serial tab opens
 * its line and unloaded when the last line closes: the device's reading
 * and writing, the modem's dialling, its words (OK, CONNECT, BUSY...),
 * hanging up. What stays in CTerm (serial.c) is the glue: the settings,
 * the phone book's fields, the menus, the tabs.
 *
 * The file reaches CTerm only through SerEnv; CTerm reaches it only
 * through SerMod. Its words go to the current tab (CTerm makes the tab
 * whose SerTab it passes the current one first).
 *
 * In the file, as for the protocols (xfermod.h): SERMOD_HEAD,
 * SERMOD_VERSION, then the address of sermod_start(); a file of another
 * version is refused before any of its code runs.
 */
#ifndef SERMOD_H
#define SERMOD_H

#include <exec/types.h>
#include <devices/timer.h>
#include "serial.h"

#define SERMOD_MAGIC   0x43545331UL     /* "CTS1": this interface */
#define SERMOD_HEAD    0x4354534cUL     /* "CTSL": the header's mark */
#define SERMOD_VERSION 1UL              /* SerEnv, SerMod, SerTab, SerLine
                                         * and Modem's shapes */

struct SerEnv {
    struct Library *dosbase;
    const Modem *modem;                 /* the Settings' modem */
    void (*say)(const char *s);         /* CTerm's words, in the tab */
    void (*text)(const UBYTE *b, LONG n);   /* the line's bytes: logged,
                                             * into the tab's text */
    void (*refresh)(void);
    int (*column)(void);                /* the tab's cursor column */
    ULONG (*sigs)(void);                /* what else a wait listens to: the
                                         * window, the Shells, ARexx */
    BOOL (*serve)(ULONG sigs);          /* those served; TRUE: give up
                                         * (Esc, the close gadget) */
    BOOL (*xfer_take)(UBYTE *b, LONG n);    /* a transfer starts in them */
    BOOL (*xfer_on)(void);              /* a transfer runs in the tab */
    void (*replies)(void);              /* the emulator's answers sent */
    void (*now)(struct timeval *tv);
    void (*num)(char *out, LONG n);     /* a number, at out's end */
    void (*linetext)(const SerLine *l, char *out, int max);
};

struct SerMod {
    ULONG magic;
    BOOL (*call)(SerTab *t);            /* open (and dial); t->ser set */
    void (*close)(SerTab *t);           /* hung up, closed; t->ser NULL */
    void (*answer)(SerTab *t);          /* ATA */
    LONG (*take)(SerTab *t, UBYTE *b, LONG max);
    BOOL (*write)(SerTab *t, const UBYTE *b, LONG n);
    ULONG (*sigmask)(void);
    void (*alarm)(LONG ms);
    void (*poll)(void);                 /* every line: what came */
    BOOL (*due)(SerTab *t);
    void (*input)(SerTab *t, UBYTE *buf, LONG size);
    int (*lines)(void);                 /* lines open (0: may unload) */
};

typedef struct SerMod *(*SerModStart)(const struct SerEnv *env);

#endif
