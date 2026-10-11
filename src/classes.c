/*
 * classes - OS 3.2's window classes (ReAction), for every window made of
 * them: the settings, the phone book, a serial line's settings, the
 * Transfers window. Opened the first time one opens and closed when the
 * last one is gone (reaction_drop), so their memory can go back.
 */
#include "cterm.h"

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
/* OS 3.2's window classes: opened the first time the window opens. Given
 * a value, so libnix does not try to open them itself (by wrong names). */
struct Library *WindowBase = NULL, *LayoutBase = NULL, *ClickTabBase = NULL,
    *ChooserBase = NULL, *CheckBoxBase = NULL, *SliderBase = NULL,
    *ButtonBase = NULL, *GetFileBase = NULL, *GetFontBase = NULL,
    *GetScreenModeBase = NULL, *LabelBase = NULL, *ListBrowserBase = NULL,
    *StringBase = NULL, *IntegerBase = NULL;

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

/* No window of the classes open any more: they may go. */
void reaction_drop(void)
{
    if (!setwin && !bookwin_sigmask() && !xwin_sigmask() &&
        !files_sigmask())
        reaction_close();
}
