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
/* The local console went raw (1) or cooked (0): a raw program's first
 * whole-screen draw gets the alternate screen until it is cooked again. */
void vt_amiga_raw(Vt *vt, int on);
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

/* ---- pictures (sixel, iTerm2's inline files) ----
 * A picture is cut into tiles of a cell each; a cell showing one holds
 * VT_PIC_CP(id, row, col) instead of a letter, so pictures scroll, go
 * into the history and are erased as text is. The pixels are kept here,
 * 24-bit, per tab, up to a cap; the oldest picture goes when it is full
 * (its cells become blanks). */
#define VT_PIC_CP(id, ty, tx) (0x80000000u | (uint32_t)(id) << 18 | \
                               (uint32_t)(ty) << 9 | (uint32_t)(tx))
#define VT_IS_PIC(cp)   ((cp) >= 0x80000000u && (cp) != VT_WIDE_TAIL)
#define VT_PIC_ID(cp)   (((cp) >> 18) & 0x1FFF)
#define VT_PIC_TY(cp)   (((cp) >> 9) & 511)
#define VT_PIC_TX(cp)   ((cp) & 511)
/* A row (vt_line's) may hold picture cells: else none is there. */
#define VT_ROW_PICS(l)  ((l)[-2].pen)

typedef struct {
    int id;
    int w, h;                   /* in pixels */
    int cw, ch;                 /* the cell it was cut into */
    uint8_t *rgb;               /* w*h pixels: R, G, B */
} VtPic;

/* Pictures understood (on) or swallowed; cap: bytes they may hold. */
void vt_set_pictures(Vt *vt, int on, long cap);
/* A cell in pixels (for placing pictures, CSI 14/16 t), and the colour
 * of pixels a picture leaves unset (the background). */
void vt_set_cell_px(Vt *vt, int cw, int ch, uint32_t bg);
/* The picture a picture cell shows (NULL: gone); its tile's top left
 * pixel is (VT_PIC_TX(cp) * pic->cw, VT_PIC_TY(cp) * pic->ch). */
const VtPic *vt_pic(const Vt *vt, uint32_t cp);
long vt_pic_bytes(const Vt *vt);
int vt_pic_count(const Vt *vt);
/* Set by the host: a whole image file (iTerm2's PNG, JPEG, GIF) into
 * 0xAARRGGBB pixels (opaque), which vt_decode_free gives back. 0: not
 * a picture. NULL: such files are swallowed. */
extern int (*vt_decode)(const uint8_t *file, long len, uint32_t **argb,
                        int *w, int *h);
extern void (*vt_decode_free)(uint32_t *argb);
/* Pixel memory (pictures, files coming in): the host's own way, so it
 * goes back to the system when a picture goes. NULL: malloc and free. */
extern void *(*vt_palloc)(long n);
extern void (*vt_pfree)(void *p);

#endif
