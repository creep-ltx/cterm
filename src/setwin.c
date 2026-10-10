/*
 * setwin - the settings window (OS 3.2's ReAction classes, in tabs).
 */
#include "cterm.h"

static void open_settings_(void);
static void close_settings_(void);
static void settings_events_(void);

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
static void booklist_free(void);
static void booklist_make(void);
static void booklist_again(void);
static void book_read(void);
static void book_show(void);
static Object *page_book(void);
static Object *page_files(void);
static Object *page_screen(void);
static Object *page_text(void);
static Object *page_colours(void);
static Object *page_keys(void);
static void finish_settings_(int how);
static void finish_settings(int how);
static void finish_settings_(int how);
static int settings_gadget(int id);

__attribute__((noinline)) __stdargs APTR RNewObject(struct IClass *c,
                                                    CONST_STRPTR id,
                                                    ULONG tag1, ...)
{
    return NewObjectA(c, id, (struct TagItem *)&tag1);
}
__attribute__((noinline)) __stdargs ULONG RSetAttrs(APTR o, ULONG tag1, ...)
{
    return SetAttrsA(o, (struct TagItem *)&tag1);
}
/* ======================================================================
 * Settings: one window for the screen, the letters and the colours
 *
 * Letters and colours change at once, as a preview; where CTerm lives
 * (screen, mode, full screen, scroll bar) changes on Use or Save, which
 * open the window, and maybe the screen, again. Cancel puts everything
 * back. Save keeps it after a reboot, Use until then (ENV: only).
 * ==================================================================== */


/* OS 3.2's window classes: opened the first time the window opens. Given
 * a value, so libnix does not try to open them itself (by wrong names). */
struct Library *WindowBase = NULL, *LayoutBase = NULL, *ClickTabBase = NULL,
    *ChooserBase = NULL, *CheckBoxBase = NULL, *SliderBase = NULL,
    *ButtonBase = NULL, *GetFileBase = NULL, *GetFontBase = NULL,
    *GetScreenModeBase = NULL, *LabelBase = NULL, *ListBrowserBase = NULL,
    *StringBase = NULL, *IntegerBase = NULL;

Object *setobj, *sg[S_N];        /* the window object, its gadgets */
static Object *setlayout;               /* its top layout */
static struct List settabs;             /* the tabs' names */
static struct TextAttr setfont;         /* what the font button shows */
static int setpart;                     /* 0 the text, 1 the background */
static int setpage;                     /* the tab last shown */
static struct List booklist;            /* the phone book's names */
static int bcur = -1;                   /* the entry shown: -1 none */
static BookEntry *booksnap;             /* the book as it was: Cancel */
static int nbooksnap;
static BOOL booksnapped;                /* booksnap/nbooksnap are taken */
static int pend_mode, pend_depth;
static ULONG pend_id;
static LONG pend_w, pend_h;
static BOOL pend_full, pend_bar;
static ULONG like_id;                   /* "Own screen" with no mode chosen: */
static LONG like_w, like_h;             /* the Workbench's mode, size and */
static int like_depth;                  /* colours (24 for a card's 32) */

static struct {                         /* what Cancel goes back to */
    int theme, ttsize, fontsize, linespace, bgsee;
    char bgfile[256];
    BOOL ownfg, ownbg, usett, s_utf8, pc, pcfont, altmeta;
    ULONG myfg, mybg;
    char ttfile[160], fontname[48];
    char userkey[2], passkey[2], dldir[160], uldir[160];
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
                              (STRPTR)"Phone book", (STRPTR)"Files", NULL };
static STRPTR kindlabels[] = { (STRPTR)"telnet", (STRPTR)"ssh", NULL };
static STRPTR booklettlabels[] = { (STRPTR)"CTerm's setting",
                                   (STRPTR)"Amiga font", (STRPTR)"TrueType",
                                   (STRPTR)"PC font (IBM VGA)", NULL };
static STRPTR themelabels[NTHEMES + 1];

BOOL reaction_open(void)
{
    static const char *const name[] = {
        "window.class", "gadgets/layout.gadget", "gadgets/clicktab.gadget",
        "gadgets/chooser.gadget", "gadgets/checkbox.gadget",
        "gadgets/slider.gadget", "gadgets/button.gadget",
        "gadgets/getfile.gadget", "gadgets/getfont.gadget",
        "gadgets/getscreenmode.gadget", "images/label.image",
        "gadgets/listbrowser.gadget", "gadgets/string.gadget",
        "gadgets/integer.gadget" };
    struct Library **base[] = { &WindowBase, &LayoutBase, &ClickTabBase,
        &ChooserBase, &CheckBoxBase, &SliderBase, &ButtonBase, &GetFileBase,
        &GetFontBase, &GetScreenModeBase, &LabelBase, &ListBrowserBase,
        &StringBase, &IntegerBase };
    int i;
    for (i = 0; i < 14; i++)
        if (!*base[i] && !(*base[i] = OpenLibrary((STRPTR)name[i], 45)))
            return FALSE;
    return TRUE;
}

void reaction_close(void)
{
    struct Library **base[] = { &WindowBase, &LayoutBase, &ClickTabBase,
        &ChooserBase, &CheckBoxBase, &SliderBase, &ButtonBase, &GetFileBase,
        &GetFontBase, &GetScreenModeBase, &LabelBase, &ListBrowserBase,
        &StringBase, &IntegerBase };
    int i;
    for (i = 0; i < 14; i++)
        if (*base[i]) {
            CloseLibrary(*base[i]);
            *base[i] = NULL;
        }
}

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

static int chars_now(void)
{
    return pcmode ? 2 : utf8 ? 0 : 1;
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

/* ---- the phone book page ---- */

static void booklist_free(void)
{
    struct Node *n;
    if (booklist.lh_Head)
        while ((n = RemHead(&booklist)))
            FreeListBrowserNode(n);
}

static void booklist_make(void)
{
    int i;
    NewList(&booklist);
    for (i = 0; i < nbook; i++) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(book[i].name[0] ? book[i].name : "?"),
            TAG_END);
        if (n)
            AddTail(&booklist, n);
    }
}

/* The list again (a name changed, an entry came or went). */
static void booklist_again(void)
{
    set_attr(S_BLIST, LISTBROWSER_Labels, ~0UL);   /* let go of it */
    booklist_free();
    booklist_make();
    set_attr(S_BLIST, LISTBROWSER_Labels, (ULONG)&booklist);
    set_attr(S_BLIST, LISTBROWSER_Selected, (ULONG)bcur);
}

/* What is typed in the fields, into the entry shown. */
static void book_read(void)
{
    BookEntry *e;
    const char *t;
    if (bcur < 0 || bcur >= nbook || !setobj)
        return;
    e = &book[bcur];
#define TXT(id, f) if ((t = (const char *)get_attr(id, STRINGA_TextVal))) { \
        strncpy(f, t, sizeof f - 1); f[sizeof f - 1] = 0; }
    TXT(S_BNAME, e->name);
    TXT(S_BHOST, e->host);
    TXT(S_BUSER, e->user);
    TXT(S_BPASS, e->pass);
#undef TXT
    if ((t = (const char *)get_attr(S_BDOWN, GETFILE_Drawer))) {
        strncpy(e->down, t, sizeof e->down - 1);
        e->down[sizeof e->down - 1] = 0;
    }
    if ((t = (const char *)get_attr(S_BUP, GETFILE_Drawer))) {
        strncpy(e->up, t, sizeof e->up - 1);
        e->up[sizeof e->up - 1] = 0;
    }
    e->port = get_attr(S_BPORT, INTEGER_Number);
    e->ssh = get_attr(S_BKIND, CHOOSER_Selected);
    e->letters = get_attr(S_BLET, CHOOSER_Selected);
    e->chars = get_attr(S_BCHR, CHOOSER_Selected);
    book_tidy(e);
}

/* The entry shown, into the fields (none: empty and asleep). */
static void book_show(void)
{
    static BookEntry none;
    const BookEntry *e = bcur >= 0 && bcur < nbook ? &book[bcur] : &none;
    static const int ids[] = { S_BNAME, S_BHOST, S_BPORT, S_BKIND, S_BUSER,
        S_BPASS, S_BLET, S_BCHR, S_BDOWN, S_BUP, S_BDEL };
    int i;
    set_attr(S_BNAME, STRINGA_TextVal, (ULONG)e->name);
    set_attr(S_BHOST, STRINGA_TextVal, (ULONG)e->host);
    set_attr(S_BPORT, INTEGER_Number, e->port);
    set_attr(S_BKIND, CHOOSER_Selected, e->ssh);
    set_attr(S_BUSER, STRINGA_TextVal, (ULONG)e->user);
    set_attr(S_BPASS, STRINGA_TextVal, (ULONG)e->pass);
    set_attr(S_BLET, CHOOSER_Selected, e->letters);
    set_attr(S_BCHR, CHOOSER_Selected, e->chars);
    set_attr(S_BDOWN, GETFILE_Drawer, (ULONG)e->down);
    set_attr(S_BUP, GETFILE_Drawer, (ULONG)e->up);
    for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
        set_attr(ids[i], GA_Disabled, e == &none);
}

static Object *page_book(void)
{
    Object *h, *o, *top, *c1, *c2, *p = r_page(), *g;
    const BookEntry *e = bcur >= 0 && bcur < nbook ? &book[bcur] : NULL;
    static BookEntry none;
    if (!e)
        e = &none;
    /* the list on the left, the entry on the right: short enough for a
     * 256-line screen */
    top = HLayoutObject, End;
    g = r_group("Places");
    booklist_make();
    o = sg[S_BLIST] = ListBrowserObject, GA_ID, S_BLIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&booklist, LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, (ULONG)bcur, End;
    if (o)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)o, CHILD_MinWidth, 110,
                 CHILD_MinHeight, 48, TAG_END);
    h = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    r_add(h, r_button(S_BNEW, "New"), NULL);
    r_add(h, r_button(S_BDEL, "Delete"), NULL);
    if (h)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (top && g)
        SetAttrs(top, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedWidth, 35,
                 TAG_END);
    g = r_group("The place");
    c1 = VLayoutObject, End;
    c2 = VLayoutObject, End;
    r_add(c1, r_string(S_BNAME, e->name, sizeof e->name - 1, FALSE), "Name");
    r_add(c1, r_string(S_BHOST, e->host, sizeof e->host - 1, FALSE),
          "Address");
    r_add(c1, sg[S_BPORT] = IntegerObject, GA_ID, S_BPORT, GA_RelVerify,
          TRUE, GA_TabCycle, TRUE, INTEGER_Number, e->port, INTEGER_Minimum,
          0, INTEGER_Maximum, 65535, INTEGER_Arrows, FALSE, End, "Port");
    r_add(c1, r_chooser(S_BKIND, kindlabels, e->ssh), "Connection");
    r_add(c2, r_string(S_BUSER, e->user, sizeof e->user - 1, FALSE),
          "User");
    r_add(c2, r_string(S_BPASS, e->pass, sizeof e->pass - 1, TRUE),
          "Password");
    r_add(c2, r_chooser(S_BLET, booklettlabels, e->letters), "Letters");
    r_add(c2, r_chooser(S_BCHR, charlabels, e->chars), "Characters");
    h = HLayoutObject, End;
    r_add(h, c1, NULL);
    r_add(h, c2, NULL);
    r_add(g, h, NULL);
    if (top && g)
        SetAttrs(top, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedWidth, 65,
                 TAG_END);
    r_put(p, top);
    g = r_group("This place's files (empty: the Files tab's)");
    h = HLayoutObject, End;
    r_add(h, r_drawer(S_BDOWN, e->down, "Downloads go to"), "Downloads");
    r_add(h, r_drawer(S_BUP, e->up, "Uploads come from"), "Uploads");
    r_add(g, h, NULL);
    r_put(p, g);
    if (e == &none) {
        static const int ids[] = { S_BNAME, S_BHOST, S_BPORT, S_BKIND,
            S_BUSER, S_BPASS, S_BLET, S_BCHR, S_BDOWN, S_BUP, S_BDEL };
        int i;
        for (i = 0; i < (int)(sizeof ids / sizeof ids[0]); i++)
            r_disable(ids[i], TRUE);
    }
    return r_wrap(p);
}

static Object *page_files(void)
{
    Object *p = r_page(), *g = r_group("Files to and from hosts");
    r_add(g, r_drawer(S_DLDIR, dldir, "Downloads go to"), "Downloads");
    r_add(g, r_drawer(S_ULDIR, uldir, "Uploads come from"), "Uploads");
    r_put(p, g);
    g = r_group("A phone book entry's own drawers come first");
    r_put(p, g);
    return r_wrap(p);
}

static void close_settings_(void)
{
    struct Node *n;
    if (setobj) {
        if (sg[S_TABS])
            setpage = get_attr(S_TABS, CLICKTAB_Current);
        DisposeObject(setobj);          /* the window and every gadget */
        setobj = NULL;
    }
    setwin = NULL;
    memset(sg, 0, sizeof sg);
    if (settabs.lh_Head)
        while ((n = RemHead(&settabs)))
            FreeClickTabNode(n);
    booklist_free();
    if (booksnap)                       /* closed without Cancel: not needed */
        FreeVec(booksnap);
    booksnap = NULL;
    booksnapped = FALSE;
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

static Object *page_keys(void)
{
    Object *p = r_page(), *g = r_group("Keys");
    r_add(g, r_chooser(S_ALT, altlabels, !altmeta), "Alt key");
    r_put(p, g);
    g = r_group("Right Amiga + a letter: the phone book's login");
    /* MaxChars counts the closing zero: 2 for one letter */
    if (g && r_string(S_KUSER, userkey, 2, FALSE))
        SetAttrs(g, LAYOUT_AddChild, (ULONG)sg[S_KUSER], CHILD_MaxWidth, 40,
                 CHILD_Label, (ULONG)r_label("Send user name"), TAG_END);
    if (g && r_string(S_KPASS, passkey, 2, FALSE))
        SetAttrs(g, LAYOUT_AddChild, (ULONG)sg[S_KPASS], CHILD_MaxWidth, 40,
                 CHILD_Label, (ULONG)r_label("Send password"), TAG_END);
    r_put(p, g);
    return r_wrap(p);
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
    snap.pcfont = pcfont;
    snap.altmeta = altmeta;
    snap.theme = theme;
    snap.ownfg = ownfg; snap.ownbg = ownbg;
    snap.myfg = myfg; snap.mybg = mybg;
    snap.usett = usett; snap.ttsize = ttsize;
    strcpy(snap.ttfile, ttfile);
    strcpy(snap.fontname, fontname);
    snap.fontsize = fontsize;
    snap.linespace = linespace;
    snap.s_utf8 = utf8; snap.pc = pcmode;
    snap.bgsee = bgsee;
    strcpy(snap.bgfile, bgfile);
    strcpy(snap.userkey, userkey);
    strcpy(snap.passkey, passkey);
    strcpy(snap.dldir, dldir);
    strcpy(snap.uldir, uldir);
    if (booksnap)
        FreeVec(booksnap);
    /* no book yet is a book of none: Cancel takes back what New added */
    booksnap = nbook ? AllocVec(nbook * sizeof *book, MEMF_ANY) : NULL;
    nbooksnap = booksnap ? nbook : 0;
    booksnapped = booksnap || !nbook;
    if (booksnap)
        memcpy(booksnap, book, nbook * sizeof *book);
    if (bcur >= nbook)
        bcur = nbook - 1;
    if (bcur < 0 && nbook)
        bcur = 0;
    pend_mode = scrmode;
    pend_depth = owndepth;
    pend_id = ownmode;
    pend_w = ownw;
    pend_h = ownh;
    pend_full = fullscreen;
    pend_bar = scrollbar;

    NewList(&settabs);
    for (i = 0; tablabels[i]; i++) {
        struct Node *n = AllocClickTabNode(TNA_Text, (ULONG)tablabels[i],
                                           TNA_Number, i, TAG_END);
        if (n)
            AddTail(&settabs, n);
    }
    pages = PageObject, PAGE_Add, (ULONG)page_screen(),
        PAGE_Add, (ULONG)page_text(), PAGE_Add, (ULONG)page_colours(),
        PAGE_Add, (ULONG)page_keys(), PAGE_Add, (ULONG)page_book(),
        PAGE_Add, (ULONG)page_files(), PAGE_Current, setpage, End;
    buttons = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    r_add(buttons, r_button(S_SAVE, "_Save"), NULL);
    r_add(buttons, r_button(S_USE, "_Use"), NULL);
    r_add(buttons, r_button(S_CANCEL, "_Cancel"), NULL);
    root = setlayout = VLayoutObject, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE, End;
    sg[S_TABS] = ClickTabObject, GA_ID, S_TABS, CLICKTAB_Labels,
        (ULONG)&settabs, CLICKTAB_Current, setpage,
        CLICKTAB_PageGroup, (ULONG)pages, End;
    if (root && sg[S_TABS]) {
        /* wide enough for every tab's name and the phone book's two
         * columns (the pages don't all say how wide they need) */
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
        close_settings();
        return;
    }
    setobj = WindowObject, WA_Title, (ULONG)"CTerm Settings",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_Layout, (ULONG)root, End;
    if (!setobj) {
        DisposeObject(root);
        memset(sg, 0, sizeof sg);
        close_settings();
        return;
    }
    setwin = (struct Window *)DoMethod(setobj, WM_OPEN);
    if (!setwin)
        close_settings();
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
    BookEntry *bs = booksnap;           /* taken before close frees it */
    int nbs = nbooksnap;
    BOOL bsok = booksnapped;
    booksnap = NULL;
    book_read();                        /* what is typed, before it goes */
    if (how != 3) {
        if ((t = (const char *)get_attr(S_KUSER, STRINGA_TextVal)))
            userkey[0] = toupper((UBYTE)t[0]);
        if ((t = (const char *)get_attr(S_KPASS, STRINGA_TextVal)))
            passkey[0] = toupper((UBYTE)t[0]);
        if ((t = (const char *)get_attr(S_DLDIR, GETFILE_Drawer))) {
            strncpy(dldir, t, sizeof dldir - 1);
            dldir[sizeof dldir - 1] = 0;
        }
        if ((t = (const char *)get_attr(S_ULDIR, GETFILE_Drawer))) {
            strncpy(uldir, t, sizeof uldir - 1);
            uldir[sizeof uldir - 1] = 0;
        }
    }
    close_settings();
    if (how == 3 && bsok && nbs <= book_room()) {   /* the book as it was */
        if (nbs)
            memcpy(book, bs, nbs * sizeof *book);
        nbook = nbs;
    }
    if (bs)
        FreeVec(bs);
    if (how == 3) {
        BOOL letters = snap.usett != usett || snap.ttsize != ttsize ||
                       strcmp(snap.ttfile, ttfile) ||
                       strcmp(snap.fontname, fontname) ||
                       snap.fontsize != fontsize ||
                       snap.linespace != linespace;
        theme = snap.theme;
        pcfont = snap.pcfont;
        altmeta = snap.altmeta;
        ownfg = snap.ownfg; ownbg = snap.ownbg;
        myfg = snap.myfg; mybg = snap.mybg;
        if (snap.bgsee != bgsee || strcmp(snap.bgfile, bgfile)) {
            BOOL other = strcmp(snap.bgfile, bgfile) != 0;
            bgsee = snap.bgsee;
            strcpy(bgfile, snap.bgfile);
            if (other)
                picture_changed();      /* colours_changed draws it */
        }
        if (snap.s_utf8 != utf8 || snap.pc != pcmode)
            set_chars(snap.pc ? 2 : snap.s_utf8 ? 0 : 1);
        if (letters) {
            usett = snap.usett;
            ttsize = snap.ttsize;
            strcpy(ttfile, snap.ttfile);
            strcpy(fontname, snap.fontname);
            fontsize = snap.fontsize;
            linespace = snap.linespace;
            load_font();
            letters_changed();
        }
        colours_changed();
        return;
    }
    if (!T->t_bookname[0]) {            /* a phone book tab's are its own */
        saved_utf8 = utf8;
        saved_pc = pcmode;
    }
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
            switch_display();
        }
        ownmode = pend_id;
        ownw = pend_w;
        ownh = pend_h;
        want_mode = pend_mode;
        want_depth = pend_depth;
        want_full = pend_mode == SCR_OWN && pend_full;
        want_bar = pend_bar;
        want_switch = TRUE;             /* the main loop does it */
    }
    used_only = how == 1;               /* Use: until a reboot, even on quit */
    envonly = used_only;
    save_prefs();
    envonly = FALSE;
    book_save();                        /* the book is kept either way */
    remake_menus();                     /* the login keys */
}

/* The login key typed in gadget id, as it would be kept (0: none). */
static char key_typed(int id)
{
    const char *t = (const char *)get_attr(id, STRINGA_TextVal);
    return t ? toupper((UBYTE)t[0]) : 0;
}

/* Does key k (Right Amiga + it) do something else already: a menu's,
 * a tab's number, the other login key? The menus are walked as they are,
 * so a new shortcut there is seen too. */
static BOOL key_taken(char k, char other)
{
    struct Menu *m;
    struct MenuItem *it, *sub;
    if (!k || k == '-')
        return FALSE;
    if ((k >= '0' && k <= '9') || k == other)
        return TRUE;
    for (m = menu; m; m = m->NextMenu)
        for (it = m->FirstItem; it; it = it->NextItem) {
            ULONG id = (ULONG)GTMENUITEM_USERDATA(it);
            if (id != M_SENDUSER && id != M_SENDPASS &&
                (it->Flags & COMMSEQ) && toupper((UBYTE)it->Command) == k)
                return TRUE;
            for (sub = it->SubItem; sub; sub = sub->NextItem)
                if ((sub->Flags & COMMSEQ) &&
                    toupper((UBYTE)sub->Command) == k)
                    return TRUE;
        }
    return FALSE;
}

/* Use or Save with a login key another shortcut has: refused, the window
 * says why and stays (else that key would steal, say, Quit). */
static BOOL keys_clash(void)
{
    char u = key_typed(S_KUSER), p = key_typed(S_KPASS);
    int bad = key_taken(u, p) ? S_KUSER : key_taken(p, u) ? S_KPASS : 0;
    if (!bad)
        return FALSE;
    DisplayBeep(scr);
    SetWindowTitles(setwin, (UBYTE *)"CTerm Settings: that key is taken "
                    "(Keys tab)", (UBYTE *)~0);
    set_attr(S_TABS, CLICKTAB_Current, 3);
    ActivateLayoutGadget((struct Gadget *)setlayout, setwin, NULL,
                         (ULONG)sg[bad]);
    return TRUE;
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
                    DisplayBeep(scr);   /* it would not open */
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
    case S_SPACE:
        v = get_attr(S_SPACE, SLIDER_Level);
        if ((int)v != linespace) {
            linespace = v;
            letters_changed();
        }
        break;
    case S_CHARS:
        set_chars(get_attr(S_CHARS, CHOOSER_Selected));
        tab_letters(FALSE);                 /* shown on closing */
        break;
    case S_PCFONT: pcfont = get_attr(S_PCFONT, GA_Selected) != 0; break;
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
                    DisplayBeep(scr);   /* not a picture it could read */
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
    case S_BLIST:
        v = get_attr(S_BLIST, LISTBROWSER_Selected);
        if ((int)v != bcur) {
            book_read();
            bcur = (LONG)v < nbook ? (int)(LONG)v : -1;
            booklist_again();
            book_show();
        }
        break;
    case S_BNEW:
        book_read();
        if (book_need()) {
            book_blank(&book[nbook], "New place");
            bcur = nbook++;
            booklist_again();
            book_show();
            ActivateLayoutGadget((struct Gadget *)setlayout, setwin, NULL,
                                 (ULONG)sg[S_BNAME]);
        }
        break;
    case S_BDEL:
        if (bcur >= 0 && bcur < nbook) {
            memmove(&book[bcur], &book[bcur + 1],
                    (nbook - bcur - 1) * sizeof book[0]);
            nbook--;
            if (bcur >= nbook)
                bcur = nbook - 1;
            booklist_again();
            book_show();
        }
        break;
    case S_BNAME:
        book_read();
        booklist_again();               /* its name in the list */
        break;
    case S_BHOST:
        book_read();
        book_show();                    /* a URL taken apart */
        break;
    case S_BDOWN: case S_BUP: case S_DLDIR: case S_ULDIR:
        DoMethod(sg[id], GFILE_REQUEST, (ULONG)setwin);
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
            how = settings_gadget(res & WMHI_GADGETMASK);
            break;
        }
    }
    if (how)
        finish_settings(how);
}

/* The settings window's work on a deep stack: OS 3.2's classes want one,
 * and CTerm's own stays small for a stock A1200 (see deep_call). */
void open_settings(void) { deep_call(open_settings_); }
void close_settings(void) { deep_call(close_settings_); }
void settings_events(void) { deep_call(settings_events_); }

/* CTerm ends with the window open: as its close gadget, Cancel - so the
 * prefs are saved (save_prefs waits while it is open) as they were. */
static void settings_cancel_(void)
{
    if (setobj)
        finish_settings(3);
}
void settings_cancel(void) { deep_call(settings_cancel_); }
