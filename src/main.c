/*
 * CTerm - a terminal for AmigaOS 3.2: the Amiga Shell in a window of its
 * own, and telnet and ssh to other computers, in tabs.
 *
 * This file: the tabs, the settings kept in ENV: and ENVARC:, the console
 * calls (console.c's term_*), and the main loop. The rest:
 *
 *   vt.c       the terminal itself (VT100/xterm), knows nothing of the Amiga
 *   console.c  the Amiga Shell's side: CTerm as its console handler
 *   draw.c     colours, drawing the cells (pens, Text, the fast painter),
 *              the scroll bar, the layout, the picture behind the text
 *   fonts.c    Amiga and TrueType letters, each tab's own
 *   screen.c   screens, windows and menus
 *   keys.c     the keyboard, and the mouse for programs on hosts
 *   net.c      telnet, calling out, phone book calls
 *   serial.c   serial lines: the settings and tabs; the line's own code
 *              is sermod.c, a file loaded when a line opens
 *   ssh.c      ssh and SFTP (libssh2 on AmiSSL), in CTermSSH only
 *   clip.c     copy and paste, and the log
 *   book.c     the phone book's file
 *   classes.c  OS 3.2's window classes, for the windows made of them
 *   parts.c    the parts: files of their own in PROGDIR:Parts/, loaded
 *              when used (part.h), and the ARexx port
 *   cterm.h    what they share
 *
 * The parts (each CTerm's own source, built with -DPART):
 *   setwin.c   the settings window (ReAction, in tabs)   Parts/Settings
 *   bookwin.c  the phone book's window                   Parts/PhoneBook
 *   serwin.c   a serial line's settings (in both of those)
 *   rexx.c     ARexx's commands, WAITFOR, starting scripts  Parts/ARexx
 *   callwin.c  the Call window                           Parts/Call
 *   xwin.c     the Transfers window                      Parts/Transfers
 *   bench.c    the speed test in the Project menu        Parts/SpeedTest
 *   files.c    the file browser, SFTP and FTP            Parts/Files
 *   ftp.c      FTP, for the browser                      (Parts/Files)
 *   askwin.c   a line asked in a small window (PhoneBook, Files)
 */
#include "cterm.h"

void __chkabort(void);
static void title_update(void);
void term_write(void *t, const UBYTE *b, LONG n);
void term_take(void *t, const UBYTE *b, LONG n);
int term_cols(void *t);
int term_rows(void *t);
int term_cursor_x(void *t);
int term_cursor_y(void *t);
static void draw_soon(void);
void term_flush(void *t);
void term_render_soon(void *t);
static ULONG hexval(const char *q);
static void hex_cat(char *out, ULONG v, int digits);
static void load_prefs(void);
static void title_update(void);
static int tab_index(struct Tab *t);
static void tab_drop(struct Tab *t);
static void host_input(UBYTE *buf, LONG size);
static void run(void);
#ifdef MEMLOG
static char mlbuf[1024];
static int mll;
void memlog(const char *what)
{
    ULONG a = AvailMem(MEMF_ANY);
    static ULONG last;
    if (mll < 960)
        mll += sprintf(mlbuf + mll, "%-12s %7lu  %+7ld\n", what, (unsigned long)a,
                       last ? (long)a - (long)last : 0L);
    last = a;
}
/* Who asked for memory that was not there: exec calls the low-memory
 * handlers on every failed allocation (counted per task, with the last
 * size asked). */
static struct { char name[16]; ULONG n, size, flags; } mh[8];
static struct Interrupt mhint;
static LONG memh(register struct MemHandlerData *d __asm("a0"),
                 register APTR data __asm("a1"))
{
    struct Task *t = FindTask(NULL);
    const char *nm = t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?";
    int i;
    (void)data;
    for (i = 0; i < 8 && mh[i].n && strncmp(mh[i].name, nm, 15); i++)
        ;
    if (i < 8) {
        if (!mh[i].n)
            strncpy(mh[i].name, nm, 15);
        mh[i].n++;
        mh[i].size = d->memh_RequestSize;
        mh[i].flags = d->memh_RequestFlags;
    }
    return MEM_DID_NOTHING;
}
static void memh_on(void)
{
    mhint.is_Node.ln_Type = NT_INTERRUPT;
    mhint.is_Node.ln_Pri = 100;
    mhint.is_Node.ln_Name = (char *)"CTerm memlog";
    mhint.is_Code = (void (*)())memh;
    AddMemHandler(&mhint);
}
static void memlog_save(void)
{
    BPTR f = Open((STRPTR)"SYS:T/ctmem.txt", MODE_NEWFILE);
    int i;
    if (f) {
        Write(f, mlbuf, mll);
        for (i = 0; i < 8 && mh[i].n; i++)
            FPrintf(f, (STRPTR)"failed alloc: %-15s %6ld times, last %ld bytes flags %lx\n",
                    (LONG)mh[i].name, mh[i].n, mh[i].size, mh[i].flags);
        Close(f);
    }
}
#define ML(x) memlog(x)
#else
#define ML(x)
#endif
static int cterm_main(int argc, char **argv);
static void __attribute__((noinline)) cterm_main_(void);
int main(int argc, char **argv);
static int cterm_main(int argc, char **argv);

#ifdef PROFILE
extern struct Device *TimerBase;
ULONG pf_vt, pf_draw, pf_pkt, pf_wait, pf_all0, pf_writes, pf_draws;
ULONG pf_scroll, pf_text, pf_cur, pf_knob, pf_rows, pf_fill, pf_nrows, pf_nall, pf_nscroll;
struct timeval pf_tv0;
ULONG pf_calib;
#endif

const char version[] = "$VER: CTerm 0.3b27 (11.10.2026)";

struct Library *SocketBase = NULL;
struct Library *KeymapBase = NULL;
struct Library *GadToolsBase = NULL;
struct Library *AslBase = NULL;
struct Library *DiskfontBase = NULL;
struct Library *TTEngineBase = NULL;
struct Library *CyberGfxBase = NULL;

void __chkabort(void) {}
#define SCROLLBACK   2000
/* ======================================================================
 * Tabs: each a terminal of its own - its text, its Shell or connection,
 * its characters, where it is scrolled, what is marked. The names below
 * stand for the current tab's (T); only the shown tab is drawn.
 * ==================================================================== */

static struct Tab tab0 = { NULL, NULL, TRUE, -1, -1 };
struct Tab *tabs[MAXTABS] = { &tab0 };
int ntabs = 1;
struct Tab *T = &tab0;           /* the current tab */
struct Tab *shown = &tab0;       /* the one in the window */
/* ======================================================================
 * Colours
 * ==================================================================== */


/* ---- for console.c ------------------------------------------------- */

/* The console's calls, for its tab (t): the text goes into that tab's
 * emulator; only the shown tab is drawn. */
/* Parse bytes now. */
static void term_parse(struct Tab *tb, const UBYTE *b, LONG n)
{
    PF_START(t0);
    if (tb->t_log)
        FWrite(tb->t_log, (APTR)b, 1, n);
    vt_write(tb->t_vt, b, n);
    PF_ADD(pf_vt, t0);
}

/* A Shell's writes are taken in at once and parsed in batches - CCON's
 * accept-then-render: the writer goes on at once, and a flood of small
 * writes (a byte at a time, a colour change each) is parsed in a few
 * calls instead of one per write (on a stock A1200 a call to the parser
 * was ~230 us before its first byte). What reads the emulator syncs
 * first: drawing, the console's own echo, cursor queries, a READ. */
#define PENDMAX 4096
void term_sync(void *t)
{
    struct Tab *tb = t;
    if (tb && tb->t_plen) {
        LONG n = tb->t_plen;
        tb->t_plen = 0;
        term_parse(tb, tb->t_pend, n);
    }
}

void term_sync_all(void)
{
    int i;
    for (i = 0; i < ntabs; i++)
        term_sync(tabs[i]);
}

/* A write from the Shell's programs: kept to parse later if it fits. */
void term_take(void *t, const UBYTE *b, LONG n)
{
    struct Tab *tb = t;
#ifdef NOPARSE
    return;                             /* to measure: the packets alone */
#endif
    if (tb->t_watch)
        rexx_feed(tb, b, n);            /* an ARexx WAITFOR looks */
    if (!tb->t_pend && !tb->t_pendno) {
        /* once: when memory is short it is parsed at once instead, and
         * not asked for again on every write */
        tb->t_pend = AllocVec(PENDMAX, MEMF_ANY);
        tb->t_pendno = !tb->t_pend;
    }
    if (tb->t_pend && n <= PENDMAX - tb->t_plen) {
        CopyMem((APTR)b, tb->t_pend + tb->t_plen, n);
        tb->t_plen += n;
    } else {
        term_sync(t);
        term_parse(tb, b, n);
    }
#ifdef PROFILE
    pf_writes++;
#endif
}

/* The console's own text (an echo, a prompt): after what came before. */
void term_write(void *t, const UBYTE *b, LONG n)
{
    PF_START(t0);
    term_sync(t);
    if (((struct Tab *)t)->t_log)
        FWrite(((struct Tab *)t)->t_log, (APTR)b, 1, n);
    if (((struct Tab *)t)->t_watch)
        rexx_feed(t, b, n);
    vt_write(((struct Tab *)t)->t_vt, b, n);
    PF_ADD(pf_vt, t0);
#ifdef PROFILE
    pf_writes++;
#endif
}
/* the tab's own size: its pane's */
int term_cols(void *t) { return vt_cols(((struct Tab *)t)->t_vt); }
int term_rows(void *t) { return vt_rows(((struct Tab *)t)->t_vt); }
int term_cursor_x(void *t)
{
    int x, y, v;
    term_sync(t);
    vt_cursor(((struct Tab *)t)->t_vt, &x, &y, &v);
    return x;
}
int term_cursor_y(void *t)
{
    int x, y, v;
    term_sync(t);
    vt_cursor(((struct Tab *)t)->t_vt, &x, &y, &v);
    return y;
}

#ifndef PACE
#define PACE 3                          /* a flood: the next draw after this
                                           many times the last one's cost -
                                           3 (was 1.5): a stock A1200 spent
                                           40% of a flood drawing; now 25%
                                           (conbench 32.9 -> 30.7 s; CCON's
                                           PACE=FAST is the same bargain) */
#endif
#ifndef PACEMAX
#define PACEMAX 320000                  /* never fewer than 3 draws a second */
#endif
static LONG lastcost;                   /* us: the last draw, planar only */

void draw_now(void)
{
    PF_START(t0);
    owed = FALSE;
    term_sync(T);
#ifdef NODRAW
    {   /* to measure: everything but the drawing (the model still runs) */
        VtChanges chg;
        vt_take_changes(vt, &chg);
        return;
    }
#endif
    if (win) {
        /* every pane: one frame timer draws them all */
        if (planar && !painter) {
            /* a stock Amiga: how long a draw takes paces the next one */
            struct timeval a, b;
            term_now(&a);
            refresh_all();
            term_now(&b);
            lastcost = (b.tv_secs - a.tv_secs) * 1000000 +
                       (LONG)b.tv_micro - (LONG)a.tv_micro;
        } else {
            refresh_all();
            lastcost = 0;               /* not measured here: an AGA
                                         * screen's cost no longer paces
                                         * a card's floods */
        }
    }
    PF_ADD(pf_draw, t0);
#ifdef PROFILE
    pf_draws++;
#endif
}

/* Draw within a frame: writes that keep coming meanwhile are drawn with
 * it, so a flood costs one draw per frame, not per write. A timer request
 * says when the frame is up - no clock is read here: on the A1200 (Emu68,
 * PiStorm) reading the system time is costly, and this runs per write. */
static void draw_soon(void)
{
    if (owed)
        return;
    owed = TRUE;
    /* jump scroll: while text pours in and every draw is a whole scrolled
     * window, draw every 60 ms instead of every frame - a third of the
     * work, still smooth to the eye; back to every frame once it calms */
    {
        /* paced: where a draw costs more than the frame (a 14 MHz Amiga
         * redrawing a flooded screen), wait half as long again as it
         * took, so the text keeps coming in between (CCON's pacing) */
        LONG us = flood ? 60000 : 20000, p = lastcost * PACE;
        /* not only in a flood: a busy screen redrawn in place (a
         * full-screen program) paces the same - the frame timer was, by
         * mistake, an IntuiTick (100 ms) until the short loop heeded it */
        if (p > us)
            us = p > PACEMAX ? PACEMAX : p;
        con_arm_render(us);
    }
}

void term_flush(void *t)
{
    term_sync(t);
    if (pane_of(t)) {                   /* in a pane: seen */
        struct Tab *keep = T;
        T = shown;
        draw_now();
        T = keep;
    }
}

/* The Shell's console went raw or cooked (console.c): Ed, More... */
void term_raw(void *t, int on)
{
    struct Tab *tb = t;
    if (tb->t_vt && (ownprog || !on))   /* cooked: always back */
        vt_amiga_raw(tb->t_vt, on);
}

void term_render_soon(void *t)
{
    if (pane_of(t))
        draw_soon();
}

void scroll_view(int to)
{
    int max = vt_history(vt);
    if (to < 0) to = 0;
    if (to > max) to = max;
    if (to == back)
        return;
    back = to;
    {
        int r;
        draw_cursor(FALSE);
        for (r = 0; r < rows; r++)
            draw_row(r);
        draw_cursor(TRUE);
    }
    update_knob();
}

/* ======================================================================
 * The connection: telnet over TCP
 * ==================================================================== */

#define PREFS_VAR "CTerm.prefs"
static BOOL hadprefs;

static ULONG hexval(const char *q)
{
    ULONG v = 0;
    while ((*q >= '0' && *q <= '9') || ((*q | 32) >= 'a' && (*q | 32) <= 'f'))
        v = v * 16 + (*q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10), q++;
    return v;
}

static void hex_cat(char *out, ULONG v, int digits)
{
    char t[9];
    int i;
    for (i = 0; i < digits; i++) {
        int n = (v >> (4 * (digits - 1 - i))) & 15;
        t[i] = n < 10 ? '0' + n : 'A' + n - 10;
    }
    t[digits] = 0;
    strcat(out, t);
}

BOOL envonly;                    /* Use: until a reboot, not saved */
BOOL used_only;                  /* the settings were Used, not Saved */
BOOL saved_pc;                   /* the setting; pcmode may be PC's */

/* the settings' text: made for a load or save, not kept */
#define PREFS_MAX 3600
static void load_prefs_(char *buf);
static void load_prefs(void)
{
    char *b = AllocVec(PREFS_MAX, MEMF_ANY);
    if (b) {
        load_prefs_(b);
        FreeVec(b);
    }
}

static void load_prefs_(char *buf)
{
    char *p = buf;
    /* the whole file: without GVF_BINARY_VAR, GetVar stops at the first
     * line break (every setting after "truetype" was lost) */
    LONG n = GetVar(PREFS_VAR, buf, PREFS_MAX,
                    GVF_GLOBAL_ONLY | GVF_BINARY_VAR);
    if (n <= 0)
        return;
    if (n >= PREFS_MAX)
        n = PREFS_MAX - 1;
    buf[n] = 0;
    hadprefs = TRUE;
    while (*p) {
        char *eol = strchr(p, '\n'), *val;
        if (eol) *eol = 0;
        val = strchr(p, '=');
        if (val) {
            *val++ = 0;
            if (!stricmp(p, "truetype")) usett = val[0] == '1';
            else if (!stricmp(p, "ttfile")) {
                strncpy(ttfile, val, sizeof ttfile - 1);
                ttfile[sizeof ttfile - 1] = 0;
            }
            else if (!stricmp(p, "ttsize")) ttsize = atoi(val);
            else if (!stricmp(p, "font")) {
                char *sl = strrchr(val, '/');
                if (sl) {
                    *sl = 0;
                    strncpy(fontname, val, sizeof fontname - 1);
                    fontsize = atoi(sl + 1);
                }
            }
            else if (!stricmp(p, "screen")) {
                scrmode = stricmp(val, "wb") ? SCR_OWN : SCR_WB;
                if (scrmode == SCR_OWN) owndepth = atoi(val + 3);
                if (owndepth < 1) owndepth = 8;
            }
            else if (!stricmp(p, "full")) fullscreen = val[0] == '1';
            else if (!stricmp(p, "scrollbar")) scrollbar = val[0] == '1';
            else if (!stricmp(p, "utf8")) utf8 = saved_utf8 = val[0] == '1';
            else if (!stricmp(p, "theme")) {
                int i;
                for (i = 0; i < NTHEMES; i++)
                    if (!stricmp(val, themes[i].name))
                        theme = i;
            }
            else if (!stricmp(p, "fg")) { myfg = hexval(val); ownfg = TRUE; }
            else if (!stricmp(p, "bg")) { mybg = hexval(val); ownbg = TRUE; }
            else if (!stricmp(p, "spacing")) {
                linespace = atoi(val);
                if (linespace < 0 || linespace > 8) linespace = 0;
            }
            else if (!stricmp(p, "pc")) pcmode = saved_pc = val[0] == '1';
            else if (!stricmp(p, "pcfont")) pcfont = val[0] != '0';
            else if (!stricmp(p, "altmeta")) altmeta = val[0] != '0';
            else if (!stricmp(p, "pictures")) pics = val[0] != '0';
            else if (!stricmp(p, "ownscreen")) ownprog = val[0] != '0';
            else if (!stricmp(p, "keyuser")) userkey[0] = val[0];
            else if (!stricmp(p, "keypass")) passkey[0] = val[0];
            else if (!stricmp(p, "dldir")) {
                strncpy(dldir, val, sizeof dldir - 1);
                dldir[sizeof dldir - 1] = 0;
            } else if (!stricmp(p, "uldir")) {
                strncpy(uldir, val, sizeof uldir - 1);
                uldir[sizeof uldir - 1] = 0;
            }
            else if (!stricmp(p, "bgpic")) {
                strncpy(bgfile, val, sizeof bgfile - 1);
                bgfile[sizeof bgfile - 1] = 0;
            }
            else if (!stricmp(p, "bgsee")) bgsee = atoi(val);
            else if (!stricmp(p, "dwindle")) dwindle = atoi(val) != 0;
            else if (!stricmp(p, "gaps")) {
                gaps = atoi(val);
                if (gaps < 0 || gaps > MAXGAPS) gaps = 0;
            }
            else if (ser_pref(p, val))
                ;                       /* the serial line, the modem */
            else if (!stricmp(p, "mode")) {     /* "hexid,w,h" */
                const char *q = val;
                ownmode = hexval(q);
                while (*q && *q != ',') q++;
                ownw = ownh = 0;
                if (*q == ',') for (q++; *q >= '0' && *q <= '9'; q++)
                    ownw = ownw * 10 + *q - '0';
                if (*q == ',') for (q++; *q >= '0' && *q <= '9'; q++)
                    ownh = ownh * 10 + *q - '0';
            }
            else if (!stricmp(p, "calls")) {
                strncpy(calls, val, sizeof calls - 1);
                calls[sizeof calls - 1] = 0;
            }
            else if (!stricmp(p, "window")) {
                /* four numbers: left,top,width,height (libnix's strtol
                 * gave no end pointer here, so read them by hand) */
                LONG v[4] = { -1, 0, 0, 0 };
                int k = 0;
                const char *q = val;
                for (k = 0; k < 4 && *q; k++) {
                    LONG n = 0;
                    while (*q >= '0' && *q <= '9')
                        n = n * 10 + (*q++ - '0');
                    v[k] = n;
                    if (*q == ',')
                        q++;
                }
                wl = v[0]; wt = v[1]; ww = v[2]; wh = v[3];
                if (k < 4 || ww < 100 || wh < 50) wl = -1;
            }
        }
        if (!eol)
            break;
        p = eol + 1;
    }
    if (ttsize < 6 || ttsize > 72)
        ttsize = 14;
}

static void save_prefs_(char *buf);
void save_prefs(void)
{
    char *b;
    if (setwin)
        return;                         /* the settings window decides */
    if ((b = AllocVec(PREFS_MAX, MEMF_ANY))) {
        save_prefs_(b);
        FreeVec(b);
    }
}

static void save_prefs_(char *buf)
{
    buf[0] = 0;
    strcat(buf, "truetype=");
    strcat(buf, (fontover ? keepfont.usett : usett) ? "1" : "0");
    strcat(buf, "\nttfile=");
    strcat(buf, fontover ? keepfont.ttfile : ttfile);
    strcat(buf, "\nttsize=");
    numstr_cat(buf, fontover ? keepfont.ttsize : ttsize);
    if (!pcfont) strcat(buf, "\npcfont=0");
    if (!altmeta) strcat(buf, "\naltmeta=0");
    if (!pics) strcat(buf, "\npictures=0");
    if (!ownprog) strcat(buf, "\nownscreen=0");
    if (dwindle) strcat(buf, "\ndwindle=1");
    if (gaps) { strcat(buf, "\ngaps="); numstr_cat(buf, gaps); }
    if (dldir[0]) { strcat(buf, "\ndldir="); strcat(buf, dldir); }
    if (uldir[0]) { strcat(buf, "\nuldir="); strcat(buf, uldir); }
    if (bgfile[0]) {
        strcat(buf, "\nbgpic="); strcat(buf, bgfile);
        strcat(buf, "\nbgsee="); numstr_cat(buf, bgsee);
    }
    if (fontname[0]) {
        strcat(buf, "\nfont=");
        strcat(buf, fontname);
        strcat(buf, "/");
        numstr_cat(buf, fontsize);
    }
    strcat(buf, "\nscreen=");
    if (scrmode == SCR_WB)
        strcat(buf, "wb");
    else {
        strcat(buf, "own");
        numstr_cat(buf, owndepth);
    }
    strcat(buf, "\nfull="); strcat(buf, fullscreen ? "1" : "0");
    strcat(buf, "\nscrollbar="); strcat(buf, scrollbar ? "1" : "0");
    strcat(buf, "\nutf8="); strcat(buf, saved_utf8 ? "1" : "0");
    strcat(buf, "\ntheme="); strcat(buf, themes[theme].name);
    if (ownfg) { strcat(buf, "\nfg="); hex_cat(buf, myfg, 6); }
    if (ownbg) { strcat(buf, "\nbg="); hex_cat(buf, mybg, 6); }
    if (linespace) { strcat(buf, "\nspacing="); numstr_cat(buf, linespace); }
    if (saved_pc) strcat(buf, "\npc=1");
    if (ownmode != INVALID_ID) {
        strcat(buf, "\nmode="); hex_cat(buf, ownmode, 8);
        strcat(buf, ","); numstr_cat(buf, ownw);
        strcat(buf, ","); numstr_cat(buf, ownh);
    }
    ser_prefs_save(buf);
    if (calls[0]) {
        strcat(buf, "\ncalls=");
        strcat(buf, calls);
    }
    if (wl >= 0) {
        strcat(buf, "\nwindow=");
        numstr_cat(buf, wl); strcat(buf, ",");
        numstr_cat(buf, wt); strcat(buf, ",");
        numstr_cat(buf, ww); strcat(buf, ",");
        numstr_cat(buf, wh);
    }
    strcat(buf, "\n");
    SetVar(PREFS_VAR, buf, -1,
           GVF_GLOBAL_ONLY | (envonly || used_only ? 0 : GVF_SAVE_VAR));
}

/* A word in the title for a moment (a pane's new direction, what did
 * not fit): gone after about two seconds of IntuiTicks (the window is
 * active: a key was just pressed in it), or with the next one. */
static char hint[64];
static int hint_ticks;

void title_hint(const char *s)
{
    strncpy(hint, s, sizeof hint - 1);
    hint[sizeof hint - 1] = 0;
    hint_ticks = 20;
    title_update_now();
}

void title_tick(void)
{
    if (hint_ticks && !--hint_ticks)
        title_update_now();
}

void title_update_now(void)
{
    title_force = TRUE;
    title_update();
}

static void title_update_(void);

/* The title is the shown tab's (the active pane's), whatever T is. */
static void title_update(void)
{
    struct Tab *keep = T;
    if (!shown)
        return;
    T = shown;
    title_update_();
    T = keep;
}

static void title_update_(void)
{
    int changed;
    const char *t = vt_title(vt, &changed);
    static char title[160];
    if (title_force)
        changed = TRUE, title_force = FALSE;
    if (!changed || !win)
        return;
    strcpy(title, "CTerm");
    if (hint_ticks) {
        strcat(title, ": ");
        strcat(title, hint);
    } else if (T->t_bookname[0]) {
        strcat(title, ": ");
        strcat(title, T->t_bookname);
    } else if (dialhost[0]) {
        strcat(title, T->t_fkind == FK_SFTP ? ": sftp " : T->t_fkind
                      ? ": ftp " : sshmode ? ": ssh " : ": ");
        strcat(title, dialhost);
    }
    if (T->t_log)
        strcat(title, " (logging)");
    if (t[0]) {
        UBYTE *d;
        int i = 8;
        strcat(title, " - ");
        d = (UBYTE *)title + strlen(title);
        /* the title is UTF-8: keep what Latin-1 can show */
        while (*t && i++ < 140 && d < (UBYTE *)title + sizeof title - 2) {
            UBYTE c = (UBYTE)*t++;
            if (c < 0x80) *d++ = c;
            else if ((c & 0xE0) == 0xC0 && *t) {
                ULONG cp = ((c & 0x1F) << 6) | (*t++ & 0x3F);
                *d++ = cp < 256 ? (UBYTE)cp : '?';
            } else if (c >= 0xC0) {
                while ((*t & 0xC0) == 0x80) t++;
            }
        }
        *d = 0;
    }
    SetWindowTitles(win, fullscreen ? (UBYTE *)~0 : (UBYTE *)title,
                    (UBYTE *)title);
}

static char fromfile[256];              /* FROM: the Shell's first script */

/* ---- tabs ---------------------------------------------------------- */

/* Every tab's Shell: the signals to wait for, the packets to answer. */
/* What a wait inside CTerm's work listens to besides its own: the
 * Shells, and the ARexx port (read only there: rexx.c). */
ULONG shells_sigmask(void)
{
    ULONG m = rexx_sigmask();
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con)
            m |= con_sigmask(tabs[i]->con);
    return m;
}

void shells_handle(ULONG sigs)
{
    struct Tab *keep = T;
    int i;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con) {
            T = tabs[i];
            con_handle(T->con, sigs);
        }
    T = keep;
    con_spent_poll();
    if (sigs & rexx_sigmask())
        rexx_serve();
}

/* Another screen, the window opened again - not while a program has a
 * Shell's console raw: Ed and More keep the window's pointer (DISK_INFO
 * gave it) and would use a closed window. Then a beep, the switch is
 * dropped (waiting would stop the window's keys: window_events reads
 * none while one is wanted) and FALSE. The processes given the old
 * window for their requesters get the new one. */
BOOL display_switch(void)
{
    struct Window *old = win;
    int i;
    for (i = 0; i < ntabs; i++)
        if (con_raw(tabs[i]->con)) {
            want_switch = FALSE;
            beep();
            return FALSE;
        }
    switch_display();
    con_window_moved(old, win);
    return TRUE;
}

/* A tab of its own: an emulator the window's size, no Shell or host yet. */
struct Tab *tab_new(void)
{
    struct Tab *t;
    ULONG lines;
    if (ntabs >= MAXTABS)
        return NULL;
    t = AllocVec(sizeof *t, MEMF_ANY | MEMF_CLEAR);
    if (!t)
        return NULL;
    lines = history_lines();
    t->t_vt = vt_new(cols > 0 ? cols : 80, rows > 0 ? rows : 25,
                   lines < SCROLLBACK ? (int)lines : SCROLLBACK);
    if (!t->t_vt) {
        FreeVec(t);
        return NULL;
    }
    t->t_sock = t->t_lfd = -1;
    t->t_local = TRUE;
    /* the default characters (Settings, Text); a phone book place, a
     * call or the browser may change them after */
    t->t_utf8 = saved_utf8 && !saved_pc;
    t->t_pcmode = saved_pc;
    vt_set_utf8(t->t_vt, t->t_utf8);
    if (t->t_pcmode)
        vt_set_cp437(t->t_vt, 1);
    tabs[ntabs++] = t;
    pics_tab(t);
    return t;
}

/* Show tab i: in the active pane (or, if another pane shows it, that
 * pane becomes the active one), its font (a PC BBS wears the PC's when
 * the window is not split), its title. */
void tab_show(int i)
{
    struct Pane *p;
    if (i < 0 || i >= ntabs)
        return;
    if ((p = pane_of(tabs[i])))
        apane = p;
    else
        apane->tab = tabs[i];
    pane_use(apane);
    shown = T = tabs[i];
    if (!win)                           /* ending, the window gone */
        return;
    tab_letters(TRUE);
    /* always: the tab bar may just have come or gone (ntabs has changed
     * already), and both are cheap when nothing did */
    layout_and_resize();
    painter_forget();
    redraw_all();
    tell_sizes();                       /* a tab new to a pane: its size */
    title_update_now();
}

/* Written backwards on purpose: the forward search, inlined into
 * tab_drop, came out of bebbo's gcc -O2 stepping the pointer twice after
 * tabs[0] - tabs[1] was never found and closing tab 2 spun for ever
 * (seen in the disassembly, 2026-10-10). */
static int tab_index(struct Tab *t)
{
    int i = ntabs;
    while (--i >= 0)
        if (tabs[i] == t)
            break;
    return i;
}

/* A tab goes: its host hung up on, its emulator freed (a local tab only
 * once its Shell has ended). */
static void tab_drop(struct Tab *t)
{
    struct Tab *keep = T;
    int i = tab_index(t);
    if (i < 0)
        return;
    T = t;
    term_sync(t);                       /* its last writes, into the log */
    log_stop();
    if (sock >= 0) {
        if (sshmode) {
            ssh_close(T->ssh);
            T->ssh = NULL;
        } else
            CloseSocket(sock);
        sock = -1;
    }
    if (lfd >= 0) {
        CloseSocket(lfd);
        lfd = -1;
    }
    serial_close();                     /* a modem hung up, the line shut */
    if (T->t_rf)
        files_tab_gone(t);              /* its browser and connection */
    if (T->con) {
        /* its signals and memory go back now; only the port is kept
         * (signal-less, polled) while processes may still open "*" */
        con_retire(T->con);
        T->con = NULL;
    }
    transfer_tab_gone(t);
    rexx_tab_gone(t);                   /* its WAITFORs answered */
    vt_free(T->t_vt);
    if (T->t_pend) {
        FreeVec(T->t_pend);
        T->t_pend = NULL;
        T->t_plen = 0;
    }
    memmove(tabs + i, tabs + i + 1, (ntabs - i - 1) * sizeof tabs[0]);
    ntabs--;
    pane_tab_gone(t, i);                /* its pane: another tab, or none */
    if (t != &tab0)
        FreeVec(t);
    T = keep == t ? NULL : keep;
    shown = apane->tab;
    if (ntabs > 0 && win) {
        T = shown;
        tab_letters(TRUE);              /* alone again: its own letters */
        layout_and_resize();            /* the tab bar may have gone */
        painter_forget();
        redraw_all();
        tell_sizes();
        title_update_now();
    }
    if (!T && ntabs > 0)
        T = shown;
}

/* A new tab with the Amiga Shell in it, not shown; T as it was. */
struct Tab *shell_tab(void)
{
    struct Tab *t = tab_new(), *keep = T;
    if (!t)
        return NULL;
    T = t;
    vt_set_amiga(vt, 1);
    T->con = con_start("S:Shell-Startup", T);
    if (!T->con)
        say("CTerm could not start a Shell.\r\n");
    T = keep;
    return t;
}

/* A new tab with the Amiga Shell in it, shown. */
void tab_new_shell(void)
{
    if (!shell_tab()) {
        beep();
        return;
    }
    tab_show(ntabs - 1);
}

/* Close the shown tab: its Shell is told the input ended (the tab goes
 * when the Shell has), a host's is hung up on. */
void tab_close_current(void)
{
    if (T->con)
        con_stop(T->con);
    T->ended = TRUE;
}

/* T's host or line hung up on (ARexx's HANGUP); the tab stays, and
 * Return calls again. */
void tab_hangup(void)
{
    if (sock >= 0) {
        if (sshmode) {
            ssh_close(T->ssh);
            T->ssh = NULL;
        } else
            CloseSocket(sock);
        sock = -1;
    }
    serial_close();
    if (T->t_rf)
        files_tab_gone(T);
    rexx_tab_over(T);
    say_key("\r\n\x1b[0;2m[hung up - ", KA_REDIAL, " calls again]\x1b[0m\r\n");
}

/* Everyone out: the Shells are told the input ended, hosts hung up on. */
void quit_all(void)
{
    int i;
    quitting = TRUE;
    for (i = 0; i < ntabs; i++)
        if (tabs[i]->con)
            con_stop(tabs[i]->con);
        else
            tabs[i]->ended = TRUE;
}

/* A host's bytes for tab t (T is t): take what has come, and what keeps
 * coming without a wait, before drawing - a burst draws once. */
static void host_input(UBYTE *buf, LONG size)
{
    if (lfd >= 0 && sock < 0) {
        connected();
        return;
    }
    {
        int rounds = 0;
        for (;;) {
            struct timeval tv;
            LONG k;
            if (sshmode) {
                /* drain it: libssh2 may hold decrypted data the socket
                 * no longer shows */
                while ((k = ssh_read(T->ssh, buf, size)) > 0) {
                    if (transfer_take(buf, k))
                        break;          /* a file transfer took the rest */
                    log_bytes(buf, k);
                    vt_write(vt, buf, k);
                }
                if (k < 0)
                    hung_up();
                break;
            }
            k = recv(sock, buf, size, 0);
            tv.tv_sec = tv.tv_usec = 0;
            if (k <= 0) {
                hung_up();
                break;
            }
            if (transfer_take(buf, k))
                break;                  /* a file transfer took the rest */
            from_host(buf, k);
            if (++rounds > 16)
                break;
            {
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(sock, &rd);
                if (WaitSelect(sock + 1, &rd, NULL, NULL, &tv, NULL) <= 0)
                    break;
            }
        }
    }
    {
        char r[256];
        int k = vt_take_reply(vt, r, sizeof r);
        if (k > 0)
            send_data((UBYTE *)r, k);
    }
    host_clip();
}

static void run(void)
{
#ifdef PROFILE
    {   /* what one clock read costs, to take it out of the buckets */
        int k;
        ULONG t0 = ticks();
        for (k = 0; k < 1000; k++)
            (void)ticks();
        pf_calib = ticks() - t0;
    }
    pf_all0 = ticks();
    term_now(&pf_tv0);
#endif
    T = shown = tabs[0];
    if (local) {
        const char *script = fromfile[0] ? fromfile : "S:Shell-Startup";
        BPTR l = Lock((STRPTR)script, ACCESS_READ);
        if (l)
            UnLock(l);
        T->con = con_start(l ? script : NULL, T);
        ML("shell");
#ifdef MEMLOG
        memlog_save();                  /* the marks so far, CTerm idle */
#endif
        if (!T->con) {
            /* no Shell: say so and wait for the window to be closed -
             * never fall into the network loop, there is no network */
            say("CTerm could not start a Shell.\r\n");
            refresh();
            for (;;) {
                struct IntuiMessage *m;
                BOOL done = FALSE;
                WaitPort(win->UserPort);
                while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                    if (m->Class == IDCMP_CLOSEWINDOW)
                        done = TRUE;
                    ReplyMsg((struct Message *)m);
                }
                if (done)
                    return;
            }
        }
    } else if (dialhost[0]) {
        say(T->t_serial && !T->t_st.phone[0] ? "\x1b[2m[opening "
                                           : "\x1b[2m[calling ");
        say(dialhost);
        say("... ");
        say(keys_word(KA_STOP));
        say(" gives up]\x1b[0m\r\n");
        refresh();
        if (!connect_out()) {
            say("\x1b[2m[");
            say(keys_word(KA_REDIAL));
            say(" calls again]\x1b[0m\r\n");
        }
        else
            rexx_after_call();          /* its place's login script */
    } else {
        lfd = listen_on();
        if (lfd < 0)
            say("\x1b[1;31mCTerm cannot listen on its port - is it already "
                "running?\x1b[0m\r\n");
        else
            say("\x1b[1mCTerm\x1b[0m - waiting for the PC (cterm-relay)..."
                "\r\n");
    }
    refresh();

    for (;;) {
        int i;
        loop_round(-1, -1);
        rexx_run();                     /* ARexx's queued commands: here,
                                         * where nothing is half done */
        pics_tidy();                    /* Parts/Pictures, if none is left */
        /* tabs whose Shell has ended go */
        for (i = 0; i < ntabs; i++)
            if (tabs[i]->ended && !(tabs[i]->con &&
                                    con_alive(tabs[i]->con))) {
                tab_drop(tabs[i]);
                i = -1;                 /* the list moved: again */
            }
        if (ntabs == 0)
            break;
        T = shown;
        if (want_switch)
            display_switch();           /* not while Ed or More runs */
        if (!win && !hidden)
            break;
    }
}

static UBYTE *hostbuf;                  /* a host's bytes: made at the first */

/* hostbuf, and its size: 16 KB, made once; when there was no memory for
 * it, a small one of its own - never asked again on every read */
static UBYTE hostsmall[1024];
static LONG host_buf(void)
{
    static BOOL tried;
    if (!hostbuf && !tried) {
        tried = TRUE;
        hostbuf = AllocVec(16384, MEMF_ANY);
    }
    if (hostbuf && hostbuf != hostsmall)
        return 16384;
    hostbuf = hostsmall;
    return sizeof hostsmall;
}

/* The Shells' packets alone (loop_round's short way): FALSE when
 * something needs the whole round - a Shell that ended. */
static BOOL shells_only(ULONG sigs)
{
    struct Tab *keep = T;
    BOOL ok = TRUE;
    int i;
    for (i = 0; i < ntabs; i++) {
        T = tabs[i];
        if (T->con) {
            char r[256];
            int k;
            PF_START(tp);
            con_handle(T->con, sigs);
            PF_ADD(pf_pkt, tp);
            k = vt_take_reply(vt, r, sizeof r);
            if (k > 0)
                con_reply(T->con, (UBYTE *)r, k);
            if (!con_alive(T->con))
                ok = FALSE;
        }
    }
    T = keep;
    return ok;
}

/* One round of CTerm's life: wait for anything - the window, the Shells,
 * the hosts, at most ms (-1: as long as it takes) - and handle it. A
 * transfer waits in here (xfd: its socket, which it reads itself), so the
 * window and the other tabs go on meanwhile; then tabs are not dropped
 * and the screen is not switched (run() does that, at the top). TRUE:
 * xfd has data. */
LONG loop_wfd = -1;

BOOL loop_round(LONG xfd, LONG ms)
{
    struct Tab *me = T;
    fd_set rd, wr;
    LONG maxfd = -1, n = 0;
    ULONG winsig = win ? 1UL << win->UserPort->mp_SigBit : 0;
    ULONG hidesig = hide_sigmask();
    ULONG setsig = setwin ? 1UL << setwin->UserPort->mp_SigBit : 0;
    ULONG sersig = serial_sigmask();
    ULONG rxsig = rexx_sigmask();
    ULONG sigs = winsig | setsig | SIGBREAKF_CTRL_C | con_timer_sig() |
                 xwin_sigmask() | bookwin_sigmask() | sersig | rxsig |
                 hidesig | about_sigmask();
    BOOL hostdata = FALSE, ready = FALSE;
    ULONG consigs = 0;
    int i;
    FD_ZERO(&rd);
    for (i = 0; i < ntabs; i++) {
        struct Tab *t = tabs[i];
        LONG fd = t->t_sock >= 0 ? t->t_sock : t->t_lfd;
        if (t->con)
            consigs |= con_sigmask(t->con);
        if (fd >= 0 && !t->t_xfer) {    /* a transfer reads its own */
            FD_SET(fd, &rd);
            if (fd > maxfd)
                maxfd = fd;
        }
    }
    sigs |= consigs;
    if (xfd >= 0) {
        FD_SET(xfd, &rd);
        if (xfd > maxfd)
            maxfd = xfd;
    }
    FD_ZERO(&wr);
    if (loop_wfd >= 0) {                /* a browser's upload: room */
        FD_SET(loop_wfd, &wr);
        if (loop_wfd > maxfd)
            maxfd = loop_wfd;
    }
    if (maxfd < 0 && ms == 0) {
        /* a look, no wait - not a WaitSelect with no sockets and no
         * time: FS-UAE's waited for a signal there (a file browser's
         * download stood still) */
        sigs = SetSignal(0, sigs) & sigs;
    } else if (maxfd < 0 && (ms < 0 || !SocketBase)) {
        /* no network: the plain Amiga wait - the socket library's
         * WaitSelect costs far more per round, and a flood of tiny
         * writes is all rounds. A serial line's transfer waits here
         * too when there is no TCP/IP stack: the line's timer ends it. */
        ULONG all = sigs;
        if (ms > 0)
            serial_alarm(ms);
        for (;;) {
            PF_START(tw);
            sigs = Wait(all);
            PF_ADD(pf_wait, tw);
            /* only the Shells' packets: answer them and wait again. The
             * window, the title, the menus wait for a real event or the
             * frame timer - on a stock A1200 the round's other checks were
             * a good part of each write (CCON: a lean loop per write).
             * The timer's bit is the Shells' too (WAIT_CHAR): a frame
             * that is up ends the loop, or an inactive window (no ticks)
             * was never drawn. */
            if ((sigs & ~consigs) || !shells_only(sigs) ||
                con_render_ready())
                break;
        }
        if (ms > 0)
            serial_alarm(-1);
    } else {
        struct timeval tv;
        tv.tv_sec = ms > 0 ? ms / 1000 : 0;
        tv.tv_usec = ms > 0 ? (ms % 1000) * 1000 : 0;
        ULONG want = sigs;
        n = WaitSelect(maxfd + 1, &rd, loop_wfd >= 0 ? &wr : NULL, NULL,
                       ms >= 0 ? &tv : NULL, &sigs);
        /* a socket already readable returns at once without the signals
         * (lwip-amiga, seen on the A1200): during a stream of host data
         * the window was never served - Esc did not stop a transfer. The
         * signals waiting are taken here as well. */
        if (n > 0)
            sigs |= SetSignal(0, want) & want;
    }
    if (n > 0 && ((xfd >= 0 && FD_ISSET(xfd, &rd)) ||
                  (loop_wfd >= 0 && FD_ISSET(loop_wfd, &wr))))
        ready = TRUE;
    serial_poll();                      /* every line: what came */
    /* anything but the Shells and the frame timer may look at the text */
    if (sigs & ~(con_timer_sig() | consigs))
        term_sync_all();
    T = shown;
    if ((sigs & SIGBREAKF_CTRL_C) && !quitting)
        quit_all();
    if (setwin && (sigs & setsig))
        settings_events();
    if (sigs & xwin_sigmask())
        xwin_events();
    if (sigs & bookwin_sigmask())
        bookwin_events();
    if (sigs & about_sigmask())
        about_events();
    if (sigs & rxsig)
        rexx_serve();                   /* before the hosts' bytes: an
                                         * UPLOAD waiting is seen first */
    if (sigs & hidesig)
        hide_events();                  /* the AppIcon, Exchange: they ask */
    T = shown;
    if ((sigs & winsig) && !window_events() && !quitting)
        quit_all();
    /* every tab: its Shell's packets, its host's bytes */
    for (i = 0; i < ntabs; i++) {
        T = tabs[i];
        if (T->con) {
            char r[256];
            int k;
            PF_START(tp);
            con_handle(T->con, sigs);
            PF_ADD(pf_pkt, tp);
            k = vt_take_reply(vt, r, sizeof r);
            if (k > 0)
                con_reply(T->con, (UBYTE *)r, k);
            host_clip();            /* the Shell's programs may copy too */
            if (!con_alive(T->con))
                T->ended = TRUE;    /* the Shell ended */
        }
        if (T->t_st.ser && !T->t_xfer && serial_due()) {
            LONG hn = host_buf();
            serial_input(hostbuf, hn);
            if (pane_of(T))
                hostdata = TRUE;
        }
        if (n > 0 && !T->t_xfer) {
            LONG fd = sock >= 0 ? sock : lfd;
            if (fd >= 0 && FD_ISSET(fd, &rd)) {
                LONG hn = host_buf();
                host_input(hostbuf, hn);
                if (pane_of(T))
                    hostdata = TRUE;
            }
        }
        if (vt_take_bell(vt) && pane_of(T))
            beep();
    }
    T = shown;
    title_update();
    if (win && !win->MenuStrip && menu)
        SetMenuStrip(win, menu);    /* Ed has gone */
    menu_ticks();                   /* the shown tab's characters */
    if (con_render_due() && owed)
        draw_now();
    if (hostdata)
        refresh_all();
    mouse_reporting();
    hide_show();                        /* hiding or showing, if asked: here,
                                         * where nothing is half drawn */
    T = me;
    return ready;
}

static BOOL arg_pc, arg_latin1, arg_serial, arg_hasunit;
static BOOL arg_plain = TRUE, arg_show;     /* no words at all; SHOW */
static char arg_dev[48];
static LONG arg_unit, arg_baud;
static int arg_book = -1;
/* asl.library and diskfont.library come from disk and take some 100 KB:
 * opened the first time a requester or a disk font is wanted, so a stock
 * A1200 keeps the memory until then. */
/* Memory the history leaves free for everything else: on a chip-only
 * Amiga programs started in the Shell need it (conbench alone wants 36
 * KB, 32 KB of it in one piece); a full machine also runs exec's
 * low-memory handlers on every allocation that fails, many times slower
 * (a stock A1200's conbench: 30 s became 96 s when the history ate the
 * last 20 KB). */
#define RESERVE_CHIP (64 * 1024)
#define RESERVE_FAST (512 * 1024)

static ULONG reserve(void)
{
    return AvailMem(MEMF_FAST) ? RESERVE_FAST : RESERVE_CHIP;
}

/* How many 80-column lines of history a tab may keep at most: what is
 * free beyond the reserve (a quarter of it with fast memory). The lines
 * are made as the text scrolls, and only while the reserve stays free
 * (hist_room), so this is a ceiling, not memory taken. */
ULONG history_lines(void)
{
    ULONG avail = AvailMem(MEMF_ANY), keep = reserve();
    ULONG part = avail <= keep ? 0 : AvailMem(MEMF_FAST)
                 ? (avail - keep) / 4 : avail - keep;
    return part / (82 * sizeof(VtCell));
}

/* The emulator's question before each new history line (only while a
 * history grows, at most its length per tab). Free memory is counted
 * every 64th line or when what was left last time is used up: AvailMem
 * walks every free block under Forbid, too slow for every line. */
static int hist_room(long n)
{
    static LONG left;
    static int asked;
    if (left < n || !(++asked & 63)) {
        ULONG a = AvailMem(MEMF_ANY), keep = reserve();
        left = a > keep ? (LONG)(a - keep) : 0;
        if (left < n)
            return 0;
    }
    left -= n;
    return 1;
}

/* Pictures in the terminal: what a tab's may hold - a quarter of what
 * is free beyond the reserve, at most 4 MB (the reserve itself is kept
 * by vt_room, as for the history). */
long pics_cap(void)
{
    ULONG avail = AvailMem(MEMF_ANY), keep = reserve();
    ULONG part = avail <= keep ? 0 : (avail - keep) / 4;
    return part > (4UL << 20) ? 4L << 20 : (long)part;
}

/* Pictures' pixels straight from exec: back to the system the moment a
 * picture goes (not kept in the C library's heap), and none of them
 * splits that heap into pieces. */
static void *pic_alloc(long n)
{
    return AllocVec(n, MEMF_ANY);
}

static void pic_release(void *p)
{
    FreeVec(p);
}

void pics_tab(struct Tab *t)
{
    if (!t->t_vt)
        return;
    vt_set_pictures(t->t_vt, pics, pics_cap());
    vt_set_cell_px(t->t_vt, cw > 0 ? cw : 8, ch > 0 ? ch : 16, defbg);
}

void pics_all(void)                     /* the setting changed */
{
    int i;
    for (i = 0; i < ntabs; i++)
        pics_tab(tabs[i]);
}

BOOL asl_open(void)
{
    if (!AslBase)
        AslBase = OpenLibrary((STRPTR)"asl.library", 38);
    return AslBase != NULL;
}

/* ttengine.library is 109 KB: opened the first time TrueType is used
 * (a person with Amiga fonts never loads it). Whether it is there to be
 * used: tt_installed(), which does not load it. */
BOOL tt_installed(void)
{
    BPTR l;
    if (TTEngineBase)
        return TRUE;
    l = Lock((STRPTR)"LIBS:ttengine.library", ACCESS_READ);
    if (l)
        UnLock(l);
    return l != 0;
}

BOOL tt_open(void)
{
    if (!TTEngineBase)
        TTEngineBase = OpenLibrary((STRPTR)"ttengine.library", 6);
    return TTEngineBase != NULL;
}

BOOL diskfont_open(void)
{
    if (!DiskfontBase)
        DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    return DiskfontBase != NULL;
}

static int m_argc, m_rc;
static char **m_argv;

static void __attribute__((noinline)) cterm_main_(void)
{
    m_rc = cterm_main(m_argc, m_argv);
}

/* CTerm itself runs on a Shell's usual 4 KB; below 3 KB it moves to
 * 8 KB of its own (libnix's __stack does not: Knowledge/amiga/
 * kickstart-13-porting.md). What wants far more - OS 3.2's window classes
 * (they crashed the emulator on 3 KB) - runs through deep_call, so a
 * stock A1200 lends the memory only while it is used. */

/* fn on a stack with 48 KB free at least: a 64 KB one of its own for
 * the call when this one has less left. */
void deep_call(void (*fn)(void))
{
    struct Task *me = FindTask(NULL);
    static struct StackSwapStruct dss;
    UBYTE here, *stk;
    if ((UBYTE *)&here - (UBYTE *)me->tc_SPLower >= 49152 ||
        !(stk = AllocVec(65536, MEMF_ANY))) {
        fn();
        return;
    }
    {
        struct StackSwapStruct keep = dss;      /* calls may nest */
        dss.stk_Lower = stk;
        dss.stk_Upper = (ULONG)(stk + 65536);
        dss.stk_Pointer = (APTR)(stk + 65536);
        StackSwap(&dss);
        fn();
        StackSwap(&dss);
        dss = keep;
    }
    FreeVec(stk);
}
int main(int argc, char **argv)
{
    struct Task *me = FindTask(NULL);
    LONG have = (char *)me->tc_SPUpper - (char *)me->tc_SPLower;
    static struct StackSwapStruct sss;
    UBYTE *stk;
#ifdef MEMLOG
    memh_on();
#endif
    ML("main");
    m_argc = argc;
    m_argv = argv;
    if (have >= 3000 || !(stk = AllocVec(8192, MEMF_ANY)))
        return cterm_main(argc, argv);
    sss.stk_Lower = stk;
    sss.stk_Upper = (ULONG)(stk + 8192);
    sss.stk_Pointer = (APTR)(stk + 8192);
    StackSwap(&sss);
    cterm_main_();
    StackSwap(&sss);
    FreeVec(stk);
    return m_rc;
}

/* CTerm needs fast memory, as it needs OS 3.2 (an Amiga with 3.2 has
 * some): with chip memory only, the Shell's programs and CTerm fight for
 * the same 2 MB, and the display's DMA slows CTerm's code down. Said in
 * the Shell, or in a requester from Workbench. */
static BOOL fast_ok(int argc)
{
    static const char why[] = "CTerm needs an Amiga with extra memory "
                              "(fast RAM).";
    if (AvailMem(MEMF_FAST) && (TypeOfMem((APTR)fast_ok) & MEMF_FAST))
        return TRUE;
    if (argc > 0) {
        PutStr((STRPTR)why);
        PutStr((STRPTR)"\n");
    } else {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0,
                                 (UBYTE *)"CTerm", (UBYTE *)why,
                                 (UBYTE *)"OK" };
        EasyRequestArgs(NULL, &es, NULL, NULL);
    }
    return FALSE;
}

static int cterm_main(int argc, char **argv)
{
    tab0.t_utf8 = TRUE;
    int rc = RETURN_OK;
    (void)version;
    (void)argv;
    if (!fast_ok(argc))
        return RETURN_FAIL;
    if (argc > 0) {
        LONG args[19];
        int k;
        struct RDArgs *rda;
        memset(args, 0, sizeof args);
        rda = ReadArgs("PORT/N,LATIN1/S,LISTEN/S,COLS/N,ROWS/N,"
                       "FROM/K,HOST/K,PC/S,SSH/K,KEY/K,BOOK/K,"
                       "TEXTDRAW/S,SERIAL/S,DEVICE/K,UNIT/N,"
                       "BAUD/N,SHOW/S,SFTP/K,FTP/K", args, NULL);
        if (!rda) {
            PrintFault(IoErr(), "CTerm");
            return RETURN_FAIL;
        }
        /* SHOW: a CTerm already running comes to the front instead */
        for (k = 0; k < 16 && !args[k]; k++)
            ;
        arg_show = args[16] != 0;
        arg_plain = k == 16 && !args[17] && !args[18];
        if (args[0]) port = *(LONG *)args[0];
        if (args[1]) arg_latin1 = TRUE;
        if (args[2]) local = FALSE;     /* wait for the PC's relay */
        if (args[3]) want_cols = *(LONG *)args[3];
        if (args[4]) want_rows = *(LONG *)args[4];
        if (args[2] || args[3] || args[4] || args[5] || args[6])
            remember = FALSE;
        if (args[5]) {                  /* the Shell runs this script first */
            strncpy(fromfile, (char *)args[5], sizeof fromfile - 1);
            fromfile[sizeof fromfile - 1] = 0;
        }
        if (args[6]) {                  /* call a host (a BBS...) */
            strncpy(dialhost, (char *)args[6], sizeof dialhost - 1);
            strcpy(dialtitle, "CTerm: ");
            strcat(dialtitle, dialhost);
            local = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[7]) arg_pc = TRUE;
        if (args[11]) nopp = TRUE;      /* letters by Text(): to compare */
        if (args[8]) {                  /* ssh to user@host[:port] */
            strncpy(dialhost, (char *)args[8], sizeof dialhost - 1);
            strcpy(dialtitle, "CTerm: ssh ");
            strcat(dialtitle, dialhost);
            sshmode = TRUE;
            local = FALSE;
            remember = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[9])
            ssh_set_key((char *)args[9]);
        if (args[12] || args[13]) {     /* a serial line (after the prefs) */
            arg_serial = TRUE;
            if (args[13]) {
                strncpy(arg_dev, (char *)args[13], sizeof arg_dev - 1);
                arg_dev[sizeof arg_dev - 1] = 0;
            }
            if (args[14]) arg_unit = *(LONG *)args[14];
            if (args[15]) arg_baud = *(LONG *)args[15];
            arg_hasunit = args[14] != 0;
            local = FALSE;
            remember = FALSE;
            T->t_serial = TRUE;
            strcpy(dialhost, "serial");  /* the real name once prefs are in */
            if (!args[3]) want_cols = 80;
        }
        if (args[17] || args[18]) {     /* the file browser: SFTP, FTP */
            BookEntry *e = AllocVec(sizeof *e, MEMF_ANY);
            if (e) {
                book_url(e, (char *)args[args[17] ? 17 : 18],
                         args[17] ? FK_SFTP : FK_FTP);
                tab_from_book(e);
                FreeVec(e);
            }
            remember = FALSE;
            if (!args[3]) want_cols = 80;
        }
        if (args[10]) {                 /* a phone book entry to call */
            load_prefs();               /* its serial line starts as the
                                         * settings' (book_blank) */
            book_load();
            arg_book = book_find((char *)args[10]);
            if (arg_book < 0) {
                Printf((STRPTR)"CTerm: no \"%s\" in the phone book\n",
                       args[10]);
                FreeArgs(rda);
                return RETURN_FAIL;
            }
            tab_from_book(&book[arg_book]);
            remember = FALSE;
            if (!args[3]) want_cols = 80;
        }
        FreeArgs(rda);
    }
    /* started again with no words (from Workbench too): a hidden CTerm
     * shows itself instead, and this one ends - as tmux attaches. With
     * SHOW, a CTerm that is not hidden comes to the front as well. A
     * start that asks for something (a call, a script) opens its own. */
    if ((arg_plain || arg_show) && rexx_wake(arg_show))
        return RETURN_OK;
#ifdef NO_SSH
    if (sshmode) {
        /* ssh is CTermSSH's: start it with the same words, beside us */
        char cmd[600], path[256];
        STRPTR a = GetArgStr();
        int n;
        BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
        own_path(path, sizeof path, "CTermSSH");
        strcpy(cmd, "Run >NIL: \"");
        strcat(cmd, path);
        strcat(cmd, "\" ");
        strncat(cmd, a ? (char *)a : "", sizeof cmd - strlen(cmd) - 1);
        n = strlen(cmd);
        while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == ' '))
            cmd[--n] = 0;
        if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                       TAG_END) != 0) {
            if (nil) Close(nil);
            PutStr("CTerm: ssh needs CTermSSH beside CTerm\n");
            return RETURN_FAIL;
        }
        return RETURN_OK;
    }
#endif
    /* the network only when there is one to talk to: the local Shell
     * needs no TCP/IP stack at all */
    ML("start");
    if (!local && !T->t_serial)         /* a serial line needs none */
        SocketBase = OpenLibrary("bsdsocket.library", 4);
    KeymapBase = OpenLibrary("keymap.library", 37);
    GadToolsBase = OpenLibrary("gadtools.library", 37);
    CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    ML("libs");
    load_prefs();
    keys_load();                        /* after the prefs: their old
                                           login keys, once */
    ML("prefs");
    panes_init(tabs[0]);
    apply_theme();
    if (bgfile[0] && !load_picture(bgfile))
        bgfile[0] = 0;
    if (arg_pc)
        pcmode = TRUE;
    if (arg_latin1)
        utf8 = FALSE;
    if (arg_book >= 0)                  /* the prefs set these: again */
        tab_from_book(&book[arg_book]);
    if (arg_serial)
        serial_tab_setup(arg_dev[0] ? arg_dev : NULL,
                         arg_hasunit ? &arg_unit : NULL, &arg_baud);
    /* first start: TrueType if it is installed (DejaVu Sans Mono) */
    if (!hadprefs)
        usett = tt_installed() && file_exists(ttfile);
    ML("book");
    con_timer_init();
    vt_room = hist_room;
    vt_decode = pics_decode;            /* iTerm2's files: Parts/Pictures */
    vt_decode_free = pics_decode_free;
    vt_palloc = pic_alloc;
    vt_pfree = pic_release;
    ML("timer");
    {
        /* the history's ceiling; its lines (80 cells: 492 bytes) only
         * while a reserve stays free: a stock A1200 keeps room to run
         * things */
        ULONG lines = history_lines();
        vt = vt_new(80, 25, lines < SCROLLBACK ? (int)lines : SCROLLBACK);
    }
    if (vt) {
        pics_tab(T);
        vt_set_utf8(vt, utf8);
        if (pcmode)
            vt_set_cp437(vt, 1);
        if (local)
            vt_set_amiga(vt, 1);        /* the Amiga console's ways */
    }
    ML("vt");
    want_bar = scrollbar;
    if (!local && !SocketBase && !T->t_serial) {
        PutStr("CTerm HOST and LISTEN need a TCP/IP stack (bsdsocket.library)\n");
        rc = RETURN_FAIL;
    } else if (!vt || !KeymapBase || !GadToolsBase ||
        !open_display()) {
        /* say why: a silent 20 helps nobody (a stock A1200 hit it) */
        PutStr(!vt ? "CTerm: not enough memory for the text\n"
               : !KeymapBase || !GadToolsBase
               ? "CTerm: keymap.library or gadtools.library is missing\n"
               : "CTerm: could not open its window (memory, or the screen)\n");
        rc = RETURN_FAIL;
    } else {
        if (pcmode)
            tab_letters(TRUE);
        ML("display");
        rexx_open();
        ML("rexx");
        cx_open();                      /* Exchange's Show, Hide, Remove */
        ML("cx");
        run();
    }
    rexx_close();                       /* before the tabs go: no command
                                         * may come for them */
#ifdef MEMLOG
    ML("quit");
    memlog_save();
    RemMemHandler(&mhint);
#endif
    settings_cancel();                  /* else save_prefs would not save */
    if (win) {
        if (remember && !fullscreen && scr != ownscr) {
            wl = win->LeftEdge; wt = win->TopEdge;
            ww = win->Width; wh = win->Height;
        }
        envonly = used_only;
        save_prefs();
    } else if (hidden) {                /* quit while hidden: as it was */
        if (remember && hid_l >= 0 && scrmode == SCR_WB) {
            wl = hid_l; wt = hid_t; ww = hid_w; wh = hid_h;
        }
        envonly = used_only;
        save_prefs();
    }
    hide_free();                        /* the AppIcon, the broker */
    /* nothing may still be open on a Shell's port when it goes */
    {
        int i;
        for (i = 0; i < ntabs; i++)
            if (tabs[i]->con && con_alive(tabs[i]->con))
                con_stop(tabs[i]->con);
        for (;;) {
            ULONG sg = shells_sigmask();
            BOOL any = FALSE;
            for (i = 0; i < ntabs; i++)
                if (tabs[i]->con && con_alive(tabs[i]->con))
                    any = TRUE;
            if (!any)
                break;
            shells_handle(Wait(sg));
        }
        while (ntabs > 0)
            tab_drop(tabs[0]);
        con_spent_free();
    }
    close_settings();
    xwin_shut();                        /* before its classes and screen go */
    bookwin_close();
    parts_free();                       /* ARexx's, which stays once used */
    reaction_close();
    clip_close();
    close_display(FALSE);
    if (ownfont) CloseFont(ownfont);
    cache_free();
    if (rowbuf) FreeVec(rowbuf);
    backdrop_free();
    if (bgsrc) FreeVec(bgsrc);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    book_free();
    transfer_free();
    if (hostbuf && hostbuf != hostsmall)
        FreeVec(hostbuf);
    close_tt();
    if (TTEngineBase) CloseLibrary(TTEngineBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    con_timer_free();
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (KeymapBase) CloseLibrary(KeymapBase);
    if (SocketBase) CloseLibrary(SocketBase);
    net_lookups_wait();                 /* a name lookup given up on runs
                                         * CTerm's code until it ends */
#ifdef PROFILE
    {
        BPTR f = Open((STRPTR)"SYS:T/cterm.prof", MODE_NEWFILE);
        if (f) {
            struct timeval t1;
            term_now(&t1);
            FPrintf(f, (STRPTR)"real s %ld; ticks: all %ld wait %ld packets %ld vt %ld draw %ld (scroll %ld text %ld cursor %ld knob %ld rows %ld fill %ld) nrows %ld nall %ld nscroll %ld calib1000 %ld draws %ld writes %ld\n",
                    (LONG)(t1.tv_secs - pf_tv0.tv_secs),
                    (ticks() - pf_all0), pf_wait, pf_pkt,
                    pf_vt, pf_draw, pf_scroll, pf_text, pf_cur, pf_knob, pf_rows, pf_fill, pf_nrows, pf_nall, pf_nscroll, pf_calib, pf_draws, pf_writes);
            Close(f);
        }
    }
#endif
    return rc;
}
