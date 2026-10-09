/*
 * console - CTerm as the local Shell's console. CTerm answers the
 * Shell's DOS packets itself (no mounted handler): writes go into the
 * emulator, keys are edited into lines (cooked) or handed over as they
 * come (raw).
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <exec/types.h>
#include <devices/timer.h>

BOOL con_start(const char *startup);    /* start the Shell; FALSE: could not */
void con_stop(void);
ULONG con_sigmask(void);                /* the packet port and the end */
void con_handle(ULONG sigs);            /* answer what has come */
BOOL con_alive(void);                   /* the Shell is still running */
void con_keys(const UBYTE *b, LONG n);  /* typed bytes, as the keymap gives */
void con_reply(const UBYTE *b, LONG n); /* reports for the program (DSR...) */
void con_close_gadget(void);            /* the window's close gadget */
void con_free(void);                    /* after con_alive() is FALSE */
void con_timer_init(void);              /* the clock, for either mode */
void con_timer_free(void);
void term_now(struct timeval *tv);
void con_arm_render(LONG us);           /* that long from now: signal */
BOOL con_render_due(void);              /* the frame is up */

/* What console.c needs from the rest of CTerm (main.c). */
extern struct Window *win;
void term_flush(void);                  /* draw what is owed, now */
void term_render_soon(void);            /* draw within a frame */
void term_write(const UBYTE *b, LONG n);/* into the emulator */
int term_cursor_x(void);
int term_cursor_y(void);
int term_cols(void);
int term_rows(void);

#endif
