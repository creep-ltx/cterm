/* keylog - a window that writes every RAWKEY it gets (code, qualifier)
 * to RAM:keylog.txt: which keys reach a window (Left Amiga ones). */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <stdio.h>
int main(void)
{
    struct Window *w = OpenWindowTags(NULL, WA_Title, (ULONG)"KeyLog",
        WA_Width, 300, WA_Height, 100, WA_Left, 100, WA_Top, 50,
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_MOUSEMOVE,
        WA_CloseGadget, TRUE, WA_DragBar, TRUE, WA_Activate, TRUE, TAG_END);
    BPTR f = Open("RAM:keylog.txt", MODE_NEWFILE);
    if (f) Close(f);
    BOOL go = TRUE;
    if (!w) return 20;
    while (go) {
        struct IntuiMessage *m;
        WaitPort(w->UserPort);
        while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            char b[64];
            if (m->Class == IDCMP_CLOSEWINDOW) go = FALSE;
            else if (m->Class == IDCMP_RAWKEY) {
                sprintf(b, "%02x %04x\n", m->Code, m->Qualifier);
                if ((f = Open("RAM:keylog.txt", MODE_READWRITE))) {
                    Seek(f, 0, OFFSET_END); FPuts(f, b); Close(f);
                }
            }
            ReplyMsg((struct Message *)m);
        }
    }
    CloseWindow(w);
    return 0;
}
