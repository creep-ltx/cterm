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
#include <proto/utility.h>
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
#include "serial.h"
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
extern struct Device *TimerBase;
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
#define MAXTABS 16


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
    UBYTE *t_pend;                      /* a Shell's writes taken, not yet
                                           parsed (term_take, term_sync) */
    LONG t_plen;
    BOOL t_pendno;                      /* no memory for t_pend: not asked
                                           again */
    struct Xfer *t_xfer;                /* a transfer in it, or NULL */
    BOOL t_serial;                      /* a serial line, not TCP */
    SerTab t_st;                        /* its line, number, device */
    void *t_watch;                      /* its host's text for ARexx's
                                           WAITFOR (rexx.c), or NULL */
    char t_script[108];                 /* its place's login script */
    BOOL t_rxdone;                      /* a transfer ended since the last
                                           ARexx SEND (DOWNLOAD sees it) */
    LONG t_rxgot;                       /* ... its files, -1 failed */
    int t_toldc, t_toldr;               /* the size its host or Shell
                                           was last told (tell_sizes) */
    int t_fkind;                        /* FK_SFTP, FK_FTP: a file
                                           browser's tab (its messages),
                                           not a terminal */
    char t_fdir[128];                   /* ... the drawer it starts in */
    void *t_rf;                         /* ... its connection (Parts/Files) */
    BOOL t_fui;                         /* ... its panels are up: keys and
                                           clicks are theirs */
    LONG t_csock;                       /* a call ringing: its socket (-1),
                                           net.c's call_out */
    void *t_look;                       /* ... its name lookup, or NULL */
    LONG t_cport;                       /* ... the port it calls */
    struct timeval t_csince;            /* ... when it began */
    LONG t_cidle;                       /* ... quiet seconds (no clock) */
};

/* A pane: a part of the window showing a tab (pane.c). As many as
 * there are tabs and room (each at least 10 x 3 letters). */
#define MAXPANES MAXTABS
#define PROWS 256                       /* rows a pane keeps marks for */
struct Pane {
    struct Tab *tab;
    int x0, y0, x1, y1;                 /* its place in the window */
    int ox, oy, cols, rows;             /* its text */
    int fx0, fx1;                       /* what its rows' fills reach */
    int curx, cury;                     /* its cursor, where drawn */
    UBYTE pgen;                         /* draw.c's marks for its rows: */
    UBYTE blank[PROWS], painted[PROWS]; /* shown blank; painted in pgen */
    UBYTE sdir;                         /* how it splits next: 'h' side by
                                           side, 'v' above each other */
    UBYTE pre;                          /* with dwindle: the next split
                                           chosen by hand (0: by shape) */
};

/* Keys as a file browser's panels get them (keys.c): characters as
 * they are (Return 13, Esc 27, Tab 9, Backspace 8, Del 127, Ctrl+R 18),
 * these for the rest; as CFile's and Ahiru's keymap numbers them. */
#define KM_UP 300
#define KM_DOWN 301
#define KM_LEFT 302
#define KM_RIGHT 303
#define KM_SUP 304                      /* Shift + the arrows */
#define KM_SDOWN 305
#define KM_SLEFT 306
#define KM_SRIGHT 307
#define KM_HELP 308
#define KM_F1 310                       /* .. KM_F1 + 9 */
#define KM_CUP 320                      /* Ctrl + the arrows */
#define KM_CDOWN 321
#define KM_CLEFT 322
#define KM_CRIGHT 323
#define KM_SBS 324                      /* Shift + Backspace */
#define KM_CBS 325                      /* Ctrl + Backspace */
#define KM_F11 326
#define KM_F12 327
#define KM_PGUP 328                     /* a PC keyboard's (OS 3.2 codes) */
#define KM_PGDN 329
#define KM_HOME 330
#define KM_END 331
#define KM_INS 332

/* CTerm's own keys (keymap.c, ENVARC:CTerm/CTerm.keys): a key is the
 * keymap's character (Shift in it) or a named key (KM_*, 13, 27, 9, 8,
 * 127, ' '), and these qualifiers (Shift only with named keys) */
#define KQ_S 0x0400                     /* Shift */
#define KQ_C 0x0800                     /* Ctrl */
#define KQ_M 0x1000                     /* Alt */
#define KQ_A 0x2000                     /* Right Amiga */
#define KQ_L 0x4000                     /* Left Amiga */
#define KQ_MODS 0x7C00
#define KQ_KEY 0x03FF
enum { KC_WINDOW = 1, KC_CALL, KC_SHELL,         /* the file's sections */
       KC_FILES, KC_FLINE,                      /* the file browser's */
       KC_PANES, KC_N = KC_PANES };             /* the panes, i3's way */
enum { KA_NONE, KA_NEWTAB, KA_CALL, KA_BOOK, KA_SERIAL, KA_ANSWER, KA_SFTP,
       KA_CLOSETAB, KA_LOG, KA_LOGSTOP, KA_SENDUSER, KA_SENDPASS, KA_REXX,
       KA_HIDE, KA_ABOUT, KA_SPEED, KA_QUIT, KA_COPY, KA_PASTE, KA_YRECV,
       KA_XRECV, KA_YSEND, KA_XSEND, KA_HSEND, KA_XWIN, KA_SPLITH,
       KA_SPLITV, KA_NEXTPANE, KA_CLOSEPANE, KA_SETTINGS, KA_FONT,
       KA_TTFONT, KA_BIGGER, KA_SMALLER, KA_SCR_WB, KA_SCR_16, KA_SCR_256,
       KA_SCR_DEEP, KA_FULL, KA_BAR, KA_UTF8, KA_PC,
       KA_TAB1, KA_TAB9 = KA_TAB1 + 8, KA_NEXTTAB, KA_PREVTAB, KA_PAGEUP,
       KA_PAGEDOWN, KA_LINEUP, KA_LINEDOWN, KA_HISTTOP, KA_HISTEND,
       KA_STOP, KA_REDIAL,
       KA_ED_LEFT, KA_ED_RIGHT, KA_ED_START, KA_ED_END, KA_ED_WORDLEFT,
       KA_ED_WORDRIGHT, KA_ED_BACK, KA_ED_DELETE, KA_ED_WORDBACK,
       KA_ED_KILLSTART, KA_ED_KILLEND, KA_ED_CLEAR, KA_ED_OLDER,
       KA_ED_NEWER, KA_ED_SEARCH, KA_ED_COMPLETE, KA_ED_ACCEPT, KA_ED_EOF,
       /* the file browser's ([files], [filesline]): fileui.c's K_* order */
       KA_FB_FIRST, KA_FB_LAST = KA_FB_FIRST + 35,
       /* [panes]: i3's keys (pane.c's PO_* do them) */
       KA_P_OPEN, KA_P_KILL, KA_P_FLEFT, KA_P_FRIGHT, KA_P_FUP, KA_P_FDOWN,
       KA_P_MLEFT, KA_P_MRIGHT, KA_P_MUP, KA_P_MDOWN, KA_P_SPLITH,
       KA_P_SPLITV, KA_P_FULL, KA_P_TABS, KA_P_PANES, KA_P_SHOW1,
       KA_P_SHOW9 = KA_P_SHOW1 + 8, KA_P_DWINDLE,
       KA_N };                          /* KA_ED_*: console.h's CE_* order */
struct KeyAct {
    const char *name;                   /* in the file */
    UBYTE ctx;                          /* its section, KC_* */
    UBYTE menu;                         /* its menu item (M_*), or 0 */
};
struct KeyBind {
    UWORD key;
    UBYTE act;
};
#define MAXKB 160
extern const struct KeyAct keys_acts[];
extern struct KeyBind keys_binds[MAXKB];
extern int keys_n;
extern char keys_bad[80];
UWORD keys_code(UWORD code, UWORD q, APTR prev);
UWORD keys_parse(const char *s, int len);
void keys_name(UWORD k, char *o, BOOL plain);    /* o: 24 bytes */
int keys_find(int ctx, UWORD k);
BOOL keys_is(int a, UWORD code, UWORD q, APTR prev);
UWORD keys_first(int a);
const char *keys_word(int a);            /* its first key in words */
void say_key(const char *before, int a, const char *after);
void keys_bind(int ctx, UWORD k, int a);
void keys_unbind(int a);
void keys_defaults(void);
void keys_load(void);
BOOL keys_save(void);

/* The tab has its host or line: something to send to. */
#define linked    (sock >= 0 || T->t_st.ser != NULL)

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
       M_XSEND, M_HSEND, M_XWIN, M_BOOK, M_SERIAL, M_ANSWER, M_REXX,
       M_ZSEND, M_HIDE, M_SPLITH, M_SPLITV, M_NEXTPANE, M_CLOSEPANE,
       M_SFTP, M_POPEN, M_PKILL, M_PFLEFT, M_PFRIGHT, M_PFUP, M_PFDOWN,
       M_PMLEFT, M_PMRIGHT, M_PMUP, M_PMDOWN, M_PSPLITH, M_PSPLITV, M_PFULL,
       M_PTABS, M_PPANES, M_PDWINDLE, M_SHNEW, M_SHCLOSE, M_INPANES,
       M_INTABS };
struct KeepFont { BOOL usett; char ttfile[160]; int ttsize; };

enum { S_TABS = 1, S_WHERE, S_MODE, S_FULL, S_BAR, S_LETTERS, S_FONT, S_TT,
       S_TTSIZE, S_SPACE, S_CHARS, S_PCFONT, S_ALT, S_THEME, S_PART,
       S_R, S_G, S_B, S_RESET, S_PIC, S_NOPIC, S_SEE, S_KLIST, S_KTEXT,
       S_KPRESS, S_KNONE, S_KDEF,
       S_DLDIR, S_ULDIR, S_SHELL,
       S_MINIT, S_MINIT2, S_MDIALK, S_MDIAL, S_MSUFFIX, S_MHANG, S_MHANGCMD,
       S_MWAIT, S_MREDIAL, S_MPAUSE, S_MANSWER, S_SERLINE, S_PICS, S_GAPS, S_OWNPROG,
       S_OUTER, S_BORDER,
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
extern BOOL nopp;
void term_sync(void *t);
void term_sync_all(void);
extern char ttfile[160];
extern BOOL pcfont;
extern BOOL altmeta;
extern char userkey[2];
extern char passkey[2];
extern char dldir[160], uldir[160];
extern char shellcmd[128];        /* a new Shell tab runs it after S:Shell-Startup */
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
extern BOOL title_force;
/* pictures in the terminal (vt.h): the setting; the pen cache's
 * generation (a new one: pens asked for before are gone) */
extern BOOL pics;
extern BOOL ownprog;    /* Ed, More... on the alternate screen */
extern ULONG pen_gen;
LONG pic_pen(ULONG rgb);                /* a pen for RGB, cached */
long pics_cap(void);                    /* a tab's pictures' memory */
void pics_tab(struct Tab *t);           /* its emulator told: on, cap */
void pics_all(void);                    /* every tab: the setting */
/* parts.c: Parts/Pictures (pics.c) */
int pics_decode(const uint8_t *file, long len, uint32_t **argb, int *w,
                int *h);
void pics_decode_free(uint32_t *argb);
BOOL pics_pens(const UBYTE *rgb, int n, int x, int y, UBYTE *out);
void pics_tidy(void);                   /* no picture left: it goes */

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
APTR mod_entry(BPTR seg, ULONG head, ULONG version, BOOL *found);
void transfer_tab_gone(struct Tab *t);   /* its Hydra queue goes */
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
void repair_damage(void);             /* REFRESHWINDOW: the damage only */
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
void call_out(void);                    /* a call, ringing in the background */
BOOL calling(struct Tab *t);
void call_stop(BOOL words);
ULONG calls_sigmask(void);
LONG calls_fds(fd_set *wr, LONG maxfd);
BOOL calls_any(void);
void calls_poll(fd_set *wr, LONG n, BOOL quiet);
void connected(void);
void hung_up(void);
void send_str(const char *s);
void send_typed(const UBYTE *b, LONG n);
BOOL key(struct IntuiMessage *m);         /* FALSE: Quit */
BOOL key_action(UWORD k);               /* a key of [window] or [panes]
                                           (the commodity's hot keys) */
void remake_menus(void);
void menu_ticks(void);
void bitmap_metrics(void);
BOOL load_font(void);
void save_prefs(void);
void close_tt(void);
BOOL file_exists(const char *path);
BOOL load_tt(void);
void tt_attach(void);
BOOL open_display(void);
#define SCREEN_IF_FREE 2
void close_display(int keep_screen);    /* TRUE, FALSE, SCREEN_IF_FREE */
void switch_display(void);
BOOL display_switch(void);              /* as switch_display, if it may */
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
void call_place(const BookEntry *e, BOOL newtab);
/* bookwin.c: the phone book's own window */
void bookwin_open(void);
void bookwin_events(void);
void bookwin_close(void);               /* the book saved */
ULONG bookwin_sigmask(void);
void send_login(BOOL pass);
BOOL net_gives_up(void);                /* Esc or the close gadget, waiting */
/* serial.c: calls over a serial line, null modem or dial-up */
BOOL serial_call(void);                 /* T's line opened (and dialled) */
void serial_close(void);                /* T's line hung up and closed */
ULONG serial_sigmask(void);
void serial_alarm(LONG ms);             /* a timed wait without TCP/IP */
void serial_poll(void);                 /* every line: what came */
BOOL serial_due(void);                  /* T's line has news */
void serial_input(UBYTE *buf, LONG size);
LONG serial_take(UBYTE *b, LONG max);   /* T's bytes: -1 the line is gone */
void serial_write(const UBYTE *b, LONG n);
void serial_answer(void);
void serial_terminal(BOOL newtab);
void serial_tab_setup(const char *dev, LONG *unit, LONG *baud);
void serial_tab_book(const BookEntry *e);
/* serwin.c: a line's settings in a window of their own */
BOOL serwin_edit(SerLine *l, char *init, char *dial, struct Window *over);
/* askwin.c: a line asked in a small window (the parts that ask) */
BOOL ask_line(const char *title, const char *label, char *buf, int max,
              struct Window *over);
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
void title_hint(const char *s);         /* in the title a moment */
void title_tick(void);                  /* an IntuiTick: the hint goes */
ULONG shells_sigmask(void);
void shells_handle(ULONG sigs);
struct Tab *tab_new(void);
void tab_show(int i);
void tab_new_shell(void);
void tab_close_current(void);
void tab_hangup(void);                  /* T's host or line hung up on */
void quit_all(void);
extern const char version[];
BOOL log_to(const char *path);
int call_rexx(const char *what, struct Tab **made);
/* transfer.c, for ARexx: M_YRECV/M_XRECV (xname: Xmodem's file), or
 * M_YSEND/M_XSEND/M_ZSEND with paths; the files done, -1 failed. */
int transfer_rexx(int what, const char *const *paths, int n,
                  const char *xname);
BOOL transfer_hydra_queue(const char *const *paths, int n);
/* transfer.c, for the file browser's transfers: shown as the protocols'
 * are (how: 0 going on, 1 begins, 2 has ended); one at a time */
BOOL xfer_busy(void);
void xfer_show(struct Tab *t, struct Xfer *x, int how);
extern LONG loop_wfd;                   /* loop_round also waits for this
                                           socket to take bytes (-1 none) */
BOOL term_ask(const char *prompt, char *buf, int max, BOOL echo);
/* parts.c: the file browser for SFTP and FTP places (Parts/Files) */
BOOL files_call(void);                  /* T's place: connected, its
                                           panels up */
void files_menu(void);                  /* Project/Open SFTP browser */
void files_key(int k);                  /* T's panels: a key (KM_*) */
/* T's panels: what 0 a click, 1 the wheel up, 2 down, at the cell r,
 * c; s, us: when (a double-click) */
void files_mouse(int what, int r, int c, ULONG s, ULONG us);
void files_resized(struct Tab *t);      /* its size changed */
void files_tab_gone(struct Tab *t);     /* its connection goes */
/* ARexx's GET, PUT, LIST, CD, MKDIR, DELETE, RENAME on T's place: the
 * RC; *res a result (AllocVec, the caller frees) or NULL */
enum { FR_LIST, FR_CD, FR_GET, FR_PUT, FR_MKDIR, FR_DELETE, FR_RENAME };
int files_rexx(int what, const char *const *names, int n, const char *to,
               char **res);
extern const struct SftpOps *sftp_ops;  /* SFTP: CTermSSH's (ssh.c), or
                                           NULL */
/* rexx.c: the ARexx port */
void rexx_open(void);
void rexx_close(void);
ULONG rexx_sigmask(void);
void rexx_serve(void);                  /* read the port: safe anywhere */
void rexx_run(void);                    /* the queued commands: run() only */
const char *rexx_port(void);
/* screen.c: the About requester, answered in the main loop */
void about_open(void);
ULONG about_sigmask(void);
void about_events(void);
void about_close(void);
void rexx_feed(struct Tab *t, const UBYTE *b, LONG n);
void rexx_watch(struct Tab *t);         /* its text kept for WAITFOR */
void rexx_tab_gone(struct Tab *t);
void rexx_tab_over(struct Tab *t);
void rexx_xfer_done(struct Tab *t, int got);
int rexx_uploads(struct Tab *t, char (*store)[256], const char **paths,
                 int max);
BOOL rexx_start(const char *path, struct Tab *t);
void rexx_after_call(void);
void rexx_menu(void);
BOOL rexx_wake(BOOL any);               /* another CTerm shown: TRUE */
/* pane.c: the window split into panes */
extern struct Pane panes[MAXPANES];      /* npanes of them */
extern int npanes;
extern struct Pane *apane, *cpane;
void panes_init(struct Tab *t);
void pane_use(struct Pane *p);
struct Pane *pane_of(struct Tab *t);
struct Pane *pane_at(int x, int y);
void panes_layout(void);
int panes_maxcols(void);
int panes_right(void);                  /* the text area's right edge */
void panes_frames(void);
void pane_activate(struct Pane *p);
void pane_next(void);
BOOL pane_split(int dir);
BOOL pane_close(void);
void pane_tab_gone(struct Tab *t, int at);
void panes_changed(void);
int divider_at(int x, int y);
BOOL pane_dragging(void);
void pane_drag_start(int n);
void pane_drag_move(int x, int y);
void pane_drag_end(void);
/* the i3 way (keys [panes], the Panes menu, ARexx PANE): what, and its
 * direction (PD_*), tab number (1..) or, for PO_KILL, ask (a transfer)
 * - FALSE: not done (no room, no pane there: beep) */
enum { PO_OPEN, PO_KILL, PO_FOCUS, PO_MOVE, PO_DIR, PO_FULL, PO_SHOW,
       PO_TABS, PO_PANES, PO_DWINDLE };
/* Dwindle (Hyprland's way): a new pane splits the active one along its
 * longer side - side by side when it is wider than high. Kept in the
 * prefs; LA+H/V then choose the next split only. */
extern BOOL dwindle;
extern BOOL tabmode;                    /* new Shells (open/kill keys): tabs, not panes */
/* The room between two panes in pixels (the prefs' gaps=): each pane
 * keeps its own thin border, the gap between two borders is the
 * background (the picture behind the text, if there is one). */
#define MAXGAPS 32
extern int gaps;
/* Outer gaps: the same room round the panes as between them (prefs
 * outergaps=0 turns them off); the border round each pane, 0-MAXBORDER
 * pixels, in the theme's colours unless set (bordera= / borderi=). */
#define MAXBORDER 8
extern BOOL outergaps, ownbact, ownbin;
extern int bordersize;
extern ULONG bordact, bordin;
ULONG pane_border_rgb(BOOL active);
int posbar_w(void);                     /* full screen's place bar: its width at the right edge */
BOOL backdrop_area(LONG x, LONG y, LONG w, LONG h);
enum { PD_LEFT, PD_RIGHT, PD_UP, PD_DOWN };
BOOL pane_do(int op, int arg);
BOOL pane_kept(struct Tab *t);          /* behind a filled window: its
                                           size stays */
BOOL pane_zoomed(void);
/* draw.c, keys.c, main.c for the panes */
void planes_save(void);
void planes_restore(void);
ULONG mix(ULONG a, ULONG b, int pct);
void knob_forget(void);
void refresh_all(void);
void tell_sizes(void);
struct Tab *shell_tab(void);
/* hide.c: the window put away while everything runs on */
extern BOOL hidden;
extern LONG hid_l, hid_t, hid_w, hid_h;
void beep(void);
BOOL hide_ask(void);                    /* FALSE: not now (Ed, More) */
void show_ask(void);
BOOL hide_state(void);
ULONG hide_sigmask(void);
void hide_events(void);
void hide_show(void);                   /* at a round's end: what was asked */
void cx_open(void);
void cx_close(void);
void cx_keys(void);                     /* the hot keys Intuition would
                                           take (Amiga + the arrows) */
void cx_keys_on(BOOL on);               /* the window active or not */
void hide_free(void);
/* parts.c, classes.c: the parts (part.h) and what they keep in CTerm
 * between loads */
void reaction_drop(void);               /* no ReAction window: classes go */
void parts_free(void);
extern struct MsgPort *rxport;          /* the ARexx port (parts.c) */
extern char rxpname[12];                /* its name */
extern struct RxsLib *RexxSysBase;
extern BOOL call_newtab;                /* the Call window's "new tab" */
extern BOOL has_ssh;                    /* CTermSSH: ssh in its tabs */
extern WORD xwleft, xwtop;              /* where the Transfers window was */
extern WORD bwleft, bwtop;              /* ... the phone book's */
extern int set_page;                    /* the settings' tab last shown */
#include "part.h"

#endif
