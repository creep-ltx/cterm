/*
 * hide - the window put away while everything in it runs on, as tmux
 * and screen detach: the Shells, calls, transfers, logs and ARexx go on;
 * nothing is drawn. Back by the AppIcon on Workbench (a double-click),
 * Exchange's Show, ARexx's WINDOW SHOW, or starting CTerm again.
 *
 * Hiding closes the window (and CTerm's own screen) as a screen switch
 * does; the emulators keep taking text, and the window opens again at
 * its size and place, on the same kind of screen, every tab as it was.
 * Asked for anywhere (a menu, ARexx, Exchange, the iconify gadget), it
 * happens at the end of a round (loop_round), where no drawing or
 * window message is half done.
 *
 * The commodity broker is there all the time (Exchange lists CTerm and
 * its Show, Hide and Remove work); the AppIcon only while hidden, with
 * workbench.library and icon.library opened for it.
 */
#include "cterm.h"
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <libraries/commodities.h>
#include <proto/wb.h>
#include <proto/icon.h>
#include <proto/commodities.h>

struct Library *WorkbenchBase = NULL, *IconBase = NULL, *CxBase = NULL;

BOOL hidden;                            /* the window is put away */
static BOOL want_hide, want_show;       /* asked for: done at a round's end */
LONG hid_l = -1, hid_t, hid_w, hid_h;   /* the window, to come back to */

static struct MsgPort *aport;           /* the AppIcon's messages */
static struct AppIcon *aicon;
static struct DiskObject *adobj;
static struct MsgPort *cxport;          /* Exchange's */
static CxObj *broker;
static char cxname[CBD_NAMELEN];
static BOOL hkon;                       /* the hot keys on (below) */

/* A beep on CTerm's screen; none while hidden (no screen to flash -
 * DisplayBeep(NULL) flashes every one). */
void beep(void)
{
    if (scr && !hidden)
        DisplayBeep(scr);
}

/* ---- the AppIcon ---- */

static BOOL appicon_add(void)
{
    char path[256];
    if (!WorkbenchBase)
        WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 37);
    if (!IconBase)
        IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (!WorkbenchBase || !IconBase)
        return FALSE;
    if (!aport && !(aport = CreateMsgPort()))
        return FALSE;
    own_path(path, sizeof path, "CTerm");   /* CTerm's own icon, if there */
    adobj = GetDiskObject((STRPTR)path);
    if (!adobj)
        adobj = GetDefDiskObject(WBTOOL);
    if (!adobj)
        return FALSE;
    adobj->do_CurrentX = adobj->do_CurrentY = NO_ICON_POSITION;
    aicon = AddAppIcon(0, 0, (STRPTR)(cxname[0] ? cxname : "CTerm"), aport, 0, adobj, WBAPPICONA_SupportsOpen, TRUE,
                       TAG_END);
    if (!aicon) {
        FreeDiskObject(adobj);
        adobj = NULL;
    }
    return aicon != NULL;
}

static void appicon_remove(void)
{
    struct Message *m;
    if (aicon)
        RemoveAppIcon(aicon);
    aicon = NULL;
    if (adobj)
        FreeDiskObject(adobj);
    adobj = NULL;
    if (aport) {
        while ((m = GetMsg(aport)))
            ReplyMsg(m);                /* a double-click on its way */
        DeleteMsgPort(aport);
        aport = NULL;
    }
    if (IconBase)
        CloseLibrary(IconBase);
    if (WorkbenchBase)
        CloseLibrary(WorkbenchBase);
    IconBase = WorkbenchBase = NULL;
}

/* ---- the commodity: Exchange's Show, Hide, Remove ---- */

void cx_open(void)
{
    struct NewBroker nb;
    const char *p = rexx_port();
    LONG err;
    if (!(CxBase = OpenLibrary((STRPTR)"commodities.library", 37)))
        return;
    if (!(cxport = CreateMsgPort())) {
        cx_close();
        return;
    }
    /* "CTerm", "CTerm.2"... as the ARexx port: each its own line */
    strcpy(cxname, "CTerm");
    if (p[0] && strchr(p, '.'))
        strcat(cxname, strchr(p, '.'));
    memset(&nb, 0, sizeof nb);
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)cxname;
    nb.nb_Title = (STRPTR)"CTerm";
    nb.nb_Descr = (STRPTR)"Terminal: Amiga Shell, telnet, ssh";
    nb.nb_Unique = NBU_DUPLICATE;
    nb.nb_Flags = COF_SHOW_HIDE;
    nb.nb_Port = cxport;
    if (!(broker = CxBroker(&nb, &err))) {
        cx_close();
        return;
    }
    ActivateCxObj(broker, TRUE);
    hkon = win && (win->Flags & WFLG_WINDOWACTIVE);
    cx_keys();                          /* (the window came first) */
}

/* ---- the hot keys Intuition takes ----
 * Amiga + an arrow moves the mouse pointer: Intuition takes the press
 * and only the release reaches a window (seen on 3.2). Such keys bound
 * in [window] or [panes] (Left Amiga + the arrows, i3's focus and move)
 * are filters of the broker, ahead of Intuition: on while CTerm's window
 * is active, else off (the pointer keys work elsewhere as ever); each
 * sends its key here (hide_events -> key_action) and is swallowed. */
#define MAXHK 16
static CxObj *hk[MAXHK];
static int nhk;

void cx_keys_on(BOOL on)
{
    int i;
    hkon = on;
    for (i = 0; i < nhk; i++)
        ActivateCxObj(hk[i], on);
}

void cx_keys(void)
{
    static const UWORD arrows[4] = { KM_UP, KM_DOWN, KM_RIGHT, KM_LEFT };
    int i, j;
    while (nhk > 0) {
        RemoveCxObj(hk[--nhk]);
        DeleteCxObjAll(hk[nhk]);
    }
    if (!broker)
        return;
    for (i = 0; i < keys_n && nhk < MAXHK; i++) {
        UWORD k = keys_binds[i].key;
        int c = keys_acts[keys_binds[i].act].ctx;
        IX ix;
        CxObj *f;
        if ((c != KC_WINDOW && c != KC_PANES) || !(k & (KQ_L | KQ_A)))
            continue;
        for (j = 0; j < 4 && (k & KQ_KEY) != arrows[j]; j++)
            ;
        if (j == 4)
            continue;
        memset(&ix, 0, sizeof ix);
        ix.ix_Version = IX_VERSION;
        ix.ix_Class = IECLASS_RAWKEY;
        ix.ix_Code = 0x4C + j;          /* the press only */
        ix.ix_CodeMask = 0xFF;
        ix.ix_Qualifier = (k & KQ_L ? IEQUALIFIER_LCOMMAND : 0) |
                          (k & KQ_A ? IEQUALIFIER_RCOMMAND : 0) |
                          /* a synonym (IXSYM_*) counts as both keys */
                          (k & KQ_S ? IEQUALIFIER_LSHIFT |
                                      IEQUALIFIER_RSHIFT : 0) |
                          (k & KQ_C ? IEQUALIFIER_CONTROL : 0) |
                          (k & KQ_M ? IEQUALIFIER_LALT | IEQUALIFIER_RALT
                                    : 0);
        ix.ix_QualMask = IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND |
                         IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT |
                         IEQUALIFIER_CONTROL | IEQUALIFIER_LALT |
                         IEQUALIFIER_RALT;
        ix.ix_QualSame = IXSYM_SHIFT | IXSYM_ALT;
        if (!(f = CxFilter(NULL)))
            break;
        SetFilterIX(f, &ix);
        AttachCxObj(f, CxSender(cxport, k));
        AttachCxObj(f, CxTranslate(NULL));  /* swallowed */
        if (CxObjError(f)) {
            DeleteCxObjAll(f);
            continue;
        }
        ActivateCxObj(f, hkon);
        AttachCxObj(broker, f);
        hk[nhk++] = f;
    }
}

void cx_close(void)
{
    struct Message *m;
    nhk = 0;                            /* (they go with the broker) */
    if (broker)
        DeleteCxObjAll(broker);
    broker = NULL;
    if (cxport) {
        while ((m = GetMsg(cxport)))
            ReplyMsg(m);
        DeleteMsgPort(cxport);
        cxport = NULL;
    }
    if (CxBase)
        CloseLibrary(CxBase);
    CxBase = NULL;
}

/* ---- asking ---- */

/* Hide, when it may be: not while a program has a Shell's console raw
 * (Ed and More keep the window's pointer, as for a screen switch). */
BOOL hide_ask(void)
{
    int i;
    for (i = 0; i < ntabs; i++)
        if (con_raw(tabs[i]->con))
            return FALSE;
    want_show = FALSE;
    want_hide = !hidden;
    return TRUE;
}

void show_ask(void)
{
    want_hide = FALSE;
    want_show = hidden;
}

/* What ARexx's WINDOW STATE says: hidden, or about to be. */
BOOL hide_state(void)
{
    return (hidden && !want_show) || want_hide;
}

ULONG hide_sigmask(void)
{
    return (aport ? 1UL << aport->mp_SigBit : 0) |
           (cxport ? 1UL << cxport->mp_SigBit : 0);
}

/* The AppIcon's and Exchange's messages: safe anywhere (they ask). */
void hide_events(void)
{
    struct Message *m;
    while (aport && (m = GetMsg(aport))) {
        /* a double-click, its menu's Open, icons dropped on it: back */
        ReplyMsg(m);
        show_ask();
    }
    while (cxport && (m = GetMsg(cxport))) {
        ULONG type = CxMsgType((CxMsg *)m), id = CxMsgID((CxMsg *)m);
        ReplyMsg(m);
        if (type == CXM_IEVENT) {       /* a hot key: CTerm's window's */
            if (hkon && win && (win->Flags & WFLG_WINDOWACTIVE) &&
                !key_action((UWORD)id))
                quit_all();             /* its key was Quit's */
            continue;
        }
        if (type != CXM_COMMAND)
            continue;
        switch (id) {
        case CXCMD_DISABLE: ActivateCxObj(broker, FALSE); break;
        case CXCMD_ENABLE: ActivateCxObj(broker, TRUE); break;
        case CXCMD_APPEAR:
        case CXCMD_UNIQUE: show_ask(); break;
        case CXCMD_DISAPPEAR:
            if (!hide_ask())
                beep();
            break;
        case CXCMD_KILL:
            quit_all();
            break;
        }
    }
}

/* ---- doing it ---- */

static void do_hide(void)
{
    struct Window *old = win;
    if (!hide_ask()) {
        beep();
        return;
    }
    if (!appicon_add() && !broker) {
        /* no AppIcon (no Workbench?) and no Exchange: no way back */
        appicon_remove();
        beep();
        return;
    }
    close_settings();                   /* their screen may be going */
    xwin_shut();
    bookwin_close();
    if (!fullscreen) {
        hid_l = win->LeftEdge; hid_t = win->TopEdge;
        hid_w = win->Width; hid_h = win->Height;
    } else
        hid_l = -1;
    con_window_moved(old, NULL);        /* requesters to the default screen */
    hidden = TRUE;
    close_display(SCREEN_IF_FREE);      /* the own screen too, if free */
}

static void do_show(void)
{
    int i;
    hidden = FALSE;
    if (!open_display()) {
        close_display(FALSE);
        screen_fallback();
        if (!open_display()) {
            close_display(FALSE);
            hidden = TRUE;              /* no window: the AppIcon stays */
            return;
        }
    }
    hid_l = -1;
    appicon_remove();
    {
        struct Con *cs[MAXTABS];
        for (i = 0; i < ntabs; i++)
            cs[i] = tabs[i]->con;
        con_window_back(win, cs, ntabs);
    }
    for (i = 0; i < ntabs && tabs[i] != shown; i++)
        ;
    tab_show(i < ntabs ? i : 0);        /* its letters, the layout, the text */
    tell_sizes();                       /* a tab bar may have come meanwhile */
    ScreenToFront(scr);
}

/* At the end of a round: what was asked. */
void hide_show(void)
{
    if (want_hide && win && !hidden)
        do_hide();
    else if (want_show && hidden)
        do_show();
    want_hide = want_show = FALSE;
}

/* At the end: the AppIcon, the broker. */
void hide_free(void)
{
    appicon_remove();
    cx_close();
}
