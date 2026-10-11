/*
 * fonts - Amiga and TrueType letters, and each tab's own.
 */
#include "cterm.h"

static APTR open_variant(const char *const *suffixes);
static int tab_wish(void);

/* The cell from the Amiga font, with the line spacing shared above and
 * below the letters. */
void bitmap_metrics(void)
{
    cw = font->tf_XSize;
    ch = font->tf_YSize + linespace;
    cbase = font->tf_Baseline + linespace / 2;
}

BOOL load_font(void)
{
    static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *f = NULL;
    if (fontname[0] && diskfont_open()) {
        struct TextAttr ta;
        ta.ta_Name = (STRPTR)fontname;
        ta.ta_YSize = fontsize;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;
        f = OpenDiskFont(&ta);
        if (f && (f->tf_Flags & FPF_PROPORTIONAL)) {
            CloseFont(f);
            f = NULL;
        }
    }
    if (!f) {
        struct TextFont *d = GfxBase->DefaultFont;
        if (d && !(d->tf_Flags & FPF_PROPORTIONAL)) {
            struct TextAttr ta;
            ta.ta_Name = (STRPTR)d->tf_Message.mn_Node.ln_Name;
            ta.ta_YSize = d->tf_YSize;
            ta.ta_Style = 0;
            ta.ta_Flags = 0;
            f = OpenFont(&ta);
        }
    }
    if (!f)
        f = OpenFont(&topaz8);
    if (!f)
        return FALSE;
    if (ownfont)
        CloseFont(ownfont);
    font = ownfont = f;
    bitmap_metrics();
    return TRUE;
}

void close_tt(void)
{
    int i;
    rp_forget();                        /* draw.c's memo of the face set */
    for (i = 0; i < 4; i++)
        if (ttface[i]) {
            TT_CloseFont(ttface[i]);
            ttface[i] = NULL;
        }
}

BOOL file_exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l)
        UnLock(l);
    return l != 0;
}

/* The bold or italic file beside a .ttf: DejaVuSansMono-Bold.ttf,
 * VeraMonoBold.ttf, ...-Oblique / -Italic / -BoldOblique. */
static APTR open_variant(const char *const *suffixes)
{
    char path[180], base[160];
    int n = strlen(ttfile), i;
    if (n < 5 || stricmp(ttfile + n - 4, ".ttf"))
        return NULL;
    strncpy(base, ttfile, n - 4);
    base[n - 4] = 0;
    for (i = 0; suffixes[i]; i++) {
        strcpy(path, base);
        strcat(path, suffixes[i]);
        strcat(path, ".ttf");
        if (file_exists(path))
            return TT_OpenFont(TT_FontFile, (ULONG)path, TT_FontSize, ttsize,
                               TAG_END);
    }
    return NULL;
}

static BOOL tt_new;                     /* load_tt opened faces anew */

/* Open the TrueType font and measure its cell. FALSE: not possible. */
BOOL load_tt(void)
{
    static const char *const bold[] = { "-Bold", "Bold", NULL };
    static const char *const ital[] = { "-Oblique", "-Italic", "Italic",
                                        "Oblique", NULL };
    static const char *const both[] = { "-BoldOblique", "-BoldItalic",
                                        "BoldItalic", "BoldOblique", NULL };
    struct RastPort trp;
    static ULONG top, bot;              /* the open faces' measures */
    static int ocw, osize = -1;
    static char ofile[sizeof ttfile];
    int ten;
    /* the same file at the same size is open already (a tab with the
     * same letters as the last): only the spacing may have changed */
    if (ttface[0] && osize == ttsize && !strcmp(ofile, ttfile)) {
        cw = ocw;
        ch = top + bot + linespace;
        cbase = top + linespace / 2;
        return TRUE;
    }
    if (!tt_open())
        return FALSE;
    close_tt();
    top = bot = 0;
    osize = -1;
    tt_new = TRUE;
    ttface[0] = TT_OpenFont(TT_FontFile, (ULONG)ttfile, TT_FontSize, ttsize,
                            TAG_END);
    if (!ttface[0])
        return FALSE;
    ttface[1] = open_variant(bold);
    ttface[2] = open_variant(ital);
    ttface[3] = open_variant(both);
    InitRastPort(&trp);
    TT_SetFont(&trp, ttface[0]);
    TT_SetAttrs(&trp, TT_Encoding, TT_Encoding_ISO8859_1, TAG_END);
    TT_GetAttrs(&trp, TT_FontMaxTop, (ULONG)&top, TT_FontMaxBottom,
                (ULONG)&bot, TAG_END);
    cw = TT_TextLength(&trp, (APTR)"0", 1);
    ten = TT_TextLength(&trp, (APTR)"0000000000", 10);
    TT_DoneRastPort(&trp);
    if (cw <= 0 || top + bot <= 0) {
        close_tt();
        return FALSE;
    }
    ttruns = ten == 10 * cw;
    ch = top + bot + linespace;         /* the spacing shared above, below */
    cbase = top + linespace / 2;
    ocw = cw;
    osize = ttsize;
    strcpy(ofile, ttfile);
    return TRUE;
}

/* After the window opens or the font changes: tell ttengine about it. */
void tt_attach(void)
{
    if (usett && win)
        TT_SetAttrs(rp, TT_Window, (ULONG)win, TT_Antialias,
                    ttaa ? TT_Antialias_On : TT_Antialias_Off,
                    TT_Encoding, TT_Encoding_UTF32_BE,
                    TAG_END);
}

void font_changed(void)
{
    SetFont(rp, font);
    tt_attach();
    layout_and_resize();
    painter_forget();
    redraw_all();
    tell_sizes();
}

void ask_ttfont(void)
{
    struct FileRequester *req;
    char path[160];
    if (!asl_open() || !tt_open()) {
        say("\r\n[TrueType needs ttengine.library in LIBS:]\r\n");
        return;
    }
    req = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_TitleText, (ULONG)"CTerm: a TrueType font (fixed width)",
        ASLFR_InitialDrawer, (ULONG)"SYS:Fonts/TrueType",
        ASLFR_InitialPattern, (ULONG)"#?.ttf", ASLFR_DoPatterns, TRUE,
        ASLFR_RejectIcons, TRUE, TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL)) {
        char old[160];
        strncpy(path, (char *)req->fr_Drawer, sizeof path - 1);
        path[sizeof path - 1] = 0;
        AddPart((STRPTR)path, req->fr_File, sizeof path);
        strcpy(old, ttfile);
        strcpy(ttfile, path);
        usett = TRUE;
        if (!load_tt()) {
            say("\r\n[that TrueType font would not open]\r\n");
            strcpy(ttfile, old);
            usett = load_tt();
            if (!usett)
                load_font();
        }
        font_changed();
        save_prefs();
    }
    FreeAslRequest(req);
}

void tt_size(int d)
{
    if (!usett)
        return;
    if (ttsize + d < 6 || ttsize + d > 72)
        return;
    ttsize += d;
    if (!load_tt()) {
        ttsize -= d;
        /* not even the old size: the Amiga font, as ask_ttfont does */
        if (!load_tt()) {
            usett = FALSE;
            load_font();
        }
    }
    font_changed();
    save_prefs();
}

/* The letters a tab asks for: its phone book entry's, or the PC's font
 * in PC BBS mode (when it is there and wanted), or CTerm's own. */
static int tab_wish(void)
{
    int l = T->t_letters;
    if (npanes > 1)
        return BL_CTERM;                /* split: one size of cell for all */
    if (l == BL_PC || (l == BL_CTERM && pcmode && pcfont))
        return BL_PC;
    return l == BL_AMIGA || l == BL_TT ? l : BL_CTERM;
}

/* The tab's letters on (TRUE), or the person's own (FALSE: the settings
 * window shows those); the person's are kept aside meanwhile. */
void tab_letters(BOOL tab)
{
    int want = tab ? tab_wish() : BL_CTERM;
    /* the letters as they are: a tab with the same ones as the last
     * needs no new layout, pictures or redraw from here */
    BOOL was_tt = usett && ttface[0], was_aa = ttaa;
    struct TextFont *was_font = font;
    int was_cw = cw, was_ch = ch, was_base = cbase;
    if (want == fontover)
        return;
    if (fontover) {                     /* the person's back first */
        usett = keepfont.usett;
        strcpy(ttfile, keepfont.ttfile);
        ttsize = keepfont.ttsize;
        ttaa = TRUE;
        fontover = BL_CTERM;
    }
    if ((want == BL_PC && (!tt_installed() || !file_exists(pcttfile))) ||
        (want == BL_TT && !tt_installed()))
        want = BL_CTERM;
    if (want != BL_CTERM) {
        keepfont.usett = usett;
        strcpy(keepfont.ttfile, ttfile);
        keepfont.ttsize = ttsize;
        usett = want != BL_AMIGA;
        if (want == BL_PC) {
            strcpy(ttfile, pcttfile);
            ttsize = 16;
            ttaa = FALSE;               /* a pixel font, at its one size */
        }
    }
    fontover = want;
    tt_new = FALSE;
    if (usett && !load_tt())
        usett = FALSE;
    if (!usett) {
        close_tt();
        load_font();
    }
    if (win && (usett != was_tt || tt_new || (!usett && font != was_font) ||
                cw != was_cw || ch != was_ch || cbase != was_base ||
                ttaa != was_aa))
        font_changed();
}

void ask_font(void)
{
    struct FontRequester *req;
    if (!asl_open())
        return;
    req = AllocAslRequestTags(ASL_FontRequest,
        ASLFO_Window, (ULONG)win, ASLFO_SleepWindow, TRUE,
        ASLFO_TitleText, (ULONG)"CTerm's font (fixed width)",
        ASLFO_FixedWidthOnly, TRUE,
        ASLFO_InitialName, (ULONG)font->tf_Message.mn_Node.ln_Name,
        ASLFO_InitialSize, font->tf_YSize, TAG_END);
    if (!req)
        return;
    if (AslRequest(req, NULL)) {
        strncpy(fontname, (char *)req->fo_Attr.ta_Name, sizeof fontname - 1);
        fontsize = req->fo_Attr.ta_YSize;
        if (load_font()) {
            usett = FALSE;
            bitmap_metrics();
            font_changed();
            save_prefs();
        }
    }
    FreeAslRequest(req);
}
