/*
 * vt - CTerm's terminal emulator: a VT100/xterm parser and the screen it
 * draws on. No Amiga headers: it builds on Linux too, where it is tested
 * against pyte (tests/).
 */
#ifndef VT_H
#define VT_H

#include <stdint.h>

/* A colour: the terminal's default, one of 256 palette entries, or RGB. */
#define VT_DEFAULT      0u
#define VT_INDEX(i)     (0x01000000u | (uint32_t)(i))
#define VT_RGB(r, g, b) (0x02000000u | ((uint32_t)(r) << 16) | \
                         ((uint32_t)(g) << 8) | (uint32_t)(b))
#define VT_IS_INDEX(c)  (((c) >> 24) == 1)
#define VT_IS_RGB(c)    (((c) >> 24) == 2)

enum {
    VT_BOLD = 1, VT_DIM = 2, VT_ITALIC = 4, VT_UNDERLINE = 8,
    VT_BLINK = 16, VT_INVERSE = 32, VT_HIDDEN = 64, VT_STRIKE = 128
};

/* The right half of a wide (2-cell) character holds this. */
#define VT_WIDE_TAIL 0xFFFFFFFFu

#define VT_PENS 1           /* cells carry pen numbers (vt_pens) */

/* How a cell looks: its colours and style. */
typedef struct {
    uint32_t fg, bg;
    uint16_t attr;
} VtPen;

/* A cell: its character and the number of its look in the pen table
 * (vt_pens). One look, one number - two cells look alike exactly when
 * their pens are equal; pen 0 is the default (no colours, no style).
 * 6 bytes on the Amiga: a stock A1200 writes every new letter to chip
 * memory, and a cell of 14 bytes was most of a letter's cost. */
typedef struct {
    uint32_t cp;            /* Unicode code point; 0 = empty (a space) */
    uint16_t pen;
} VtCell;

typedef struct Vt Vt;

/* What changed since the display last looked (vt_take_changes). */
typedef struct {
    int scroll_top, scroll_bottom;  /* a region scrolled up by scroll_n */
    int scroll_n;                   /* (negative: down); 0: none */
    const uint8_t *dirty;           /* per row: 1 = repaint it */
    const uint16_t *x0, *x1;        /* ... from column x0 to x1 (incl.) */
    int all;                        /* everything: repaint the lot */
} VtChanges;

Vt *vt_new(int cols, int rows, int scrollback);
/* Set by the host: may the history take n more bytes for a new line?
 * 0: it stops growing there (until a clear). NULL: always. */
extern int (*vt_room)(long n);
void vt_free(Vt *vt);
void vt_resize(Vt *vt, int cols, int rows);

/* Feed bytes from the host. */
void vt_write(Vt *vt, const uint8_t *buf, int len);

/* Input encoding: 1 = UTF-8 (default), 0 = Latin-1 (0x9B is CSI). */
void vt_set_utf8(Vt *vt, int on);

/* Code page 437 (a PC BBS): bytes 0x80-0xFF are the PC's characters. */
void vt_set_cp437(Vt *vt, int on);

/* The Amiga console's ways (Latin-1, 0x9B CSI, LF = new line, FF = clear
 * page, window bounds report...), for the local Shell. */
void vt_set_amiga(Vt *vt, int on);
unsigned long vt_raw_events(const Vt *vt);  /* classes from CSI n{ */
int vt_window_bg(const Vt *vt);             /* CSI >n m, 0 = default */

int vt_cols(const Vt *vt);
int vt_rows(const Vt *vt);

/* Row r of the visible screen: 0..rows-1; with a scrollback offset of
 * `back` lines, rows come from the history first. */
const VtCell *vt_line(const Vt *vt, int r, int back, int *width);
/* The pen table: vt_pens(vt)[cell.pen] is the cell's look. It may move
 * when bytes are written: take it again after vt_write. */
const VtPen *vt_pens(const Vt *vt);
int vt_history(const Vt *vt);       /* lines in the scrollback */

void vt_cursor(const Vt *vt, int *x, int *y, int *visible);
/* 0: nothing changed. ch's arrays are shared by all Vts: use them before
 * the next call. */
int vt_take_changes(Vt *vt, VtChanges *ch);

/* Replies the host asked for (DA, cursor position...): take them out. */
int vt_take_reply(Vt *vt, char *buf, int max);

/* Modes the keyboard and the main loop need to know. */
int vt_app_cursor(const Vt *vt);    /* DECCKM: arrows send ESC O A */
int vt_app_keypad(const Vt *vt);
int vt_bracketed_paste(const Vt *vt);
int vt_alt_screen(const Vt *vt);
int vt_reverse_video(const Vt *vt);
/* What the program asked of the mouse: 0 none, 9 presses (X10), 1000
 * presses and releases, 1002 + motion while pressed, 1003 + all motion;
 * *sgr: the SGR form (1006). */
int vt_mouse(const Vt *vt, int *sgr);
int vt_focus(const Vt *vt);             /* 1004: focus in/out reports */
int vt_sync(const Vt *vt);              /* 2026: hold drawing till done */
int vt_cursor_shape(const Vt *vt, int *blink);  /* 0 block, 1 under, 2 bar */
const char *vt_title(Vt *vt, int *changed);
/* Text a program copied (OSC 52), UTF-8, len bytes: the caller frees it
 * (free()). NULL: none since last asked. */
char *vt_take_clip(Vt *vt, int *len);
int vt_take_bell(Vt *vt);

#endif
