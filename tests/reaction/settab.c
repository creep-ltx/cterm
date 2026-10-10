/*
 * settab - a look at CTerm's settings window built with ReAction (OS 3.2):
 * real tabs (clicktab.gadget with a page group), every control a
 * ReAction one, laid out by layout.gadget. A test of the look only:
 * nothing is read or saved.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/clicktab.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/slider.h>
#include <gadgets/button.h>
#include <gadgets/getfile.h>
#include <gadgets/getfont.h>
#include <gadgets/getscreenmode.h>
#include <images/label.h>
#include <images/bevel.h>
#include <reaction/reaction_macros.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/clicktab.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/slider.h>
#include <proto/button.h>
#include <proto/getfile.h>
#include <proto/getfont.h>
#include <proto/getscreenmode.h>
#include <proto/label.h>
#include <proto/dos.h>
#include <string.h>
static void STEP(STRPTR t)
{
    BPTR f = Open("SYS:T/settab.log", MODE_READWRITE);
    if (f) {
        Seek(f, 0, OFFSET_END);
        Write(f, t, strlen(t));
        Write(f, "\n", 1);
        Close(f);
    }
}
#include <clib/alib_protos.h>

/* The ReAction macros (LayoutObject ... End) split one NewObject call
 * across macros; the inline NewObject macro can't take that: plain
 * functions instead (m68k varargs are the tag list on the stack). */
#undef NewObject
#undef SetAttrs
#define NewObject RNewObject
#define SetAttrs RSetAttrs
__attribute__((noinline)) __stdargs APTR RNewObject(struct IClass *c, CONST_STRPTR id, ULONG tag1, ...)
{
    return NewObjectA(c, id, (struct TagItem *)&tag1);
}
__attribute__((noinline)) __stdargs ULONG RSetAttrs(APTR o, ULONG tag1, ...)
{
    return SetAttrsA(o, (struct TagItem *)&tag1);
}

struct Library *WindowBase, *LayoutBase, *ClickTabBase, *ChooserBase,
    *CheckBoxBase, *SliderBase, *ButtonBase, *GetFileBase, *GetFontBase,
    *GetScreenModeBase, *LabelBase;

enum { G_SAVE = 1, G_USE, G_CANCEL };

static STRPTR where[] = { "Workbench window", "Own screen", NULL };
static STRPTR letters[] = { "Amiga font", "TrueType", NULL };
static STRPTR chars[] = { "UTF-8", "Latin-1", "PC BBS (CP437)", NULL };
static STRPTR themes[] = { "Nord", "Black", "Workbench", "Amber", "Green",
                           "Light", NULL };
static STRPTR part[] = { "Text", "Background", NULL };
static STRPTR alt[] = { "Meta for hosts (bash, emacs)",
                        "Amiga characters everywhere", NULL };
static STRPTR tabnames[] = { "Screen", "Text", "Colours", "Keys", NULL };
static struct TextAttr mk = { "MicroKnight.font", 8, 0, 0 };

static Object *label(STRPTR t)
{
    return LabelObject, LABEL_Text, t, LabelEnd;
}

static Object *chooser(STRPTR *l, int sel)
{
    return ChooserObject, CHOOSER_LabelArray, l, CHOOSER_Selected, sel,
        CHOOSER_DropDown, FALSE, End;
}

static Object *slider(int min, int max, int lev, STRPTR fmt)
{
    return SliderObject, SLIDER_Min, min, SLIDER_Max, max, SLIDER_Level, lev,
        SLIDER_Orientation, SLIDER_HORIZONTAL, SLIDER_LevelFormat, fmt,
        SLIDER_LevelPlace, PLACETEXT_RIGHT, SLIDER_LevelMaxLen, 4,
        GA_RelVerify, TRUE, End;
}

static Object *check(STRPTR t, BOOL on)
{
    return CheckBoxObject, GA_Text, t, CHECKBOX_Checked, on, End;
}

static Object *group(STRPTR title)
{
    return VLayoutObject, LAYOUT_BevelStyle, BVS_GROUP, LAYOUT_Label, title,
        LAYOUT_SpaceOuter, TRUE, End;
}

static void ADD(Object *g, Object *child, STRPTR lab)
{
    if (!child) {
        STEP("a NULL child:");
        STEP(lab ? lab : (STRPTR)"(no label)");
        return;
    }
    SetAttrs(g, LAYOUT_AddChild, child, lab ? CHILD_Label : TAG_IGNORE,
             lab ? label(lab) : NULL, TAG_END);
}

static Object *page_screen(void)
{
    Object *o, *p = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    Object *g;
    STEP("p1 in");
    g = group("Where CTerm opens");
    STEP("p1 group");
    o = chooser(where, 1);
    STEP("p1 chooser");
    o = label("Screen");
    STEP("p1 label");
    ADD(g, chooser(where, 1), "Screen");
    STEP("p1 add");
    o = GetScreenModeObject, GETSCREENMODE_TitleText, "CTerm's own screen",
        GETSCREENMODE_DisplayID, 0x50031000, GETSCREENMODE_DoDepth, TRUE,
        GETSCREENMODE_DisplayDepth, 24, End;
    STEP("p1 smode");
    ADD(g, o, "Screen mode");
    STEP("p1 smode added");
    ADD(g, check("Full screen", TRUE), NULL);
    STEP("p1 full");
    ADD(p, g, NULL);
    g = group("The window");
    ADD(g, check("Scroll bar", TRUE), NULL);
    ADD(p, g, NULL);
    return p;
}

static Object *page_text(void)
{
    Object *o, *p = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    Object *g = group("Letters");
    ADD(g, chooser(letters, 1), "Letters");
    o = GetFontObject, GETFONT_TextAttr, &mk, GETFONT_FixedWidthOnly, TRUE,
        GETFONT_TitleText, "CTerm's font (fixed width)", End;
    ADD(g, o, "Amiga font");
    o = GetFileObject, GETFILE_FullFile,
        "SYS:Fonts/TrueType/DejaVuSansMono.ttf", GETFILE_TitleText,
        "A TrueType font (fixed width)", GETFILE_ReadOnly, TRUE, End;
    ADD(g, o, "TrueType font");
    ADD(g, slider(6, 40, 14, "%ld"), "TrueType size");
    ADD(g, slider(0, 8, 0, "%ld"), "Line spacing");
    ADD(p, g, NULL);
    g = group("Characters");
    ADD(g, chooser(chars, 0), "Characters");
    ADD(g, check("The IBM VGA font in PC BBS mode", TRUE), NULL);
    ADD(p, g, NULL);
    return p;
}

static Object *page_colours(void)
{
    Object *o, *p = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    Object *g = group("Colours");
    ADD(g, chooser(themes, 0), "Theme");
    ADD(g, chooser(part, 0), "Colour of");
    ADD(g, slider(0, 255, 216, "%3ld"), "Red");
    ADD(g, slider(0, 255, 222, "%3ld"), "Green");
    ADD(g, slider(0, 255, 233, "%3ld"), "Blue");
    o = ButtonObject, GA_Text, "Back to the theme's colours", End;
    ADD(g, o, NULL);
    ADD(p, g, NULL);
    g = group("A picture behind the text");
    o = GetFileObject, GETFILE_FullFile,
        "Dump:Backdrops/boing-the-matrix/boing-the-matrix_1280x960.jpg",
        GETFILE_TitleText, "A picture behind the text", GETFILE_ReadOnly,
        TRUE, End;
    ADD(g, o, "Picture");
    ADD(g, slider(0, 100, 35, "%3ld%%"), "Picture shows");
    ADD(p, g, NULL);
    return p;
}

static Object *page_keys(void)
{
    Object *o, *p = VLayoutObject, LAYOUT_SpaceOuter, TRUE, End;
    Object *g = group("Keys");
    ADD(g, chooser(alt, 0), "Alt key");
    ADD(p, g, NULL);
    return p;
}

static struct Library *lib(STRPTR n)
{
    struct Library *l = OpenLibrary(n, 44);
    if (!l) {
        PutStr("could not open ");
        PutStr(n);
        PutStr("\n");
    }
    return l;
}

int main(void)
{
    struct List tabs;
    Object *win = NULL, *pages, *root;
    struct Window *w;
    int i, rc = 20;

    STEP("start");
    NewList(&tabs);
    WindowBase = lib("window.class");
    LayoutBase = lib("gadgets/layout.gadget");
    ClickTabBase = lib("gadgets/clicktab.gadget");
    ChooserBase = lib("gadgets/chooser.gadget");
    CheckBoxBase = lib("gadgets/checkbox.gadget");
    SliderBase = lib("gadgets/slider.gadget");
    ButtonBase = lib("gadgets/button.gadget");
    GetFileBase = lib("gadgets/getfile.gadget");
    GetFontBase = lib("gadgets/getfont.gadget");
    GetScreenModeBase = lib("gadgets/getscreenmode.gadget");
    LabelBase = lib("images/label.image");
    if (!WindowBase || !LayoutBase || !ClickTabBase || !ChooserBase ||
        !CheckBoxBase || !SliderBase || !ButtonBase || !GetFileBase ||
        !GetFontBase || !GetScreenModeBase || !LabelBase)
        goto out;

    STEP("libraries open");
    for (i = 0; tabnames[i]; i++) {
        struct Node *n = AllocClickTabNode(TNA_Text, tabnames[i],
                                           TNA_Number, i, TAG_END);
        if (n)
            AddTail(&tabs, n);
    }
    STEP("tabs made");
    {
        Object *p1, *p2, *p3, *p4;
        p1 = page_screen(); STEP("page 1");
        p2 = page_text(); STEP("page 2");
        p3 = page_colours(); STEP("page 3");
        p4 = page_keys(); STEP("page 4");
    pages = PageObject,
        PAGE_Add, p1, PAGE_Add, p2, PAGE_Add, p3, PAGE_Add, p4,
    End;
    }
    STEP("pages");
    root = VLayoutObject, LAYOUT_SpaceOuter, TRUE, LAYOUT_DeferLayout, TRUE,
        LAYOUT_AddChild, ClickTabObject, CLICKTAB_Labels, &tabs,
            CLICKTAB_Current, 0, CLICKTAB_PageGroup, pages, End,
        LAYOUT_AddChild, HLayoutObject, LAYOUT_EvenSize, TRUE,
            LAYOUT_AddChild, ButtonObject, GA_ID, G_SAVE, GA_RelVerify, TRUE,
                GA_Text, "_Save", End,
            LAYOUT_AddChild, ButtonObject, GA_ID, G_USE, GA_RelVerify, TRUE,
                GA_Text, "_Use", End,
            LAYOUT_AddChild, ButtonObject, GA_ID, G_CANCEL, GA_RelVerify,
                TRUE, GA_Text, "_Cancel", End,
        End, CHILD_WeightedHeight, 0,
    End;
    STEP("root");
    win = WindowObject, WA_Title, "CTerm Settings", WA_DragBar, TRUE,
        WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SizeGadget, TRUE, WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, root, End;
    STEP("window object");
    if (!win || !(w = (struct Window *)DoMethod(win, WM_OPEN)))
        goto out;
    {
        ULONG sig, res;
        UWORD code;
        BOOL done = FALSE;
        STEP("open");
        GetAttr(WINDOW_SigMask, win, &sig);
        while (!done) {
            Wait(sig | SIGBREAKF_CTRL_C);
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
                break;
            while ((res = DoMethod(win, WM_HANDLEINPUT, &code)) !=
                   WMHI_LASTMSG) {
                switch (res & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW: done = TRUE; break;
                case WMHI_GADGETUP:
                    if ((res & WMHI_GADGETMASK) <= G_CANCEL)
                        done = TRUE;
                    break;
                }
            }
        }
    }
    rc = 0;
out:
    if (win)
        DisposeObject(win);
    /* the tab nodes go after the gadget that used them */
    {
        struct Node *n;
        while ((n = RemHead(&tabs)))
            FreeClickTabNode(n);
    }
    CloseLibrary(LabelBase); CloseLibrary(GetScreenModeBase);
    CloseLibrary(GetFontBase); CloseLibrary(GetFileBase);
    CloseLibrary(ButtonBase); CloseLibrary(SliderBase);
    CloseLibrary(CheckBoxBase); CloseLibrary(ChooserBase);
    CloseLibrary(ClickTabBase); CloseLibrary(LayoutBase);
    CloseLibrary(WindowBase);
    return rc;
}
