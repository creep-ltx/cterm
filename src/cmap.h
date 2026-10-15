/*
 * cmap - the characters a TrueType font has (its cmap table), as sorted
 * ranges of code points. Plain C, no Amiga headers (tested on Linux).
 */
#ifndef CMAP_H
#define CMAP_H

typedef struct {
    unsigned long *r;                   /* first, last; first, last; ... */
    long n, cap;                        /* ranges; room for */
    void *(*alloc)(unsigned long n);    /* the host's memory */
    void (*release)(void *p);
} CmapRanges;

/* Where the cmap table is in the font file: from the file's first bytes
 * (12 + 16 per table; 12 + 16 * 64 is plenty). 0: no cmap. */
int cmap_where(const unsigned char *head, unsigned long hlen,
               unsigned long *off, unsigned long *len);
/* The cmap table (len bytes) into r (alloc and release set, the rest
 * zero or as cmap_free left it). 0: nothing read. */
int cmap_table(CmapRanges *r, const unsigned char *cm, unsigned long len);
int cmap_has(const CmapRanges *r, unsigned long cp);
void cmap_free(CmapRanges *r);

#endif
