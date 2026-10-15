/*
 * console - CTerm as the local Shell's console. CTerm answers the
 * Shell's DOS packets itself (no mounted handler): writes go into the
 * emulator, keys are edited into lines (cooked) or handed over as they
 * come (raw). One Con per tab.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <exec/types.h>
#include <devices/timer.h>

struct Con;
struct Window;

/* A Shell on a console of its own; user is handed to the term_ calls. */
struct Con *con_start(const char *startup, const char *then, void *user);
void con_stop(struct Con *c);
ULONG con_sigmask(struct Con *c);       /* its port, its end, the timer */
void con_handle(struct Con *c, ULONG sigs);   /* answer what has come */
BOOL con_alive(struct Con *c);          /* the Shell is still running */
BOOL con_done(struct Con *c);           /* the tab may go (con_retire) */
BOOL con_raw(struct Con *c);            /* raw: Ed, More may hold win */
void con_window_moved(struct Window *old, struct Window *now);
void con_window_back(struct Window *now, struct Con **cs, int n);
void con_keys(struct Con *c, const UBYTE *b, LONG n);  /* typed bytes */
/* The line editor's actions, in the order of CTerm.keys' [shell] (keys.c) */
enum { CE_LEFT, CE_RIGHT, CE_START, CE_END, CE_WORDLEFT, CE_WORDRIGHT,
       CE_BACK, CE_DELETE, CE_WORDBACK, CE_KILLSTART, CE_KILLEND, CE_CLEAR,
       CE_OLDER, CE_NEWER, CE_SEARCH, CE_COMPLETE, CE_ACCEPT, CE_EOF, CE_N };
void con_edit(struct Con *c, int what);  /* cooked: an editing key */
BOOL con_keeping(struct Con *c);         /* typed-ahead keys are kept as keys now */
void con_reply(struct Con *c, const UBYTE *b, LONG n); /* DSR replies... */
BOOL con_paste(struct Con *c, const UBYTE *b, LONG n); /* as text */
void con_close_gadget(struct Con *c);   /* the window's close gadget */
void con_resized(struct Con *c);        /* after the window's size changed */
void con_free(struct Con *c);           /* after con_alive() is FALSE */
void con_retire(struct Con *c);         /* its tab closes: all but the port */
void con_spent_poll(void);              /* closed tabs' ports: refuse */
void con_spent_free(void);              /* at the end */
void con_spent_wait(void);              /* at the end: until no handle is left */

/* The timer, shared: the frame (draw what is owed) and each console's
 * WaitForChar timeout signal its port. */
void con_timer_init(void);
void con_timer_free(void);
ULONG con_timer_sig(void);
void term_now(struct timeval *tv);
void con_arm_render(LONG us);           /* that long from now: signal */
BOOL con_render_due(void);              /* the frame is up */
BOOL con_render_ready(void);            /* the same, not taken */

/* What console.c needs from the rest of CTerm (main.c), for a tab. */
extern struct Window *win;
void term_flush(void *tab);             /* draw what is owed, now */
void term_render_soon(void *tab);       /* draw within a frame */
void term_write(void *tab, const UBYTE *b, LONG n);   /* into its emulator */
void term_take(void *tab, const UBYTE *b, LONG n);    /* ... later, batched */
void term_sync(void *tab);              /* ... what was taken, now */
void term_raw(void *tab, int on);       /* raw (Ed, More) or cooked */
int term_cursor_x(void *tab);
int term_cursor_y(void *tab);
int term_cols(void *tab);
int term_rows(void *tab);
int term_keys_ahead(void *tab);         /* the shell asked: typed-ahead keys stay keys */

#endif
