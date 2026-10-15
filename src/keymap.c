/*
 * keymap - CTerm's own keys: every action CTerm itself does on a key
 * (the menus, tabs, panes, the history, the Shell's line editor, giving
 * up a call) is bound in ENVARC:CTerm/CTerm.keys, written with the keys
 * below when there is none. The format is CFile's and Ahiru's: one key
 * a line, KEYS ACTION; [sections] (the file browser's too: [files],
 * [filesline] - its own files.keys until 0.3b18, read in once);
 * `;` or `#` starts a comment; ACTION none takes a key away.
 *
 * What the terminal sends to a program (arrows, function keys, Alt as
 * Meta) is the terminal standard's, not CTerm's: a key no action has
 * goes to the program as before.
 *
 * A key here is a UWORD: the character the keymap gives (Shift in it,
 * Ctrl, Alt and Amiga left out), or a named key (KM_*, Return 13, Esc
 * 27, Tab 9, BS 8, Del 127, Space ' '), and KQ_* for the qualifiers -
 * Shift only with named keys. Letters with Ctrl or Amiga are lower case.
 */
#include "cterm.h"
#pragma GCC optimize("Os")              /* a lookup a key: small before quick */

/* in every file written since 0.3b41 (show1-9 got LA+Ctrl+1-9) */
#define KEYMARK "; (keys of CTerm 0.3b41 and later)"
#define KEYFILE "ENVARC:CTerm/CTerm.keys"

/* the actions: their names in the file, their section, their menu item */
const struct KeyAct keys_acts[] = {
    { "none", 0, 0 },
    { "newtab", KC_WINDOW, M_NEWTAB },
    { "call", KC_WINDOW, M_CALL },
    { "phonebook", KC_WINDOW, M_BOOK },
    { "serial", KC_WINDOW, M_SERIAL },
    { "answer", KC_WINDOW, M_ANSWER },
    { "sftp", KC_WINDOW, M_SFTP },
    { "closetab", KC_WINDOW, M_CLOSETAB },
    { "log", KC_WINDOW, M_LOG },
    { "logstop", KC_WINDOW, M_LOGSTOP },
    { "senduser", KC_WINDOW, M_SENDUSER },
    { "sendpass", KC_WINDOW, M_SENDPASS },
    { "rexx", KC_WINDOW, M_REXX },
    { "hide", KC_WINDOW, M_HIDE },
    { "about", KC_WINDOW, M_ABOUT },
    { "speedtest", KC_WINDOW, M_SPEED },
    { "quit", KC_WINDOW, M_QUIT },
    { "copy", KC_WINDOW, M_COPY },
    { "paste", KC_WINDOW, M_PASTE },
    { "yrecv", KC_WINDOW, M_YRECV },
    { "xrecv", KC_WINDOW, M_XRECV },
    { "ysend", KC_WINDOW, M_YSEND },
    { "xsend", KC_WINDOW, M_XSEND },
    { "hsend", KC_WINDOW, M_HSEND },
    { "transfers", KC_WINDOW, M_XWIN },
    { "splitside", KC_WINDOW, M_SPLITH },
    { "splitunder", KC_WINDOW, M_SPLITV },
    { "nextpane", KC_WINDOW, M_NEXTPANE },
    { "closepane", KC_WINDOW, M_CLOSEPANE },
    { "settings", KC_WINDOW, M_SETTINGS },
    { "font", KC_WINDOW, M_FONT },
    { "ttfont", KC_WINDOW, M_TTFONT },
    { "bigger", KC_WINDOW, M_BIGGER },
    { "smaller", KC_WINDOW, M_SMALLER },
    { "workbench", KC_WINDOW, M_SCR_WB },
    { "screen16", KC_WINDOW, M_SCR_16 },
    { "screen256", KC_WINDOW, M_SCR_256 },
    { "screendeep", KC_WINDOW, M_SCR_DEEP },
    { "fullscreen", KC_WINDOW, M_FULL },
    { "scrollbar", KC_WINDOW, M_BAR },
    { "utf8", KC_WINDOW, M_UTF8 },
    { "pcbbs", KC_WINDOW, M_PC },
    { "tab1", KC_WINDOW, 0 },
    { "tab2", KC_WINDOW, 0 },
    { "tab3", KC_WINDOW, 0 },
    { "tab4", KC_WINDOW, 0 },
    { "tab5", KC_WINDOW, 0 },
    { "tab6", KC_WINDOW, 0 },
    { "tab7", KC_WINDOW, 0 },
    { "tab8", KC_WINDOW, 0 },
    { "tab9", KC_WINDOW, 0 },
    { "nexttab", KC_WINDOW, 0 },
    { "prevtab", KC_WINDOW, 0 },
    { "pageup", KC_WINDOW, 0 },
    { "pagedown", KC_WINDOW, 0 },
    { "lineup", KC_WINDOW, 0 },
    { "linedown", KC_WINDOW, 0 },
    { "historytop", KC_WINDOW, 0 },
    { "historyend", KC_WINDOW, 0 },
    { "stop", KC_CALL, 0 },
    { "redial", KC_CALL, 0 },
    { "left", KC_SHELL, 0 },
    { "right", KC_SHELL, 0 },
    { "start", KC_SHELL, 0 },
    { "end", KC_SHELL, 0 },
    { "wordleft", KC_SHELL, 0 },
    { "wordright", KC_SHELL, 0 },
    { "back", KC_SHELL, 0 },
    { "delete", KC_SHELL, 0 },
    { "wordback", KC_SHELL, 0 },
    { "killstart", KC_SHELL, 0 },
    { "killend", KC_SHELL, 0 },
    { "clear", KC_SHELL, 0 },
    { "older", KC_SHELL, 0 },
    { "newer", KC_SHELL, 0 },
    { "search", KC_SHELL, 0 },
    { "complete", KC_SHELL, 0 },
    { "accept", KC_SHELL, 0 },
    { "eof", KC_SHELL, 0 },
    /* the file browser's (Parts/Files, fileui.c's K_* order) */
    { "up", KC_FILES, 0 },
    { "down", KC_FILES, 0 },
    { "pageup", KC_FILES, 0 },
    { "pagedown", KC_FILES, 0 },
    { "top", KC_FILES, 0 },
    { "bottom", KC_FILES, 0 },
    { "left", KC_FILES, 0 },
    { "right", KC_FILES, 0 },
    { "switch", KC_FILES, 0 },
    { "open", KC_FILES, 0 },
    { "parent", KC_FILES, 0 },
    { "mark", KC_FILES, 0 },
    { "invert", KC_FILES, 0 },
    { "help", KC_FILES, 0 },
    { "refresh", KC_FILES, 0 },
    { "copy", KC_FILES, 0 },
    { "rename", KC_FILES, 0 },
    { "mkdir", KC_FILES, 0 },
    { "delete", KC_FILES, 0 },
    { "log", KC_FILES, 0 },
    { "quit", KC_FILES, 0 },
    { "stop", KC_FILES, 0 },
    { "command", KC_FILES, 0 },
    { "redraw", KC_FILES, 0 },
    { "left", KC_FLINE, 0 },
    { "right", KC_FLINE, 0 },
    { "start", KC_FLINE, 0 },
    { "end", KC_FLINE, 0 },
    { "back", KC_FLINE, 0 },
    { "delete", KC_FLINE, 0 },
    { "kill", KC_FLINE, 0 },
    { "accept", KC_FLINE, 0 },
    { "cancel", KC_FLINE, 0 },
    { "complete", KC_FLINE, 0 },
    { "older", KC_FLINE, 0 },
    { "newer", KC_FLINE, 0 },
    /* [panes]: i3's way */
    /* a new Shell and closing it: a pane, or a tab when the Panes menu
     * says new Shells go in tabs (tabmode) */
    { "open", KC_PANES, M_SHNEW },
    { "kill", KC_PANES, M_SHCLOSE },
    { "focusleft", KC_PANES, M_PFLEFT },
    { "focusright", KC_PANES, M_PFRIGHT },
    { "focusup", KC_PANES, M_PFUP },
    { "focusdown", KC_PANES, M_PFDOWN },
    { "moveleft", KC_PANES, M_PMLEFT },
    { "moveright", KC_PANES, M_PMRIGHT },
    { "moveup", KC_PANES, M_PMUP },
    { "movedown", KC_PANES, M_PMDOWN },
    { "splith", KC_PANES, M_PSPLITH },
    { "splitv", KC_PANES, M_PSPLITV },
    { "full", KC_PANES, M_PFULL },
    { "tabs", KC_PANES, M_PTABS },
    { "panes", KC_PANES, M_PPANES },
    { "show1", KC_PANES, 0 },
    { "show2", KC_PANES, 0 },
    { "show3", KC_PANES, 0 },
    { "show4", KC_PANES, 0 },
    { "show5", KC_PANES, 0 },
    { "show6", KC_PANES, 0 },
    { "show7", KC_PANES, 0 },
    { "show8", KC_PANES, 0 },
    { "show9", KC_PANES, 0 },
    { "dwindle", KC_PANES, M_PDWINDLE },
    { "ws1", KC_PANES, 0 }, { "ws2", KC_PANES, 0 }, { "ws3", KC_PANES, 0 },
    { "ws4", KC_PANES, 0 }, { "ws5", KC_PANES, 0 }, { "ws6", KC_PANES, 0 },
    { "ws7", KC_PANES, 0 }, { "ws8", KC_PANES, 0 }, { "ws9", KC_PANES, 0 },
    { "movews1", KC_PANES, 0 }, { "movews2", KC_PANES, 0 },
    { "movews3", KC_PANES, 0 }, { "movews4", KC_PANES, 0 },
    { "movews5", KC_PANES, 0 }, { "movews6", KC_PANES, 0 },
    { "movews7", KC_PANES, 0 }, { "movews8", KC_PANES, 0 },
    { "movews9", KC_PANES, 0 },
    { "resize", KC_PANES, 0 },
    { "scratch", KC_PANES, 0 },
    { "toscratch", KC_PANES, 0 },
    { "find", KC_WINDOW, M_FIND },
    { "openlink", KC_WINDOW, M_OPENLINK },
};

/* (one entry an action, in the enum's order) */
typedef char key_acts_check[sizeof keys_acts / sizeof keys_acts[0] == KA_N
                            ? 1 : -1];

#define A(c) (KQ_A | (c))
#define C(c) (KQ_C | (c))
/* the keys CTerm comes with */
static const struct KeyBind defbinds[] = {
    { A('t'), KA_NEWTAB }, { A('k'), KA_CALL }, { A('b'), KA_BOOK },
    { A('w'), KA_CLOSETAB }, { A('l'), KA_LOG }, { A('u'), KA_SENDUSER },
    { A('p'), KA_SENDPASS }, { A('q'), KA_QUIT }, { A('c'), KA_COPY },
    { A('v'), KA_PASTE }, { A('s'), KA_SETTINGS }, { A('+'), KA_BIGGER },
    { A('-'), KA_SMALLER }, { A('f'), KA_FULL },
    { A('h'), KA_FIND }, { A('o'), KA_OPENLINK },
    { A('1'), KA_TAB1 }, { A('2'), KA_TAB1 + 1 }, { A('3'), KA_TAB1 + 2 },
    { A('4'), KA_TAB1 + 3 }, { A('5'), KA_TAB1 + 4 },
    { A('6'), KA_TAB1 + 5 }, { A('7'), KA_TAB1 + 6 },
    { A('8'), KA_TAB1 + 7 }, { A('9'), KA_TAB1 + 8 },
    { C(9), KA_NEXTTAB }, { C(KQ_S | 9), KA_PREVTAB },
    { KQ_S | KM_UP, KA_PAGEUP }, { KQ_S | KM_DOWN, KA_PAGEDOWN },
    { 27, KA_STOP }, { 13, KA_REDIAL },
    { KM_LEFT, KA_ED_LEFT }, { KM_RIGHT, KA_ED_RIGHT },
    { KQ_S | KM_LEFT, KA_ED_START }, { C('a'), KA_ED_START },
    { KQ_S | KM_RIGHT, KA_ED_END }, { C('e'), KA_ED_END },
    { C(KM_LEFT), KA_ED_WORDLEFT }, { C(KM_RIGHT), KA_ED_WORDRIGHT },
    { 8, KA_ED_BACK }, { 127, KA_ED_DELETE }, { C('w'), KA_ED_WORDBACK },
    { C('u'), KA_ED_KILLSTART }, { C('k'), KA_ED_KILLEND },
    { C('x'), KA_ED_CLEAR }, { KM_UP, KA_ED_OLDER }, { KM_DOWN, KA_ED_NEWER },
    { C('r'), KA_ED_SEARCH }, { 9, KA_ED_COMPLETE }, { 13, KA_ED_ACCEPT },
    { C('\\'), KA_ED_EOF },
#define F(n) (KA_FB_FIRST + (n))
#define S(c) (KQ_S | (c))
    /* [files]: the panels */
    { KM_UP, F(0) }, { KM_DOWN, F(1) }, { S(KM_UP), F(2) },
    { KM_PGUP, F(2) }, { S(KM_DOWN), F(3) }, { KM_PGDN, F(3) },
    { C(KM_UP), F(4) }, { KM_HOME, F(4) }, { C(KM_DOWN), F(5) },
    { KM_END, F(5) }, { KM_LEFT, F(6) }, { KM_RIGHT, F(7) }, { 9, F(8) },
    { 13, F(9) }, { 8, F(10) }, { ' ', F(11) }, { KM_INS, F(11) },
    { '*', F(12) }, { KM_F1, F(13) }, { KM_HELP, F(13) },
    { KM_F1 + 1, F(14) }, { C('r'), F(14) }, { KM_F1 + 4, F(15) },
    { KM_F1 + 5, F(16) }, { KM_F1 + 6, F(17) }, { KM_F1 + 7, F(18) },
    { 127, F(18) }, { KM_F1 + 8, F(19) }, { KM_F1 + 9, F(20) },
    { 27, F(21) }, { ':', F(22) }, { C('l'), F(23) },
    /* [filesline]: a line typed in the browser */
    { KM_LEFT, F(24) }, { KM_RIGHT, F(25) }, { S(KM_LEFT), F(26) },
    { C('a'), F(26) }, { KM_HOME, F(26) }, { S(KM_RIGHT), F(27) },
    { C('e'), F(27) }, { KM_END, F(27) }, { 8, F(28) }, { 127, F(29) },
    { S(8), F(30) }, { C('u'), F(30) }, { 13, F(31) }, { 27, F(32) },
    { 9, F(33) }, { KM_UP, F(34) }, { KM_DOWN, F(35) },
#define L(c) (KQ_L | (c))
    /* [panes]: Left Amiga, as i3's Mod key */
    { L(13), KA_P_OPEN }, { L('q'), KA_P_KILL },
    { L(KM_LEFT), KA_P_FLEFT }, { L(KM_RIGHT), KA_P_FRIGHT },
    { L(KM_UP), KA_P_FUP }, { L(KM_DOWN), KA_P_FDOWN },
    { L(S(KM_LEFT)), KA_P_MLEFT }, { L(S(KM_RIGHT)), KA_P_MRIGHT },
    { L(S(KM_UP)), KA_P_MUP }, { L(S(KM_DOWN)), KA_P_MDOWN },
    { L('h'), KA_P_SPLITH }, { L('v'), KA_P_SPLITV }, { L('f'), KA_P_FULL },
    { L('t'), KA_P_TABS }, { L('p'), KA_P_PANES },
    { L('1'), KA_P_WS1 }, { L('2'), KA_P_WS1 + 1 },
    { L('3'), KA_P_WS1 + 2 }, { L('4'), KA_P_WS1 + 3 },
    { L('5'), KA_P_WS1 + 4 }, { L('6'), KA_P_WS1 + 5 },
    { L('7'), KA_P_WS1 + 6 }, { L('8'), KA_P_WS1 + 7 },
    { L('9'), KA_P_WS1 + 8 }, { L('d'), KA_P_DWINDLE },
    { L('r'), KA_P_RESIZE }, { L('-'), KA_P_SCRATCH },
    { L(C('1')), KA_P_SHOW1 }, { L(C('2')), KA_P_SHOW1 + 1 },
    { L(C('3')), KA_P_SHOW1 + 2 }, { L(C('4')), KA_P_SHOW1 + 3 },
    { L(C('5')), KA_P_SHOW1 + 4 }, { L(C('6')), KA_P_SHOW1 + 5 },
    { L(C('7')), KA_P_SHOW1 + 6 }, { L(C('8')), KA_P_SHOW1 + 7 },
    { L(C('9')), KA_P_SHOW1 + 8 },
#undef L
#undef F
#undef S
};
#undef A
#undef C
#define NDEF ((int)(sizeof defbinds / sizeof defbinds[0]))

struct KeyBind keys_binds[MAXKB];
int keys_n;
char keys_bad[80];                      /* the key file's first bad line */

static const char *const secnames[] = { "window", "call", "shell", "files",
                                        "filesline", "panes",
                                        /* files.keys's, read once */
                                        "panels", "line" };
#define NSEC 8
/* the order the file has them in */
static const UBYTE secorder[KC_N] = { KC_WINDOW, KC_PANES, KC_CALL,
                                      KC_SHELL, KC_FILES, KC_FLINE };
#define KEYFILE_OLD "ENVARC:CTerm/files.keys"
#define KEYFILE_GONE "ENVARC:CTerm/files.keys.old"

/* named keys: their names in the file (CFile's, and a PC keyboard's) */
static const struct { const char *n; UWORD k; } kn[] = {
    { "Up", KM_UP }, { "Down", KM_DOWN }, { "Left", KM_LEFT },
    { "Right", KM_RIGHT }, { "Return", 13 }, { "Esc", 27 }, { "Tab", 9 },
    { "Space", ' ' }, { "Del", 127 }, { "BS", 8 }, { "Help", KM_HELP },
    { "PgUp", KM_PGUP }, { "PgDn", KM_PGDN }, { "Home", KM_HOME },
    { "End", KM_END }, { "Ins", KM_INS }, { "F11", KM_F11 },
    { "F12", KM_F12 },
};
#define NKN ((int)(sizeof kn / sizeof kn[0]))
/* CFile's shifted and controlled arrows, as one name */
static const struct { const char *n; UWORD k; } kn2[] = {
    { "S-BS", KQ_S | 8 }, { "C-BS", KQ_C | 8 },
};

/* The raw keys that are named keys, and theirs */
static const UBYTE rawk[] = { 0x4C, 0x4D, 0x4E, 0x4F, 0x5F, 0x47, 0x48, 0x49,
                              0x70, 0x71, 0x41, 0x46, 0x45, 0x44, 0x43, 0x42,
                              0x40, 0x4B, 0x6F };
static const UWORD rawv[] = { KM_UP, KM_DOWN, KM_RIGHT, KM_LEFT, KM_HELP,
                              KM_INS, KM_PGUP, KM_PGDN, KM_HOME, KM_END, 8,
                              127, 27, 13, 13, 9, ' ', KM_F11, KM_F12 };

/* A key as CTerm's actions know it (0: none of theirs: a qualifier, a
 * release, Left Amiga, a key the keymap makes a string). prev: the dead
 * keys before it (the message's IAddress). */
UWORD keys_code(UWORD code, UWORD q, APTR prev)
{
    UWORD k = 0, m = 0;
    int i;
    if ((code & IECODE_UP_PREFIX) || (code >= 0x60 && code <= 0x67) ||
        code == 0x7A || code == 0x7B)
        return 0;
    if (q & IEQUALIFIER_CONTROL) m |= KQ_C;
    if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) m |= KQ_M;
    if (q & IEQUALIFIER_RCOMMAND) m |= KQ_A;
    if (q & IEQUALIFIER_LCOMMAND) m |= KQ_L;
    if (code >= 0x50 && code <= 0x59)
        k = KM_F1 + code - 0x50;
    for (i = 0; !k && i < (int)sizeof rawk; i++)
        if (code == rawk[i])
            k = rawv[i];
    if (k) {
        if (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
            m |= KQ_S;
    } else {
        struct InputEvent ie;
        char b[4];
        memset(&ie, 0, sizeof ie);
        ie.ie_Class = IECLASS_RAWKEY;
        ie.ie_Code = code;
        ie.ie_Qualifier = q & ~(IEQUALIFIER_CONTROL | IEQUALIFIER_LALT |
                                IEQUALIFIER_RALT | IEQUALIFIER_RCOMMAND |
                                IEQUALIFIER_LCOMMAND);
        ie.ie_EventAddress = prev;
        if (MapRawKey(&ie, (STRPTR)b, sizeof b, NULL) != 1 ||
            (UBYTE)b[0] < ' ')
            return 0;
        k = (UBYTE)b[0];
        if ((m & (KQ_C | KQ_A | KQ_L)) && k >= 'A' && k <= 'Z')
            k += 32;
    }
    return k | m;
}

/* A key from its text in the file ("q", "<F5>", "<A-t>", "<C-S-Tab>"),
 * 0: not a key. */
UWORD keys_parse(const char *s, int len)
{
    UWORD m = 0, k = 0;
    int i;
    if (len == 1)
        return (UBYTE)s[0];
    if (len < 3 || s[0] != '<' || s[len - 1] != '>')
        return 0;
    s++;
    len -= 2;
    for (i = 0; i < 2; i++)             /* <S-BS>, <C-BS> as CFile has them */
        if ((int)strlen(kn2[i].n) == len && !strncmp(kn2[i].n, s, len))
            return kn2[i].k;
    while (len > 2 && s[1] == '-' && strchr("SCMAL", s[0])) {
        m |= s[0] == 'S' ? KQ_S : s[0] == 'C' ? KQ_C : s[0] == 'M' ? KQ_M
           : s[0] == 'L' ? KQ_L : KQ_A;
        s += 2;
        len -= 2;
    }
    if (len == 1) {
        k = (UBYTE)s[0];
        if (m & KQ_S)                   /* Shift is in the character */
            return 0;
        if ((m & (KQ_C | KQ_A | KQ_L)) && k >= 'A' && k <= 'Z')
            k += 32;
        return k | m;
    }
    for (i = 0; i < NKN; i++)
        if ((int)strlen(kn[i].n) == len && !strncmp(kn[i].n, s, len))
            return kn[i].k | m;
    if (s[0] == 'F' && len <= 3 && atoi(s + 1) >= 1 && atoi(s + 1) <= 10)
        return (KM_F1 + atoi(s + 1) - 1) | m;
    return 0;
}

/* A key's name: plain FALSE as the file writes it ("<A-t>", "<L-q>"),
 * TRUE in words ("Right Amiga+T", "Left Amiga+Q"). o: 40 bytes. */
void keys_name(UWORD k, char *o, BOOL plain)
{
    UWORD b = k & KQ_KEY;
    const char *n = NULL;
    char one[2];
    int i;
    o[0] = 0;
    for (i = 0; i < NKN; i++)
        if (kn[i].k == b)
            n = kn[i].n;
    if (!plain) {
        if (!(k & KQ_MODS) && !n && !(b >= KM_F1 && b < KM_F1 + 10) &&
            b > ' ') {
            o[0] = b;
            o[1] = 0;
            return;
        }
        strcpy(o, "<");
        if (k & KQ_L) strcat(o, "L-");
        if (k & KQ_A) strcat(o, "A-");
        if (k & KQ_C) strcat(o, "C-");
        if (k & KQ_M) strcat(o, "M-");
        if (k & KQ_S) strcat(o, "S-");
    } else {
        if (k & KQ_L) strcat(o, "Left Amiga+");
        if (k & KQ_A) strcat(o, "Right Amiga+");
        if (k & KQ_C) strcat(o, "Ctrl+");
        if (k & KQ_M) strcat(o, "Alt+");
        if (k & KQ_S) strcat(o, "Shift+");
    }
    if (n)
        strcat(o, n);
    else if (b >= KM_F1 && b < KM_F1 + 10) {
        strcat(o, "F");
        numstr_cat(o, b - KM_F1 + 1);
    } else {
        one[0] = plain && b >= 'a' && b <= 'z' ? b - 32 : b;
        one[1] = 0;
        strcat(o, one);
    }
    if (!plain)
        strcat(o, ">");
}

/* The action key k does in section ctx: 0 none. */
int keys_find(int ctx, UWORD k)
{
    int i;
    if (!k)
        return 0;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].key == k && keys_acts[keys_binds[i].act].ctx == ctx)
            return keys_binds[i].act;
    return 0;
}

/* Is this raw key action a's? (a window message, before its reply) */
BOOL keys_is(int a, UWORD code, UWORD q, APTR prev)
{
    return keys_find(keys_acts[a].ctx, keys_code(code, q, prev)) == a;
}

/* Action a's first key (0: none). */
UWORD keys_first(int a)
{
    int i;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].act == a)
            return keys_binds[i].key;
    return 0;
}

/* Action a's first key in words, for messages ("Esc gives up"); "-"
 * when it has none. */
const char *keys_word(int a)
{
    static char w[40];
    UWORD k = keys_first(a);
    if (!k)
        return "-";
    keys_name(k, w, TRUE);
    return w;
}

/* say() with action a's key in words between (a message in the tab) */
void say_key(const char *before, int a, const char *after)
{
    say(before);
    say(keys_word(a));
    say(after);
}

/* Bind k to a (in a's section), in place of what k did there; a 0
 * (none) with section ctx takes k away there. */
void keys_bind(int ctx, UWORD k, int a)
{
    int i;
    if (a)
        ctx = keys_acts[a].ctx;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].key == k && keys_acts[keys_binds[i].act].ctx == ctx) {
            memmove(&keys_binds[i], &keys_binds[i + 1],
                    (keys_n - i - 1) * sizeof keys_binds[0]);
            keys_n--;
            break;
        }
    if (a && keys_n < MAXKB) {
        keys_binds[keys_n].key = k;
        keys_binds[keys_n++].act = a;
    }
}

/* Every key of action a gone. */
void keys_unbind(int a)
{
    int i, n = 0;
    for (i = 0; i < keys_n; i++)
        if (keys_binds[i].act != a)
            keys_binds[n++] = keys_binds[i];
    keys_n = n;
}

/* The character Shift + a raw key gives on this keymap (Shift+1 is ! on
 * most, Shift+- is _ on a Swedish one): for i3's Mod+Shift keys. */
static UWORD shifted(UWORD code)
{
    struct InputEvent ie;
    char b[4];
    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code;
    ie.ie_Qualifier = IEQUALIFIER_LSHIFT;
    if (MapRawKey(&ie, (STRPTR)b, sizeof b, NULL) != 1 || (UBYTE)b[0] <= ' ')
        return 0;
    return (UBYTE)b[0];
}

/* The raw key that gives - on this keymap (0x0B on a US one, 0x3A on a
 * Swedish one), for Shift+- */
static UWORD minus_key(void)
{
    struct InputEvent ie;
    char b[4];
    UWORD code;
    for (code = 0; code < 0x40; code++) {
        memset(&ie, 0, sizeof ie);
        ie.ie_Class = IECLASS_RAWKEY;
        ie.ie_Code = code;
        if (MapRawKey(&ie, (STRPTR)b, sizeof b, NULL) == 1 && b[0] == '-')
            return code;
    }
    return 0x0B;
}

/* Left Amiga+Shift+1-9: a tab to workspace n; +Shift+-: to the scratchpad
 * (only where the key is free). */
static void shifted_defaults(void)
{
    int n;
    UWORD c;
    for (n = 0; n < NWS; n++)
        if ((c = shifted(0x01 + n)) && !keys_find(KC_PANES, KQ_L | c))
            keys_bind(KC_PANES, KQ_L | c, KA_P_MOVEWS1 + n);
    if ((c = shifted(minus_key())) && !keys_find(KC_PANES, KQ_L | c))
        keys_bind(KC_PANES, KQ_L | c, KA_P_TOSCRATCH);
}

void keys_defaults(void)
{
    keys_n = NDEF;
    CopyMem((APTR)defbinds, keys_binds, sizeof defbinds);
    shifted_defaults();
    /* the login keys of older settings (keyuser=, keypass=) */
    if (userkey[0] != 'U') {
        keys_unbind(KA_SENDUSER);
        if (userkey[0] > ' ' && userkey[0] != '-')
            keys_bind(0, KQ_A | (UBYTE)tolower((UBYTE)userkey[0]),
                     KA_SENDUSER);
    }
    if (passkey[0] != 'P') {
        keys_unbind(KA_SENDPASS);
        if (passkey[0] > ' ' && passkey[0] != '-')
            keys_bind(0, KQ_A | (UBYTE)tolower((UBYTE)passkey[0]),
                     KA_SENDPASS);
    }
}

/* One line of the file: pass 0 notes the actions it names (they get
 * the file's keys only), pass 1 binds. "none action": no key. */
static void key_line(char *l, int *ctx, int no, int pass, UBYTE *named)
{
    char *keys, *act;
    int a, kl, i;
    BOOL nokey;
    UWORD k = 0;
    while (*l == ' ' || *l == '\t')
        l++;
    if (!*l || *l == ';' || *l == '#')
        return;
    if (*l == '[' && l[1] && strchr(l, ']') && !strchr(l, ' ')) {
        *ctx = 0;
        for (i = 0; i < NSEC; i++)
            if (!strnicmp(l + 1, secnames[i], strlen(secnames[i])) &&
                l[1 + strlen(secnames[i])] == ']')
                *ctx = i < KC_N ? i + 1 : i - 2;    /* [panels] [line] */
        if (!*ctx && !keys_bad[0] && pass) {
            strcpy(keys_bad, "line ");
            numstr_cat(keys_bad, no);
            strcat(keys_bad, ": unknown section");
        }
        return;
    }
    keys = l;
    while (*l && *l != ' ' && *l != '\t')
        l++;
    kl = l - keys;
    while (*l == ' ' || *l == '\t')
        l++;
    act = l;
    while (*l && *l != ' ' && *l != '\t')
        l++;
    *l = 0;
    for (a = 0; a < KA_N; a++)
        if ((!a || keys_acts[a].ctx == *ctx) && !stricmp(keys_acts[a].name, act))
            break;
    nokey = kl == 4 && !strnicmp(keys, "none", 4) && a && a < KA_N;
    if (!nokey)
        k = keys_parse(keys, kl);
    if ((!k && !nokey) || a == KA_N || !*ctx) {
        if (!keys_bad[0] && pass) {
            strcpy(keys_bad, "line ");
            numstr_cat(keys_bad, no);
            strcat(keys_bad, " not understood");
        }
        return;
    }
    if (!pass) {
        named[a] = 1;
        return;
    }
    if (k)
        keys_bind(*ctx, k, a);
}

static void key_text(char *t, int ctx, int pass, UBYTE *named)
{
    int no = 0;
    char *e;
    while (t && *t) {
        int l;
        if ((e = strchr(t, '\n')))
            *e = 0;
        l = strlen(t);
        while (l && (t[l - 1] == '\r' || t[l - 1] == ' '))
            t[--l] = 0;
        key_line(t, &ctx, ++no, pass, named);
        if (e)
            *e = '\n';                  /* for the second pass */
        t = e ? e + 1 : NULL;
    }
}

/* A key file's text (NULL: none; *gone: it is not there). */
static char *key_read(const char *name, BOOL *gone)
{
    char *t = NULL;
    BPTR f;
    LONG n;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR wp = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;        /* no "insert volume" */
    f = Open((STRPTR)name, MODE_OLDFILE);
    *gone = !f && IoErr() == ERROR_OBJECT_NOT_FOUND;
    me->pr_WindowPtr = wp;
    if (!f)
        return NULL;
    Seek(f, 0, OFFSET_END);
    n = Seek(f, 0, OFFSET_BEGINNING);
    if (n < 0 || n > 16000)
        n = 16000;
    if ((t = AllocVec(n + 1, MEMF_ANY))) {
        n = Read(f, t, n);
        t[n > 0 ? n : 0] = 0;
    }
    Close(f);
    return t;
}

/* A file's keys over the built-in ones: an action it names has its
 * keys only. ctx: the section before the first [section]. */
static void key_apply(char *t, int ctx, UBYTE *named)
{
    int a;
    memset(named, 0, KA_N);
    key_text(t, ctx, 0, named);
    for (a = 1; a < KA_N; a++)
        if (named[a])
            keys_unbind(a);
    key_text(t, ctx, 1, named);
}

/* The built-in keys, then the file: an action it names has the file's
 * keys only; the others (new ones since it was written) keep theirs.
 * No file: written with the built-in keys. The file browser's keys
 * were files.keys of their own until 0.3b18: when CTerm.keys has no
 * [files] or [filesline] yet, files.keys is read into it once, written
 * whole, and renamed files.keys.old (left for the person, not read
 * again). A file from before [panes] (0.3b19) is written again once,
 * so it lists them. */
void keys_load(void)
{
    char *t, bad[80];
    BOOL gone, oldgone, infile, browser = FALSE, panes = FALSE;
    BOOL showkeys = TRUE;
    UBYTE named[KA_N];
    int a;
    keys_defaults();
    keys_bad[0] = 0;
    t = key_read(KEYFILE, &gone);
    if (!t && !gone)
        return;                         /* there, but not readable: kept */
    if ((infile = t != NULL)) {
        key_apply(t, KC_WINDOW, named);
        showkeys = strstr(t, KEYMARK) != NULL;
        FreeVec(t);
        for (a = KA_FB_FIRST; a <= KA_FB_LAST; a++)
            if (named[a])
                browser = TRUE;
        for (a = KA_P_OPEN; a <= KA_P_SHOW9; a++)
            if (named[a])
                panes = TRUE;
    }
    if (infile && browser && !panes)
        keys_save();                    /* (0.3b19) listed with [panes] */
    if (infile && panes) {
        /* (0.3b38) a file from before workspaces: LA+1-9 showed tab n in
         * the pane; now they are i3's workspaces, and resize, scratchpad
         * and Mod+Shift come in where their keys are free */
        BOOL ws = FALSE;
        for (a = KA_P_WS1; a < KA_N; a++)
            if (named[a])
                ws = TRUE;
        if (!ws) {
            int n, i;
            for (n = 0; n < NWS; n++)
                for (i = 0; i < keys_n; i++)
                    if (keys_binds[i].act == KA_P_SHOW1 + n && keys_binds[i].key == (KQ_L | ('1' + n)))
                        keys_binds[i].act = KA_P_WS1 + n;
            if (!keys_find(KC_PANES, KQ_L | 'r')) keys_bind(KC_PANES, KQ_L | 'r', KA_P_RESIZE);
            if (!keys_find(KC_PANES, KQ_L | '-')) keys_bind(KC_PANES, KQ_L | '-', KA_P_SCRATCH);
            shifted_defaults();
            keys_save();
        }
    }
    if (infile && panes && !showkeys) {
        /* (0.3b41) show1-9 lost LA+1-9 to the workspaces in 0.3b38:
         * LA+Ctrl+1-9 now, where free; once (the file then has KEYMARK,
         * so a show key taken away later stays away) */
        int n;
        for (n = 0; n < NWS; n++)
            if (!keys_first(KA_P_SHOW1 + n) &&
                !keys_find(KC_PANES, KQ_L | KQ_C | ('1' + n)))
                keys_bind(KC_PANES, KQ_L | KQ_C | ('1' + n), KA_P_SHOW1 + n);
        keys_save();
    }
    if (!browser) {
        BOOL old = FALSE;
        if ((t = key_read(KEYFILE_OLD, &oldgone))) {
            strcpy(bad, keys_bad);      /* its bad lines are not said */
            key_apply(t, KC_FILES, named);  /* [panels] if it names none */
            FreeVec(t);
            strcpy(keys_bad, bad);
            old = TRUE;
        }
        if (keys_save()) {              /* written whole, the browser's in */
            infile = TRUE;
            if (old) {
                DeleteFile((STRPTR)KEYFILE_GONE);
                Rename((STRPTR)KEYFILE_OLD, (STRPTR)KEYFILE_GONE);
            }
        }
    }
    if (infile) {
        userkey[0] = 'U';               /* the old settings' keys: in the */
        passkey[0] = 'P';               /* file now */
    }
}

static void kput(BPTR f, const char *s)
{
    FPuts(f, (STRPTR)s);
}

/* The keys as they are, into the file: each section's actions in order,
 * a line a key ("none action" for one with none). FALSE: not written. */
BOOL keys_save(void)
{
    BPTR f;
    int a, i, c, o;
    char kb[40];
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR wp = me->pr_WindowPtr;
    BOOL ok;
    me->pr_WindowPtr = (APTR)-1;
    UnLock(CreateDir((STRPTR)"ENVARC:CTerm"));
    f = Open((STRPTR)KEYFILE, MODE_NEWFILE);
    me->pr_WindowPtr = wp;
    if (!f)
        return FALSE;
    kput(f, "; CTerm's own keys (README: Keys). One a line: KEYS ACTION.\n"
         "; KEYS: a character, or a name in <>: <Up> <Down> <Left> <Right>\n"
         "; <Return> <Esc> <Tab> <Space> <Del> <BS> <Help> <F1>..<F12> <PgUp>\n"
         "; <PgDn> <Home> <End> <Ins>, or a character, after any of A- (Right\n"
         "; Amiga), L- (Left Amiga), C- (Ctrl), M- (Alt), S- (Shift, for\n"
         "; names): <A-t> <L-q> <C-r> <S-Up> <C-S-Tab> <L-S-Left>.\n"
         "; 'none action': the action has no key; 'KEYS none': the key does\n"
         "; nothing of CTerm's. A key no action has goes to the program\n"
         "; (the Shell, the host), as on any terminal.\n"
         "; Each section's keys are its own: a key may be in two sections.\n"
         "; [window]'s come first, in every tab (but its history keys in\n"
         "; the file browser), then [panes]'s.\n"
         KEYMARK "\n");
    for (o = 0; o < KC_N; o++) {
        c = secorder[o];
        kput(f, c == KC_WINDOW ? "\n; in every tab (menus, tabs, history)\n"
             : c == KC_PANES ? "\n; the panes, as i3 and Sway on Linux "
                               "drive them (L- is Left Amiga)\n"
             : c == KC_CALL ? "\n; while a call is made or a transfer runs; "
                              "after a call ended\n"
             : c == KC_SHELL ? "\n; the Amiga Shell's line, as it is typed\n"
             : c == KC_FILES ? "\n; the file browser's panels (FTP, SFTP). "
                               "Letters no key has find a\n; name as they are "
                               "typed; yes and no are always y and n. Right\n"
                               "; Amiga and Alt do not come here (Alt types "
                               "its character)\n"
             : "\n; a line typed in the file browser (a name, the command "
               "line)\n");
        kput(f, "[");
        kput(f, secnames[c - 1]);
        kput(f, "]\n");
        for (a = 1; a < KA_N; a++) {
            BOOL any = FALSE;
            if (keys_acts[a].ctx != c)
                continue;
            for (i = 0; i < keys_n; i++)
                if (keys_binds[i].act == a) {
                    keys_name(keys_binds[i].key, kb, FALSE);
                    kput(f, kb);
                    kput(f, " ");
                    kput(f, keys_acts[a].name);
                    kput(f, "\n");
                    any = TRUE;
                }
            if (!any) {
                kput(f, "none ");
                kput(f, keys_acts[a].name);
                kput(f, "\n");
            }
        }
    }
    ok = Close(f) != 0;
    return ok;
}
