/*
 * serial - calls over a serial line, CTerm's side: the line's and the
 * modem's settings, the serial tabs, the menu's calls. The line's own
 * code (the device, dialling, the modem's words) is a file of its own,
 * PROGDIR:Protocols/Serial (sermod.c, sermod.h): loaded when a serial
 * tab opens its line, unloaded when the last line closes, so a person
 * without a modem never has it in memory.
 */
#include "cterm.h"
#pragma GCC optimize("Os")              /* called rarely: small before quick */
#include "sermod.h"

SerLine serdef = { "serial.device", 0, 38400, 8, SP_NONE, 1, SF_RTSCTS, 1,
                   0, 16 };
Modem modem = { "ATZ", "", "ATDT", "^M", "ATH0", SH_BOTH, 60, 0, 30, 0 };

const LONG ser_rates[] = { 300, 1200, 2400, 4800, 9600, 14400, 19200, 28800,
    31250, 38400, 57600, 76800, 115200, 230400, 460800, 0 };
const char *const ser_parname[] = { "N", "E", "O", "M", "S" };
static const char *const flowname[] = { "none", "rtscts", "xonxoff" };

void ser_line_default(SerLine *l)
{
    *l = serdef;
}

/* ---- text: the settings and the summary ---- */

void ser_line_text(const SerLine *l, char *out, int max)
{
    char s[120];
    strcpy(s, l->device);
    strcat(s, " ");
    numstr_cat(s, l->unit);
    strcat(s, ", ");
    numstr_cat(s, l->baud);
    strcat(s, " ");
    numstr_cat(s, l->bits);
    strcat(s, ser_parname[l->parity % 5]);
    numstr_cat(s, l->stop);
    strcat(s, l->flow == SF_RTSCTS ? ", RTS/CTS"
              : l->flow == SF_XONXOFF ? ", XON/XOFF" : ", no flow control");
    strncpy(out, s, max - 1);
    out[max - 1] = 0;
}

static void put(char *dst, int max, const char *v)
{
    strncpy(dst, v, max - 1);
    dst[max - 1] = 0;
}

static int which(const char *const *names, int n, const char *v)
{
    int i;
    for (i = 0; i < n; i++)
        if (!stricmp((char *)names[i], (char *)v))
            return i;
    return 0;
}

BOOL ser_pref(const char *k, const char *v)
{
    if (!stricmp(k, "serdev")) put(serdef.device, sizeof serdef.device, v);
    else if (!stricmp(k, "serunit")) serdef.unit = atol(v);
    else if (!stricmp(k, "serbaud")) serdef.baud = atol(v);
    else if (!stricmp(k, "serbits")) serdef.bits = atol(v) == 7 ? 7 : 8;
    else if (!stricmp(k, "serparity")) serdef.parity = which(ser_parname, 5, v);
    else if (!stricmp(k, "serstop")) serdef.stop = atol(v) == 2 ? 2 : 1;
    else if (!stricmp(k, "serflow")) serdef.flow = which(flowname, 3, v);
    else if (!stricmp(k, "serbuf")) serdef.bufkb = atol(v);
    else if (!stricmp(k, "serlocked")) serdef.locked = v[0] != '0';
    else if (!stricmp(k, "minit")) put(modem.init, sizeof modem.init, v);
    else if (!stricmp(k, "minit2")) put(modem.init2, sizeof modem.init2, v);
    else if (!stricmp(k, "mdial")) put(modem.dial, sizeof modem.dial, v);
    else if (!stricmp(k, "msuffix")) put(modem.suffix, sizeof modem.suffix, v);
    else if (!stricmp(k, "mhangcmd")) put(modem.hangcmd, sizeof modem.hangcmd, v);
    else if (!stricmp(k, "mhangup")) modem.hangup = atol(v) % 3;
    else if (!stricmp(k, "mwait")) modem.wait = atol(v);
    else if (!stricmp(k, "mredial")) modem.redial = atol(v);
    else if (!stricmp(k, "mpause")) modem.pause = atol(v);
    else if (!stricmp(k, "manswer")) modem.answer = v[0] != '0';
    else
        return FALSE;
    if (serdef.baud <= 0) serdef.baud = 38400;
    if (serdef.bufkb < 1) serdef.bufkb = 16;
    if (modem.wait < 5) modem.wait = 60;
    return TRUE;
}

static void kv(char *buf, const char *k, const char *v)
{
    strcat(buf, "\n");
    strcat(buf, k);
    strcat(buf, "=");
    strcat(buf, v);
}

static void kn(char *buf, const char *k, LONG n)
{
    kv(buf, k, "");
    numstr_cat(buf, n);
}

void ser_prefs_save(char *buf)
{
    kv(buf, "serdev", serdef.device);
    kn(buf, "serunit", serdef.unit);
    kn(buf, "serbaud", serdef.baud);
    kn(buf, "serbits", serdef.bits);
    kv(buf, "serparity", ser_parname[serdef.parity % 5]);
    kn(buf, "serstop", serdef.stop);
    kv(buf, "serflow", flowname[serdef.flow % 3]);
    kn(buf, "serbuf", serdef.bufkb);
    kn(buf, "serlocked", serdef.locked);
    kv(buf, "minit", modem.init);       /* empty is a choice too */
    kv(buf, "minit2", modem.init2);
    kv(buf, "mdial", modem.dial);
    kv(buf, "msuffix", modem.suffix);
    kv(buf, "mhangcmd", modem.hangcmd);
    kn(buf, "mhangup", modem.hangup);
    kn(buf, "mwait", modem.wait);
    kn(buf, "mredial", modem.redial);
    kn(buf, "mpause", modem.pause);
    kn(buf, "manswer", modem.answer);
}

/* ---- the line's file: loaded while a line is open ---- */

#define SERFILE "PROGDIR:Protocols/Serial"

static BPTR serseg;                     /* the file, loaded */
static struct SerMod *sm;               /* its calls */
static int inside;                      /* its code is running (a wait in
                                         * it serves the window): not
                                         * unloaded under its own feet */

static void e_text(const UBYTE *b, LONG n)
{
    log_bytes(b, n);
    vt_write(vt, b, n);
}

static int e_column(void)
{
    int x, y, v;
    vt_cursor(vt, &x, &y, &v);
    return x;
}

static ULONG e_sigs(void)
{
    return (win ? 1UL << win->UserPort->mp_SigBit : 0) | shells_sigmask();
}

static BOOL e_serve(ULONG sigs)
{
    shells_handle(sigs);                /* the other tabs go on */
    return win && (sigs & (1UL << win->UserPort->mp_SigBit)) &&
           net_gives_up();
}

static BOOL e_xfer_on(void)
{
    return T->t_xfer != NULL;
}

static void e_replies(void)
{
    char r[256];
    int n = vt_take_reply(vt, r, sizeof r);
    if (n > 0)
        send_data((UBYTE *)r, n);
    host_clip();
}

static struct SerEnv senv = { NULL, &modem, say, e_text, refresh, e_column,
                              e_sigs, e_serve, transfer_take, e_xfer_on,
                              e_replies, term_now, numstr_cat,
                              ser_line_text };

/* The file in memory: TRUE when it is. Missing or of another version:
 * said in the tab. */
static BOOL ser_load(void)
{
    BOOL found;
    SerModStart start;
    if (sm)
        return TRUE;
    serseg = LoadSeg((STRPTR)SERFILE);
    if (!serseg) {
        say("\x1b[1;31m[serial support missing: " SERFILE " not found]"
            "\x1b[0m\r\n");
        refresh();
        return FALSE;
    }
    senv.dosbase = (struct Library *)DOSBase;
    start = (SerModStart)mod_entry(serseg, SERMOD_HEAD, SERMOD_VERSION,
                                   &found);
    if (start && (sm = start(&senv)) && sm->magic != SERMOD_MAGIC)
        sm = NULL;
    if (!sm) {
        UnLoadSeg(serseg);
        serseg = 0;
        say("\x1b[1;31m[" SERFILE " is not CTerm's serial part of this "
            "version]\x1b[0m\r\n");
        refresh();
        return FALSE;
    }
    return TRUE;
}

/* No line open, and none of its code running: the file goes. */
static void ser_unload(void)
{
    if (sm && !inside && !sm->lines()) {
        sm = NULL;
        UnLoadSeg(serseg);
        serseg = 0;
    }
}

ULONG serial_sigmask(void)
{
    return sm ? sm->sigmask() : 0;
}

/* loop_round's timed wait when there is no TCP/IP stack: ms (-1: off). */
void serial_alarm(LONG ms)
{
    if (sm)
        sm->alarm(ms);
}

/* What came on T's line: up to max bytes, 0 none, -1 the line is gone. */
LONG serial_take(UBYTE *b, LONG max)
{
    return sm && T->t_st.ser ? sm->take(&T->t_st, b, max) : -1;
}

/* Data to T's line, as it is (no telnet here). */
void serial_write(const UBYTE *b, LONG n)
{
    BOOL ok;
    if (!sm || !T->t_st.ser)
        return;
    inside++;
    ok = sm->write(&T->t_st, b, n);
    inside--;
    if (!ok && T->t_xfer)
        T->t_xfer->stop = 1;            /* Esc while a transfer waited */
}

/* Call T's line: open it, and with a phone number dial it. TRUE: the
 * line (or the call) is up. */
BOOL serial_call(void)
{
    BOOL ok;
    if (!ser_load())
        return FALSE;
    T->t_st.named = T->t_bookname[0] != 0;
    T->ended = FALSE;
    inside++;
    ok = sm->call(&T->t_st);
    inside--;
    ser_unload();
    return ok;
}

/* T's line goes: a modem's call hung up first. */
void serial_close(void)
{
    if (!sm || !T->t_st.ser)
        return;
    inside++;
    sm->close(&T->t_st);
    inside--;
    ser_unload();
}

/* Project/Answer: the modem picks up (ATA). */
void serial_answer(void)
{
    if (local || !sm || !T->t_st.ser) {
        DisplayBeep(scr);
        return;
    }
    inside++;
    sm->answer(&T->t_st);
    inside--;
}

/* Every line: what came, into its buffer; once a second, the carriers.
 * A file no line needs any more goes here too. */
void serial_poll(void)
{
    if (!sm)
        return;
    sm->poll();
    ser_unload();
}

/* T's line has something for loop_round: bytes, or its end. */
BOOL serial_due(void)
{
    return sm && T->t_st.ser && sm->due(&T->t_st);
}

/* T's bytes, shown (loop_round): a transfer's start is noticed, the
 * modem's RING, CONNECT and NO CARRIER heeded. */
void serial_input(UBYTE *buf, LONG size)
{
    if (!sm)
        return;
    inside++;
    sm->input(&T->t_st, buf, size);
    inside--;
    ser_unload();
}

/* ---- serial terminal tabs ---- */

static void tab_line(void)
{
    char n[12] = "";
    numstr_cat(n, T->t_st.line.unit);
    local = FALSE;
    T->t_serial = TRUE;
    sshmode = FALSE;
    if (T->t_st.phone[0])
        strcpy(dialhost, T->t_st.phone);
    else {
        strncpy(dialhost, T->t_st.line.device, sizeof dialhost - 14);
        strcat(dialhost, " ");
        strcat(dialhost, n);
    }
    strcpy(dialtitle, "CTerm: ");
    strncat(dialtitle, dialhost, sizeof dialtitle - 10);
}

/* CTerm SERIAL, before the window: the settings' line (device, unit and
 * speed given on the command line win). */
void serial_tab_setup(const char *dev, LONG *unit, LONG *baud)
{
    T->t_st.line = serdef;
    if (dev)
        put(T->t_st.line.device, sizeof T->t_st.line.device, dev);
    if (unit)
        T->t_st.line.unit = *unit;
    if (baud && *baud > 0)
        T->t_st.line.baud = *baud;
    T->t_st.phone[0] = 0;
    tab_line();
}

/* A phone book place on a serial line, for T. */
void serial_tab_book(const BookEntry *e)
{
    T->t_st.line = e->line;
    strcpy(T->t_st.phone, e->phone);
    strcpy(T->t_st.init, e->init);
    strcpy(T->t_st.dialpre, e->dial);
    tab_line();
}

/* Project/Serial terminal: the settings' line in a new tab, or (no room,
 * or asked) in a CTerm of its own. */
void serial_terminal(BOOL newtab)
{
    struct Tab *t;
    if (!newtab || !(t = tab_new())) {
        char cmd[300], path[256];
        BPTR nil;
        own_path(path, sizeof path, NULL);
        strcpy(cmd, "Run >NIL: \"");
        strcat(cmd, path);
        strcat(cmd, "\" SERIAL");
        nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
        if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL,
                       TAG_END) != 0 && nil)
            Close(nil);
        return;
    }
    T = t;
    serial_tab_setup(NULL, NULL, NULL);
    tab_show(ntabs - 1);
    if (!serial_call())
        say("\x1b[2m[Return tries again]\x1b[0m\r\n");
    refresh();
}
