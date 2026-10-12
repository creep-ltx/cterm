/*
 * callwin - Project/Call...: the Call window (GadTools), the recent
 * calls it remembers, and a call started from it. A part of its own
 * (PROGDIR:Parts/Call): loaded when the window opens, gone when it
 * closes.
 */
#include "cterm.h"

static int calls_split(void);
static void calls_add(const char *entry);
static void call_start(const char *host, BOOL pc, BOOL ssh);
static void call_parse(const char *entry, char *host, BOOL *pc, BOOL *ssh);
static void call_tab(const char *host, BOOL pc, BOOL ssh);

#define NCALLS 8

/* The recent calls as a list for the list view. */
static struct List calllist;
static struct Node callnode[NCALLS];
static char callname[NCALLS][64];

static int calls_split(void)
{
    const char *p = calls;
    int n = 0;
    calllist.lh_Head = (struct Node *)&calllist.lh_Tail;  /* NewList */
    calllist.lh_Tail = NULL;
    calllist.lh_TailPred = (struct Node *)&calllist.lh_Head;
    while (*p && n < NCALLS) {
        int k = 0;
        while (*p && *p != ';' && k < 63)
            callname[n][k++] = *p++;
        callname[n][k] = 0;
        while (*p && *p != ';')
            p++;
        if (*p == ';')
            p++;
        if (k) {
            callnode[n].ln_Name = callname[n];
            AddTail(&calllist, &callnode[n]);
            n++;
        }
    }
    return n;
}

/* host (with a ! when PC) to the front of the recent calls. */
static void calls_add(const char *entry)
{
    char out[sizeof calls];
    int i, n = calls_split();
    strcpy(out, entry);
    for (i = 0; i < n; i++) {
        if (!stricmp(callname[i], entry))
            continue;
        if (strlen(out) + strlen(callname[i]) + 2 >= sizeof out)
            break;
        strcat(out, ";");
        strcat(out, callname[i]);
    }
    strcpy(calls, out);
}

static void call_start(const char *host, BOOL pc, BOOL ssh)
{
    /* Run >NIL: "path" SSH "host" PC: 11 + 255 + 7 + 127 + 4, and 0 */
    char cmd[408], path[256];
    BPTR nil;
    own_path(path, sizeof path, ssh && !has_ssh ? "CTermSSH" : NULL);
    strcpy(cmd, "Run >NIL: \"");
    strcat(cmd, path);
    strcat(cmd, ssh ? "\" SSH \"" : "\" HOST \"");
    strcat(cmd, host);
    strcat(cmd, pc ? "\" PC" : "\"");
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                   TAG_END) != 0 && nil)
        Close(nil);
}

/* A recent call's text: "ssh:user@host", or "host" with "!" for a PC BBS. */
static void call_parse(const char *entry, char *host, BOOL *pc, BOOL *ssh)
{
    *ssh = !strncmp(entry, "ssh:", 4);
    strcpy(host, *ssh ? entry + 4 : entry);
    *pc = FALSE;
    if (host[0] && host[strlen(host) - 1] == '!') {
        host[strlen(host) - 1] = 0;
        *pc = TRUE;
    }
}

/* A call in a new tab of this window. */
static void call_tab(const char *host, BOOL pc, BOOL ssh)
{
    struct Tab *t = tab_new();
    if (!t) {
        call_start(host, pc, ssh);      /* no room: a window of its own */
        return;
    }
    T = t;
    local = FALSE;
    strncpy(dialhost, host, sizeof dialhost - 1);
    strcpy(dialtitle, ssh ? "CTerm: ssh " : "CTerm: ");
    strncat(dialtitle, dialhost, sizeof dialtitle - 20);
    sshmode = ssh;
    if (pc != pcmode) {                 /* the window's choice wins */
        pcmode = pc;
        if (pc)
            vt_set_cp437(vt, 1);
        else
            vt_set_utf8(vt, utf8 = saved_utf8);
    }
    tab_show(ntabs - 1);
    say("\x1b[2m[calling ");
    say(dialhost);
    say_key("... ", KA_STOP, " gives up]\x1b[0m\r\n");
    refresh();
    call_out();                         /* rings while the window goes on */
}

static void callwin_run(void)
{
    enum { G_HOST, G_PC, G_SSH, G_NEWTAB, G_LIST, G_CALL, G_SERIAL,
           G_CANCEL };
    struct Gadget *glist = NULL, *g, *gad[8];
    struct NewGadget ng;
    struct Window *dw;
    struct TextAttr *ta = scr->Font;
    int th = ta->ta_YSize, n, W, H, x0, y, bw;
    BOOL pc = pcmode, ssh = FALSE, done = FALSE, go = FALSE, line = FALSE;
    char host[128], booked[136];
    ULONG dsig;
    static struct Node booknode[BOOK_MAX];
    int nb, pick = -1, i;

    n = calls_split();
    host[0] = 0;
    if (n)                              /* the last place, ready to call */
        call_parse(callname[0], host, &pc, &ssh);
    /* the phone book first, then the recent calls */
    book_use();
    nb = nbook;
    for (i = nb - 1; i >= 0; i--) {
        booknode[i].ln_Name = book[i].name;
        AddHead(&calllist, &booknode[i]);
    }
    x0 = scr->WBorLeft + 8 + TextLength(&scr->RastPort, (STRPTR)"Recent", 6) + 8;
    W = x0 + 300 + scr->WBorRight + 8;
    bw = 90;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = ta;
    ng.ng_VisualInfo = visual;
    g = CreateContext(&glist);
    y = scr->WBorTop + th + 1 + 6;

    ng.ng_LeftEdge = x0; ng.ng_TopEdge = y; ng.ng_Width = 260;
    ng.ng_Height = th + 6; ng.ng_GadgetText = (STRPTR)"Host";
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = G_HOST;
    g = gad[G_HOST] = CreateGadget(STRING_KIND, g, &ng, GTST_String,
                                   (ULONG)host, GTST_MaxChars, 120, TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"PC BBS (CP437)"; ng.ng_Flags = PLACETEXT_RIGHT;
    ng.ng_GadgetID = G_PC;
    g = gad[G_PC] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, pc,
                                 GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0 + 150; ng.ng_GadgetText = (STRPTR)"ssh (user@host)";
    ng.ng_GadgetID = G_SSH;
    g = gad[G_SSH] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, ssh,
                                  GTCB_Scaled, TRUE, TAG_END);
    ng.ng_LeftEdge = x0;
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 26; ng.ng_Height = th + 4;
    ng.ng_GadgetText = (STRPTR)"in a new tab of this window";
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_GadgetID = G_NEWTAB;
    g = gad[G_NEWTAB] = CreateGadget(CHECKBOX_KIND, g, &ng,
                                     GTCB_Checked, call_newtab, GTCB_Scaled, TRUE,
                                     TAG_END);
    y += th + 10;
    ng.ng_TopEdge = y; ng.ng_Width = 260; ng.ng_Height = (th + 2) * 5 + 4;
    ng.ng_GadgetText = (STRPTR)(nb ? "Book" : "Recent");
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = G_LIST;
    g = gad[G_LIST] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels,
                                   (ULONG)&calllist, TAG_END);
    y += ng.ng_Height + 8;
    ng.ng_TopEdge = y; ng.ng_Width = bw; ng.ng_Height = th + 6;
    ng.ng_GadgetText = (STRPTR)"Call"; ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetID = G_CALL; ng.ng_LeftEdge = scr->WBorLeft + 8;
    g = gad[G_CALL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    /* the settings' serial line, typed to by hand (a modem's AT) */
    ng.ng_LeftEdge = (W - 130) / 2; ng.ng_Width = 130;
    ng.ng_GadgetText = (STRPTR)"Serial terminal"; ng.ng_GadgetID = G_SERIAL;
    g = gad[G_SERIAL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    ng.ng_Width = bw;
    ng.ng_LeftEdge = W - scr->WBorRight - 8 - bw;
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = G_CANCEL;
    g = gad[G_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_END);
    H = y + th + 6 + 6 + scr->WBorBottom;
    if (!g) {
        FreeGadgets(glist);
        return;
    }
    dw = OpenWindowTags(NULL,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
        WA_Width, W, WA_Height, H, WA_Title, (ULONG)"CTerm: Call",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_IDCMP, STRINGIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP | BUTTONIDCMP |
                  IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_END);
    if (!dw) {
        FreeGadgets(glist);
        return;
    }
    GT_RefreshWindow(dw, NULL);
    ActivateGadget(gad[G_HOST], dw, NULL);
    dsig = 1UL << dw->UserPort->mp_SigBit;
    while (!done) {
        struct IntuiMessage *m;
        ULONG sigs = Wait(dsig | shells_sigmask());
        if (sigs & ~dsig) {
            /* the Shells go on meanwhile */
            shells_handle(sigs);
            if (con_render_due() && owed)
                draw_now();
        }
        while ((m = GT_GetIMsg(dw->UserPort))) {
            ULONG cl = m->Class;
            UWORD code = m->Code;
            struct Gadget *src = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            switch (cl) {
            case IDCMP_CLOSEWINDOW: done = TRUE; break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(dw);
                GT_EndRefresh(dw, TRUE);
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27) done = TRUE;
                break;
            case IDCMP_GADGETUP:
                switch (src->GadgetID) {
                case G_HOST:            /* Return in the host field */
                    if (code != 9) go = done = TRUE;
                    break;
                case G_PC: pc = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_SSH: ssh = (src->Flags & GFLG_SELECTED) != 0; break;
                case G_NEWTAB:
                    call_newtab = (src->Flags & GFLG_SELECTED) != 0;
                    break;
                case G_LIST:
                    pick = -1;
                    if (code < nb) {        /* a phone book entry */
                        const BookEntry *e = &book[code];
                        pick = code;
                        book_target(e, host, sizeof host);
                        strcpy(booked, host);
                        pc = e->chars == BC_PC;
                        ssh = e->ssh;
                        GT_SetGadgetAttrs(gad[G_HOST], dw, NULL,
                                          GTST_String, (ULONG)host, TAG_END);
                        GT_SetGadgetAttrs(gad[G_PC], dw, NULL,
                                          GTCB_Checked, pc, TAG_END);
                        GT_SetGadgetAttrs(gad[G_SSH], dw, NULL,
                                          GTCB_Checked, ssh, TAG_END);
                    } else if ((code -= nb) < n) {
                        call_parse(callname[code], host, &pc, &ssh);
                        GT_SetGadgetAttrs(gad[G_HOST], dw, NULL,
                                          GTST_String, (ULONG)host, TAG_END);
                        GT_SetGadgetAttrs(gad[G_PC], dw, NULL,
                                          GTCB_Checked, pc, TAG_END);
                        GT_SetGadgetAttrs(gad[G_SSH], dw, NULL,
                                          GTCB_Checked, ssh, TAG_END);
                    }
                    break;
                case G_CALL: go = done = TRUE; break;
                case G_SERIAL: line = done = TRUE; break;
                case G_CANCEL: done = TRUE; break;
                }
                break;
            }
        }
    }
    if (go) {
        STRPTR s0 = ((struct StringInfo *)gad[G_HOST]->SpecialInfo)->Buffer;
        strncpy(host, (char *)s0, sizeof host - 2);
        host[sizeof host - 2] = 0;
    }
    CloseWindow(dw);
    FreeGadgets(glist);
    if (line) {
        serial_terminal(call_newtab);
        return;
    }
    if (go && pick >= 0 && !strcmp(host, booked)) {
        call_book(pick, call_newtab);        /* as the entry says, all of it */
        return;
    }
    if (go && host[0]) {
        char entry[136], *at, *pw;
        strcpy(entry, ssh ? "ssh:" : "");
        strcat(entry, host);
        /* a URL's password is not remembered with the recent calls */
        if ((pw = strstr(entry, "://")) && (at = strrchr(pw, '@')) &&
            (pw = strchr(pw + 3, ':')) && pw < at)
            memmove(pw, at, strlen(at) + 1);
        if (pc)
            strcat(entry, "!");
        calls_add(entry);
        save_prefs();
        if (strstr(host, "://")) {      /* ftp://, sftp://: the browser */
            BookEntry *e = AllocVec(sizeof *e, MEMF_ANY);
            if (e) {
                book_url(e, host, FK_NONE);
                call_place(e, call_newtab);
                FreeVec(e);
            }
            return;
        }
        if (call_newtab && (has_ssh || !ssh))  /* ssh in CTerm: CTermSSH's,
                                                  * a window */
            call_tab(host, pc, ssh);
        else
            call_start(host, pc, ssh);
    }
}

APTR part_main(void)
{
    static struct CallPart p = { callwin_run };
    return &p;
}
