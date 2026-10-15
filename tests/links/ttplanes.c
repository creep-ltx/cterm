/* ttplanes FONT.ttf - can ttengine.library draw a letter past U+FFFF?
 * Draws U+1D670 (and U+0041, and U+231A) from FONT in a Workbench window
 * as UTF-32, UTF-16 with a surrogate pair and UTF-8, and prints each
 * one's TT_TextLength. A box (or a length of the box) where the letter
 * should be: that way does not reach the font's 32-bit cmap.
 * Built: m68k-amigaos-gcc -noixemul -Iinclude tests/links/ttplanes.c */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/ttengine.h>
#include <stdio.h>

struct Library *TTEngineBase;

int main(int argc, char **argv)
{
    struct Window *w;
    APTR f;
    static const ULONG u32[] = { 0x41, 0x231A, 0x1D670, 0xE600 };
    static const UWORD u16[] = { 0x0041, 0x231A, 0xD835, 0xDE70, 0xD83D, 0xDE00 };
    static const UBYTE u8[] = { 0x41, 0xE2, 0x8C, 0x9A, 0xF0, 0x9D, 0x99, 0xB0,
                                0xF0, 0x9F, 0x98, 0x80 };
    if (argc < 2 || !(TTEngineBase = OpenLibrary("ttengine.library", 7)))
        return 20;
    w = OpenWindowTags(NULL, WA_Left, 20, WA_Top, 40, WA_Width, 400,
                       WA_Height, 120, WA_Title, (ULONG)"ttplanes",
                       WA_DragBar, TRUE, TAG_END);
    f = TT_OpenFont(TT_FontFile, (ULONG)argv[1], TT_FontSize, 20, TAG_END);
    if (!w || !f) {
        printf("no window or font\n");
        return 20;
    }
    TT_SetAttrs(w->RPort, TT_Window, (ULONG)w, TAG_END);
    TT_SetFont(w->RPort, f);
    SetAPen(w->RPort, 1);
    TT_SetAttrs(w->RPort, TT_Encoding, TT_Encoding_UTF32_BE, TAG_END);
    printf("utf32: %ld %ld %ld %ld\n",
           (long)TT_TextLength(w->RPort, (APTR)&u32[0], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u32[1], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u32[2], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u32[3], 1));
    Move(w->RPort, 20, 50);
    TT_Text(w->RPort, (APTR)u32, 4);
    TT_SetAttrs(w->RPort, TT_Encoding, TT_Encoding_UTF16_BE, TAG_END);
    printf("utf16: %ld %ld %ld %ld\n",
           (long)TT_TextLength(w->RPort, (APTR)&u16[0], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u16[1], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u16[2], 2),
           (long)TT_TextLength(w->RPort, (APTR)&u16[4], 2));
    Move(w->RPort, 20, 80);
    TT_Text(w->RPort, (APTR)u16, 6);
    TT_SetAttrs(w->RPort, TT_Encoding, TT_Encoding_UTF8, TAG_END);
    printf("utf8: %ld %ld %ld %ld\n",
           (long)TT_TextLength(w->RPort, (APTR)&u8[0], 1),
           (long)TT_TextLength(w->RPort, (APTR)&u8[1], 3),
           (long)TT_TextLength(w->RPort, (APTR)&u8[4], 4),
           (long)TT_TextLength(w->RPort, (APTR)&u8[8], 4));
    Move(w->RPort, 20, 110);
    TT_Text(w->RPort, (APTR)u8, 12);
    Delay(50 * 8);
    TT_DoneRastPort(w->RPort);
    TT_CloseFont(f);
    CloseWindow(w);
    CloseLibrary(TTEngineBase);
    return 0;
}
