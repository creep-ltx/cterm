/*
 * askwin - a line asked for in a small window of its own (a new name, a
 * drawer on a host), over the window given: OK, or Return in the field,
 * takes it; Cancel or the close gadget does not. The Shells go on
 * meanwhile. In the parts that ask (PhoneBook, Files).
 */
#include "cterm.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */

enum { A_TEXT = 1, A_OK, A_CANCEL, A_N };

BOOL ask_line(const char *title, const char *label, char *buf, int max,
              struct Window *over)
{
    Object *obj, *root, *h, *g[A_N];
    ULONG wsig = 0, res;
    UWORD code;
    struct Window *w;
    int how = 0;
    if (!reaction_open()) {
        beep();
        return FALSE;
    }
    memset(g, 0, sizeof g);
    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
        End;
    g[A_TEXT] = StringObject, GA_ID, A_TEXT, GA_RelVerify, TRUE,
        STRINGA_TextVal, (ULONG)buf, STRINGA_MaxChars, max - 1, End;
    if (root && g[A_TEXT])
        SetAttrs(root, LAYOUT_AddChild, (ULONG)g[A_TEXT], CHILD_Label,
                 (ULONG)(LabelObject, LABEL_Text, (ULONG)label, LabelEnd),
                 CHILD_MinWidth, 30 * scr->RastPort.TxWidth, TAG_END);
    h = HLayoutObject, LAYOUT_EvenSize, TRUE, End;
    g[A_OK] = ButtonObject, GA_ID, A_OK, GA_RelVerify, TRUE,
        GA_Text, (ULONG)"OK", End;
    g[A_CANCEL] = ButtonObject, GA_ID, A_CANCEL, GA_RelVerify, TRUE,
        GA_Text, (ULONG)"Cancel", End;
    if (h && g[A_OK])
        SetAttrs(h, LAYOUT_AddChild, (ULONG)g[A_OK], TAG_END);
    if (h && g[A_CANCEL])
        SetAttrs(h, LAYOUT_AddChild, (ULONG)g[A_CANCEL], TAG_END);
    if (root && h)
        SetAttrs(root, LAYOUT_AddChild, (ULONG)h, CHILD_WeightedHeight, 0,
                 TAG_END);
    if (!root || !g[A_TEXT]) {
        if (root)
            DisposeObject(root);
        beep();
        return FALSE;
    }
    obj = WindowObject, WA_Title, (ULONG)title,
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
    w = (struct Window *)DoMethod(obj, WM_OPEN);
    if (w) {
        GetAttr(WINDOW_SigMask, obj, &wsig);
        ActivateLayoutGadget((struct Gadget *)root, w, NULL,
                             (ULONG)g[A_TEXT]);
    }
    while (w && !how) {
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
                case A_TEXT:
                    if (code != 9)      /* Return, not Tab */
                        how = 1;
                    break;
                case A_OK: how = 1; break;
                case A_CANCEL: how = 2; break;
                }
                break;
            }
        }
    }
    if (how == 1) {
        const char *t = NULL;
        GetAttr(STRINGA_TextVal, g[A_TEXT], (ULONG *)&t);
        if (t) {
            strncpy(buf, t, max - 1);
            buf[max - 1] = 0;
        }
    }
    DisposeObject(obj);                 /* the window and every gadget */
    return how == 1;
}
