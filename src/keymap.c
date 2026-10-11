/*
 * keymap - CTerm's own keys: every action CTerm itself does on a key
 * (the menus, tabs, panes, the history, the Shell's line editor, giving
 * up a call) is bound in ENVARC:CTerm/CTerm.keys, written with the keys
 * below when there is none. The format is CFile's and Ahiru's (and the
 * file browser's files.keys): one key a line, KEYS ACTION; [sections];
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
};
#undef A
#undef C
#define NDEF ((int)(sizeof defbinds / sizeof defbinds[0]))

struct KeyBind keys_binds[MAXKB];
int keys_n;
char keys_bad[80];                      /* the key file's first bad line */

static const char *const secnames[] = { "window", "call", "shell" };

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
        code == 0x7A || code == 0x7B ||
        ((q & IEQUALIFIER_LCOMMAND) && !(q & IEQUALIFIER_RCOMMAND)))
        return 0;
    if (q & IEQUALIFIER_CONTROL) m |= KQ_C;
    if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) m |= KQ_M;
    if (q & IEQUALIFIER_RCOMMAND) m |= KQ_A;
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
                                IEQUALIFIER_RALT | IEQUALIFIER_RCOMMAND);
        ie.ie_EventAddress = prev;
        if (MapRawKey(&ie, (STRPTR)b, sizeof b, NULL) != 1 ||
            (UBYTE)b[0] < ' ')
            return 0;
        k = (UBYTE)b[0];
        if ((m & (KQ_C | KQ_A)) && k >= 'A' && k <= 'Z')
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
    while (len > 2 && s[1] == '-' && strchr("SCMA", s[0])) {
        m |= s[0] == 'S' ? KQ_S : s[0] == 'C' ? KQ_C : s[0] == 'M' ? KQ_M
                                                                   : KQ_A;
        s += 2;
        len -= 2;
    }
    if (len == 1) {
        k = (UBYTE)s[0];
        if (m & KQ_S)                   /* Shift is in the character */
            return 0;
        if ((m & (KQ_C | KQ_A)) && k >= 'A' && k <= 'Z')
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

/* A key's name: plain FALSE as the file writes it ("<A-t>"), TRUE in
 * words ("Right Amiga+T", "Ctrl+Tab"). o: 24 bytes. */
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
        if (k & KQ_A) strcat(o, "A-");
        if (k & KQ_C) strcat(o, "C-");
        if (k & KQ_M) strcat(o, "M-");
        if (k & KQ_S) strcat(o, "S-");
    } else {
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
    static char w[24];
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

void keys_defaults(void)
{
    keys_n = NDEF;
    CopyMem((APTR)defbinds, keys_binds, sizeof defbinds);
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
        for (i = 0; i < 3; i++)
            if (!strnicmp(l + 1, secnames[i], strlen(secnames[i])) &&
                l[1 + strlen(secnames[i])] == ']')
                *ctx = i + 1;
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

static void key_text(char *t, int pass, UBYTE *named)
{
    int ctx = KC_WINDOW, no = 0;
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

/* The built-in keys, then the file: an action it names has the file's
 * keys only; the others (new ones since it was written) keep theirs.
 * No file: written with the built-in keys. */
void keys_load(void)
{
    char *t;
    BPTR f;
    LONG n;
    UBYTE named[KA_N];
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR wp = me->pr_WindowPtr;
    keys_defaults();
    keys_bad[0] = 0;
    me->pr_WindowPtr = (APTR)-1;        /* no "insert volume" */
    f = Open((STRPTR)KEYFILE, MODE_OLDFILE);
    if (!f) {
        if (IoErr() == ERROR_OBJECT_NOT_FOUND && keys_save()) {
            userkey[0] = 'U';
            passkey[0] = 'P';
        }
        me->pr_WindowPtr = wp;
        return;
    }
    me->pr_WindowPtr = wp;
    Seek(f, 0, OFFSET_END);
    n = Seek(f, 0, OFFSET_BEGINNING);
    if (n < 0 || n > 16000)
        n = 16000;
    if ((t = AllocVec(n + 1, MEMF_ANY))) {
        n = Read(f, t, n);
        t[n > 0 ? n : 0] = 0;
        memset(named, 0, sizeof named);
        key_text(t, 0, named);
        for (n = 1; n < KA_N; n++)
            if (named[n])
                keys_unbind(n);
        key_text(t, 1, named);
        FreeVec(t);
    }
    Close(f);
    userkey[0] = 'U';                   /* the old settings' keys: in the */
    passkey[0] = 'P';                   /* file now */
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
    int a, i, c;
    char kb[24];
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
         "; Amiga), C- (Ctrl), M- (Alt), S- (Shift, for names): <A-t> <C-r>\n"
         "; <S-Up> <C-S-Tab>. 'none action': the action has no key; 'KEYS\n"
         "; none': the key does nothing of CTerm's. A key no action has goes\n"
         "; to the program (the Shell, the host), as on any terminal.\n"
         "; The file browser's keys are in files.keys.\n");
    for (c = 1; c <= 3; c++) {
        kput(f, c == KC_WINDOW ? "\n; in every tab (menus, tabs, history)\n"
             : c == KC_CALL ? "\n; while a call is made or a transfer runs; "
                              "after a call ended\n"
             : "\n; the Amiga Shell's line, as it is typed\n");
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
