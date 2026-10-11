/*
 * bookwin - the phone book, a window of its own (Project/Phone book...):
 * the places on the left, the one chosen on the right, Call and Close.
 * What is typed counts at once; the book is saved when the window closes
 * or calls. OS 3.2's ReAction classes, as the settings window. A part
 * of its own (PROGDIR:Parts/PhoneBook, with serwin.c): loaded when the
 * window opens, gone when it closes; where it was is CTerm's (bwleft,
 * bwtop).
 */
#include "cterm.h"

enum { B_LIST = 1, B_NEW, B_COPY, B_DEL, B_NAME, B_HOST, B_PORT, B_KIND,
       B_USER, B_PASS, B_LET, B_CHR, B_DOWN, B_UP, B_CALL, B_CLOSE,
       B_PHONE, B_LINE, B_SCRIPT, B_N };

static Object *bwobj, *bg[B_N], *bwlayout;
static struct Window *bwwin;
static struct List bwlist;              /* the places' names */
static int bcur = -1;                   /* the entry shown: -1 none */

static STRPTR kindlabels[] = { (STRPTR)"telnet", (STRPTR)"ssh",
                               (STRPTR)"serial", NULL };
static STRPTR lettlabels[] = { (STRPTR)"CTerm's setting",
                               (STRPTR)"Amiga font", (STRPTR)"TrueType",
                               (STRPTR)"PC font (IBM VGA)", NULL };
static STRPTR chrlabels[] = { (STRPTR)"UTF-8", (STRPTR)"Latin-1",
                              (STRPTR)"PC BBS (CP437)", NULL };
/* the controls that mean nothing with no entry chosen */
static const int entry_ids[] = { B_NAME, B_HOST, B_PORT, B_KIND, B_USER,
    B_PASS, B_LET, B_CHR, B_DOWN, B_UP, B_COPY, B_DEL, B_CALL, B_PHONE,
    B_LINE, B_SCRIPT };
#define NENTRY ((int)(sizeof entry_ids / sizeof entry_ids[0]))

static ULONG bw_sigmask(void)
{
    return bwwin ? 1UL << bwwin->UserPort->mp_SigBit : 0;
}

static void bset(int id, ULONG tag, ULONG val)
{
    if (bwwin && bg[id] &&
        SetGadgetAttrs((struct Gadget *)bg[id], bwwin, NULL, tag, val,
                       TAG_END))
        RefreshGList((struct Gadget *)bg[id], bwwin, NULL, 1);
}

static ULONG bget(int id, ULONG tag)
{
    ULONG v = 0;
    if (bg[id])
        GetAttr(tag, bg[id], &v);
    return v;
}

static void list_free(void)
{
    struct Node *n;
    if (bwlist.lh_Head)
        while ((n = RemHead(&bwlist)))
            FreeListBrowserNode(n);
}

static void list_make(void)
{
    int i;
    NewList(&bwlist);
    for (i = 0; i < nbook; i++) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(book[i].name[0] ? book[i].name : "?"),
            TAG_END);
        if (n)
            AddTail(&bwlist, n);
    }
}

/* The list again (a name changed, an entry came or went). */
static void list_again(void)
{
    bset(B_LIST, LISTBROWSER_Labels, ~0UL);     /* let go of it */
    list_free();
    list_make();
    bset(B_LIST, LISTBROWSER_Labels, (ULONG)&bwlist);
    bset(B_LIST, LISTBROWSER_Selected, (ULONG)bcur);
    if (bcur >= 0)
        bset(B_LIST, LISTBROWSER_MakeVisible, (ULONG)bcur);
}

/* What is typed in the fields, into the entry shown. */
static void entry_read(void)
{
    BookEntry *e;
    const char *t;
    if (bcur < 0 || bcur >= nbook || !bwobj)
        return;
    e = &book[bcur];
#define TXT(id, f) if ((t = (const char *)bget(id, STRINGA_TextVal))) { \
        strncpy(f, t, sizeof f - 1); f[sizeof f - 1] = 0; }
    TXT(B_NAME, e->name);
    TXT(B_HOST, e->host);
    TXT(B_USER, e->user);
    TXT(B_PASS, e->pass);
    TXT(B_PHONE, e->phone);
#undef TXT
    if ((t = (const char *)bget(B_DOWN, GETFILE_Drawer))) {
        strncpy(e->down, t, sizeof e->down - 1);
        e->down[sizeof e->down - 1] = 0;
    }
    if ((t = (const char *)bget(B_UP, GETFILE_Drawer))) {
        strncpy(e->up, t, sizeof e->up - 1);
        e->up[sizeof e->up - 1] = 0;
    }
    if ((t = (const char *)bget(B_SCRIPT, GETFILE_FullFile))) {
        /* a drawer alone (no file chosen): no script */
        strncpy(e->script, *FilePart((STRPTR)t) ? t : "",
                sizeof e->script - 1);
        e->script[sizeof e->script - 1] = 0;
    }
    e->port = bget(B_PORT, INTEGER_Number);
    e->ssh = bget(B_KIND, CHOOSER_Selected) == 1;
    e->serial = bget(B_KIND, CHOOSER_Selected) == 2;
    e->letters = bget(B_LET, CHOOSER_Selected);
    e->chars = bget(B_CHR, CHOOSER_Selected);
    book_tidy(e);
}

/* The fields that mean something for the kind of place: an address and
 * port for telnet and ssh, a phone number and a line for serial. */
static void kind_fields(const BookEntry *e, BOOL none)
{
    bset(B_HOST, GA_Disabled, none || e->serial);
    bset(B_PORT, GA_Disabled, none || e->serial);
    bset(B_PHONE, GA_Disabled, none || !e->serial);
    bset(B_LINE, GA_Disabled, none || !e->serial);
}

/* The entry shown, into the fields (none: empty and asleep). */
static void entry_show(void)
{
    static BookEntry none;
    const BookEntry *e = bcur >= 0 && bcur < nbook ? &book[bcur] : &none;
    int i;
    bset(B_NAME, STRINGA_TextVal, (ULONG)e->name);
    bset(B_HOST, STRINGA_TextVal, (ULONG)e->host);
    bset(B_PORT, INTEGER_Number, e->port);
    bset(B_KIND, CHOOSER_Selected, e->serial ? 2 : e->ssh);
    bset(B_PHONE, STRINGA_TextVal, (ULONG)e->phone);
    bset(B_USER, STRINGA_TextVal, (ULONG)e->user);
    bset(B_PASS, STRINGA_TextVal, (ULONG)e->pass);
    bset(B_LET, CHOOSER_Selected, e->letters);
    bset(B_CHR, CHOOSER_Selected, e->chars);
    bset(B_DOWN, GETFILE_Drawer, (ULONG)e->down);
    bset(B_UP, GETFILE_Drawer, (ULONG)e->up);
    bset(B_SCRIPT, GETFILE_FullFile, (ULONG)e->script);
    for (i = 0; i < NENTRY; i++)
        bset(entry_ids[i], GA_Disabled, e == &none);
    kind_fields(e, e == &none);
}

/* ---- building it ---- */

static Object *label(const char *t)
{
    return LabelObject, LABEL_Text, (ULONG)t, LabelEnd;
}

static void add(Object *g, Object *child, const char *lab)
{
    if (g && child)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)child,
                 lab ? CHILD_Label : TAG_IGNORE, lab ? (ULONG)label(lab) : 0,
                 TAG_END);
}

static Object *string(int id, const char *t, int max, BOOL secret)
{
    return bg[id] = StringObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, STRINGA_TextVal, (ULONG)t, STRINGA_MaxChars, max,
        secret ? STRINGA_HookType : TAG_IGNORE, SHK_PASSWORD, End;
}

static Object *chooser(int id, STRPTR *l, int sel)
{
    return bg[id] = ChooserObject, GA_ID, id, GA_RelVerify, TRUE,
        CHOOSER_LabelArray, (ULONG)l, CHOOSER_Selected, sel,
        CHOOSER_DropDown, FALSE, End;
}

static Object *button(int id, const char *t)
{
    return bg[id] = ButtonObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)t, End;
}

static Object *drawer(int id, const char *path, const char *title)
{
    return bg[id] = GetFileObject, GA_ID, id, GA_RelVerify, TRUE,
        GETFILE_Drawer, (ULONG)path, GETFILE_DrawersOnly, TRUE,
        GETFILE_ReadOnly, TRUE, GETFILE_TitleText, (ULONG)title, End;
}

static Object *group(const char *title)
{
    return VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)title, LAYOUT_SpaceOuter, TRUE, End;
}

static void bookwin_close_(void)
{
    if (!bwobj)
        return;
    entry_read();
    book_save();
    if (bwwin) {
        bwleft = bwwin->LeftEdge;
        bwtop = bwwin->TopEdge;
    }
    DisposeObject(bwobj);               /* the window and every gadget */
    bwobj = NULL;
    bwwin = NULL;
    bwlayout = NULL;
    memset(bg, 0, sizeof bg);
    list_free();
}

static void bookwin_open_(void)
{
    Object *root, *top, *g, *h, *o, *right;
    static BookEntry none;
    const BookEntry *e;
    int i;
    if (bwwin) {
        WindowToFront(bwwin);
        ActivateWindow(bwwin);
        return;
    }
    book_use();
    if (!reaction_open() || (!book && (book_need(), !book))) {
        beep();
        return;
    }
    if (bcur >= nbook)
        bcur = nbook - 1;
    if (bcur < 0 && nbook)
        bcur = 0;
    e = bcur >= 0 ? &book[bcur] : &none;
    memset(bg, 0, sizeof bg);
    list_make();

    /* the list on the left, the entry on the right: short enough for a
     * 256-line screen */
    top = HLayoutObject, End;
    g = group("Places");
    o = bg[B_LIST] = ListBrowserObject, GA_ID, B_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&bwlist, LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected, (ULONG)bcur, End;
    if (g && o)                         /* 16 letters wide, 6 places high */
        SetAttrs(g, LAYOUT_AddChild, (ULONG)o,
                 CHILD_MinWidth, 16 * scr->RastPort.TxWidth + 24,
                 CHILD_MinHeight, 6 * (scr->RastPort.TxHeight + 2) + 4,
                 TAG_END);
    h = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    add(h, button(B_NEW, "New"), NULL);
    add(h, button(B_COPY, "Copy"), NULL);
    add(h, button(B_DEL, "Delete"), NULL);
    if (g && h)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (top && g)
        SetAttrs(top, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedWidth, 35,
                 TAG_END);

    /* one column: on a 640-pixel screen two would leave the fields too
     * narrow for a host name */
    g = group("The place");
    add(g, string(B_NAME, e->name, sizeof e->name - 1, FALSE), "Name");
    add(g, string(B_HOST, e->host, sizeof e->host - 1, FALSE), "Address");
    h = HLayoutObject, End;
    add(h, bg[B_PORT] = IntegerObject, GA_ID, B_PORT, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, INTEGER_Number, e->port, INTEGER_Minimum, 0,
        INTEGER_Maximum, 65535, INTEGER_Arrows, FALSE, End, NULL);
    add(h, chooser(B_KIND, kindlabels, e->serial ? 2 : e->ssh), "Connection");
    add(g, h, "Port");
    /* a serial line: the number a modem dials (none: a null modem), and
     * the line's settings in a window of their own */
    h = HLayoutObject, End;
    add(h, string(B_PHONE, e->phone, sizeof e->phone - 1, FALSE), NULL);
    if (h && button(B_LINE, "Line..."))
        SetAttrs(h, LAYOUT_AddChild, (ULONG)bg[B_LINE],
                 CHILD_WeightedWidth, 0, TAG_END);
    add(g, h, "Phone");
    /* the name and password side by side: room for the script below,
     * the window still within a 256-line screen */
    h = HLayoutObject, End;
    add(h, string(B_USER, e->user, sizeof e->user - 1, FALSE), NULL);
    add(h, string(B_PASS, e->pass, sizeof e->pass - 1, TRUE), "Password");
    add(g, h, "User");
    add(g, chooser(B_LET, lettlabels, e->letters), "Letters");
    add(g, chooser(B_CHR, chrlabels, e->chars), "Characters");
    /* an ARexx script run when the call is up: a BBS login */
    add(g, bg[B_SCRIPT] = GetFileObject, GA_ID, B_SCRIPT, GA_RelVerify, TRUE,
        GETFILE_FullFile, (ULONG)e->script, GETFILE_Pattern,
        (ULONG)"#?.rexx", GETFILE_DoPatterns, TRUE, GETFILE_ReadOnly, TRUE,
        GETFILE_TitleText, (ULONG)"Its login script (ARexx)", End,
        "Script");
    right = VLayoutObject, End;
    if (right && g)                     /* room for a host name */
        SetAttrs(right, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 CHILD_MinWidth, 40 * scr->RastPort.TxWidth, TAG_END);
    g = group("Its own drawers (empty: Settings, Files)");
    h = HLayoutObject, End;
    add(h, drawer(B_DOWN, e->down, "Downloads go to"), "Downloads");
    add(h, drawer(B_UP, e->up, "Uploads come from"), "Uploads");
    add(g, h, NULL);
    if (right && g)
        SetAttrs(right, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (top && right)
        SetAttrs(top, LAYOUT_AddChild, (ULONG)right, CHILD_WeightedWidth, 65,
                 TAG_END);

    h = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    add(h, button(B_CALL, "C_all"), NULL);
    add(h, button(B_CLOSE, "Cl_ose"), NULL);
    root = bwlayout = VLayoutObject, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_DeferLayout, TRUE, End;
    if (root && top)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)top, TAG_END);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (!root || !top || !h) {
        /* what was not given to root goes by hand */
        if (root) DisposeObject(root);
        if (!root && top) DisposeObject(top);
        if (!root && h) DisposeObject(h);
        memset(bg, 0, sizeof bg);
        list_free();
        beep();
        return;
    }
    if (e == &none)
        for (i = 0; i < NENTRY; i++)
            if (bg[entry_ids[i]])
                SetAttrs(bg[entry_ids[i]], GA_Disabled, TRUE, TAG_END);
    {   /* what the kind of place does not use, asleep */
        static const int tcp[] = { B_HOST, B_PORT }, ser[] = { B_PHONE,
                                                               B_LINE };
        for (i = 0; i < 2; i++) {
            if (bg[tcp[i]] && e->serial)
                SetAttrs(bg[tcp[i]], GA_Disabled, TRUE, TAG_END);
            if (bg[ser[i]] && !e->serial)
                SetAttrs(bg[ser[i]], GA_Disabled, TRUE, TAG_END);
        }
    }
    bwobj = WindowObject, WA_Title, (ULONG)"CTerm Phone book",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, TRUE,
        bwleft >= 0 ? WA_Left : TAG_IGNORE, bwleft,
        bwleft >= 0 ? WA_Top : TAG_IGNORE, bwtop,
        bwleft >= 0 ? TAG_IGNORE : WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, (ULONG)root, End;
    if (!bwobj) {
        DisposeObject(root);
        memset(bg, 0, sizeof bg);
        list_free();
        beep();
        return;
    }
    bwwin = (struct Window *)DoMethod(bwobj, WM_OPEN);
    if (!bwwin)
        bookwin_close_();
}

/* A new entry (or a copy of the one shown), chosen, its name to type. */
static void entry_new(BOOL copy)
{
    entry_read();
    if (!book_need())
        return;
    if (copy && bcur >= 0 && bcur < nbook) {
        book[nbook] = book[bcur];
        strncat(book[nbook].name, " (copy)",
                sizeof book[nbook].name - 1 - strlen(book[nbook].name));
    } else
        book_blank(&book[nbook], "New place");
    bcur = nbook++;
    list_again();
    entry_show();
    ActivateLayoutGadget((struct Gadget *)bwlayout, bwwin, NULL,
                         (ULONG)bg[B_NAME]);
}

/* Call the entry shown, in a new tab; the book closes (and is saved). */
static void entry_call(void)
{
    int i = bcur;
    entry_read();
    if (i < 0 || i >= nbook || (!book[i].host[0] && !book[i].serial)) {
        beep();
        return;
    }
    bookwin_close_();
    if (win)
        ActivateWindow(win);
    call_book(i, TRUE);
}

static void bookwin_events_(void)
{
    ULONG res, v;
    UWORD code;
    while (bwobj &&
           (res = DoMethod(bwobj, WM_HANDLEINPUT, (ULONG)&code)) !=
           WMHI_LASTMSG) {
        switch (res & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            bookwin_close_();
            return;
        case WMHI_GADGETUP:
            switch (res & WMHI_GADGETMASK) {
            case B_LIST:
                v = bget(B_LIST, LISTBROWSER_Selected);
                if ((int)v != bcur) {
                    entry_read();
                    bcur = (LONG)v < nbook ? (int)(LONG)v : -1;
                    list_again();
                    entry_show();
                } else if (bget(B_LIST, LISTBROWSER_RelEvent) ==
                           LBRE_DOUBLECLICK) {
                    entry_call();       /* a place double-clicked: call */
                    return;
                }
                break;
            case B_NEW: entry_new(FALSE); break;
            case B_COPY: entry_new(TRUE); break;
            case B_DEL:
                if (bcur >= 0 && bcur < nbook) {
                    memmove(&book[bcur], &book[bcur + 1],
                            (nbook - bcur - 1) * sizeof book[0]);
                    nbook--;
                    if (bcur >= nbook)
                        bcur = nbook - 1;
                    list_again();
                    entry_show();
                }
                break;
            case B_NAME:
                entry_read();
                list_again();           /* its name in the list */
                break;
            case B_HOST:
                entry_read();
                entry_show();           /* a URL taken apart */
                break;
            case B_KIND:
                entry_read();
                if (bcur >= 0 && bcur < nbook)
                    kind_fields(&book[bcur], FALSE);
                break;
            case B_LINE:
                entry_read();
                if (bcur >= 0 && bcur < nbook) {
                    BookEntry *e = &book[bcur];
                    serwin_edit(&e->line, e->init, e->dial, bwwin);
                }
                break;
            case B_SCRIPT:
                if (!bget(B_SCRIPT, GETFILE_FullFile) ||
                    !*(char *)bget(B_SCRIPT, GETFILE_FullFile))
                    SetAttrs(bg[B_SCRIPT], GETFILE_Drawer, (ULONG)"REXX:",
                             TAG_END);
                DoMethod(bg[B_SCRIPT], GFILE_REQUEST, (ULONG)bwwin);
                entry_read();
                break;
            case B_DOWN: case B_UP:
                DoMethod(bg[res & WMHI_GADGETMASK], GFILE_REQUEST,
                         (ULONG)bwwin);
                break;
            case B_CALL:
                entry_call();
                return;
            case B_CLOSE:
                bookwin_close_();
                return;
            }
            break;
        }
    }
}

/* Its work on a deep stack, as the settings window's (see deep_call). */
static void bw_open(void) { deep_call(bookwin_open_); }
static void bw_events(void) { deep_call(bookwin_events_); }
/* Before the screen goes, and at the end: closed, the book saved. */
static void bw_close(void) { deep_call(bookwin_close_); }

APTR part_main(void)
{
    static struct BookPart p = { bw_open, bw_events,
                                 bw_close, bw_sigmask };
    return &p;
}
