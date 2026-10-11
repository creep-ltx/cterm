/*
 * serwin - a serial line's settings in a small window of their own: the
 * device and unit, the speed, the framing (data bits, parity, stop
 * bits), flow control, the device's buffer, whether the speed is locked,
 * and for a phone book place its own init and dial prefix. Opened from
 * the phone book (Line...) and from Settings, Modem (the serial
 * terminal's line). It waits for OK or Cancel; the Shells go on.
 */
#include "cterm.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

enum { L_DEV = 1, L_UNIT, L_RATE, L_BAUD, L_BITS, L_PAR, L_STOP, L_FLOW,
       L_BUF, L_LOCK, L_INIT, L_DIAL, L_OK, L_CANCEL, L_N };

static Object *lg[L_N];
static struct Window *lw;

static STRPTR bitlabels[] = { (STRPTR)"8", (STRPTR)"7", NULL };
static STRPTR parlabels[] = { (STRPTR)"None", (STRPTR)"Even", (STRPTR)"Odd",
                              (STRPTR)"Mark", (STRPTR)"Space", NULL };
static STRPTR stoplabels[] = { (STRPTR)"1", (STRPTR)"2", NULL };
static STRPTR flowlabels[] = { (STRPTR)"None",
                               (STRPTR)"RTS/CTS (hardware)",
                               (STRPTR)"XON/XOFF (software)", NULL };
static STRPTR ratelabels[20];
static char ratetext[16][8];
static int nrates;

static void lset(int id, ULONG tag, ULONG val)
{
    if (lw && lg[id] &&
        SetGadgetAttrs((struct Gadget *)lg[id], lw, NULL, tag, val, TAG_END))
        RefreshGList((struct Gadget *)lg[id], lw, NULL, 1);
}

static ULONG lget(int id, ULONG tag)
{
    ULONG v = 0;
    if (lg[id])
        GetAttr(tag, lg[id], &v);
    return v;
}

/* The speed's place in the chooser; Other (the last) if not usual. */
static int rate_index(LONG baud)
{
    int i;
    for (i = 0; i < nrates; i++)
        if (ser_rates[i] == baud)
            return i;
    return nrates;
}

static Object *lab(const char *t)
{
    return LabelObject, LABEL_Text, (ULONG)t, LabelEnd;
}

static void add(Object *g, Object *child, const char *l)
{
    if (g && child)
        SetAttrs(g, LAYOUT_AddChild, (ULONG)child,
                 l ? CHILD_Label : TAG_IGNORE, l ? (ULONG)lab(l) : 0,
                 TAG_END);
}

static Object *choose(int id, STRPTR *l, int sel)
{
    return lg[id] = ChooserObject, GA_ID, id, GA_RelVerify, TRUE,
        CHOOSER_LabelArray, (ULONG)l, CHOOSER_Selected, sel,
        CHOOSER_DropDown, FALSE, End;
}

static Object *text(int id, const char *t, int max)
{
    return lg[id] = StringObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, STRINGA_TextVal, (ULONG)t, STRINGA_MaxChars, max,
        End;
}

static Object *number(int id, LONG n, LONG min, LONG max)
{
    return lg[id] = IntegerObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_TabCycle, TRUE, INTEGER_Number, n, INTEGER_Minimum, min,
        INTEGER_Maximum, max, INTEGER_Arrows, FALSE, End;
}

static Object *button(int id, const char *t)
{
    return lg[id] = ButtonObject, GA_ID, id, GA_RelVerify, TRUE,
        GA_Text, (ULONG)t, End;
}

/* Edit l (and a place's init and dial prefix, if given) in a window
 * over the window over. TRUE: OK, l is changed. */
BOOL serwin_edit(SerLine *l, char *init, char *dial, struct Window *over)
{
    Object *obj, *root, *g, *h;
    ULONG wsig = 0, res;
    UWORD code;
    int how = 0, i;
    if (!reaction_open()) {
        beep();
        return FALSE;
    }
    for (nrates = 0; ser_rates[nrates] && nrates < 16; nrates++) {
        ratetext[nrates][0] = 0;
        numstr_cat(ratetext[nrates], ser_rates[nrates]);
        ratelabels[nrates] = (STRPTR)ratetext[nrates];
    }
    ratelabels[nrates] = (STRPTR)"Other";
    ratelabels[nrates + 1] = NULL;
    memset(lg, 0, sizeof lg);

    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
        End;
    g = VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_SpaceOuter, TRUE,
        LAYOUT_Label, (ULONG)"The line", End;
    h = HLayoutObject, End;
    add(h, text(L_DEV, l->device, sizeof l->device - 1), NULL);
    add(h, number(L_UNIT, l->unit, 0, 255), "Unit");
    add(g, h, "Device");
    h = HLayoutObject, End;
    add(h, choose(L_RATE, ratelabels, rate_index(l->baud)), NULL);
    add(h, number(L_BAUD, l->baud, 50, 0x7fffffff), NULL);
    add(g, h, "Baud");
    h = HLayoutObject, End;
    add(h, choose(L_BITS, bitlabels, l->bits == 7), NULL);
    add(h, choose(L_PAR, parlabels, l->parity % 5), "Parity");
    add(h, choose(L_STOP, stoplabels, l->stop == 2), "Stop bits");
    add(g, h, "Data bits");
    add(g, choose(L_FLOW, flowlabels, l->flow % 3), "Flow control");
    h = HLayoutObject, End;
    add(h, number(L_BUF, l->bufkb, 1, 1024), NULL);
    add(h, lg[L_LOCK] = CheckBoxObject, GA_ID, L_LOCK, GA_RelVerify, TRUE,
        GA_Text, (ULONG)"Baud locked (CONNECT's speed ignored)",
        CHECKBOX_Checked, l->locked, End, NULL);
    add(g, h, "Buffer (KB)");
    if (root && g)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)g, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (init && dial) {
        g = VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_Label, (ULONG)"This place's own (empty: Settings, Modem)",
            End;
        add(g, text(L_INIT, init, 63), "Init string");
        add(g, text(L_DIAL, dial, 23), "Dial prefix");
        if (root && g)
            SetAttrs(root, LAYOUT_AddChild, (ULONG)g,
                     CHILD_WeightedHeight, 0, TAG_END);
    }
    h = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    add(h, button(L_OK, "_OK"), NULL);
    add(h, button(L_CANCEL, "_Cancel"), NULL);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (!root) {
        beep();
        return FALSE;
    }
    obj = WindowObject, WA_Title, (ULONG)"CTerm Serial line",
        scr == ownscr ? WA_CustomScreen : WA_PubScreen, (ULONG)scr,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE,
        over ? WINDOW_RefWindow : TAG_IGNORE, (ULONG)over,
        WINDOW_Position, over ? WPOS_CENTERWINDOW : WPOS_CENTERSCREEN,
        WINDOW_Layout, (ULONG)root, End;
    if (!obj) {
        DisposeObject(root);
        beep();
        return FALSE;
    }
    lw = (struct Window *)DoMethod(obj, WM_OPEN);
    if (lw)
        GetAttr(WINDOW_SigMask, obj, &wsig);
    while (lw && !how) {
        ULONG sigs = Wait(wsig | shells_sigmask());
        if (sigs & ~wsig)
            shells_handle(sigs);        /* the Shells go on meanwhile */
        while (!how && (res = DoMethod(obj, WM_HANDLEINPUT, (ULONG)&code)) !=
               WMHI_LASTMSG) {
            switch (res & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                how = 2;
                break;
            case WMHI_GADGETUP:
                switch (res & WMHI_GADGETMASK) {
                case L_RATE:
                    i = lget(L_RATE, CHOOSER_Selected);
                    if (i < nrates)
                        lset(L_BAUD, INTEGER_Number, ser_rates[i]);
                    break;
                case L_BAUD:
                    lset(L_RATE, CHOOSER_Selected,
                         rate_index(lget(L_BAUD, INTEGER_Number)));
                    break;
                case L_OK: how = 1; break;
                case L_CANCEL: how = 2; break;
                }
                break;
            }
        }
    }
    if (how == 1) {
        const char *t = (const char *)lget(L_DEV, STRINGA_TextVal);
        if (t && t[0]) {
            strncpy(l->device, t, sizeof l->device - 1);
            l->device[sizeof l->device - 1] = 0;
        }
        l->unit = lget(L_UNIT, INTEGER_Number);
        l->baud = lget(L_BAUD, INTEGER_Number);
        l->bits = lget(L_BITS, CHOOSER_Selected) ? 7 : 8;
        l->parity = lget(L_PAR, CHOOSER_Selected);
        l->stop = lget(L_STOP, CHOOSER_Selected) ? 2 : 1;
        l->flow = lget(L_FLOW, CHOOSER_Selected);
        l->bufkb = lget(L_BUF, INTEGER_Number);
        l->locked = lget(L_LOCK, GA_Selected) != 0;
        if (init && (t = (const char *)lget(L_INIT, STRINGA_TextVal))) {
            strncpy(init, t, 63);
            init[63] = 0;
        }
        if (dial && (t = (const char *)lget(L_DIAL, STRINGA_TextVal))) {
            strncpy(dial, t, 23);
            dial[23] = 0;
        }
    }
    DisposeObject(obj);                 /* the window and every gadget */
    lw = NULL;
    memset(lg, 0, sizeof lg);
    return how == 1;
}
