/*
 * keys - the keyboard, and the mouse for a program in a host's tab.
 */
#include "cterm.h"

static BOOL redial_key(const char *b);
static void raw_report(int cls, int code, int qual);
static void bar_pull(int y);

/* After a call ended: Return calls again; other keys do nothing. */
static BOOL redial_key(const char *b)
{
    if (local || !dialhost[0] || sock >= 0)
        return FALSE;
    if (b[0] == '\r') {
        say("\x1b[2m[calling ");
        say(dialhost);
        say("... Esc gives up]\x1b[0m\r\n");
        refresh();
        if (connect_out())
            say("\x1b[2m[connected]\x1b[0m\r\n");
        else
            say("\x1b[2m[Return calls again]\x1b[0m\r\n");
        refresh();                      /* the screen waited with the call */
    }
    return TRUE;
}

void send_str(const char *s)
{
    send_data((const UBYTE *)s, strlen(s));
}

/* Latin-1 typed on the Amiga, sent as UTF-8 when the host speaks it. */
/* Typed bytes (Latin-1) in one write: a key's ESC and letter must not go
 * out as two packets - programs time a lone ESC. */
void send_typed(const UBYTE *b, LONG n)
{
    UBYTE out[512];
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        if (k > (LONG)sizeof out - 2) {
            send_data(out, k);
            k = 0;
        }
        if (utf8 && b[i] >= 0x80) {
            out[k++] = 0xC0 | (b[i] >> 6);
            out[k++] = 0x80 | (b[i] & 63);
        } else
            out[k++] = b[i];
    }
    if (k)
        send_data(out, k);
}

void key(struct IntuiMessage *m)
{
    UWORD code = m->Code, q = m->Qualifier;
    BOOL shift = (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    BOOL ctrl = (q & IEQUALIFIER_CONTROL) != 0;
    /* Alt is Meta in a host's tab (the setting); the local Shell keeps
     * the Amiga's Alt characters */
    BOOL meta = altmeta && (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT));
    int mod = 1 + (shift ? 1 : 0) + (meta ? 2 : 0) + (ctrl ? 4 : 0);
    char buf[16];
    LONG n;
    static const char *const fkeys[10] = {
        "\x1bOP", "\x1bOQ", "\x1bOR", "\x1bOS", "\x1b[15~", "\x1b[17~",
        "\x1b[18~", "\x1b[19~", "\x1b[20~", "\x1b[21~"
    };
    if (shown && shown->t_xfer) {
        /* a transfer has the line: Esc stops it, other keys wait, but
         * Right Amiga keys (tabs, menus) still work */
        if (code == 0x45)
            shown->t_xfer->stop = 1;
        if (!(q & IEQUALIFIER_RCOMMAND))
            return;
    }

    if (code & IECODE_UP_PREFIX)
        return;
    if ((q & IEQUALIFIER_RCOMMAND) && code >= 0x01 && code <= 0x09) {
        tab_show(code - 0x01);          /* Right Amiga + 1..9: that tab */
        return;
    }
    if (local) {
        /* the Amiga Shell: the keymap's own bytes, as CON: gives them;
         * only the scrollback keys stay CTerm's */
        if (code == 0x7A) { scroll_view(back + 3); return; }
        if (code == 0x7B) { scroll_view(back - 3); return; }
        if (shift && (code == 0x4C || code == 0x4D)) {
            scroll_view(back + (code == 0x4C ? rows - 1 : -(rows - 1)));
            return;
        }
        {
            struct InputEvent ie;
            memset(&ie, 0, sizeof ie);
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = code;
            ie.ie_Qualifier = q;
            ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
            n = MapRawKey(&ie, (STRPTR)buf, sizeof buf, NULL);
        }
        if (n > 0) {
            if (back)
                scroll_view(0);
            con_keys(T->con, (UBYTE *)buf, n);
        }
        return;
    }
    switch (code) {
    case 0x7A: case 0x7B:                                /* the wheel */
        if (mouse_to_host(0) && !back) {
            int r, c;
            mouse_cell(win->MouseX, win->MouseY, &r, &c);
            mouse_send(code == 0x7A ? 64 : 65, r, c, FALSE, q);
            return;
        }
        scroll_view(back + (code == 0x7A ? 3 : -3));
        return;
    case 0x4C: case 0x4D: case 0x4E: case 0x4F: {        /* arrows */
        static const char dirs[4] = { 'A', 'B', 'C', 'D' };
        char d = dirs[code - 0x4C];
        if (mod == 2 && (code == 0x4C || code == 0x4D)) {   /* shift alone */
            scroll_view(back + (code == 0x4C ? rows - 1 : -(rows - 1)));
            return;
        }
        if (mod > 1)                    /* xterm: ESC [ 1 ; mod X */
            buf[0] = 0x1b, buf[1] = '[', buf[2] = '1', buf[3] = ';',
            buf[4] = '0' + mod, buf[5] = d, buf[6] = 0;
        else if (vt_app_cursor(vt))
            buf[0] = 0x1b, buf[1] = 'O', buf[2] = d, buf[3] = 0;
        else
            buf[0] = 0x1b, buf[1] = '[', buf[2] = d, buf[3] = 0;
        break;
    }
    case 0x41:                                           /* backspace */
        strcpy(buf, meta ? "\x1b\x7f" : "\x7f");
        break;
    case 0x46:                                           /* del */
        if (mod > 1)
            strcpy(buf, "\x1b[3;"), buf[4] = '0' + mod, buf[5] = '~',
            buf[6] = 0;
        else
            strcpy(buf, "\x1b[3~");
        break;
    case 0x45: strcpy(buf, meta ? "\x1b\x1b" : "\x1b"); break;  /* esc */
    case 0x44: case 0x43: strcpy(buf, meta ? "\x1b\r" : "\r"); break;
    case 0x42:                                           /* tab */
        strcpy(buf, shift ? "\x1b[Z" : meta ? "\x1b\t" : "\t");
        break;
    case 0x5F: strcpy(buf, "\x1b[2~"); break;            /* help: insert */
    default:
        if (code >= 0x50 && code <= 0x59) {
            strcpy(buf, fkeys[code - 0x50]);
            break;
        }
        {
            /* Meta: the key as without Alt, after an ESC (as xterm's
             * metaSendsEscape) - bash's Alt+B/F, emacs, Claude Code */
            struct InputEvent ie;
            memset(&ie, 0, sizeof ie);
            ie.ie_Class = IECLASS_RAWKEY;
            ie.ie_Code = code;
            ie.ie_Qualifier = meta ? q & ~(IEQUALIFIER_LALT | IEQUALIFIER_RALT)
                                   : q;
            ie.ie_EventAddress = m->IAddress ? *((APTR *)m->IAddress) : NULL;
            n = MapRawKey(&ie, (STRPTR)buf + 1, sizeof buf - 1, NULL);
        }
        if (n <= 0)
            return;
        if (back)
            scroll_view(0);
        if (meta) {
            buf[0] = 0x1b;
            n++;
        }
        if (!redial_key(buf + 1))
            send_typed((UBYTE *)(meta ? buf : buf + 1), n);
        return;
    }
    if (back)
        scroll_view(0);
    if (!redial_key(buf))
        send_str(buf);
}
/* ======================================================================
 * Screens, windows and menus
 * ==================================================================== */


/* A raw input event report, for an Amiga program that asked (CSI n{). */
static void raw_report(int cls, int code, int qual)
{
    char r[64];
    UBYTE *p;
    if (!local || !(vt_raw_events(vt) & (1UL << cls)))
        return;
    r[0] = (char)0x9B;
    r[1] = 0;
    numstr_cat(r, cls); strcat(r, ";0;");
    numstr_cat(r, code); strcat(r, ";");
    numstr_cat(r, qual); strcat(r, ";0;0;0;0|");
    p = (UBYTE *)r;
    con_reply(T->con, p, strlen(r));
}

/* ---- the mouse for the program in a host's tab (xterm's reports) ------
 * The program asks (CSI ?1000h and friends); then clicks, the wheel and,
 * when asked, motion go to it, and Shift+drag still marks text here. The
 * right button stays the Amiga's menu button. */

static int mbutton = -1;                /* held, as the program sees it */
static int mlast_r = -1, mlast_c = -1;  /* the cell last reported */
static BOOL mreporting;                 /* ReportMouse is on */

/* The program wants the mouse (and the person is not Shift-marking). */
BOOL mouse_to_host(UWORD q)
{
    int sgr;
    return !local && sock >= 0 && vt_mouse(vt, &sgr) &&
           !(q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT));
}

/* b: 0 left, 1 middle, 2 right, 3 release (old form), +32 motion, 64/65
 * the wheel; the cell is 0-based. */
void mouse_send(int b, int r, int c, BOOL release, UWORD q)
{
    char out[40];
    int sgr, mode = vt_mouse(vt, &sgr);
    if (mode == 9) {                    /* X10: presses only, no keys */
        if (release || (b & 32))
            return;
    } else {
        if (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) b |= 4;
        if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) b |= 8;
        if (q & IEQUALIFIER_CONTROL) b |= 16;
    }
    if (sgr) {
        strcpy(out, "\x1b[<");
        numstr_cat(out, b);
        strcat(out, ";");
        numstr_cat(out, c + 1);
        strcat(out, ";");
        numstr_cat(out, r + 1);
        strcat(out, release ? "m" : "M");
        send_data((UBYTE *)out, strlen(out));
    } else {
        UBYTE o[6];
        if (c + 1 + 32 > 255 || r + 1 + 32 > 255)
            return;                     /* the old form ends at 223 */
        o[0] = 0x1b; o[1] = '['; o[2] = 'M';
        o[3] = 32 + (release && !(b & 64) ? 3 | (b & ~3) : b);
        o[4] = 32 + c + 1;
        o[5] = 32 + r + 1;
        send_data(o, 6);
    }
}

/* Motion reports and selection drags need Intuition's mouse moves. */
static BOOL bardrag;                    /* pulling full screen's place bar */

/* Full screen's place bar, pulled to y: the knob's middle goes there. */
static void bar_pull(int y)
{
    LONG hist = vt_history(vt), total = hist + rows, top;
    int h = rows * ch, knob = (int)((LONG)h * rows / total);
    if (hist <= 0)
        return;
    top = (LONG)(y - oy - knob / 2) * total / h;
    if (top < 0) top = 0;
    if (top > hist) top = hist;
    scroll_view(hist - top);
}

void mouse_reporting(void)
{
    int sgr, mode = !local && sock >= 0 ? vt_mouse(vt, &sgr) : 0;
    BOOL want = sel_drag || bardrag || mode == 1003 ||
                (mode == 1002 && mbutton >= 0);
    if (want != mreporting && win) {
        ReportMouse(want, win);
        mreporting = want;
    }
}

BOOL window_events(void)
{
    struct IntuiMessage *m;
    BOOL go = TRUE;
    while (!want_switch && (m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        struct IntuiMessage copy = *m;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        if (copy.Class == IDCMP_RAWKEY) {
            /* IAddress is valid only until the reply: keep what it points
             * at (the dead keys before, for MapRawKey) and answer first -
             * a key can start a long wait (Return calls again), and while
             * one is unanswered no more messages came */
            APTR prev = m->IAddress ? *((APTR *)m->IAddress) : NULL;
            copy.IAddress = &prev;
            ReplyMsg((struct Message *)m);
            key(&copy);
            continue;
        }
        ReplyMsg((struct Message *)m);
        switch (copy.Class) {
        case IDCMP_CLOSEWINDOW:
            if (local && !T->con)
                T->ended = TRUE;        /* no Shell ever started here */
            else if (local) {
                if (vt_raw_events(vt) & (1UL << 11))
                    raw_report(11, 0, 0);
                else
                    con_close_gadget(T->con);
            } else if (ntabs > 1)
                T->ended = TRUE;        /* this host's tab goes */
            else
                go = FALSE;
            break;
        case IDCMP_NEWSIZE:
            layout_and_resize();
            painter_setup();
            redraw_all();
            RefreshWindowFrame(win);
            {
                struct Tab *keep = T;
                int i;
                for (i = 0; i < ntabs; i++) {
                    T = tabs[i];
                    if (sock >= 0)
                        send_naws();
                    if (T->con)
                        raw_report(12, 0, 0);
                }
                T = keep;
            }
            break;
        case IDCMP_REFRESHWINDOW:
            repair_damage();            /* only the rows a window uncovered */
            break;
        case IDCMP_MENUPICK: {
            UWORD code = copy.Code;
            if (win->MenuStrip != menu) {
                /* Ed's own menus on our window: its business */
                raw_report(10, code, copy.Qualifier);
                break;
            }
            while (code != MENUNULL && go && !want_switch) {
                struct MenuItem *it = ItemAddress(menu, code);
                if (!it) break;
                go = menu_pick((ULONG)GTMENUITEM_USERDATA(it));
                code = it->NextSelect;
            }
            break;
        }
        case IDCMP_ACTIVEWINDOW:
        case IDCMP_INACTIVEWINDOW:
            if (!local && sock >= 0 && vt_focus(vt))
                send_data((UBYTE *)(copy.Class == IDCMP_ACTIVEWINDOW
                                        ? "\x1b[I" : "\x1b[O"), 3);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (copy.Code == SELECTDOWN &&
                xfer_click(copy.MouseX, copy.MouseY))
                break;                  /* the transfer's Stop */
            if (copy.Code == SELECTDOWN && fullscreen &&
                copy.MouseX >= win->Width - POSBAR - 4 && copy.MouseY >= oy &&
                copy.MouseY < oy + rows * ch && vt_history(vt) > 0) {
                bardrag = TRUE;         /* the thin bar, pulled */
                bar_pull(copy.MouseY);
                mouse_reporting();
                break;
            }
            if (copy.Code == SELECTUP && bardrag) {
                bardrag = FALSE;
                mouse_reporting();
                break;
            }
            if (copy.Code == SELECTDOWN && tabbar_h() &&
                copy.MouseY >= win->BorderTop &&
                copy.MouseY < win->BorderTop + tabbar_h()) {
                int wd = win->Width - win->BorderLeft - win->BorderRight, i;
                i = (copy.MouseX - win->BorderLeft) * ntabs / wd;
                if (i >= 0 && i < ntabs)
                    tab_show(i);
                break;
            }
            if ((copy.Code == SELECTDOWN || copy.Code == MIDDLEDOWN ||
                 ((copy.Code == SELECTUP || copy.Code == MIDDLEUP) &&
                  mbutton >= 0)) &&
                mouse_to_host(copy.Qualifier)) {
                int r, c;
                BOOL up = copy.Code == SELECTUP || copy.Code == MIDDLEUP;
                int b = copy.Code == MIDDLEDOWN || copy.Code == MIDDLEUP;
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                mouse_send(b, r, c, up, copy.Qualifier);
                mbutton = up ? -1 : b;
                mlast_r = r;
                mlast_c = c;
                mouse_reporting();
                break;
            }
            if (copy.Code == SELECTDOWN && copy.MouseX >= ox &&
                copy.MouseY >= oy && copy.MouseY < oy + rows * ch &&
                copy.MouseX < ox + cols * cw) {
                static ULONG ls, lm;
                static int lr = -1, lc, clicks;
                int r, c;
                sel_clear();
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                if (r == lr && c == lc &&
                    DoubleClick(ls, lm, copy.Seconds, copy.Micros))
                    clicks = clicks % 3 + 1;
                else
                    clicks = 1;
                ls = copy.Seconds; lm = copy.Micros;
                lr = r; lc = c;
                sa_r = sb_r = r;
                sa_c = sb_c = c;
                if (clicks > 1 && sel_word(r, c, clicks == 3)) {
                    sel_on = TRUE;      /* a word or the line, marked */
                    sel_redraw(r, r);
                } else
                    sel_drag = TRUE;    /* a selection only once it moves */
                mouse_reporting();
            } else if (copy.Code == SELECTUP && sel_drag) {
                sel_drag = FALSE;
                mouse_reporting();
            }
            break;
        case IDCMP_GADGETDOWN:
            if (g == &agup || g == &agdn) {
                arrowheld = g == &agup ? 1 : -1;
                arrowticks = 0;
                scroll_view(back + arrowheld);
            }
            break;
        case IDCMP_GADGETUP:
        case IDCMP_MOUSEMOVE:
            if (copy.Class == IDCMP_MOUSEMOVE && bardrag) {
                bar_pull(copy.MouseY);
                break;
            }
            if (copy.Class == IDCMP_MOUSEMOVE && !sel_drag &&
                mouse_to_host(0)) {
                int r, c, sgr, mode = vt_mouse(vt, &sgr);
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                if ((r != mlast_r || c != mlast_c) &&
                    (mode == 1003 || (mode == 1002 && mbutton >= 0))) {
                    mouse_send(32 + (mbutton >= 0 ? mbutton : 3), r, c,
                               FALSE, copy.Qualifier);
                    mlast_r = r;
                    mlast_c = c;
                }
                break;
            }
            if (copy.Class == IDCMP_MOUSEMOVE && sel_drag) {
                int r, c, r0, c0, r1, c1, n0, n1, m0, m1;
                mouse_cell(copy.MouseX, copy.MouseY, &r, &c);
                if (sel_on && r == sb_r && c == sb_c)
                    break;
                if (!sel_on && r == sa_r && c == sa_c)
                    break;              /* a click that has not moved */
                if (sel_on) {
                    sel_bounds(&r0, &c0, &r1, &c1);
                } else
                    r0 = r1 = sa_r;
                sb_r = r;
                sb_c = c;
                sel_on = TRUE;
                sel_bounds(&n0, &m0, &n1, &m1);
                (void)c0; (void)c1; (void)m0; (void)m1;
                sel_redraw(r0 < n0 ? r0 : n0, r1 > n1 ? r1 : n1);
                break;
            }
            if (g == &vgad || (vpi.Flags & KNOBHIT)) {
                LONG max = vt_history(vt);
                LONG top = ((ULONG)vpi.VertPot * max + 0x7FFF) / 0xFFFF;
                scroll_view(max - top);
            }
            if (copy.Class == IDCMP_GADGETUP)
                arrowheld = 0;
            break;
        case IDCMP_INTUITICKS:
            if (vpi.Flags & KNOBHIT) {
                LONG max = vt_history(vt);
                LONG top = ((ULONG)vpi.VertPot * max + 0x7FFF) / 0xFFFF;
                scroll_view(max - top);
            }
            if (arrowheld) {
                struct Gadget *a = arrowheld > 0 ? &agup : &agdn;
                if (!(a->Flags & GFLG_SELECTED)) arrowheld = 0;
                else if (++arrowticks > 2) scroll_view(back + arrowheld);
            }
            break;
        }
    }
    return go;
}

/* ======================================================================
 * The main loop
 * ==================================================================== */

BOOL title_force;
