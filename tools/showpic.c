/* ShowPic - shows a picture in CTerm, as imgcat does on Linux: the file
 * (PNG, JPEG, GIF, IFF: whatever the Amiga's datatypes read) is written
 * to the Shell as iTerm2's inline picture code, and CTerm draws it.
 *
 *   ShowPic FILE [WIDTH w] [HEIGHT h]
 *
 * w, h: in letters (40), in pixels (300px) or of the window (50%); none:
 * the picture's own size (narrowed to fit the window). CTerm's setting
 * "Pictures in the terminal" must be on. Built by `make showpic`. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

static const char version[] __attribute__((used)) =
    "$VER: ShowPic 1.0 (11.10.2026)";

static const char abc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int main(void)
{
    LONG arg[3] = { 0, 0, 0 }, n, i, k;
    struct RDArgs *ra = ReadArgs((STRPTR)"FILE/A,WIDTH/K,HEIGHT/K", arg, NULL);
    static UBYTE in[3 * 1024], out[4 * 1024];
    BPTR fh, o = Output();
    char head[96];
    if (!ra) {
        PrintFault(IoErr(), (STRPTR)"ShowPic");
        return RETURN_ERROR;
    }
    if (!(fh = Open((STRPTR)arg[0], MODE_OLDFILE))) {
        PrintFault(IoErr(), (STRPTR)arg[0]);
        FreeArgs(ra);
        return RETURN_ERROR;
    }
    strcpy(head, "\x1b]1337;File=inline=1");
    if (arg[1] && strlen((char *)arg[1]) < 20) {
        strcat(head, ";width=");
        strcat(head, (char *)arg[1]);
    }
    if (arg[2] && strlen((char *)arg[2]) < 20) {
        strcat(head, ";height=");
        strcat(head, (char *)arg[2]);
    }
    strcat(head, ":");
    Write(o, head, strlen(head));
    /* 3 KB at a time (a whole number of 3-byte groups but at the end) */
    for (;;) {
        LONG got;
        for (n = 0; n < (LONG)sizeof in; n += got)     /* full groups */
            if ((got = Read(fh, in + n, sizeof in - n)) <= 0)
                break;
        if (n <= 0)
            break;
        for (i = 0, k = 0; i < n; i += 3) {
            ULONG v = (ULONG)in[i] << 16 |
                      (i + 1 < n ? (ULONG)in[i + 1] << 8 : 0) |
                      (i + 2 < n ? in[i + 2] : 0);
            out[k++] = abc[v >> 18];
            out[k++] = abc[(v >> 12) & 63];
            out[k++] = i + 1 < n ? abc[(v >> 6) & 63] : '=';
            out[k++] = i + 2 < n ? abc[v & 63] : '=';
        }
        Write(o, out, k);
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
            break;
    }
    Write(o, "\x07\n", 2);
    Close(fh);
    FreeArgs(ra);
    return RETURN_OK;
}
