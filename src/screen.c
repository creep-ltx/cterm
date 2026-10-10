/*
 * screen - screens, windows and menus.
 */
#include "cterm.h"

static void make_menus(void);
static void make_menus(void);
static BOOL open_own_screen(void);
static void close_own_screen(void);
static void ask_switch(int mode, int depth, BOOL full, BOOL bar);

static struct NewMenu newmenu[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"New Shell tab", (STRPTR)"T", 0, 0, (APTR)M_NEWTAB },
    {  NM_ITEM, (STRPTR)"Call...", (STRPTR)"K", 0, 0, (APTR)M_CALL },
    {  NM_ITEM, (STRPTR)"Close tab", (STRPTR)"W", 0, 0, (APTR)M_CLOSETAB },
    {  NM_ITEM, (STRPTR)"Start log...", (STRPTR)"L", 0, 0, (APTR)M_LOG },
    {  NM_ITEM, (STRPTR)"Stop log", 0, 0, 0, (APTR)M_LOGSTOP },
    {  NM_ITEM, (STRPTR)"Send user name", (STRPTR)"U", 0, 0,
       (APTR)M_SENDUSER },
    {  NM_ITEM, (STRPTR)"Send password", (STRPTR)"P", 0, 0,
       (APTR)M_SENDPASS },
    {  NM_ITEM, (STRPTR)"About", 0, 0, 0, (APTR)M_ABOUT },
    {  NM_ITEM, (STRPTR)"Speed test", 0, 0, 0, (APTR)M_SPEED },
    {  NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Copy", (STRPTR)"C", 0, 0, (APTR)M_COPY },
    {  NM_ITEM, (STRPTR)"Paste", (STRPTR)"V", 0, 0, (APTR)M_PASTE },
    { NM_TITLE, (STRPTR)"Transfer", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Receive with Ymodem", 0, 0, 0, (APTR)M_YRECV },
    {  NM_ITEM, (STRPTR)"Receive with Xmodem...", 0, 0, 0, (APTR)M_XRECV },
    {  NM_ITEM, (STRPTR)"Send with Ymodem...", 0, 0, 0, (APTR)M_YSEND },
    {  NM_ITEM, (STRPTR)"Send with Xmodem...", 0, 0, 0, (APTR)M_XSEND },
    {  NM_ITEM, (STRPTR)"Send with Hydra...", 0, 0, 0, (APTR)M_HSEND },
    { NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Settings...", (STRPTR)"S", 0, 0, (APTR)M_SETTINGS },
    {  NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
    {  NM_ITEM, (STRPTR)"Amiga font...", 0, 0, 0, (APTR)M_FONT },
    {  NM_ITEM, (STRPTR)"TrueType font...", 0, 0, 0,
       (APTR)M_TTFONT },
    {  NM_ITEM, (STRPTR)"Bigger text", (STRPTR)"+", 0, 0, (APTR)M_BIGGER },
    {  NM_ITEM, (STRPTR)"Smaller text", (STRPTR)"-", 0, 0, (APTR)M_SMALLER },
    {  NM_ITEM, (STRPTR)"Screen", 0, 0, 0, 0 },
    {   NM_SUB, (STRPTR)"Workbench window", 0, CHECKIT, ~1, (APTR)M_SCR_WB },
    {   NM_SUB, (STRPTR)"Own screen, 16 colours", 0, CHECKIT, ~2,
        (APTR)M_SCR_16 },
    {   NM_SUB, (STRPTR)"Own screen, 256 colours", 0, CHECKIT, ~4,
        (APTR)M_SCR_256 },
    {   NM_SUB, (STRPTR)"Own screen, like Workbench", 0, CHECKIT, ~8,
        (APTR)M_SCR_DEEP },
    {  NM_ITEM, (STRPTR)"Full screen", (STRPTR)"F", CHECKIT | MENUTOGGLE, 0,
       (APTR)M_FULL },
    {  NM_ITEM, (STRPTR)"Scroll bar", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_BAR },
    {  NM_ITEM, (STRPTR)"UTF-8", 0, CHECKIT | MENUTOGGLE, 0, (APTR)M_UTF8 },
    {  NM_ITEM, (STRPTR)"PC BBS (CP437)", 0, CHECKIT | MENUTOGGLE, 0,
       (APTR)M_PC },
    { NM_END, 0, 0, 0, 0, 0 },
};
struct Window *setwin;

/* The menus again: their keys changed. */
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

static void make_menus(void)
{
    struct NewMenu *n;
    for (n = newmenu; n->nm_Type != NM_END; n++) {
        ULONG id = (ULONG)n->nm_UserData;
        BOOL on;
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
        case M_SENDUSER:                /* their keys are the person's */
            n->nm_CommKey = (STRPTR)(userkey[0] && userkey[0] != '-'
                                     ? userkey : NULL);
            continue;
        case M_SENDPASS:
            n->nm_CommKey = (STRPTR)(passkey[0] && passkey[0] != '-'
                                     ? passkey : NULL);
            continue;
        default: continue;
        }
        if (on) n->nm_Flags |= CHECKED;
        else n->nm_Flags &= ~CHECKED;
    }
    visual = GetVisualInfo(scr, TAG_END);
    if (visual && (menu = CreateMenus(newmenu, TAG_END)) &&
        LayoutMenus(menu, visual, GTMN_NewLookMenus, TRUE, TAG_END))
        SetMenuStrip(win, menu);
}

static BOOL open_own_screen(void)
{
    struct Screen *wb = LockPubScreen(NULL);
    ULONG id = INVALID_ID;
    int depth = owndepth;
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
    ownscr = OpenScreenTags(NULL,
        SA_DisplayID, id, SA_Depth, depth,
        SA_Width, ownmode != INVALID_ID && ownw > 0 ? ownw : STDSCREENWIDTH,
        SA_Height, ownmode != INVALID_ID && ownh > 0 ? ownh : STDSCREENHEIGHT,
        SA_Title, (ULONG)"CTerm", SA_PubName, (ULONG)"CTERM",
        SA_Type, PUBLICSCREEN, SA_Pens, (ULONG)pens, SA_SysFont, 1,
        SA_SharePens, TRUE, SA_Interleaved, TRUE,
        SA_ShowTitle, !fullscreen, TAG_END);
    if (!ownscr)
        return FALSE;
    PubScreenStatus(ownscr, 0);
    return TRUE;
}

static void close_own_screen(void)
{
    if (ownscr && (PubScreenStatus(ownscr, PSNF_PRIVATE) & PSNF_PRIVATE)) {
        CloseScreen(ownscr);
        ownscr = NULL;
    }
}

/* ======================================================================
 * Settings: ENV:CTerm.prefs (and ENVARC:), one "key=value" per line.
 * ==================================================================== */


BOOL open_display(void)
{
    ULONG idcmp = IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_RAWKEY |
                  IDCMP_GADGETDOWN | IDCMP_GADGETUP | IDCMP_MOUSEMOVE |
                  IDCMP_INTUITICKS | IDCMP_MENUPICK | IDCMP_ACTIVEWINDOW |
                  IDCMP_INACTIVEWINDOW | IDCMP_MOUSEBUTTONS;
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
            WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_END);
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

void close_display(BOOL keep_screen)
{
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
    if (!keep_screen)
        close_own_screen();
}

void switch_display(void)
{
    if (setwin)
        close_settings();               /* its screen may be going */
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
    if (sock >= 0)
        send_naws();
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
    case M_SETTINGS: open_settings(); break;
    case M_NEWTAB: tab_new_shell(); break;
    case M_CLOSETAB: tab_close_current(); break;
    case M_COPY: sel_copy(); break;
    case M_PASTE: paste(); break;
    case M_LOG: log_start(); break;
    case M_SENDUSER: send_login(FALSE); break;
    case M_SENDPASS: send_login(TRUE); break;
    case M_YRECV: case M_XRECV: case M_YSEND: case M_XSEND: case M_HSEND:
        transfer_menu((int)id);
        break;
    case M_LOGSTOP: log_stop(); break;
    case M_ABOUT:
        say("\r\n\x1b[1mCTerm\x1b[0m - a VT100/xterm terminal for the "
            "Amiga.\r\n"
            "File transfers: Zmodem, Ymodem, Xmodem, and the HYDRA file "
            "transfer protocol\r\n"
            "(designed by Arjen G. Lentz and Joaquim H. Homrighausen; "
            "HydraCom's code by\r\n"
            "Arjen G. Lentz, LENTZ SOFTWARE-DEVELOPMENT - Hydra and HydraCom "
            "are free).\r\n");
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
        utf8 = saved_utf8 = !utf8;
        pcmode = FALSE;
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
