/*
 * setwin - the settings window (OS 3.2's ReAction classes, in tabs). A
 * part of its own (PROGDIR:Parts/Settings, with serwin.c): loaded when
 * the window opens, gone when it closes. The tab last shown is CTerm's
 * (set_page), kept for the next time.
 */
#include "cterm.h"

static void open_settings_(void);
static void close_settings_(void);
static void settings_events_(void);

static void set_close(void);
static void set_attr(int id, ULONG tag, ULONG val);
static ULONG get_attr(int id, ULONG tag);
static Object *r_label(const char *t);
static Object *r_chooser(int id, STRPTR *l, int sel);
static Object *r_slider(int id, int min, int max, int lev, const char *fmt);
static Object *r_check(int id, const char *t, BOOL on);
static Object *r_button(int id, const char *t);
static Object *r_string(int id, const char *t, int max, BOOL secret);
static Object *r_drawer(int id, const char *path, const char *title);
static Object *r_group(const char *title);
static Object *r_page(void);
static void r_add(Object *g, Object *child, const char *lab);
static void r_put(Object *page, Object *g);
static Object *r_wrap(Object *p);
static void r_disable(int id, BOOL off);
static void set_sliders(void);
static void colours_changed(void);
static int chars_now(void);
static void set_chars(int k);
static void font_attr(void);
static void letters_changed(void);
static void where_changed(void);
static void picture_changed(void);
static Object *page_files(void);
static Object *page_screen(void);
static Object *page_text(void);
static Object *page_colours(void);
static Object *page_keys(void);
static void klist_free(void);
static void key_shown(void);
static void key_typed(void);
static void keys_changed(void);
static void ktitle(const char *t, UWORD k);
static BOOL keys_clash(void);
static BOOL kgrab, kwarned;             /* waiting for a key; warned once */
static Object *page_modem(void);
static void finish_settings_(int how);
static void finish_settings(int how);
static void finish_settings_(int how);
static int settings_gadget(int id);

/* ======================================================================
 * Settings: one window for the screen, the letters and the colours
 *
 * Letters and colours change at once, as a preview; where CTerm lives
 * (screen, mode, full screen, scroll bar) changes on Use or Save, which
 * open the window, and maybe the screen, again. Cancel puts everything
 * back. Save keeps it after a reboot, Use until then (ENV: only).
 * ==================================================================== */


static Object *setobj, *sg[S_N];        /* the window object, its gadgets */
static Object *setlayout;               /* its top layout */
static struct List settabs;             /* the tabs' names */
static struct TextAttr setfont;         /* what the font button shows */
static int setpart;                     /* 0 the text, 1 the background */
static int pend_mode, pend_depth;
static ULONG pend_id;
static LONG pend_w, pend_h;
static BOOL pend_full, pend_bar;
static ULONG like_id;                   /* "Own screen" with no mode chosen: */
static LONG like_w, like_h;             /* the Workbench's mode, size and */
static int like_depth;                  /* colours (24 for a card's 32) */

static struct {                         /* what Cancel goes back to */
    int k_theme, k_ttsize, k_fontsize, k_linespace, k_bgsee, k_gaps;
    char k_bgfile[256];
    BOOL k_ownfg, k_ownbg, k_usett, k_s_utf8, k_pc, k_pcfont, k_altmeta;
    BOOL k_pics, k_ownprog;
    ULONG k_myfg, k_mybg;
    char k_ttfile[160], k_fontname[48];
    char k_dldir[160], k_uldir[160];
    SerLine k_serdef;
    Modem k_modem;
} snap;

static STRPTR wherelabels[] = { (STRPTR)"Workbench window",
                                (STRPTR)"Own screen", NULL };
static STRPTR letterlabels[] = { (STRPTR)"Amiga font", (STRPTR)"TrueType",
                                 NULL };
static STRPTR charlabels[] = { (STRPTR)"UTF-8", (STRPTR)"Latin-1",
                               (STRPTR)"PC BBS (CP437)", NULL };
static STRPTR partlabels[] = { (STRPTR)"Text", (STRPTR)"Background", NULL };
static STRPTR altlabels[] = { (STRPTR)"Meta for hosts (bash, emacs)",
                              (STRPTR)"Amiga characters everywhere", NULL };
static STRPTR tablabels[] = { (STRPTR)"Screen", (STRPTR)"Text",
                              (STRPTR)"Colours", (STRPTR)"Keys",
                              (STRPTR)"Files", (STRPTR)"Modem", NULL };
static STRPTR dlabels[] = { (STRPTR)"Tone (ATDT)", (STRPTR)"Pulse (ATDP)",
                            (STRPTR)"Own", NULL };
static STRPTR hlabels[] = { (STRPTR)"+++, command, DTR",
                            (STRPTR)"Dropping DTR", NULL };
static char linetext[100];              /* what the Line button shows */
static STRPTR themelabels[NTHEMES + 1];

/* A gadget's attribute, changed in the open window / read. */
static void set_attr(int id, ULONG tag, ULONG val)
{
    /* a nonzero answer: the gadget wants drawing again itself */
    if (setwin && sg[id] &&
        SetGadgetAttrs((struct Gadget *)sg[id], setwin, NULL, tag, val,
                       TAG_END))
        RefreshGList((struct Gadget *)sg[id], setwin, NULL, 1);
}

static ULONG get_attr(int id, ULONG tag)
{
    ULONG v = 0;
    if (sg[id])
        GetAttr(tag, sg[id], &v);
    return v;
}

/* ---- the pieces the pages are made of ---- */

static Object *r_label(const char *t)
{
    return LabelObject, LABEL_Text, (ULONG)t, LabelEnd;
}

static Object *r_chooser(int id, STRPTR *l, int sel)
{
    return sg[id] = ChooserObject, GA_ID, id, GA_RelVerify, TRUE,
        CHOOSER_LabelArray, (ULONG)l, CHOOSER_Selected, sel,
        CHOOSER_DropDown, FALSE, End;
}

static Object *r_slider(int id, int min, int max, int lev, const char *fmt)
{
    return sg[id] = SliderObject, GA_ID, id, GA_RelVerify, TRUE,
        SLIDER_Min, min, SLIDER_Max, max, SLIDER_Level, lev,
        SLIDER_Orientation, SLIDER_HORIZONTAL, SLIDER_LevelFormat, (ULONG)fmt,
        SLIDER_LevelPlace, PLACETEXT_RIGHT, SLIDER_LevelMaxLen, 4, End;
}

static Object *r_check(int id, const char *t, BOOL on)
{
    return sg[id] = CheckBoxObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)t, CHECKBOX_Checked, on, End;
}

static Object *r_button(int id, const char *t)
{
    return sg[id] = ButtonObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)t, End;
}

static Object *r_string(int id, const char *t, int max, BOOL secret)
{
    return sg[id] = StringObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, STRINGA_TextVal, (ULONG)t, STRINGA_MaxChars, max,
        secret ? STRINGA_HookType : TAG_IGNORE, SHK_PASSWORD, End;
}

static Object *r_drawer(int id, const char *path, const char *title)
{
    return sg[id] = GetFileObject, GA_ID, id, GA_RelVerify, TRUE,
        GETFILE_Drawer, (ULONG)path, GETFILE_DrawersOnly, TRUE,
        GETFILE_ReadOnly, TRUE, GETFILE_TitleText, (ULONG)title, End;
}

static Object *r_group(const char *title)
{
    return VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)title, LAYOUT_SpaceOuter, TRUE, End;
}

static Object *r_page(void)
{
    return VLayoutObject, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_VertAlignment, LALIGN_TOP, End;
}

/* A child into a layout, with its label on the left (or none). */
static void r_add(Object *g, Object *child, const char *lab)
{
    if (g && child)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)child,
                 lab ? CHILD_Label : TAG_IGNORE, lab ? (ULONG)r_label(lab) : 0,
                 TAG_END);
}

/* A group onto a page: as tall as it needs, the rest stays empty below. */
static void r_put(Object *page, Object *g)
{
    if (page && g)
        SetAttrs(page, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 TAG_END);
}

/* A page whole: its groups kept together at the top, not spread out
 * over the height the tallest page gives every page. */
static Object *r_wrap(Object *p)
{
    Object *o = VLayoutObject, LAYOUT_VertAlignment, LALIGN_TOP, End;
    if (o && p)
        SetAttrs(o, LAYOUT_AddChild, (ULONG)p, CHILD_WeightedHeight, 0,
                 TAG_END);
    return o;
}

static void r_disable(int id, BOOL off)
{
    if (sg[id])
        SetAttrs(sg[id], GA_Disabled, off, TAG_END);
}

/* ---- keeping the controls and CTerm in step ---- */

static void set_sliders(void)
{
    ULONG c = setpart ? defbg : deffg;
    set_attr(S_R, SLIDER_Level, (c >> 16) & 255);
    set_attr(S_G, SLIDER_Level, (c >> 8) & 255);
    set_attr(S_B, SLIDER_Level, c & 255);
}

static void colours_changed(void)
{
    apply_theme();
    painter_forget();
    redraw_all();
}

/* The characters setting is the default, what a new tab starts in; the
 * shown tab follows it at once unless it has its own (a phone book
 * place's, the file browser's). */
static int defchars;

static int chars_now(void)
{
    return defchars;
}

static void set_chars(int k)
{
    pcmode = k == 2;
    utf8 = k == 0;
    tab_letters(TRUE);
    if (pcmode)
        vt_set_cp437(vt, 1);
    else
        vt_set_utf8(vt, utf8);
    painter_forget();
    redraw_all();
}

static void font_attr(void)
{
    /* none chosen: the default one CTerm falls back on (load_font) */
    struct TextFont *d = GfxBase->DefaultFont;
    BOOL none = !fontname[0] && d;
    setfont.ta_Name = none ? (STRPTR)d->tf_Message.mn_Node.ln_Name
                           : (STRPTR)fontname;
    setfont.ta_YSize = none ? d->tf_YSize : fontsize;
    setfont.ta_Style = 0;
    setfont.ta_Flags = 0;
}

/* The letters again after a font, size or spacing change. */
static void letters_changed(void)
{
    if (usett && !load_tt())
        usett = FALSE;
    if (!usett)
        bitmap_metrics();
    font_changed();
    font_attr();
    set_attr(S_LETTERS, CHOOSER_Selected, usett);
    set_attr(S_TTSIZE, GA_Disabled, !usett);
    set_attr(S_TTSIZE, SLIDER_Level, ttsize);
    set_attr(S_FONT, GETFONT_TextAttr, (ULONG)&setfont);
    set_attr(S_TT, GETFILE_FullFile, (ULONG)ttfile);
}

static void where_changed(void)
{
    set_attr(S_WHERE, CHOOSER_Selected, pend_mode == SCR_OWN);
    set_attr(S_FULL, GA_Disabled, pend_mode == SCR_WB);
    set_attr(S_MODE, GA_Disabled, pend_mode == SCR_WB);
}

/* The picture behind the text, again (a new one, or none). */
static void picture_changed(void)
{
    if (bgfile[0] && !load_picture(bgfile))
        bgfile[0] = 0;
    if (!bgfile[0] && bgsrc) {
        FreeVec(bgsrc);
        bgsrc = NULL;
    }
    backdrop_make();
    redraw_all();
    set_attr(S_PIC, GETFILE_FullFile, (ULONG)bgfile);
}

static Object *page_files(void)
{
    Object *p = r_page(), *g = r_group("Files to and from hosts");
    r_add(g, r_drawer(S_DLDIR, dldir, "Downloads go to"), "Downloads");
    r_add(g, r_drawer(S_ULDIR, uldir, "Uploads come from"), "Uploads");
    r_put(p, g);
    g = r_group("A phone book entry's own drawers come first "
                "(Project/Phone book...)");
    r_put(p, g);
    return r_wrap(p);
}

static Object *r_int(int id, LONG n, LONG max)
{
    return sg[id] = IntegerObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, INTEGER_Number, n, INTEGER_Minimum, 0,
        INTEGER_Maximum, max, INTEGER_Arrows, FALSE, End;
}

static int dial_kind(const char *d)
{
    return !stricmp(d, "ATDT") ? 0 : !stricmp(d, "ATDP") ? 1 : 2;
}

/* The modem every dial-up place uses, and the serial terminal's line. */
static Object *page_modem(void)
{
    Object *h, *p = r_page(),
           *g = r_group("The modem (phone book places with a number)");
    r_add(g, r_string(S_MINIT, modem.init, sizeof modem.init, FALSE),
          "Init string");
    r_add(g, r_string(S_MINIT2, modem.init2, sizeof modem.init2, FALSE),
          "Second init");
    h = HLayoutObject, End;
    r_add(h, r_chooser(S_MDIALK, dlabels, dial_kind(modem.dial)), NULL);
    r_add(h, r_string(S_MDIAL, modem.dial, sizeof modem.dial, FALSE), NULL);
    r_add(h, r_string(S_MSUFFIX, modem.suffix, sizeof modem.suffix, FALSE),
          "number, then");
    r_add(g, h, "Dial");
    h = HLayoutObject, End;
    r_add(h, r_chooser(S_MHANG, hlabels, modem.hangup == SH_DTR), NULL);
    r_add(h, r_string(S_MHANGCMD, modem.hangcmd, sizeof modem.hangcmd,
                      FALSE), "command");
    r_add(g, h, "Hang up by");
    h = HLayoutObject, End;
    r_add(h, r_int(S_MWAIT, modem.wait, 999), NULL);
    r_add(h, r_int(S_MREDIAL, modem.redial, 99), "Redial (times)");
    r_add(h, r_int(S_MPAUSE, modem.pause, 999), "pause (s)");
    r_add(g, h, "Give up after (s)");
    r_add(g, r_check(S_MANSWER, "Answer when it rings (ATA)",
                     modem.answer), NULL);
    r_put(p, g);
    g = r_group("The serial terminal (Project/Serial terminal)");
    ser_line_text(&serdef, linetext, sizeof linetext);
    r_add(g, r_button(S_SERLINE, linetext), "Line");
    r_put(p, g);
    return r_wrap(p);
}

static void close_settings_(void)
{
    struct Node *n;
    if (setobj) {
        if (sg[S_TABS])
            set_page = get_attr(S_TABS, CLICKTAB_Current);
        DisposeObject(setobj);          /* the window and every gadget */
        setobj = NULL;
    }
    setwin = NULL;
    memset(sg, 0, sizeof sg);
    klist_free();
    kgrab = FALSE;
    if (settabs.lh_Head)
        while ((n = RemHead(&settabs)))
            FreeClickTabNode(n);
}

static Object *page_screen(void)
{
    Object *o, *p = r_page(), *g = r_group("Where CTerm opens");
    ULONG id = pend_id;
    struct Screen *wb = LockPubScreen(NULL);
    like_id = INVALID_ID;
    if (wb) {
        struct BitMap *bm = wb->RastPort.BitMap;
        int d = bm->Depth;
        if (CyberGfxBase && GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX))
            d = GetCyberMapAttr(bm, CYBRMATTR_DEPTH);
        like_id = GetVPModeID(&wb->ViewPort);
        like_w = wb->Width;
        like_h = wb->Height;
        like_depth = d > 24 ? 24 : d;
        UnlockPubScreen(NULL, wb);
    }
    if (id == INVALID_ID) {
        id = like_id;
        if (pend_w <= 0) { pend_w = like_w; pend_h = like_h; }
    }
    r_add(g, r_chooser(S_WHERE, wherelabels, pend_mode == SCR_OWN),
          "Screen");
    o = sg[S_MODE] = GetScreenModeObject, GA_ID, S_MODE, GA_RelVerify, TRUE,
        GETSCREENMODE_TitleText, (ULONG)"CTerm's own screen",
        GETSCREENMODE_DisplayID, id,
        GETSCREENMODE_DoWidth, TRUE, GETSCREENMODE_DoHeight, TRUE,
        pend_w > 0 ? GETSCREENMODE_DisplayWidth : TAG_IGNORE, pend_w,
        pend_h > 0 ? GETSCREENMODE_DisplayHeight : TAG_IGNORE, pend_h,
        GETSCREENMODE_DoDepth, TRUE, GETSCREENMODE_MinDepth, 1,
        GETSCREENMODE_MaxDepth, 24,
        GETSCREENMODE_DisplayDepth,
        pend_id == INVALID_ID ? like_depth : pend_depth > 8 ? 24 : pend_depth,
        End;
    /* one line high: left alone its monitor picture makes it two */
    if (o)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)o, CHILD_WeightedHeight, 0,
                 CHILD_MaxHeight, 22, CHILD_Label,
                 (ULONG)r_label("Screen mode"), TAG_END);
    r_add(g, r_check(S_FULL, "Full screen", pend_full), NULL);
    r_disable(S_MODE, pend_mode == SCR_WB);
    r_disable(S_FULL, pend_mode == SCR_WB);
    r_put(p, g);
    g = r_group("The window");
    r_add(g, r_check(S_BAR, "Scroll bar", pend_bar), NULL);
    r_add(g, r_slider(S_GAPS, 0, MAXGAPS, gaps, "%ld"),
          "Gap between panes");
    r_put(p, g);
    return r_wrap(p);
}

static Object *page_text(void)
{
    Object *p = r_page(), *g = r_group("Letters");
    font_attr();
    r_add(g, r_chooser(S_LETTERS, letterlabels, usett), "Letters");
    r_disable(S_LETTERS, !tt_installed());
    r_add(g, sg[S_FONT] = GetFontObject, GA_ID, S_FONT, GA_RelVerify, TRUE,
          GETFONT_TextAttr, (ULONG)&setfont, GETFONT_FixedWidthOnly, TRUE,
          GETFONT_TitleText, (ULONG)"CTerm's font (fixed width)", End,
          "Amiga font");
    r_add(g, sg[S_TT] = GetFileObject, GA_ID, S_TT, GA_RelVerify, TRUE,
          GETFILE_FullFile, (ULONG)ttfile, GETFILE_ReadOnly, TRUE,
          GETFILE_TitleText, (ULONG)"A TrueType font (fixed width)",
          GETFILE_Pattern, (ULONG)"#?.ttf", GETFILE_DoPatterns, TRUE,
          GETFILE_RejectIcons, TRUE, End, "TrueType font");
    r_disable(S_TT, !tt_installed());
    r_add(g, r_slider(S_TTSIZE, 6, 40, ttsize, "%ld"), "TrueType size");
    r_disable(S_TTSIZE, !usett);
    r_add(g, r_slider(S_SPACE, 0, 8, linespace, "%ld"), "Line spacing");
    r_put(p, g);
    g = r_group("Characters");
    r_add(g, r_chooser(S_CHARS, charlabels, chars_now()), "Characters");
    r_add(g, r_check(S_PCFONT, "The IBM VGA font in PC BBS mode", pcfont),
          NULL);
    r_add(g, r_check(S_PICS, "Pictures in the terminal (sixel, iTerm2)",
                     pics), NULL);
    r_add(g, r_check(S_OWNPROG, "Full-screen Amiga programs get their own "
                     "screen", ownprog), NULL);
    r_put(p, g);
    return r_wrap(p);
}

static Object *page_colours(void)
{
    Object *h, *p = r_page(), *g = r_group("Colours");
    ULONG c = setpart ? defbg : deffg;
    r_add(g, r_chooser(S_THEME, themelabels, theme), "Theme");
    r_add(g, r_chooser(S_PART, partlabels, setpart), "Colour of");
    r_add(g, r_slider(S_R, 0, 255, (c >> 16) & 255, "%3ld"), "Red");
    r_add(g, r_slider(S_G, 0, 255, (c >> 8) & 255, "%3ld"), "Green");
    r_add(g, r_slider(S_B, 0, 255, c & 255, "%3ld"), "Blue");
    r_add(g, r_button(S_RESET, "Back to the theme's colours"), NULL);
    r_put(p, g);
    g = r_group(painter ? "A picture behind the text"
                        : "A picture (needs 16 or more bits of colour)");
    h = HLayoutObject, End;
    r_add(h, sg[S_PIC] = GetFileObject, GA_ID, S_PIC, GA_RelVerify, TRUE,
          GETFILE_FullFile, (ULONG)bgfile, GETFILE_ReadOnly, TRUE,
          GETFILE_TitleText, (ULONG)"A picture behind the text",
          GETFILE_RejectIcons, TRUE, End, NULL);
    if (h && r_button(S_NOPIC, "None"))
        SetAttrs(h, LAYOUT_AddChild, (ULONG)sg[S_NOPIC],
                 CHILD_WeightedWidth, 0, TAG_END);
    r_add(g, h, "Picture");
    r_add(g, r_slider(S_SEE, 0, 100, bgsee, "%3ld%%"), "Picture shows");
    r_disable(S_PIC, !painter);
    r_disable(S_NOPIC, !painter);
    r_disable(S_SEE, !painter);
    r_put(p, g);
    return r_wrap(p);
}

/* ---- the keys (CTerm.keys): a list of the actions and their keys ----
 * A row chosen, its keys in the field below: typed (as in the file:
 * <A-t> <F11>), or a key pressed after "Press a key", or none. A key
 * taken from another action of the same section is said in the title;
 * a key that is also another section's is marked ! in the list (the
 * window's keys come first). */

static const char *const kdesc[KA_N] = {
    NULL, "New Shell tab", "Call...", "Phone book...", "Serial terminal",
    "Answer call (ATA)", "Open SFTP browser", "Close tab", "Start log...",
    "Stop log", "Send user name", "Send password", "Run ARexx script...",
    "Hide window", "About", "Speed test", "Quit", "Copy", "Paste",
    "Receive with Ymodem", "Receive with Xmodem...", "Send with Ymodem...",
    "Send with Xmodem...", "Send with Hydra...", "Show transfers...",
    "Split left/right", "Split top/bottom", "Next pane", "Close pane",
    "Settings...", "Amiga font...", "TrueType font...", "Bigger text",
    "Smaller text", "Screen: Workbench window", "Screen: own, 16 colours",
    "Screen: own, 256 colours", "Screen: own, like Workbench",
    "Full screen", "Scroll bar", "UTF-8", "PC BBS (CP437)",
    "Tab 1", "Tab 2", "Tab 3", "Tab 4", "Tab 5", "Tab 6", "Tab 7", "Tab 8",
    "Tab 9", "Next tab", "Previous tab", "History: a page up",
    "History: a page down", "History: a line up", "History: a line down",
    "History: the top", "History: back to the end",
    "Stop a transfer, give up a call", "Call again (a call ended)",
    "A letter left", "A letter right", "To the start", "To the end",
    "A word left", "A word right", "Delete the letter before",
    "Delete the letter under", "Delete the word before",
    "Delete to the start", "Delete to the end", "Clear the line",
    "The line before (history)", "The line after (history)",
    "Find in the history", "Complete a name", "Enter the line",
    "End of input",
    "The line above", "The line below", "A page up", "A page down",
    "The first line", "The last line", "The Amiga's panel (left)",
    "The host's panel (right)", "The other panel", "Into the drawer",
    "The drawer above", "Mark or unmark, then down", "Turn the marks over",
    "These keys (help)", "Read both drawers again",
    "Copy the marked across", "Rename", "A new drawer",
    "Delete the marked", "The messages", "Hang up, close the tab",
    "Stop a transfer, a listing", "The command line", "Draw it all again",
    "A letter left", "A letter right", "To the start", "To the end",
    "Delete the letter before", "Delete the letter under",
    "Delete to the start", "Done", "Nothing done (cancel)",
    "Complete a name or command", "The command line before",
    "The command line after",
    "A new Shell in a new pane", "Close the pane and its tab",
    "Focus the pane to the left", "Focus the pane to the right",
    "Focus the pane above", "Focus the pane below",
    "Move the pane left", "Move the pane right", "Move the pane up",
    "Move the pane down", "New panes side by side",
    "New panes one below the other", "The pane fills the window (again: back)",
    "All panes to tabs", "All tabs to panes",
    "Tab 1 in this pane", "Tab 2 in this pane", "Tab 3 in this pane",
    "Tab 4 in this pane", "Tab 5 in this pane", "Tab 6 in this pane",
    "Tab 7 in this pane", "Tab 8 in this pane", "Tab 9 in this pane",
};
typedef char kdesc_check[sizeof kdesc / sizeof kdesc[0] == KA_N ? 1 : -1];
static const char *const khead[KC_N] = {
    "-- Everywhere (menus, tabs, history) --",
    "-- Calling, transfers --", "-- The Shell's line, typed --",
    "-- File browser: the panels (its own) --",
    "-- File browser: a line typed (its own) --",
    "-- Panes, the i3 way (everywhere) --" };
/* the order the list has them in (the file's) */
static const UBYTE korder[KC_N] = { KC_WINDOW, KC_PANES, KC_CALL, KC_SHELL,
                                    KC_FILES, KC_FLINE };

static struct List klist;               /* the rows */
static struct ColumnInfo kcols[] = {
    { 60, (STRPTR)"What it does", 0 }, { 40, (STRPTR)"Keys", 0 },
    { -1, (STRPTR)~0, -1 }
};
static UBYTE krow[KA_N + KC_N];            /* each row's action (0: a heading) */
static int kcur = -1;                   /* the action shown in the field */
static int knrow;
static struct KeyBind kept[MAXKB];      /* the keys at the start (Cancel) */
static int nkept;
static char ktitlebuf[100];
static struct Hook khook;

static void ktitle(const char *t, UWORD k)
{
    strcpy(ktitlebuf, "CTerm Settings");
    if (t) {
        strcat(ktitlebuf, ": ");
        if (k) {
            keys_name(k, ktitlebuf + strlen(ktitlebuf), TRUE);
            strcat(ktitlebuf, " ");
        }
        strncat(ktitlebuf, t, sizeof ktitlebuf - strlen(ktitlebuf) - 1);
    }
    if (setwin)
        SetWindowTitles(setwin, (UBYTE *)ktitlebuf, (UBYTE *)~0);
}

/* Do actions a and b (two sections) both want key k? The window's keys
 * come first everywhere; the file browser's act in its tab only, where
 * the window's history keys stand aside: a browser key clashes with a
 * window key but those. */
#define KGLOBAL(c) ((c) == KC_WINDOW || (c) == KC_PANES)
static BOOL kclash(int a, int b)
{
    int ca = keys_acts[a].ctx, cb = keys_acts[b].ctx;
    if (ca == cb || (!KGLOBAL(ca) && !KGLOBAL(cb)))
        return FALSE;
    if (!KGLOBAL(ca)) {
        int t = a; a = b; b = t;
        t = ca; ca = cb; cb = t;
    }
    if (KGLOBAL(cb))
        return TRUE;                    /* [window] and [panes] */
    return ca != KC_WINDOW || (cb != KC_FILES && cb != KC_FLINE) ||
           a < KA_PAGEUP || a > KA_HISTEND;
}

/* Does a's key come before b's (where both are wanted)? */
static BOOL kfirst(int a, int b)
{
    int ca = keys_acts[a].ctx, cb = keys_acts[b].ctx;
    return ca == KC_WINDOW || (ca == KC_PANES && cb != KC_WINDOW);
}

/* Action a's key k: another section's too? (the action, or 0) */
static int kother(int a, UWORD k)
{
    int i;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].key == k && keys_binds[i].act != a &&
            kclash(a, keys_binds[i].act))
            return keys_binds[i].act;
    return 0;
}

/* Action a's keys: in words for the list (o: 64 bytes; "!" first when
 * one is also another section's), or as the file has them */
static void ktext(int a, char *o, BOOL plain)
{
    int i;
    char n[40];
    o[0] = 0;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].act == a) {
            keys_name(keys_binds[i].key, n, plain);
            if (strlen(o) + strlen(n) + 4 >= 64)
                break;
            if (o[0])
                strcat(o, plain ? ", " : " ");
            if (plain && kother(a, keys_binds[i].key))
                strcat(o, "! ");
            strcat(o, n);
        }
}

static void klist_free(void)
{
    struct Node *n;
    if (klist.lh_Head)
        while ((n = RemHead(&klist)))
            FreeListBrowserNode(n);
}

static void klist_make(void)
{
    int a, c, o;
    char t[64];
    NewList(&klist);
    knrow = 0;
    for (o = 0; o < KC_N; o++) {
        c = korder[o];
        struct Node *n = AllocListBrowserNode(2, LBNA_Flags, LBFLG_READONLY,
            LBNA_Column, 0, LBNCA_Text, (ULONG)khead[c - 1],
            LBNA_Column, 1, LBNCA_Text, (ULONG)"", TAG_END);
        if (n)
            AddTail(&klist, n), krow[knrow++] = 0;
        for (a = 1; a < KA_N; a++) {
            if (keys_acts[a].ctx != c)
                continue;
            ktext(a, t, TRUE);
            n = AllocListBrowserNode(2, LBNA_Column, 0, LBNCA_Text,
                (ULONG)kdesc[a], LBNA_Column, 1, LBNCA_CopyText, TRUE,
                LBNCA_Text, (ULONG)t, TAG_END);
            if (n)
                AddTail(&klist, n), krow[knrow++] = a;
        }
    }
}

/* The field: the chosen action's keys, as the file writes them */
static void key_shown(void)
{
    LONG r = get_attr(S_KLIST, LISTBROWSER_Selected);
    char t[64];
    kcur = r >= 0 && r < knrow ? krow[r] : 0;
    ktext(kcur, t, FALSE);
    set_attr(S_KTEXT, STRINGA_TextVal, (ULONG)(kcur ? t : ""));
    ktitle(kcur ? keys_acts[kcur].name : NULL, 0);
}

/* The list again, after a change: the same row chosen */
static void keys_changed(void)
{
    LONG r = get_attr(S_KLIST, LISTBROWSER_Selected);
    LONG top = get_attr(S_KLIST, LISTBROWSER_Top);
    set_attr(S_KLIST, LISTBROWSER_Labels, ~0UL);
    klist_free();
    klist_make();
    set_attr(S_KLIST, LISTBROWSER_Labels, (ULONG)&klist);
    set_attr(S_KLIST, LISTBROWSER_Selected, (ULONG)r);
    set_attr(S_KLIST, LISTBROWSER_Top, (ULONG)top);
    if (kcur > 0) {
        char t[64];
        ktext(kcur, t, FALSE);
        set_attr(S_KTEXT, STRINGA_TextVal, (ULONG)t);
    }
    kwarned = FALSE;
}

/* k given to action a: the action that had it in a's section, or
 * another section's that has it too, is said in the title */
static void key_give(int a, UWORD k)
{
    int had = keys_find(keys_acts[a].ctx, k), o = 0;
    keys_bind(0, k, a);
    if (had && had != a)
        ktitle("taken from ", k);
    else if ((o = kother(a, k)))
        ktitle(kfirst(a, o) ? "comes first, before " : "is first ", k);
    if ((had && had != a) || o) {
        strncat(ktitlebuf, kdesc[o ? o : had],
                sizeof ktitlebuf - strlen(ktitlebuf) - 1);
        SetWindowTitles(setwin, (UBYTE *)ktitlebuf, (UBYTE *)~0);
    }
}

/* Keys typed in the field: all of them, or nothing changes */
static void key_typed(void)
{
    const char *t = (const char *)get_attr(S_KTEXT, STRINGA_TextVal);
    UWORD ks[8];
    int n = 0, l;
    if (kcur <= 0 || !t)
        return;
    while (*t) {
        while (*t == ' ' || *t == ',')
            t++;
        for (l = 0; t[l] && t[l] != ' ' && !(t[l] == ',' && l); l++)
            ;
        if (!l)
            break;
        if (n == 8 || !(ks[n++] = keys_parse(t, l))) {
            beep();
            ktitle("not a key (as in the file: <A-t> <C-r> <F5> <S-Up>)",
                   0);
            return;
        }
        t += l;
    }
    keys_unbind(kcur);
    ktitle(keys_acts[kcur].name, 0);
    for (l = 0; l < n; l++)
        key_give(kcur, ks[l]);
    keys_changed();
}

/* The window's raw keys, while a key is waited for: kept for
 * settings_events (the window's work is not done here) */
static UWORD kgot;
static ULONG key_hook(register struct Hook *h __asm("a0"),
                      register APTR o __asm("a2"),
                      register struct IntuiMessage *m __asm("a1"))
{
    (void)h; (void)o;
    if (kgrab && m->Class == IDCMP_RAWKEY && !kgot)
        kgot = keys_code(m->Code, m->Qualifier,
                         m->IAddress ? *((APTR *)m->IAddress) : NULL);
    return 0;                           /* 0: a qualifier, a release */
}

/* The key pressed for the chosen action */
static void key_pressed(void)
{
    UWORD k = kgot;
    kgot = 0;
    if (!kgrab || kcur <= 0)
        return;
    kgrab = FALSE;
    keys_unbind(kcur);
    ktitle(keys_acts[kcur].name, 0);
    key_give(kcur, k);
    keys_changed();
}

/* Use or Save with a key two sections share: said once (marked ! in the
 * list); pressed again, done */
static BOOL keys_clash(void)
{
    int i, o;
    for (i = 0; i < keys_n && !kwarned; i++)
        if ((o = kother(keys_binds[i].act, keys_binds[i].key))) {
            kwarned = TRUE;
            beep();
            set_attr(S_TABS, CLICKTAB_Current, 3);
            ktitle("is two actions' (marked !) - again to keep it",
                   keys_binds[i].key);
            return TRUE;
        }
    return FALSE;
}

static Object *page_keys(void)
{
    Object *h, *p = r_page(),
           *g = r_group("CTerm's keys (CTerm.keys; the file browser's too)");
    r_add(g, r_chooser(S_ALT, altlabels, !altmeta), "Alt key");
    klist_make();
    kcur = -1;
    sg[S_KLIST] = ListBrowserObject, GA_ID, S_KLIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&klist, LISTBROWSER_ColumnInfo,
        (ULONG)kcols, LISTBROWSER_ColumnTitles, TRUE,
        LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_AutoFit, TRUE, End;
    if (g && sg[S_KLIST])
        SetAttrs(g, LAYOUT_AddChild, (ULONG)sg[S_KLIST],
                 CHILD_MinHeight, 5 * (scr->RastPort.TxHeight + 2) + 4,
                 TAG_END);
    h = HLayoutObject, End;
    r_add(h, r_string(S_KTEXT, "", 64, FALSE), "Keys");
    if (h && r_button(S_KPRESS, "Press a key") && r_button(S_KNONE, "None")
        && r_button(S_KDEF, "Defaults"))
        SetAttrs(h, LAYOUT_AddChild, (ULONG)sg[S_KPRESS],
                 CHILD_WeightedWidth, 0, LAYOUT_AddChild, (ULONG)sg[S_KNONE],
                 CHILD_WeightedWidth, 0, LAYOUT_AddChild, (ULONG)sg[S_KDEF],
                 CHILD_WeightedWidth, 0, TAG_END);
    if (g && h)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (p && g)                         /* the list takes the page's height */
        SetAttrs(p, LAYOUT_AddChild, (ULONG)g, TAG_END);
    return p;
}

static void open_settings_(void)
{
    Object *pages, *root, *buttons;
    int i;
    if (setobj) {
        WindowToFront(setwin);
        ActivateWindow(setwin);
        return;
    }
    if (!reaction_open()) {
        say("\r\n[the settings window needs AmigaOS 3.2's window classes, and\r\n the memory to load them: the Settings menu works without]\r\n");
        return;
    }
    for (i = 0; i < NTHEMES; i++)
        themelabels[i] = (STRPTR)themes[i].name;
    themelabels[NTHEMES] = NULL;

    tab_letters(FALSE);                     /* the controls show the person's */
    snap.k_pcfont = pcfont;
    snap.k_pics = pics;
    snap.k_ownprog = ownprog;
    snap.k_altmeta = altmeta;
    snap.k_theme = theme;
    snap.k_ownfg = ownfg; snap.k_ownbg = ownbg;
    snap.k_myfg = myfg; snap.k_mybg = mybg;
    snap.k_usett = usett; snap.k_ttsize = ttsize;
    strcpy(snap.k_ttfile, ttfile);
    strcpy(snap.k_fontname, fontname);
    snap.k_fontsize = fontsize;
    snap.k_linespace = linespace;
    snap.k_gaps = gaps;
    snap.k_s_utf8 = utf8; snap.k_pc = pcmode;
    defchars = saved_pc ? 2 : saved_utf8 ? 0 : 1;
    snap.k_bgsee = bgsee;
    strcpy(snap.k_bgfile, bgfile);
    CopyMem(keys_binds, kept, sizeof kept);   /* the keys, for Cancel */
    nkept = keys_n;
    kgrab = kwarned = FALSE;
    strcpy(snap.k_dldir, dldir);
    strcpy(snap.k_uldir, uldir);
    snap.k_serdef = serdef;
    snap.k_modem = modem;
    pend_mode = scrmode;
    pend_depth = owndepth;
    pend_id = ownmode;
    pend_w = ownw;
    pend_h = ownh;
    pend_full = fullscreen;
    pend_bar = scrollbar;

    khook.h_Entry = (ULONG (*)())key_hook;
    NewList(&settabs);
    for (i = 0; tablabels[i]; i++) {
        struct Node *n = AllocClickTabNode(TNA_Text, (ULONG)tablabels[i],
                                           TNA_Number, i, TAG_END);
        if (n)
            AddTail(&settabs, n);
    }
    pages = PageObject, PAGE_Add, (ULONG)page_screen(),
        PAGE_Add, (ULONG)page_text(), PAGE_Add, (ULONG)page_colours(),
        PAGE_Add, (ULONG)page_keys(), PAGE_Add, (ULONG)page_files(),
        PAGE_Add, (ULONG)page_modem(), PAGE_Current, set_page, End;
    buttons = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    r_add(buttons, r_button(S_SAVE, "_Save"), NULL);
    r_add(buttons, r_button(S_USE, "_Use"), NULL);
    r_add(buttons, r_button(S_CANCEL, "_Cancel"), NULL);
    root = setlayout = VLayoutObject, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE, End;
    sg[S_TABS] = ClickTabObject, GA_ID, S_TABS, CLICKTAB_Labels,
        (ULONG)&settabs, CLICKTAB_Current, set_page,
        CLICKTAB_PageGroup, (ULONG)pages, End;
    if (root && sg[S_TABS]) {
        /* wide enough for every tab's name (the pages don't all say
         * how wide they need) */
        int w = 40;
        for (i = 0; tablabels[i]; i++)
            w += TextLength(&scr->RastPort, tablabels[i],
                            strlen((char *)tablabels[i])) + 24;
        if (w < 32 * scr->RastPort.TxWidth + 300)
            w = 32 * scr->RastPort.TxWidth + 300;
        if (w > scr->Width - 24)
            w = scr->Width - 24;
        SetAttrs(root, LAYOUT_AddChild, (ULONG)sg[S_TABS],
                 CHILD_MinWidth, w, TAG_END);
    }
    if (root && buttons)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)buttons,
                 CHILD_WeightedHeight, 0, TAG_END);
    if (!root || !sg[S_TABS] || !buttons) {
        /* what was not given to root goes by hand (the tabs own pages) */
        if (!sg[S_TABS] && pages)
            DisposeObject(pages);
        if (!root && sg[S_TABS])
            DisposeObject(sg[S_TABS]);
        if (!root && buttons)
            DisposeObject(buttons);
        if (root)
            DisposeObject(root);
        memset(sg, 0, sizeof sg);
        set_close();
        return;
    }
    setobj = WindowObject, WA_Title, (ULONG)"CTerm Settings",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_Layout, (ULONG)root,
        WINDOW_IDCMPHook, (ULONG)&khook, WINDOW_IDCMPHookBits, IDCMP_RAWKEY,
        End;
    if (!setobj) {
        DisposeObject(root);
        memset(sg, 0, sizeof sg);
        set_close();
        return;
    }
    setwin = (struct Window *)DoMethod(setobj, WM_OPEN);
    if (!setwin)
        set_close();
    else if (keys_bad[0])
        ktitle(keys_bad, 0);            /* CTerm.keys: a line not taken */
}

/* Use (1), Save (2) or Cancel (3). */
static void finish_settings(int how)
{
    finish_settings_(how);
    tab_letters(TRUE);                    /* PC BBS mode wears the PC's */
}

static void finish_settings_(int how)
{
    const char *t;
    BOOL go = TRUE;
    if (how != 3) {
        if ((t = (const char *)get_attr(S_DLDIR, GETFILE_Drawer))) {
            strncpy(dldir, t, sizeof dldir - 1);
            dldir[sizeof dldir - 1] = 0;
        }
        if ((t = (const char *)get_attr(S_ULDIR, GETFILE_Drawer))) {
            strncpy(uldir, t, sizeof uldir - 1);
            uldir[sizeof uldir - 1] = 0;
        }
#define MTXT(id, f) if ((t = (const char *)get_attr(id, STRINGA_TextVal))) { \
        strncpy(f, t, sizeof f - 1); f[sizeof f - 1] = 0; }
        MTXT(S_MINIT, modem.init);
        MTXT(S_MINIT2, modem.init2);
        MTXT(S_MDIAL, modem.dial);
        MTXT(S_MSUFFIX, modem.suffix);
        MTXT(S_MHANGCMD, modem.hangcmd);
#undef MTXT
        modem.hangup = get_attr(S_MHANG, CHOOSER_Selected) ? SH_DTR : SH_BOTH;
        modem.wait = get_attr(S_MWAIT, INTEGER_Number);
        if (modem.wait < 5)
            modem.wait = 5;
        modem.redial = get_attr(S_MREDIAL, INTEGER_Number);
        modem.pause = get_attr(S_MPAUSE, INTEGER_Number);
        modem.answer = get_attr(S_MANSWER, GA_Selected) != 0;
    }
    set_close();
    if (how == 3) {
        BOOL letters = snap.k_usett != usett || snap.k_ttsize != ttsize ||
                       strcmp(snap.k_ttfile, ttfile) ||
                       strcmp(snap.k_fontname, fontname) ||
                       snap.k_fontsize != fontsize ||
                       snap.k_linespace != linespace;
        theme = snap.k_theme;
        if (gaps != snap.k_gaps) {
            gaps = snap.k_gaps;
            panes_changed();
        }
        CopyMem(kept, keys_binds, sizeof kept);
        keys_n = nkept;
        serdef = snap.k_serdef;
        modem = snap.k_modem;
        pcfont = snap.k_pcfont;
        if (pics != snap.k_pics) {
            pics = snap.k_pics;
            pics_all();
        }
        altmeta = snap.k_altmeta;
        ownprog = snap.k_ownprog;
        ownfg = snap.k_ownfg; ownbg = snap.k_ownbg;
        myfg = snap.k_myfg; mybg = snap.k_mybg;
        if (snap.k_bgsee != bgsee || strcmp(snap.k_bgfile, bgfile)) {
            BOOL other = strcmp(snap.k_bgfile, bgfile) != 0;
            bgsee = snap.k_bgsee;
            strcpy(bgfile, snap.k_bgfile);
            if (other)
                picture_changed();      /* colours_changed draws it */
        }
        if (snap.k_s_utf8 != utf8 || snap.k_pc != pcmode)
            set_chars(snap.k_pc ? 2 : snap.k_s_utf8 ? 0 : 1);
        if (letters) {
            usett = snap.k_usett;
            ttsize = snap.k_ttsize;
            strcpy(ttfile, snap.k_ttfile);
            strcpy(fontname, snap.k_fontname);
            fontsize = snap.k_fontsize;
            linespace = snap.k_linespace;
            load_font();
            letters_changed();
        }
        colours_changed();
        return;
    }
    saved_utf8 = defchars == 0;         /* the default: new tabs' */
    saved_pc = defchars == 2;
    if (pend_mode != scrmode || pend_full != fullscreen ||
        pend_bar != scrollbar ||
        (pend_mode == SCR_OWN && (pend_id != ownmode ||
         pend_depth != owndepth || pend_w != ownw || pend_h != ownh))) {
        if (pend_mode == SCR_OWN && scrmode == SCR_OWN &&
            (pend_id != ownmode || pend_depth != owndepth ||
             pend_w != ownw || pend_h != ownh)) {
            /* a different own screen: leave this one first */
            want_mode = SCR_WB;
            want_full = FALSE;
            want_depth = owndepth;
            want_bar = scrollbar;
            go = display_switch();      /* refused while Ed or More runs */
        }
        if (go) {
            ownmode = pend_id;
            ownw = pend_w;
            ownh = pend_h;
            want_mode = pend_mode;
            want_depth = pend_depth;
            want_full = pend_mode == SCR_OWN && pend_full;
            want_bar = pend_bar;
            want_switch = TRUE;         /* the main loop does it */
        }
    }
    used_only = how == 1;               /* Use: until a reboot, even on quit */
    envonly = used_only;
    save_prefs();
    envonly = FALSE;
    if (how == 2 && !keys_save())       /* the keys: ENVARC: (Use: kept */
        beep();                         /* until CTerm quits) */
    remake_menus();                     /* the keys */
}

/* One control was used: 1 Use, 2 Save, 3 Cancel, 0 stay open. */
static int settings_gadget(int id)
{
    ULONG v;
    switch (id) {
    case S_WHERE:
        pend_mode = get_attr(S_WHERE, CHOOSER_Selected) ? SCR_OWN : SCR_WB;
        if (pend_mode == SCR_OWN && ownmode == INVALID_ID &&
            pend_id == INVALID_ID && like_id != INVALID_ID) {
            pend_id = like_id;          /* what the mode box shows */
            pend_w = like_w;
            pend_h = like_h;
            pend_depth = like_depth;
        }
        where_changed();
        break;
    case S_MODE:
        if (DoMethod(sg[S_MODE], GSM_REQUEST, (ULONG)setwin)) {
            pend_id = get_attr(S_MODE, GETSCREENMODE_DisplayID);
            pend_w = get_attr(S_MODE, GETSCREENMODE_DisplayWidth);
            pend_h = get_attr(S_MODE, GETSCREENMODE_DisplayHeight);
            pend_depth = get_attr(S_MODE, GETSCREENMODE_DisplayDepth);
            pend_mode = SCR_OWN;
            where_changed();
        }
        break;
    case S_FULL: pend_full = get_attr(S_FULL, GA_Selected) != 0; break;
    case S_BAR: pend_bar = get_attr(S_BAR, GA_Selected) != 0; break;
    case S_LETTERS:
        usett = get_attr(S_LETTERS, CHOOSER_Selected) && tt_installed();
        if (!usett)
            load_font();
        letters_changed();
        break;
    case S_FONT:
        if (DoMethod(sg[S_FONT], GFONT_REQUEST, (ULONG)setwin)) {
            struct TextAttr *ta =
                (struct TextAttr *)get_attr(S_FONT, GETFONT_TextAttr);
            if (ta && ta->ta_Name) {
                strncpy(fontname, (char *)ta->ta_Name, sizeof fontname - 1);
                fontname[sizeof fontname - 1] = 0;
                fontsize = ta->ta_YSize;
                if (load_font())
                    usett = FALSE;
            }
            letters_changed();
        }
        break;
    case S_TT:
        if (DoMethod(sg[S_TT], GFILE_REQUEST, (ULONG)setwin)) {
            const char *f = (const char *)get_attr(S_TT, GETFILE_FullFile);
            char old[160];
            strcpy(old, ttfile);
            if (f && f[0]) {
                strncpy(ttfile, f, sizeof ttfile - 1);
                ttfile[sizeof ttfile - 1] = 0;
                usett = TRUE;
                if (!load_tt()) {
                    beep();   /* it would not open */
                    strcpy(ttfile, old);
                    usett = load_tt();
                    if (!usett)
                        load_font();
                }
            }
            letters_changed();
        }
        break;
    case S_TTSIZE:
        v = get_attr(S_TTSIZE, SLIDER_Level);
        if (usett && (int)v != ttsize) {
            ttsize = v;
            letters_changed();
        }
        break;
    case S_GAPS:                        /* seen at once, as the letters */
        v = get_attr(S_GAPS, SLIDER_Level);
        if ((int)v != gaps) {
            gaps = v;
            panes_changed();
        }
        break;
    case S_SPACE:
        v = get_attr(S_SPACE, SLIDER_Level);
        if ((int)v != linespace) {
            linespace = v;
            letters_changed();
        }
        break;
    case S_CHARS:
        defchars = get_attr(S_CHARS, CHOOSER_Selected);
        if (!T->t_bookname[0] && !T->t_fui) {
            set_chars(defchars);
            tab_letters(FALSE);             /* shown on closing */
        }
        break;
    case S_PCFONT: pcfont = get_attr(S_PCFONT, GA_Selected) != 0; break;
    case S_PICS:
        pics = get_attr(S_PICS, GA_Selected) != 0;
        pics_all();                     /* at once: DA1 says so, or not */
        break;
    case S_OWNPROG: ownprog = get_attr(S_OWNPROG, GA_Selected) != 0; break;
    case S_ALT: altmeta = get_attr(S_ALT, CHOOSER_Selected) == 0; break;
    case S_THEME:
        theme = get_attr(S_THEME, CHOOSER_Selected);
        ownfg = ownbg = FALSE;
        colours_changed();
        set_sliders();
        break;
    case S_PART:
        setpart = get_attr(S_PART, CHOOSER_Selected);
        set_sliders();
        break;
    case S_R: case S_G: case S_B: {
        int sh = id == S_R ? 16 : id == S_G ? 8 : 0;
        ULONG c = setpart ? defbg : deffg;
        v = get_attr(id, SLIDER_Level);
        c = (c & ~(255UL << sh)) | ((v & 255) << sh);
        if (setpart) { mybg = c; ownbg = TRUE; }
        else { myfg = c; ownfg = TRUE; }
        colours_changed();
        break;
    }
    case S_RESET:
        ownfg = ownbg = FALSE;
        colours_changed();
        set_sliders();
        break;
    case S_PIC:
        if (!bgfile[0])                 /* start where the backdrops are */
            SetAttrs(sg[S_PIC], GETFILE_Drawer,
                     (ULONG)"SYS:Prefs/Presets/Backdrops", TAG_END);
        if (DoMethod(sg[S_PIC], GFILE_REQUEST, (ULONG)setwin)) {
            const char *f = (const char *)get_attr(S_PIC, GETFILE_FullFile);
            if (f && f[0]) {
                strncpy(bgfile, f, sizeof bgfile - 1);
                bgfile[sizeof bgfile - 1] = 0;
                picture_changed();
                if (!bgfile[0])
                    beep();   /* not a picture it could read */
            }
        }
        break;
    case S_NOPIC:
        bgfile[0] = 0;
        picture_changed();
        break;
    case S_SEE:
        v = get_attr(S_SEE, SLIDER_Level);
        if ((int)v != bgsee) {
            bgsee = v;
            backdrop_make();
            redraw_all();
        }
        break;
    case S_DLDIR: case S_ULDIR:
        DoMethod(sg[id], GFILE_REQUEST, (ULONG)setwin);
        break;
    case S_MDIALK:
        v = get_attr(S_MDIALK, CHOOSER_Selected);
        if (v < 2)
            set_attr(S_MDIAL, STRINGA_TextVal, (ULONG)(v ? "ATDP" : "ATDT"));
        break;
    case S_MDIAL: {
        const char *t = (const char *)get_attr(S_MDIAL, STRINGA_TextVal);
        set_attr(S_MDIALK, CHOOSER_Selected, dial_kind(t ? t : ""));
        break;
    }
    case S_SERLINE:
        if (serwin_edit(&serdef, NULL, NULL, setwin)) {
            ser_line_text(&serdef, linetext, sizeof linetext);
            set_attr(S_SERLINE, GA_Text, (ULONG)linetext);
        }
        break;
    case S_KLIST: kgrab = FALSE; key_shown(); break;
    case S_KTEXT: key_typed(); break;
    case S_KPRESS:
        if (kcur > 0) {
            kgrab = !kgrab;
            ktitle(kgrab ? "press the new key (this button again: no change)"
                         : NULL, 0);
        }
        break;
    case S_KNONE:
        if (kcur > 0) {
            keys_unbind(kcur);
            keys_changed();
        }
        break;
    case S_KDEF:
        keys_defaults();
        keys_changed();
        ktitle("the keys CTerm comes with", 0);
        break;
    case S_SAVE: return keys_clash() ? 0 : 2;
    case S_USE: return keys_clash() ? 0 : 1;
    case S_CANCEL: return 3;
    }
    return 0;
}

static void settings_events_(void)
{
    ULONG res;
    UWORD code;
    int how = 0;
    while (setobj && !how &&
           (res = DoMethod(setobj, WM_HANDLEINPUT, (ULONG)&code)) !=
           WMHI_LASTMSG) {
        switch (res & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            how = 3;
            break;
        case WMHI_GADGETUP:
            if ((kgrab || kgot) && (res & WMHI_GADGETMASK) != S_KPRESS)
                break;                  /* a key pressed for the list */
            how = settings_gadget(res & WMHI_GADGETMASK);
            break;
        }
        if (kgot)
            key_pressed();
    }
    if (how)
        finish_settings(how);
}

/* The settings window's work on a deep stack: OS 3.2's classes want one,
 * and CTerm's own stays small for a stock A1200 (see deep_call). */
static void set_open(void) { deep_call(open_settings_); }
static void set_close(void) { deep_call(close_settings_); }
static void set_events(void) { deep_call(settings_events_); }

/* CTerm ends with the window open: as its close gadget, Cancel - so the
 * prefs are saved (save_prefs waits while it is open) as they were. */
static void settings_cancel_(void)
{
    if (setobj)
        finish_settings(3);
}
static void set_cancel(void) { deep_call(settings_cancel_); }

APTR part_main(void)
{
    static struct SetPart p = { set_open, set_close,
                                set_events, set_cancel };
    return &p;
}
