/*
 * cterm.h - what the parts of CTerm share: the tabs, the settings,
 * the window, and each part's calls.
 */
#ifndef CTERM_H
#define CTERM_H


#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <dos/var.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/imageclass.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <graphics/gfxmacros.h>
#include <graphics/view.h>
#include <graphics/modeid.h>
#include <devices/inputevent.h>
#include <devices/clipboard.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/ttengine.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <proto/datatypes.h>
#include <proto/alib.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/clicktab.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/slider.h>
#include <gadgets/button.h>
#include <gadgets/getfile.h>
#include <gadgets/getfont.h>
#include <gadgets/getscreenmode.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <gadgets/fuelgauge.h>
#include <images/label.h>
#include <images/bevel.h>
#include <reaction/reaction_macros.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/clicktab.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/slider.h>
#include <proto/button.h>
#include <proto/getfile.h>
#include <proto/getfont.h>
#include <proto/getscreenmode.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/fuelgauge.h>
#include <proto/label.h>

/* The ReAction macros (LayoutObject ... End) spread one NewObject call
 * over macros, which the inline NewObject can't take: functions instead.
 * Not static - at -O2 gcc rewrote static ones and &tag1 no longer met
 * the caller's tags (Knowledge/amiga/toolchain-and-testing.md). */
#undef NewObject
#undef SetAttrs
#define NewObject RNewObject
#define SetAttrs RSetAttrs
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "vt.h"
#include "boxtable.h"
#include "console.h"
#include "ssh.h"
#include "book.h"
#include "dbg.h"

#define DEFAULT_PORT 7478
#define POSBAR       6
#define vt        (T->t_vt)
#define local     (T->t_local)
#define sock      (T->t_sock)
#define lfd       (T->t_lfd)
#define tstate    (T->t_tstate)
#define tcmd      (T->t_tcmd)
#define sbuf      (T->t_sbuf)
#define sblen     (T->t_sblen)
#define dialhost  (T->t_dialhost)
#define dialtitle (T->t_dialtitle)
#define sshmode   (T->t_sshmode)
#define utf8      (T->t_utf8)
#define pcmode    (T->t_pcmode)
#define back      (T->t_back)
#define sel_on    (T->t_sel_on)
#define sel_drag  (T->t_sel_drag)
#define sa_r      (T->t_sa_r)
#define sa_c      (T->t_sa_c)
#define sb_r      (T->t_sb_r)
#define sb_c      (T->t_sb_c)
#define NTHEMES 6                       /* the themes below */
#ifdef PROFILE
/* A profiling build (make PROFILE=1): Emu68's free-running 54 MHz counter
 * (MOVEC $e1, as Wasabi's health.c reads it), ticks per part. */
#define MOVEC_D0(rc, out) \
    asm volatile(".short 0x4e7a, %c1\n\tmove.l %%d0,%0" \
                 : "=g"(out) : "i"(rc) : "d0")
#ifdef PROFILE_ECLOCK
/* make PROFILE=1 PROFILE_ECLOCK=1: the CIA E clock (709379 Hz on PAL),
 * for a stock Amiga or an emulator; TPMS: ticks per millisecond */
#include <proto/timer.h>
extern extern struct Device *TimerBase;
#define TPMS 709
static ULONG ticks(void)
{
    struct EClockVal e;
    if (!TimerBase)
        return 0;
    ReadEClock(&e);
    return e.ev_lo;
}
#else
#define TPMS 54000
static ULONG ticks(void)
{
    ULONG lo;
    APTR ssp = SuperState();
    MOVEC_D0(0xe1, lo);
    if (ssp)
        UserState(ssp);
    return lo;
}
#endif
extern ULONG pf_vt, pf_draw, pf_pkt, pf_wait, pf_all0, pf_writes, pf_draws;
extern ULONG pf_scroll, pf_text, pf_cur, pf_knob, pf_rows, pf_fill, pf_nrows, pf_nall, pf_nscroll;
extern struct timeval pf_tv0;
extern ULONG pf_calib;
#define PF_START(v) ULONG v = ticks()
#define PF_ADD(acc, v) (acc += ticks() - (v))
#else
#define PF_START(v)
#define PF_ADD(acc, v)
#endif
#define MAXTABS 9


/* A transfer running in a tab: what the panel, the tab's name and the
 * Transfers window show. */
struct Xfer {
    int stop;                           /* Esc, Stop, the tab closing */
    int dir;                            /* 1 receiving, 2 sending, 3 both */
    const char *proto;                  /* "Zmodem"... */
    char name[108];                     /* the file now */
    char where[160];                    /* its drawer (receiving) */
    ULONG done, size;                   /* the file now: bytes, its size */
    ULONG bdone, bsize;                 /* the batch: bytes, all (0: ?) */
    ULONG fbase;                        /* bdone when this file began */
    int files, nfiles;                  /* files done, all (0: ?) */
    ULONG t0;                           /* when it began (ticks) */
    ULONG tlast;                        /* last shown */
    int stopx0, stopx1, stopy0, stopy1; /* the panel's Stop button */
};

struct Tab {
    Vt *t_vt;
    struct Con *con;                    /* a local Shell's console */
    BOOL t_local;                        /* the Amiga Shell, not a host */
    LONG t_sock, t_lfd;                     /* a host; LISTEN's socket */
    int t_tstate, t_tcmd;                   /* telnet's parser */
    UBYTE t_sbuf[64];                     /* a subnegotiation coming in */
    int t_sblen;
    char t_dialhost[128];                 /* HOST/SSH: the place called */
    char t_dialtitle[140];                /* "CTerm: host" */
    BOOL t_sshmode;                       /* dialhost is user@host */
    BOOL t_utf8, t_pcmode;                  /* its characters: UTF-8; PC BBS */
    int t_back;                           /* lines scrolled back */
    BOOL t_sel_on, t_sel_drag;              /* the selection, in its cells */
    int t_sa_r, t_sa_c, t_sb_r, t_sb_c;
    BOOL ended;                         /* its Shell or connection is over */
    void *ssh;                          /* its ssh connection */
    char t_user[64], t_pass[64];        /* its phone book login */
    char t_bookname[48];                /* its phone book name: the label */
    BOOL t_passed;                      /* the password given to ssh once */
    int t_letters;                      /* its letters: BL_* */
    char t_down[160], t_up[160];        /* its drawers for files */
    BPTR t_log;                         /* Start log: everything that came */
    struct Xfer *t_xfer;                /* a transfer in it, or NULL */
};

/* Themes: the text and background, the 16 ANSI colours, and what the
 * Amiga's pens 2-7 mean in the local Shell (Ed and friends use them). */
struct Theme {
    const char *name;
    ULONG fg, bg, ansi[16], wbpens[6];
};

enum { SCR_WB, SCR_OWN };

enum { M_QUIT = 1, M_FONT, M_BAR, M_FULL, M_SCR_WB, M_SCR_16, M_SCR_256,
       M_SCR_DEEP, M_UTF8, M_ABOUT, M_TTFONT, M_BIGGER, M_SMALLER, M_SPEED,
       M_PC, M_CALL, M_COPY, M_PASTE, M_SETTINGS, M_NEWTAB, M_CLOSETAB,
       M_LOG, M_LOGSTOP, M_SENDUSER, M_SENDPASS, M_YRECV, M_XRECV, M_YSEND,
       M_XSEND, M_HSEND, M_XWIN };
struct KeepFont { BOOL usett; char ttfile[160]; int ttsize; };

enum { S_TABS = 1, S_WHERE, S_MODE, S_FULL, S_BAR, S_LETTERS, S_FONT, S_TT,
       S_TTSIZE, S_SPACE, S_CHARS, S_PCFONT, S_ALT, S_THEME, S_PART,
       S_R, S_G, S_B, S_RESET, S_PIC, S_NOPIC, S_SEE, S_KUSER, S_KPASS,
       S_BLIST, S_BNEW, S_BDEL, S_BNAME, S_BHOST, S_BPORT, S_BKIND, S_BUSER,
       S_BPASS, S_BLET, S_BCHR, S_BDOWN, S_BUP, S_DLDIR, S_ULDIR,
       S_SAVE, S_USE, S_CANCEL, S_N };

/* ---- shared state ---- */
extern struct Library *SocketBase;
extern struct Library *AslBase;
extern struct Library *DiskfontBase;
extern struct Library *TTEngineBase;
extern struct Library *CyberGfxBase;
extern struct Tab *tabs[MAXTABS];
extern int ntabs;
extern struct Tab *T;
extern struct Tab *shown;
extern const struct Theme themes[NTHEMES];
extern int theme;
extern BOOL ownfg, ownbg;
extern ULONG myfg, mybg;
extern ULONG deffg, defbg;
extern int scrmode, owndepth;
extern ULONG ownmode;
extern LONG ownw, ownh;
extern BOOL fullscreen, scrollbar;
extern struct Screen *ownscr, *scr;
extern struct DrawInfo *dri;
extern struct Window *win;
extern struct RastPort *rp;
extern struct TextFont *font, *ownfont;
extern char fontname[48];
extern int fontsize;
extern int cw, ch, cbase;
extern int linespace;
extern BOOL usett;
extern char ttfile[160];
extern BOOL pcfont;
extern BOOL altmeta;
extern char userkey[2];
extern char passkey[2];
extern char dldir[160], uldir[160];
extern char pcttfile[160];
extern int fontover;
extern BOOL ttaa;
extern struct KeepFont keepfont;
extern int ttsize;
extern APTR ttface[4];
extern BOOL ttruns;
extern int ox, oy, cols, rows;
extern APTR visual;
extern struct Menu *menu;
extern BOOL want_switch;
extern int want_mode, want_depth;
extern BOOL want_full, want_bar;
extern LONG want_cols, want_rows;
extern LONG wl, wt, ww, wh;
extern char calls[300];
extern BOOL remember;
extern BOOL quitting;
extern BOOL owed;
extern BOOL flood;
extern int npc;
extern BOOL painter;
extern char bgfile[256];
extern int bgsee;
extern ULONG *bgsrc;
extern BOOL planar;
extern ULONG *rowbuf;
extern struct Gadget vgad, agup, agdn;
extern struct PropInfo vpi;
extern APTR iup, idn;
extern BOOL gadsok;
extern int arrowheld, arrowticks;
extern struct Library *DataTypesBase;
extern LONG port;
extern BOOL saved_utf8;
extern struct Window *setwin;
extern BOOL envonly;
extern BOOL used_only;
extern BOOL saved_pc;
extern Object *setobj, *sg[S_N];
extern BOOL title_force;

/* ---- the parts' calls ---- */
BOOL asl_open(void);
BOOL tt_installed(void);
BOOL tt_open(void);
ULONG history_lines(void);
BOOL diskfont_open(void);
BOOL transfer_take(UBYTE *buf, LONG n);
BOOL loop_round(LONG xfd, LONG ms);
ULONG xwin_sigmask(void);
void xwin_open(void);
void xwin_update(void);
void xwin_shut(void);
BOOL reaction_open(void);
void xwin_events(void);
ULONG ticks50(void);
ULONG pct100(ULONG done, ULONG all);
void xfer_panel(void);
void draw_tabbar(void);
BOOL xfer_click(int x, int y);
void transfer_menu(int what);
void transfer_free(void);
void deep_call(void (*fn)(void));
__attribute__((noinline)) __stdargs APTR RNewObject(struct IClass *c, CONST_STRPTR id, ULONG tag1, ...);
__attribute__((noinline)) __stdargs ULONG RSetAttrs(APTR o, ULONG tag1, ...);
void apply_theme(void);
void planes_full(void);
void rp_forget(void);
void apen(ULONG rgb);
void bpen(ULONG rgb);
void release_pens(void);
void sel_bounds(int *r0, int *c0, int *r1, int *c1);
void draw_row(int r);
void draw_cursor(BOOL on);
void cache_free(void);
void painter_setup(void);
void painter_forget(void);
void add_scroller(void);
void update_knob(void);
int tabbar_h(void);
void layout_and_resize(void);
void tab_label(struct Tab *t, char *out);
BOOL load_picture(const char *path);
void backdrop_free(void);
void backdrop_make(void);
void redraw_all(void);
void refresh(void);
void log_bytes(const UBYTE *b, LONG n);
void draw_now(void);
void scroll_view(int to);
void send_data(const UBYTE *b, LONG n);
void send_naws(void);
void from_host(UBYTE *b, LONG n);
void numstr_cat(char *out, LONG n);
void say(const char *s);
LONG listen_on(void);
BOOL connect_out(void);
void connected(void);
void hung_up(void);
void send_str(const char *s);
void send_typed(const UBYTE *b, LONG n);
void key(struct IntuiMessage *m);
void remake_menus(void);
void bitmap_metrics(void);
BOOL load_font(void);
void save_prefs(void);
void close_tt(void);
BOOL file_exists(const char *path);
BOOL load_tt(void);
void tt_attach(void);
BOOL open_display(void);
void close_display(BOOL keep_screen);
void switch_display(void);
void font_changed(void);
void ask_ttfont(void);
void tt_size(int d);
void tab_letters(BOOL tab);
void speed_test(void);
void ask_font(void);
BOOL menu_pick(ULONG id);
void clip_close(void);
void sel_redraw(int ra, int rb);
BOOL sel_word(int r, int c, BOOL line);
void sel_clear(void);
void sel_copy(void);
void clip_put(UBYTE *buf, ULONG n);
void host_clip(void);
void log_stop(void);
void log_start(void);
void paste(void);
void mouse_cell(int mx, int my, int *r, int *c);
void own_path(char *buf, int max, const char *sibling);
void tab_from_book(const BookEntry *e);
void call_book(int i, BOOL newtab);
void send_login(BOOL pass);
void call_dialog(void);
void reaction_close(void);
void close_settings(void);
void settings_cancel(void);             /* at the end: as Cancel */
void open_settings(void);
void settings_events(void);
BOOL mouse_to_host(UWORD q);
void mouse_send(int b, int r, int c, BOOL release, UWORD q);
void mouse_reporting(void);
BOOL window_events(void);
void title_update_now(void);
ULONG shells_sigmask(void);
void shells_handle(ULONG sigs);
struct Tab *tab_new(void);
void tab_show(int i);
void tab_new_shell(void);
void tab_close_current(void);

#endif
