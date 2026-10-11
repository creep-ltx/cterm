/*
 * screen - screens, windows and menus.
 */
#include "cterm.h"

static void make_menus(void);
static BOOL open_own_screen(void);
static void close_own_screen(BOOL ask);
static void clear_leftover(void);
static void ask_switch(int mode, int depth, BOOL full, BOOL bar);

static struct NewMenu newmenu[] = {
    { NM_TITLE, (STRPTR)"CTerm", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"New Shell tab", 0, 0, 0, (APTR)M_NEWTAB },
    {  NM_ITEM, (STRPTR)"Close tab", 0, 0, 0, (APTR)M_CLOSETAB },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Run ARexx script...", 0, 0, 0, (APTR)M_REXX },
    {  NM_ITEM, (STRPTR)"Speed test", 0, 0, 0, (APTR)M_SPEED },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"About", 0, 0, 0, (APTR)M_ABOUT },
    {  NM_ITEM, (STRPTR)"Hide window", 0, 0, 0, (APTR)M_HIDE },
    {  NM_ITEM, (STRPTR)"Quit", 0, 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Call", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Call...", 0, 0, 0, (APTR)M_CALL },
    {  NM_ITEM, (STRPTR)"Phone book...", 0, 0, 0, (APTR)M_BOOK },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Serial terminal", 0, 0, 0, (APTR)M_SERIAL },
    {  NM_ITEM, (STRPTR)"Answer call (ATA)", 0, 0, 0, (APTR)M_ANSWER },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Send user name", 0, 0, 0, (APTR)M_SENDUSER },
    {  NM_ITEM, (STRPTR)"Send password", 0, 0, 0, (APTR)M_SENDPASS },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Copy", 0, 0, 0, (APTR)M_COPY },
    {  NM_ITEM, (STRPTR)"Paste", 0, 0, 0, (APTR)M_PASTE },
    { NM_TITLE, (STRPTR)"Files", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Open SFTP browser", 0, 0, 0, (APTR)M_SFTP },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Send file", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"With Ymodem...", 0, 0, 0, (APTR)M_YSEND },
    {   NM_SUB, (STRPTR)"With Xmodem...", 0, 0, 0, (APTR)M_XSEND },
    {   NM_SUB, (STRPTR)"With Hydra...", 0, 0, 0, (APTR)M_HSEND },
    {  NM_ITEM, (STRPTR)"Receive file", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"With Ymodem", 0, 0, 0, (APTR)M_YRECV },
    {   NM_SUB, (STRPTR)"With Xmodem...", 0, 0, 0, (APTR)M_XRECV },
    {  NM_ITEM, (STRPTR)"Show transfers...", 0, 0, 0, (APTR)M_XWIN },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Start log...", 0, 0, 0, (APTR)M_LOG },
    {  NM_ITEM, (STRPTR)"Stop log", 0, 0, 0, (APTR)M_LOGSTOP },
    { NM_TITLE, (STRPTR)"Panes", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"New Shell pane", 0, 0, 0, (APTR)M_POPEN },
    {  NM_ITEM, (STRPTR)"Close pane and tab", 0, 0, 0, (APTR)M_PKILL },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Focus", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"Left", 0, 0, 0, (APTR)M_PFLEFT },
    {   NM_SUB, (STRPTR)"Right", 0, 0, 0, (APTR)M_PFRIGHT },
    {   NM_SUB, (STRPTR)"Up", 0, 0, 0, (APTR)M_PFUP },
    {   NM_SUB, (STRPTR)"Down", 0, 0, 0, (APTR)M_PFDOWN },
    {  NM_ITEM, (STRPTR)"Move", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"Left", 0, 0, 0, (APTR)M_PMLEFT },
    {   NM_SUB, (STRPTR)"Right", 0, 0, 0, (APTR)M_PMRIGHT },
    {   NM_SUB, (STRPTR)"Up", 0, 0, 0, (APTR)M_PMUP },
    {   NM_SUB, (STRPTR)"Down", 0, 0, 0, (APTR)M_PMDOWN },
    {  NM_ITEM, (STRPTR)"Fill the window", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_PFULL },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"New panes side by side", 0, CHECKIT, 0,
       (APTR)M_PSPLITH },
    {  NM_ITEM, (STRPTR)"New panes one below other", 0, CHECKIT, 0,
       (APTR)M_PSPLITV },
    {  NM_ITEM, (STRPTR)"Automatic split (dwindle)", 0,
       CHECKIT | MENUTOGGLE, 0, (APTR)M_PDWINDLE },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"All panes to tabs", 0, 0, 0, (APTR)M_PTABS },
    {  NM_ITEM, (STRPTR)"All tabs to panes", 0, 0, 0, (APTR)M_PPANES },
    { NM_TITLE, (STRPTR)"View", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Bigger text", 0, 0, 0, (APTR)M_BIGGER },
    {  NM_ITEM, (STRPTR)"Smaller text", 0, 0, 0, (APTR)M_SMALLER },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Screen", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"Workbench window", 0, CHECKIT, ~1, (APTR)M_SCR_WB },
    {   NM_SUB, (STRPTR)"Own screen, 16 colours", 0, CHECKIT, ~2,
        (APTR)M_SCR_16 },
    {   NM_SUB, (STRPTR)"Own screen, 256 colours", 0, CHECKIT, ~4,
        (APTR)M_SCR_256 },
    {   NM_SUB, (STRPTR)"Own screen, like Workbench", 0, CHECKIT, ~8,
        (APTR)M_SCR_DEEP },
    {  NM_ITEM, (STRPTR)"Full screen", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_FULL },
    {  NM_ITEM, (STRPTR)"Scroll bar", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_BAR },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Characters", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"UTF-8", 0, CHECKIT | MENUTOGGLE, 0, (APTR)M_UTF8 },
    {   NM_SUB, (STRPTR)"PC BBS (CP437)", 0, CHECKIT | MENUTOGGLE, 0,
        (APTR)M_PC },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Settings...", 0, 0, 0, (APTR)M_SETTINGS },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Amiga font...", 0, 0, 0, (APTR)M_FONT },
    {  NM_ITEM, (STRPTR)"TrueType font...", 0, 0, 0, (APTR)M_TTFONT },
    { NM_END, 0, 0, 0, 0, 0 },
};
struct Window *setwin;
/* the items whose ticks follow the shown tab or pane: menu_ticks */
static const UBYTE tickid[] = { M_UTF8, M_PC, M_PSPLITH, M_PSPLITV, M_PFULL,
                                M_PDWINDLE };
#define NTICK ((int)sizeof tickid)
static struct MenuItem *ticks[NTICK];

/* The menus again: their keys changed (CTerm.keys, Settings). */
void remake_menus(void)
{
    if (!win)
        return;
    if (menu) {
        ClearMenuStrip(win);
        FreeMenus(menu);
        menu = NULL;
    }
    if (visual) {
        FreeVisualInfo(visual);
        visual = NULL;
    }
    make_menus();
}

/* A menu item's shortcut from CTerm.keys: Right Amiga + a character as
 * Intuition's own shortcut; any other key written beside it in words
 * (keys.c does it). */
static char mkpool[1200];                /* the items' key texts, one */
static int mkused;                      /* after another (make_menus) */

static void menu_key(struct NewMenu *n, ULONG id)
{
    char *o = mkpool + mkused;
    int a, i;
    n->nm_CommKey = NULL;
    n->nm_Flags &= ~NM_COMMANDSTRING;
    for (a = 1; a < KA_N && keys_acts[a].menu != id; a++)
        ;
    if (a == KA_N)
        return;
    for (i = 0; i < keys_n; i++)     /* Right Amiga + a character */
        if (keys_binds[i].act == a && (keys_binds[i].key & KQ_MODS) == KQ_A &&
            (keys_binds[i].key & KQ_KEY) > ' ' &&
            (keys_binds[i].key & KQ_KEY) < 256) {
            UBYTE c = keys_binds[i].key & KQ_KEY;
            if (mkused + 2 > (int)sizeof mkpool)
                return;
            o[0] = c >= 'a' && c <= 'z' ? c - 32 : c;
            o[1] = 0;
            mkused += 2;
            n->nm_CommKey = (STRPTR)o;
            return;
        }
    if (!keys_first(a) || mkused + 40 > (int)sizeof mkpool)
        return;
    keys_name(keys_first(a), o, TRUE);
    mkused += strlen(o) + 1;
    n->nm_CommKey = (STRPTR)o;
    n->nm_Flags |= NM_COMMANDSTRING;
}

static void make_menus(void)
{
    struct NewMenu *n;
    mkused = 0;
    for (n = newmenu; n->nm_Type != NM_END; n++) {
        ULONG id = (ULONG)n->nm_UserData;
        BOOL on;
        if (id && n->nm_Type != NM_TITLE)
            menu_key(n, id);
        switch (id) {
        case M_SCR_WB: on = scrmode == SCR_WB; break;
        case M_SCR_16: on = scrmode == SCR_OWN && owndepth == 4; break;
        case M_SCR_256: on = scrmode == SCR_OWN && owndepth == 8; break;
        case M_SCR_DEEP: on = scrmode == SCR_OWN && owndepth > 8; break;
        case M_FULL:
            on = fullscreen;
            if (scrmode == SCR_OWN) n->nm_Flags &= ~NM_ITEMDISABLED;
            else n->nm_Flags |= NM_ITEMDISABLED;
            break;
        case M_BAR: on = scrollbar; break;
        case M_UTF8: on = utf8 && !pcmode; break;
        case M_PC: on = pcmode; break;
        default: continue;
        }
        if (on) n->nm_Flags |= CHECKED;
        else n->nm_Flags &= ~CHECKED;
    }
    visual = GetVisualInfo(scr, TAG_END);
    memset(ticks, 0, sizeof ticks);
    if (visual && (menu = CreateMenus(newmenu, TAG_END)) &&
        LayoutMenus(menu, visual, GTMN_NewLookMenus, TRUE, TAG_END)) {
        struct Menu *m;
        struct MenuItem *it;
        int i;
        for (m = menu; m; m = m->NextMenu)
            for (it = m->FirstItem; it; it = it->NextItem) {
                struct MenuItem *sub;
                for (i = 0; i < NTICK; i++)
                    if ((ULONG)GTMENUITEM_USERDATA(it) == tickid[i])
                        ticks[i] = it;
                for (sub = it->SubItem; sub; sub = sub->NextItem)
                    for (i = 0; i < NTICK; i++)   /* Characters > */
                        if ((ULONG)GTMENUITEM_USERDATA(sub) == tickid[i])
                            ticks[i] = sub;
            }
        SetMenuStrip(win, menu);
    }
    cx_keys();                          /* Amiga + the arrows, as bound */
}

/* The UTF-8 and PC BBS ticks as the shown tab reads: a tab switch, a
 * pane switch, a new tab, a phone book place's characters. Cheap when
 * they are right (the main loop calls it every round); the strip comes
 * off for a change only (never while the menu is open: this runs in
 * CTerm's own loop, between messages). */
void menu_ticks(void)
{
    BOOL on[NTICK], attached, same = TRUE;
    int i;
    if (!win || !shown)
        return;
    on[1] = shown->t_pcmode;
    on[0] = shown->t_utf8 && !on[1];
    on[2] = apane->sdir != 'v';         /* the active pane's direction */
    on[3] = apane->sdir == 'v';
    on[4] = pane_zoomed();
    on[5] = dwindle;
    for (i = 0; i < NTICK; i++)
        if (ticks[i] && !(ticks[i]->Flags & CHECKED) != !on[i])
            same = FALSE;
    if (same)
        return;
    attached = win->MenuStrip == menu;
    if (attached)
        ClearMenuStrip(win);
    for (i = 0; i < NTICK; i++)
        if (ticks[i]) {
            if (on[i]) ticks[i]->Flags |= CHECKED;
            else ticks[i]->Flags &= ~CHECKED;
        }
    if (attached)
        ResetMenuStrip(win, menu);
}

static BOOL open_own_screen(void)
{
    struct Screen *wb = LockPubScreen(NULL);
    ULONG id = INVALID_ID;
    int depth = owndepth, tries;
    UWORD pens[] = { (UWORD)~0 };
    if (wb) {
        id = GetVPModeID(&wb->ViewPort);
        if (depth > 8 && ownmode == INVALID_ID) {
            /* a graphics card's bitmap says 8 in Depth whatever it is */
            struct BitMap *bm = wb->RastPort.BitMap;
            int d = bm->Depth;
            if (CyberGfxBase && GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX))
                d = GetCyberMapAttr(bm, CYBRMATTR_DEPTH);
            depth = d > 24 ? 24 : d > 8 ? d : 8;   /* 32 is 24 + alpha */
        }
        UnlockPubScreen(NULL, wb);
    }
    if (ownmode != INVALID_ID)
        id = ownmode;                   /* chosen in the settings */
    else if (depth <= 8) {
        ULONG best = BestModeID(BIDTAG_SourceID, id, BIDTAG_Depth, depth,
                                TAG_END);
        if (best != INVALID_ID)
            id = best;
    }
    for (tries = 0; tries < 2 && !ownscr; tries++) {
        ULONG err = 0;
        ownscr = OpenScreenTags(NULL,
            SA_DisplayID, id, SA_Depth, depth,
            SA_Width, ownmode != INVALID_ID && ownw > 0 ? ownw
                                                        : STDSCREENWIDTH,
            SA_Height, ownmode != INVALID_ID && ownh > 0 ? ownh
                                                         : STDSCREENHEIGHT,
            SA_Title, (ULONG)"CTerm", SA_PubName, (ULONG)"CTERM",
            SA_Type, PUBLICSCREEN, SA_Pens, (ULONG)pens, SA_SysFont, 1,
            SA_SharePens, TRUE, SA_Interleaved, TRUE,
            SA_ShowTitle, !fullscreen, SA_ErrorCode, (ULONG)&err, TAG_END);
        if (!ownscr && err == OSERR_PUBNOTUNIQUE && !tries)
            clear_leftover();           /* then once more */
        else if (!ownscr)
            break;
    }
    if (!ownscr)
        return FALSE;
    PubScreenStatus(ownscr, 0);
    return TRUE;
}

/* The own screen closes only without visitors (other programs' windows
 * on "CTERM", or their locks on it): Intuition will not make it private
 * while there are any (bit 0 clear). They get two seconds to go, then
 * the person is asked, on this screen, to close them. "Leave it open"
 * keeps it as it is: it is used again if CTerm goes back to its own
 * screen, and after Quit it stays until a later CTerm clears it up.
 * A requester a Shell's program opened there (pr_WindowPtr) is no
 * visitor, but CloseScreen fails while it is open: then the screen is
 * public again and waited for the same way (it was taken for closed,
 * and stayed for good). ask FALSE (hiding): a screen that cannot close
 * at once stays, without a question, and is used again when shown. */
static void close_own_screen(BOOL ask)
{
    static struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)"CTerm",
        (UBYTE *)"Other programs still have windows on CTerm's screen.\n"
                 "Close them, then choose Retry.",
        (UBYTE *)"Retry|Leave it open"
    };
    int i;
    while (ownscr) {
        for (i = 0; i < (ask ? 20 : 1); i++) {
            if (PubScreenStatus(ownscr, PSNF_PRIVATE) & PSNF_PRIVATE) {
                if (CloseScreen(ownscr)) {
                    ownscr = NULL;
                    return;
                }
                PubScreenStatus(ownscr, 0);     /* a window still on it */
            }
            if (ask)
                Delay(5);
        }
        if (!ask)
            return;
        {
            /* a window of our own, out of the way, for the requester to
             * appear on this screen (ours: not a visitor) */
            struct Window *w = OpenWindowTags(NULL, WA_CustomScreen,
                (ULONG)ownscr, WA_Left, 0, WA_Top, 0, WA_Width, 1,
                WA_Height, 1, WA_Borderless, TRUE, WA_Backdrop, TRUE,
                TAG_END);
            i = EasyRequestArgs(w, &es, NULL, NULL);
            if (w)
                CloseWindow(w);
        }
        if (i == 0)
            return;                     /* left open */
    }
}

/* A "CTERM" screen left by an earlier CTerm (its visitors kept it open
 * past Quit): with no windows left it can go, so ours can open. */
static void clear_leftover(void)
{
    struct List *l = LockPubScreenList();
    struct PubScreenNode *n, *found = NULL;
    for (n = (struct PubScreenNode *)l->lh_Head; n->psn_Node.ln_Succ;
         n = (struct PubScreenNode *)n->psn_Node.ln_Succ)
        if (n->psn_Node.ln_Name && !strcmp(n->psn_Node.ln_Name, "CTERM") &&
            !n->psn_Screen->FirstWindow)
            found = n;
    UnlockPubScreenList();
    if (found && (PubScreenStatus(found->psn_Screen, PSNF_PRIVATE) &
                  PSNF_PRIVATE))
        CloseScreen(found->psn_Screen);
}

/* ======================================================================
 * Settings: ENV:CTerm.prefs (and ENVARC:), one "key=value" per line.
 * ==================================================================== */


BOOL open_display(void)
{
    ULONG idcmp = IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY |
                  IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE |
                  IDCMP_INTUITICKS | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW |
                  IDCMP_INACTIVEWINDOW | IDCMP_MOUSEBUTTONS |
                  IDCMP_EXTENDEDMOUSE;
    if (scrmode == SCR_OWN && !ownscr && !open_own_screen())
        scrmode = SCR_WB;
    if (scrmode == SCR_WB) {
        fullscreen = FALSE;
        scr = LockPubScreen(NULL);
        if (!scr) {
            DBGS("open_display: no screen\n");
            return FALSE;
        }
    } else {
        scr = ownscr;
        ShowTitle(ownscr, !fullscreen);
    }
    dri = GetScreenDrawInfo(scr);
    if (!font && !load_font()) {        /* the bitmap font is always there */
        DBGS("open_display: no font\n");
        return FALSE;
    }
    if (usett && !ttface[0] && !load_tt())
        usett = FALSE;
    if (!usett) {
        bitmap_metrics();
    }

    if (fullscreen) {
        win = OpenWindowTags(NULL,
            WA_Left, 0, WA_Top, 0, WA_Width, scr->Width,
            WA_Height, scr->Height, WA_CustomScreen, (ULONG)scr,
            WA_Backdrop, TRUE, WA_Borderless, TRUE,
            WA_SimpleRefresh, TRUE, WA_Activate, TRUE,
            WA_NewLookMenus, TRUE, WA_IDCMP,
            idcmp | IDCMP_REFRESHWINDOW, TAG_END);  /* simple refresh:
                                         * a closed window's place is ours
                                         * to draw again, or stays grey */
    } else {
        int top0 = scr->BarHeight + 1;
        /* the window for want_cols x want_rows cells, frame included */
        int w = want_cols * cw + 4 + scr->WBorLeft + 22 + 2,
            h = want_rows * ch + 2 + scr->WBorTop + scr->Font->ta_YSize + 1 +
                scr->WBorBottom + 2;
        int l = 0, t = top0;
        if (dialhost[0]) {              /* a call: beside the Shell's window */
            l = 32;
            t = top0 + 24;
        }
        if (remember && wl >= 0 && scr != ownscr) {
            l = wl; t = wt; w = ww; h = wh;     /* as it was left */
        }
        if (hid_l >= 0) {               /* back from hiding: as it was */
            l = hid_l; t = hid_t; w = hid_w; h = hid_h;
        }
        if (w > scr->Width) w = scr->Width;
        if (h > scr->Height - top0) h = scr->Height - top0;
        if (l + w > scr->Width) l = scr->Width - w;
        if (t + h > scr->Height) t = scr->Height - h;
        if (l < 0) l = 0;
        if (t < top0) t = top0;
        win = OpenWindowTags(NULL,
            WA_Left, l, WA_Top, t, WA_Width, w, WA_Height, h,
            WA_Title, (ULONG)(dialhost[0] ? dialtitle : "CTerm"),
            scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
            WA_IconifyGadget, TRUE,     /* 3.2: CLOSEWINDOW, Code 1 - hide */
            WA_SizeGadget, TRUE, WA_SizeBRight, scrollbar,
            WA_SizeBBottom, !scrollbar,
            WA_SimpleRefresh, TRUE, WA_Activate, TRUE,
            WA_NewLookMenus, TRUE, WA_MinWidth, 200, WA_MinHeight, 80,
            WA_MaxWidth, -1, WA_MaxHeight, -1, WA_IDCMP,
            idcmp | IDCMP_REFRESHWINDOW, TAG_END);
    }
    if (!win) {
        DBGS("open_display: no window\n");
        return FALSE;
    }
    rp = win->RPort;
    /* every plane, as a new window has: the mask kept is the last
     * screen's (a 16-colour one's narrows a graphics card's colours) */
    planes_full();
    SetFont(rp, font);
    tt_attach();
    make_menus();
    if (!fullscreen && scrollbar)
        add_scroller();
    layout_and_resize();
    painter_setup();
    redraw_all();
    return TRUE;
}

/* keep_screen: TRUE keeps the own screen, FALSE closes it (asking about
 * other programs' windows on it), SCREEN_IF_FREE closes it only if it
 * can be at once (hiding). */
/* About: a requester of its own that does not stop CTerm (Build-
 * EasyRequest, answered in the main loop), not text in the terminal. */
static struct Window *aboutwin;
static char abouttext[640];

void about_open(void)
{
    static struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)"About CTerm",
        (UBYTE *)"%s", (UBYTE *)"OK"
    };
    APTR arg = abouttext;
    if (aboutwin) {
        WindowToFront(aboutwin);
        return;
    }
    strcpy(abouttext, version + 6);     /* past "$VER: " */
    strcat(abouttext, "\n"
        "A VT100/xterm terminal for AmigaOS 3.2\n"
        "\n"
        "File transfers: Zmodem, Ymodem, Xmodem, and the HYDRA file\n"
        "transfer protocol (designed by Arjen G. Lentz and Joaquim H.\n"
        "Homrighausen; HydraCom's code by Arjen G. Lentz, LENTZ\n"
        "SOFTWARE-DEVELOPMENT - Hydra and HydraCom are free).");
    if (rexx_port()[0]) {
        strcat(abouttext, "\n\nARexx port: ");
        strcat(abouttext, rexx_port());
    }
    aboutwin = BuildEasyRequestArgs(win, &es, 0, &arg);
    if (aboutwin == (struct Window *)1)
        aboutwin = NULL;                /* (answered at once: no window) */
    if (!aboutwin)
        beep();
}

ULONG about_sigmask(void)
{
    return aboutwin ? 1UL << aboutwin->UserPort->mp_SigBit : 0;
}

void about_events(void)
{
    if (aboutwin && SysReqHandler(aboutwin, NULL, FALSE) != -2) {
        FreeSysRequest(aboutwin);       /* OK, or closed */
        aboutwin = NULL;
    }
}

void about_close(void)
{
    if (aboutwin) {
        FreeSysRequest(aboutwin);
        aboutwin = NULL;
    }
}

void close_display(int keep_screen)
{
    about_close();                      /* its screen may be going */
    if (win && remember && !fullscreen && scr != ownscr) {
        wl = win->LeftEdge; wt = win->TopEdge;  /* kept for next time */
        ww = win->Width; wh = win->Height;
    }
    if (win) {
        if (menu) ClearMenuStrip(win);
        if (gadsok) RemoveGList(win, &vgad, -1);
        gadsok = FALSE;
        release_pens();
        if (TTEngineBase)
            TT_DoneRastPort(rp);
        cx_keys_on(FALSE);              /* its hot keys: none now */
        CloseWindow(win);
        win = NULL;
    }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (visual) { FreeVisualInfo(visual); visual = NULL; }
    if (iup) { DisposeObject(iup); iup = NULL; }
    if (idn) { DisposeObject(idn); idn = NULL; }
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    if (scr && scr != ownscr)
        UnlockPubScreen(NULL, scr);
    scr = NULL;
    if (keep_screen != TRUE)
        close_own_screen(keep_screen == FALSE);
}

void switch_display(void)
{
    if (setwin)
        close_settings();               /* its screen may be going */
    xwin_shut();
    bookwin_close();
    BOOL keep = want_mode == SCR_OWN && scrmode == SCR_OWN &&
                want_depth == owndepth;
    want_switch = FALSE;
    close_display(keep);
    scrmode = want_mode;
    owndepth = want_depth;
    fullscreen = want_full;
    scrollbar = want_bar;
    if (!open_display()) {
        close_display(FALSE);
        scrmode = SCR_WB;
        fullscreen = FALSE;
        open_display();
    }
    tell_sizes();
    save_prefs();
}

static void ask_switch(int mode, int depth, BOOL full, BOOL bar)
{
    want_mode = mode;
    want_depth = depth;
    want_full = mode == SCR_OWN && full;
    want_bar = bar;
    want_switch = TRUE;
}

BOOL menu_pick(ULONG id)
{
    switch (id) {
    case M_QUIT: return FALSE;
    case M_FONT: ask_font(); break;
    case M_TTFONT: ask_ttfont(); break;
    case M_BIGGER: tt_size(+2); break;
    case M_SMALLER: tt_size(-2); break;
    case M_SPEED: speed_test(); break;
    case M_CALL: call_dialog(); break;
    case M_BOOK: bookwin_open(); break;
    case M_SERIAL: serial_terminal(TRUE); break;
    case M_ANSWER: serial_answer(); break;
    case M_SFTP: files_menu(); break;
    case M_SETTINGS: open_settings(); break;
    case M_NEWTAB: tab_new_shell(); break;
    case M_CLOSETAB: tab_close_current(); break;
    case M_COPY: sel_copy(); break;
    case M_PASTE: paste(); break;
    case M_LOG: log_start(); break;
    case M_SENDUSER: send_login(FALSE); break;
    case M_SENDPASS: send_login(TRUE); break;
    case M_YRECV: case M_XRECV: case M_YSEND: case M_XSEND: case M_HSEND:
    case M_XWIN:
        transfer_menu((int)id);
        break;
    case M_LOGSTOP: log_stop(); break;
    case M_REXX: rexx_menu(); break;
    case M_SPLITH:
    case M_SPLITV:
        if (!pane_split(id == M_SPLITH ? 'h' : 'v'))
            beep();                     /* four already, or no room */
        break;
    case M_NEXTPANE: pane_next(); break;
    case M_PDWINDLE:
        pane_do(PO_DWINDLE, 0);
        save_prefs();
        break;
    case M_POPEN: case M_PKILL: case M_PFLEFT: case M_PFRIGHT: case M_PFUP:
    case M_PFDOWN: case M_PMLEFT: case M_PMRIGHT: case M_PMUP: case M_PMDOWN:
    case M_PSPLITH: case M_PSPLITV: case M_PFULL: case M_PTABS:
    case M_PPANES: {
        static const UBYTE op[] = { PO_OPEN, PO_KILL, PO_FOCUS, PO_FOCUS,
            PO_FOCUS, PO_FOCUS, PO_MOVE, PO_MOVE, PO_MOVE, PO_MOVE, PO_DIR,
            PO_DIR, PO_FULL, PO_TABS, PO_PANES };
        static const UBYTE arg[] = { 0, 1, PD_LEFT, PD_RIGHT, PD_UP,
            PD_DOWN, PD_LEFT, PD_RIGHT, PD_UP, PD_DOWN, 'h', 'v', 0, 0, 0 };
        if (!pane_do(op[id - M_POPEN], arg[id - M_POPEN]))
            beep();                     /* no pane that way, no room */
        break;
    }
    case M_CLOSEPANE:
        if (!pane_close())
            beep();
        break;
    case M_HIDE:
        if (!hide_ask())
            beep();                     /* Ed or More has the window */
        break;
    case M_ABOUT:
        about_open();
#ifdef PROFILE
        {   /* ms = ticks / TPMS */
            char l[200];
            ULONG all = ticks() - pf_all0;
            strcpy(l, "[profile ms: all ");
            numstr_cat(l, all / TPMS);
            strcat(l, ", waiting ");
            numstr_cat(l, pf_wait / TPMS);
            strcat(l, ", packets ");
            numstr_cat(l, pf_pkt / TPMS);
            strcat(l, " (emulator ");
            numstr_cat(l, pf_vt / TPMS);
            strcat(l, ", drawing ");
            numstr_cat(l, pf_draw / TPMS);
            strcat(l, " in ");
            numstr_cat(l, pf_draws);
            strcat(l, " draws); writes ");
            numstr_cat(l, pf_writes);
            strcat(l, "]\r\n");
            say(l);
            pf_vt = pf_draw = pf_pkt = pf_wait = pf_writes = pf_draws = 0;
            pf_all0 = ticks();
            refresh();
        }
#endif
        break;
    case M_UTF8:
        utf8 = pcmode || !utf8;         /* this tab's; the default is */
        pcmode = FALSE;                 /* Settings' (Text) */
        tab_letters(TRUE);
        vt_set_utf8(vt, utf8);
        painter_forget();
        redraw_all();
        break;
    case M_PC:
        pcmode = !pcmode;
        if (pcmode)
            vt_set_cp437(vt, 1);
        else
            vt_set_utf8(vt, utf8);
        tab_letters(TRUE);
        painter_forget();
        redraw_all();
        break;
    case M_BAR: ask_switch(scrmode, owndepth, fullscreen, !scrollbar); break;
    case M_FULL: ask_switch(scrmode, owndepth, !fullscreen, scrollbar); break;
    case M_SCR_WB: ask_switch(SCR_WB, owndepth, FALSE, scrollbar); break;
    case M_SCR_16: ask_switch(SCR_OWN, 4, fullscreen, scrollbar); break;
    case M_SCR_256: ask_switch(SCR_OWN, 8, fullscreen, scrollbar); break;
    case M_SCR_DEEP: ask_switch(SCR_OWN, 24, fullscreen, scrollbar); break;
    }
    return TRUE;
}
