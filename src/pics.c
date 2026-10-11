/*
 * pics - Parts/Pictures: what pictures in the terminal need beyond the
 * emulator (vt.c keeps them and decodes sixel itself), loaded when the
 * first picture needs it and gone when no tab holds one:
 *  - iTerm2's inline files (PNG, JPEG, GIF...) decoded by the Amiga's
 *    datatypes (picture.datatype V43, read as ARGB);
 *  - pixels made screen pens for 8-bit cards and plain (AGA) screens: a
 *    colour cube as big as the screen's colours allow (6x6x6 on 256, the
 *    8 pure colours on 16), its pens asked of CTerm once (exact colours
 *    while there are free pens), ordered 4x4 dithering.
 */
#include "cterm.h"

/* ---- decoding: a whole file, through a file in T: (datatypes read
 * files; a memory source is not one every class takes) ---- */

static const UBYTE *d_file;
static LONG d_len;
static ULONG *d_out;
static LONG d_w, d_h;

static void decode_(void)
{
    char name[32];
    ULONG id = (ULONG)FindTask(NULL);
    BPTR fh;
    Object *o;
    struct BitMapHeader *bmh = NULL;
    ULONG alpha = 0;
    LONG y, i;
    int k;
    d_out = NULL;
    if (!DataTypesBase)
        DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    if (!DataTypesBase)
        return;
    strcpy(name, "T:CTerm-pic.");
    for (k = 0; k < 8; k++)             /* one name each CTerm */
        name[12 + k] = "0123456789abcdef"[(id >> (28 - 4 * k)) & 15];
    name[20] = 0;
    if (!(fh = Open((STRPTR)name, MODE_NEWFILE)))
        return;
    i = Write(fh, (APTR)d_file, d_len);
    Close(fh);
    if (i != d_len) {
        DeleteFile((STRPTR)name);
        return;
    }
    o = NewDTObject((APTR)name, DTA_SourceType, DTST_FILE,
                    DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
                    PDTA_Remap, FALSE, TAG_DONE);
    if (o) {
        DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, NULL, 1);
        GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
        GetDTAttrs(o, PDTA_AlphaChannel, (ULONG)&alpha, TAG_DONE);
        if (bmh && bmh->bmh_Width > 0 && bmh->bmh_Height > 0 &&
            bmh->bmh_Width <= 8192 && bmh->bmh_Height <= 8192) {
            d_w = bmh->bmh_Width;
            d_h = bmh->bmh_Height;
            if (bmh->bmh_Masking == mskHasAlpha)
                alpha = 1;
            d_out = AllocVec(d_w * d_h * 4, MEMF_ANY);
            for (y = 0; d_out && y < d_h; y++)
                if (!DoMethod(o, PDTM_READPIXELARRAY,
                              (ULONG)(d_out + y * d_w), PBPAFMT_ARGB,
                              d_w * 4, 0, y, d_w, 1)) {
                    FreeVec(d_out);
                    d_out = NULL;
                }
            /* see-through pixels: over the background, as they show */
            for (i = 0; d_out && i < d_w * d_h; i++) {
                ULONG c = d_out[i], a = alpha ? c >> 24 : 255, o2 = 0;
                int sh;
                if (a == 255) {
                    d_out[i] = c | 0xFF000000UL;
                    continue;
                }
                for (sh = 0; sh <= 16; sh += 8) {
                    int f = (c >> sh) & 255, b = (defbg >> sh) & 255;
                    o2 |= (ULONG)(b + (f - b) * (LONG)a / 255) << sh;
                }
                d_out[i] = o2 | 0xFF000000UL;
            }
        }
        DisposeDTObject(o);
    }
    DeleteFile((STRPTR)name);
}

static int p_decode(const UBYTE *file, LONG len, ULONG **argb, int *w, int *h)
{
    d_file = file;
    d_len = len;
    deep_call(decode_);                 /* the classes want a deep stack */
    if (!d_out)
        return 0;
    *argb = d_out;
    *w = d_w;
    *h = d_h;
    return 1;
}

static void p_decode_free(ULONG *argb)
{
    if (argb)
        FreeVec(argb);
}

/* ---- pens: RGB to the screen's pens, dithered ---- */

static UBYTE cube[216];                 /* level r*L*L + g*L + b: its pen */
static int lv;                          /* levels a channel: 2..6 */
static ULONG made_gen = ~0UL;           /* pen_gen it was made for */
static APTR made_scr;
static UBYTE base[256], frac[256];      /* v*(L-1)/255: whole, rest */
/* the 4x4 ordered dither's thresholds, 0..254 */
static const UBYTE bayer[16] = {
    8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88
};

static void make_cube(void)
{
    int d = scr ? GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) : 8;
    int r, g, b, i;
    /* what fits beside the text's colours: 216 of 256, 64 of 128, 27 of
     * 64, the 8 pure colours on 32 and 16 (27 on 16 pens: the bright
     * ones fell onto dark neighbours, yellow came out grey) */
    lv = d >= 8 ? 6 : d == 7 ? 4 : d == 6 ? 3 : 2;
    for (i = 0; i < 256; i++) {
        base[i] = i * (lv - 1) / 255;
        frac[i] = i * (lv - 1) % 255;
    }
    for (r = 0, i = 0; r < lv; r++)
        for (g = 0; g < lv; g++)
            for (b = 0; b < lv; b++, i++)
                cube[i] = (UBYTE)pic_pen((ULONG)(r * 255 / (lv - 1)) << 16 |
                                         (ULONG)(g * 255 / (lv - 1)) << 8 |
                                         (ULONG)(b * 255 / (lv - 1)));
    made_gen = pen_gen;
    made_scr = scr;
}

/* n pixels (R G B) from screen column x on screen row y, as pens */
static void p_pens(const UBYTE *rgb, int n, int x, int y, UBYTE *out)
{
    const UBYTE *th = bayer + ((y & 3) << 2);
    int L = lv;
    if (made_gen != pen_gen || made_scr != scr) {
        make_cube();
        L = lv;
    }
    while (n-- > 0) {
        int t = th[x++ & 3];
        int r = base[rgb[0]] + (frac[rgb[0]] > t);
        int g = base[rgb[1]] + (frac[rgb[1]] > t);
        int b = base[rgb[2]] + (frac[rgb[2]] > t);
        *out++ = cube[(r * L + g) * L + b];
        rgb += 3;
    }
}

static const struct PicsPart fns = { p_decode, p_decode_free, p_pens };

APTR part_main(void)
{
    made_gen = ~0UL;                    /* pens asked again after a load */
    return (APTR)&fns;
}
