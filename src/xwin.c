/*
 * xwin - Transfer/Show transfers...: a window listing every transfer
 * with its bars and a Stop for each (ReAction). A part of its own
 * (PROGDIR:Parts/Transfers): loaded when the window opens, gone when it
 * closes. Where it was is CTerm's (xwleft, xwtop), kept for the next.
 */
#include "cterm.h"

/* ---- the Transfers window (Transfer/Show transfers...) ---- */

#define XW 4                            /* transfers it shows */
enum { XW_STOP = 1 };
static Object *xwobj, *xwlab[XW], *xwfile[XW], *xwall[XW], *xwstop[XW];
static struct Window *xwwin;
static int xwslots;                     /* the transfers it has room for */
struct Library *FuelGaugeBase = NULL;   /* the part's own */

static ULONG xw_sigmask(void)
{
    return xwwin ? 1UL << xwwin->UserPort->mp_SigBit : 0;
}

static void xw_close(void)
{
    if (xwwin) {
        xwleft = xwwin->LeftEdge;
        xwtop = xwwin->TopEdge;
    }
    if (xwobj)
        DisposeObject(xwobj);
    xwobj = NULL;
    xwwin = NULL;
}

/* The transfers, in order of their tabs: the ones the window shows. */
static int xlist(struct Tab **out)
{
    int i, n = 0;
    for (i = 0; i < ntabs && n < XW; i++)
        if (tabs[i]->t_xfer)
            out[n++] = tabs[i];
    return n;
}

static void xset(Object *o, ULONG tag, ULONG val)
{
    if (o && xwwin &&
        SetGadgetAttrs((struct Gadget *)o, xwwin, NULL, tag, val, TAG_END))
        RefreshGList((struct Gadget *)o, xwwin, NULL, 1);
}

static void xwin_open_(void);

static void xw_update(void)
{
    struct Tab *t[XW];
    int i, n;
    static char lab[XW][160];
    if (!xwwin)
        return;
    n = xlist(t);
    if ((n > 1 ? n : 1) != xwslots) {   /* a transfer came or went */
        xw_close();
        deep_call(xwin_open_);
        if (!xwwin)
            return;
    }
    for (i = 0; i < xwslots; i++) {
        struct Xfer *x = i < n ? t[i]->t_xfer : NULL;
        if (x) {
            char nm[64];
            tab_label(t[i], nm);
            strcpy(lab[i], nm);
            strcat(lab[i], ": ");
            strcat(lab[i], x->dir == 2 ? "sending " : "receiving ");
            strncat(lab[i], x->name[0] ? x->name : "...", 60);
        } else
            strcpy(lab[i], i == 0 && !n ? "No transfers now." : "");
        xset(xwlab[i], GA_Text, (ULONG)lab[i]);
        xset(xwfile[i], FUELGAUGE_Level,
             x ? pct100(x->done, x->size) : 0);
        xset(xwall[i], FUELGAUGE_Level,
             x ? pct100(x->bdone, x->bsize) : 0);
        xset(xwstop[i], GA_Disabled, !x);
    }
}

static void xwin_open_(void)
{
    Object *root;
    int i;
    if (xwwin) {
        WindowToFront(xwwin);
        return;
    }
    if (!reaction_open())
        return;
    if (!FuelGaugeBase &&
        !(FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 45)))
        return;
    {
        struct Tab *t[XW];
        int n = xlist(t);
        xwslots = n > 1 ? n : 1;        /* one row per transfer */
    }
    memset(xwlab, 0, sizeof xwlab);
    memset(xwfile, 0, sizeof xwfile);
    memset(xwall, 0, sizeof xwall);
    memset(xwstop, 0, sizeof xwstop);
    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    for (i = 0; i < xwslots && root; i++) {
        Object *g = VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_SpaceOuter, TRUE, End, *h;
        xwlab[i] = ButtonObject, GA_ReadOnly, TRUE, GA_Text, (ULONG)"",
            BUTTON_BevelStyle, BVS_NONE, BUTTON_Justification, BCJ_LEFT, End;
        xwfile[i] = FuelGaugeObject, FUELGAUGE_Min, 0, FUELGAUGE_Max, 100,
            FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE, End;
        xwall[i] = FuelGaugeObject, FUELGAUGE_Min, 0, FUELGAUGE_Max, 100,
            FUELGAUGE_Level, 0, FUELGAUGE_Percent, TRUE, End;
        xwstop[i] = ButtonObject, GA_ID, XW_STOP + i, GA_RelVerify, TRUE,
            GA_Text, (ULONG)"Stop", GA_Disabled, TRUE, End;
        if (!g)
            continue;
        SetAttrs(g, LAYOUT_AddChild, (ULONG)xwlab[i], TAG_END);
        h = HLayoutObject, End;
        if (h) {
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwfile[i], CHILD_Label,
                     (ULONG)(LabelObject, LABEL_Text, (ULONG)"File",
                             LabelEnd), TAG_END);
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwall[i], CHILD_Label,
                     (ULONG)(LabelObject, LABEL_Text, (ULONG)"All",
                             LabelEnd), TAG_END);
            SetAttrs(h, LAYOUT_AddChild, (ULONG)xwstop[i],
                     CHILD_WeightedWidth, 0, TAG_END);
            SetAttrs(g, LAYOUT_AddChild, (ULONG)h, TAG_END);
        }
        SetAttrs(root, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 TAG_END);
    }
    if (!root)
        return;
    xwobj = WindowObject, WA_Title, (ULONG)"CTerm Transfers",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_Activate, FALSE, WA_InnerWidth, 360,
        xwleft >= 0 ? WA_Left : TAG_IGNORE, xwleft,
        xwleft >= 0 ? WA_Top : TAG_IGNORE, xwtop,
        xwleft >= 0 ? TAG_IGNORE : WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, (ULONG)root, End;
    if (!xwobj) {
        DisposeObject(root);
        return;
    }
    xwwin = (struct Window *)DoMethod(xwobj, WM_OPEN);
    if (!xwwin)
        xw_close();
    else {
        int keep = xwslots;
        xw_update();                  /* (same count: no reopening) */
        (void)keep;
    }
}

static void xw_open(void) { deep_call(xwin_open_); }

static void xwin_events_(void)
{
    ULONG res;
    UWORD code;
    while (xwobj &&
           (res = DoMethod(xwobj, WM_HANDLEINPUT, (ULONG)&code)) !=
           WMHI_LASTMSG) {
        if ((res & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
            xw_close();
            return;
        }
        if ((res & WMHI_CLASSMASK) == WMHI_GADGETUP) {
            int id = res & WMHI_GADGETMASK;
            struct Tab *t[XW];
            int n = xlist(t);
            if (id >= XW_STOP && id < XW_STOP + n)
                t[id - XW_STOP]->t_xfer->stop = 1;
        }
    }
}

static void xw_events(void) { deep_call(xwin_events_); }

/* Before the screen goes, and at the end. */
static void xw_shut(void)
{
    xw_close();
    if (FuelGaugeBase) {
        CloseLibrary(FuelGaugeBase);
        FuelGaugeBase = NULL;
    }
}

APTR part_main(void)
{
    static struct XwinPart p = { xw_open, xw_update, xw_events,
                                 xw_shut, xw_sigmask };
    return &p;
}
