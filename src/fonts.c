/*
 * fonts - Amiga and TrueType letters, and each tab's own.
 */
#include "cterm.h"
#include "cmap.h"
#include "dbg.h"

static APTR open_variant(const char *const *suffixes);
static void fall_close(void);
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
    fall_close();
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
 * NotoSansMono-Bold.ttf beside NotoSansMono-Regular.ttf,
 * VeraMonoBold.ttf, ...-Oblique / -Italic / -BoldOblique. */
static APTR open_variant(const char *const *suffixes)
{
    char path[180], base[160];
    int n = strlen(ttfile), i;
    if (n < 5 || stricmp(ttfile + n - 4, ".ttf"))
        return NULL;
    strncpy(base, ttfile, n - 4);
    base[n - 4] = 0;
    n -= 4;                             /* NotoSansMono-Regular.ttf: */
    if (n > 8 && !stricmp(base + n - 8, "-Regular"))
        base[n - 8] = 0;                /* its bold is ...-Bold.ttf */
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

/* ---- the fallback font: letters the TrueType font lacks ---------------
 * A second TrueType file (fallbackfont= in CTerm.prefs, Settings' Text
 * tab) for the code points the main font has no glyph for - emoji and
 * symbols, monochrome (Noto Emoji, Symbola): ttengine draws no colour.
 * Which letters a font has comes from its cmap (cmap.c), read once per
 * file; ttengine itself would draw its empty box.
 *
 * ttengine reaches only the BMP (U+0000-FFFF; tested with UTF-32,
 * UTF-16 surrogates and UTF-8): the emoji at U+1Fxxx come out as boxes
 * from any font. tools/emojifont.py makes a copy of an emoji font with
 * them also at U+E000 + (cp - U+1F000), marked by U+FDD0; from such a
 * font they are drawn from there (tt_fallcp).
 *
 * Opened twice when its letters are wider than a cell: once to fit two
 * cells (what the terminal counts as wide: most emoji) and once to fit
 * one (symbols counted narrow, as U+231A and the like are). */

#define PUA_FIRST 0x1F000UL             /* (as tools/emojifont.py) */
#define PUA_LAST  0x1FAFFUL
#define PUA_AT    0xE000UL
#define PUA_MARK  0xFDD0UL

char fallfile[160];                     /* none: "" */
APTR ttfall, ttfall1;                   /* for two cells, for one */
int fallgen;                            /* a new fallback face: drawn anew */
static CmapRanges mainmap, fallmap;
static BOOL fallpua;                    /* made by tools/emojifont.py */
static char mainmapof[sizeof ttfile], fallof[sizeof fallfile];
static int fallsize = -1, fallcw;

static void *cm_alloc(unsigned long n) { return AllocVec(n, MEMF_ANY); }
static void cm_release(void *p) { FreeVec(p); }

/* A font file's characters into m (the table directory, then the cmap
 * table only: not the whole file into memory). */
static BOOL read_cmap(const char *path, CmapRanges *m)
{
    UBYTE head[12 + 16 * 40], *t;
    unsigned long off, len;
    LONG got;
    BPTR f;
    BOOL ok = FALSE;
    cmap_free(m);
    m->alloc = cm_alloc;
    m->release = cm_release;
    if (!(f = Open((STRPTR)path, MODE_OLDFILE)))
        return FALSE;
    got = Read(f, head, sizeof head);
    if (got > 0 && cmap_where(head, got, &off, &len) && len < 1024 * 1024 &&
        Seek(f, off, OFFSET_BEGINNING) >= 0 &&
        (t = AllocVec(len, MEMF_ANY))) {
        if (Read(f, t, len) == (LONG)len)
            ok = cmap_table(m, t, len);
        FreeVec(t);
    }
    Close(f);
    return ok;
}

/* The code point to draw cp with from the fallback font (ttengine's
 * reach: a U+1Fxxx emoji at its place in the Private Use Area) */
ULONG tt_fallcp(ULONG cp)
{
    if (cp > 0xFFFF && fallpua && cp >= PUA_FIRST && cp <= PUA_LAST)
        return PUA_AT + (cp - PUA_FIRST);
    return cp;
}

/* Does the main font lack cp, and the fallback have it (where ttengine
 * reaches it)? Latin-1 is taken as there (any terminal font has it). */
BOOL tt_missing(ULONG cp)
{
    static ULONG last = 0;
    static BOOL lastans;
    ULONG fc;
    if (!ttfall || cp <= 0xFF || !mainmap.n || VT_IS_PIC(cp) ||
        cp == VT_WIDE_TAIL)
        return FALSE;
    if (cp == last)
        return lastans;
    last = cp;
    fc = tt_fallcp(cp);
    lastans = !cmap_has(&mainmap, cp) && fc <= 0xFFFF &&
              (!fallmap.n || cmap_has(&fallmap, fc));
    return lastans;
}

static void fall_close(void)
{
    if (ttfall || ttfall1)
        rp_forget();                    /* draw.c's memo of the face set */
    if (ttfall1 && ttfall1 != ttfall)
        TT_CloseFont(ttfall1);
    if (ttfall)
        TT_CloseFont(ttfall);
    if (ttfall || ttfall1)
        fallgen++;
    ttfall = ttfall1 = NULL;
    fallsize = -1;
}

/* The fallback font at the size where letter u is at most `px` wide
 * (ttsize when it fits, or nothing to measure) */
static APTR fall_open(ULONG u, int px, int *size)
{
    struct RastPort trp;
    APTR f;
    int w;
    *size = ttsize;
    if (!(f = TT_OpenFont(TT_FontFile, (ULONG)fallfile, TT_FontSize, ttsize,
                          TAG_END)) || !u)
        return f;
    InitRastPort(&trp);
    TT_SetFont(&trp, f);
    TT_SetAttrs(&trp, TT_Encoding, TT_Encoding_UTF32_BE, TAG_END);
    w = TT_TextLength(&trp, (APTR)&u, 1);
    TT_DoneRastPort(&trp);
    if (w > px) {                       /* too wide: smaller */
        *size = ttsize * px / w;
        if (*size < 4)
            *size = 4;
        TT_CloseFont(f);
        f = TT_OpenFont(TT_FontFile, (ULONG)fallfile, TT_FontSize, *size,
                        TAG_END);
    }
    return f;
}

/* After the main font is open and measured (cw known) */
static void fall_load(void)
{
    static const ULONG probe[] = { 0x1F600, 0x231A, 0x2603, 0x4E00, 0 };
    ULONG u = 0;
    int i, s2, s1;
    if (strcmp(mainmapof, ttfile)) {    /* the main font's characters */
        if (!read_cmap(ttfile, &mainmap))
            cmap_free(&mainmap);        /* unknown: no fallback used */
        strcpy(mainmapof, ttfile);
        DBGS("fall: main cmap ranges ");
        DBGX(mainmap.n);
        DBGS("\n");
    }
    if (!fallfile[0]) {
        fall_close();
        fallof[0] = 0;
        return;
    }
    if (ttfall && fallsize >= 0 && fallcw == cw && !strcmp(fallof, fallfile))
        return;                         /* as it is */
    fall_close();
    if (strcmp(fallof, fallfile)) {
        if (!read_cmap(fallfile, &fallmap))
            cmap_free(&fallmap);
        fallpua = fallmap.n && cmap_has(&fallmap, PUA_MARK);
        strcpy(fallof, fallfile);
        DBGS("fall: fallback cmap ranges ");
        DBGX(fallmap.n);
        DBGS("\n");
    }
    /* measured on an emoji (or what it has of the others), where
     * ttengine can reach it */
    for (i = 0; probe[i]; i++)
        if (!fallmap.n || (tt_fallcp(probe[i]) <= 0xFFFF &&
                           cmap_has(&fallmap, tt_fallcp(probe[i])))) {
            u = tt_fallcp(probe[i]);
            break;
        }
    if (!(ttfall = fall_open(u, 2 * cw, &s2)))
        return;
    ttfall1 = fall_open(u, cw, &s1);
    if (!ttfall1 || s1 == s2) {         /* the same size: one face */
        if (ttfall1)
            TT_CloseFont(ttfall1);
        ttfall1 = ttfall;
    }
    fallsize = s2;
    fallcw = cw;
    fallgen++;
    DBGS("fall: open ");
    DBGX((ULONG)ttfall);
    DBGS(" sizes ");
    DBGX(s2);
    DBGS(" ");
    DBGX(s1);
    DBGS("\n");
}

/* At the end: the faces are closed by close_tt; the tables here */
void fall_free(void)
{
    fall_close();
    cmap_free(&mainmap);
    cmap_free(&fallmap);
    mainmapof[0] = fallof[0] = 0;
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
        fall_load();                    /* (the setting may have changed) */
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
    {
        /* The row as tall as the Latin-1 letters (Å and g included), as
         * PC terminals size it from the font: MaxTop/MaxBottom are the
         * tallest glyph anywhere (Noto Sans Mono's stacked Vietnamese
         * accents made its rows a third taller than a PC's) */
        UBYTE l[191];
        struct TextExtent te;
        int k = 0, c;
        for (c = 0x20; c < 0x7F; c++) l[k++] = c;
        for (c = 0xA0; c <= 0xFF; c++) l[k++] = c;
        TT_TextExtent(&trp, l, k, &te);
        if (te.te_Extent.MinY < 0 && te.te_Extent.MaxY >= 0 &&
            -te.te_Extent.MinY <= top && te.te_Extent.MaxY + 1 <= bot) {
            top = -te.te_Extent.MinY;
            bot = te.te_Extent.MaxY + 1;
        }
    }
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
    fall_load();
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
