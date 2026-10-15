/*
 * drop - files dropped on CTerm's window from Workbench (an AppWindow):
 * in a Shell tab their full names are typed at the cursor (quoted when
 * they need it), in a host's tab (telnet, ssh, serial) they wait for the
 * host's Zmodem upload (its rz, a board's upload command: transfer.c
 * takes them instead of asking in a requester), on a file browser tab
 * they are uploaded into its drawer.
 *
 * Workbench takes drops only on windows on its own screen: on CTerm's
 * own screen (or full screen) there is no AppWindow, and nothing to drop.
 * workbench.library is opened here with a base of its own: hide.c opens
 * and closes its own for the AppIcon.
 */
#define WorkbenchBase DropWBBase        /* (proto/wb.h's calls use it) */
#include "cterm.h"
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/wb.h>

struct Library *DropWBBase;
static struct MsgPort *dport;
static struct AppWindow *awin;

#define DROPMAX 16                      /* files taken from one drop */

/* Files dropped on a host's tab, waiting for its Zmodem upload */
static struct Tab *qtab;
static char (*queue)[256];
static int nqueue;

/* The window just opened: an AppWindow, when it is on Workbench's screen */
void drop_add(void)
{
    if (awin || !win || (scr->Flags & SCREENTYPE) != WBENCHSCREEN)
        return;
    if (!DropWBBase &&
        !(DropWBBase = OpenLibrary((STRPTR)"workbench.library", 37)))
        return;
    if (!dport && !(dport = CreateMsgPort()))
        return;
    awin = AddAppWindowA(0, 0, win, dport, NULL);
}

/* The window about to close (CloseWindow after this) */
void drop_remove(void)
{
    struct Message *m;
    if (awin)
        RemoveAppWindow(awin);
    awin = NULL;
    while (dport && (m = GetMsg(dport)))
        ReplyMsg(m);                    /* a drop on its way: too late */
}

/* At the end */
void drop_free(void)
{
    drop_remove();
    if (queue)
        FreeVec(queue);
    queue = NULL;
    nqueue = 0;
    if (dport)
        DeleteMsgPort(dport);
    dport = NULL;
    if (DropWBBase)
        CloseLibrary(DropWBBase);
    DropWBBase = NULL;
}

ULONG drop_sigmask(void)
{
    return dport ? 1UL << dport->mp_SigBit : 0;
}

/* A name as the Shell wants it: in quotes when it has a space or a
 * letter the Shell (or csh) would take as its own; a quote inside
 * becomes *" (AmigaDOS's way). */
static void quoted(char *out, int max, const char *name)
{
    const char *p;
    int n = 0;
    BOOL q = !*name;
    for (p = name; *p; p++)
        if (strchr(" \t;\"'*?#()[]|<>`$&~{}!", *p))
            q = TRUE;
    if (q)
        out[n++] = '"';
    for (p = name; *p && n < max - 4; p++) {
        if (*p == '"')
            out[n++] = '*';
        out[n++] = *p;
    }
    if (q)
        out[n++] = '"';
    out[n] = 0;
}

/* The dropped files' names (NameFromLock + AddPart) into names[]: the
 * count. A drawer or a disk dropped (no name): the drawer's own. */
static int drop_names(struct AppMessage *am, char (*names)[256])
{
    int i, n = 0;
    for (i = 0; i < am->am_NumArgs && n < DROPMAX; i++) {
        struct WBArg *a = &am->am_ArgList[i];
        if (!a->wa_Lock || !NameFromLock(a->wa_Lock, (STRPTR)names[n], 256))
            continue;
        if (a->wa_Name && a->wa_Name[0] &&
            !AddPart((STRPTR)names[n], a->wa_Name, 256))
            continue;
        n++;
    }
    return n;
}

/* What to do with them, in the tab they were dropped on */
static void drop_files(char (*names)[256], int n)
{
    const char *paths[DROPMAX];
    int i;
    for (i = 0; i < n; i++)
        paths[i] = names[i];
    if (T->t_rf) {                      /* a file browser: into its drawer */
        char *res = NULL;
        int rc = files_rexx(FR_PUT, paths, n, NULL, &res);
        if (res)
            FreeVec(res);
        title_hint(rc == 0 ? "uploaded" : rc == 5 ? "some files failed"
                                                  : "the upload failed");
    } else if (local) {                 /* a Shell: the names typed in */
        char q[300];
        if (!T->con)
            return;
        if (back)
            scroll_view(0);
        for (i = 0; i < n; i++) {
            quoted(q, sizeof q - 1, names[i]);
            strcat(q, " ");
            if (!con_paste(T->con, (UBYTE *)q, strlen(q))) {
                beep();
                return;
            }
        }
    } else if (linked) {                /* a host: its next Zmodem upload */
        char m[60];
        if (!queue && !(queue = AllocVec(DROPMAX * 256, MEMF_ANY))) {
            beep();
            return;
        }
        for (i = 0; i < n; i++)
            strcpy(queue[i], names[i]);
        nqueue = n;
        qtab = T;
        strcpy(m, n == 1 ? "1 file waits" : "files wait");
        strcat(m, " for the host's Zmodem upload (start it there)");
        title_hint(m);
    } else {
        title_hint("no call up: nothing to send the files to");
        beep();
    }
}

/* transfer.c: the host asks for files (Zmodem's ZRINIT) in tab t - the
 * ones dropped there, if any (then they are taken). 0: none. */
int drop_uploads(struct Tab *t, char (*store)[256], const char **paths,
                 int max)
{
    int i, n = 0;
    if (!nqueue || qtab != t)
        return 0;
    for (i = 0; i < nqueue && n < max; i++, n++) {
        strcpy(store[n], queue[i]);
        paths[n] = store[n];
    }
    nqueue = 0;
    qtab = NULL;
    return n;
}

/* A tab closes: what waits for it goes */
void drop_tab_gone(struct Tab *t)
{
    if (qtab == t) {
        nqueue = 0;
        qtab = NULL;
    }
}

/* Drops that came: each answered at once (Workbench waits for it), then
 * done in the tab under the pointer. */
void drop_events(void)
{
    static char (*names)[256];
    struct AppMessage *am;
    while (dport && (am = (struct AppMessage *)GetMsg(dport))) {
        int n = 0;
        struct Pane *p;
        WORD mx = am->am_MouseX, my = am->am_MouseY;
        if (am->am_Type == AMTYPE_APPWINDOW &&
            (names || (names = AllocVec(DROPMAX * 256, MEMF_ANY))))
            n = drop_names(am, names);
        ReplyMsg((struct Message *)am);
        if (!n || !win || hidden)
            continue;
        if ((p = pane_at(mx, my)) && p->tab != shown)
            pane_activate(p);           /* the pane it fell on */
        T = shown;
        if (T)
            drop_files(names, n);
        T = shown;
    }
    if (names) {
        FreeVec(names);
        names = NULL;
    }
}
