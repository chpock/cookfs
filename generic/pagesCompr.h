/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_PAGESCOMPR_H
#define COOKFS_PAGESCOMPR_H 1

/* only handle pages code if enabled in configure */

#ifdef COOKFS_USECPAGES

extern const char *cookfsCompressionOptions[];
extern const int cookfsCompressionOptionMap[];

#define COOKFS_COMPRESSION_NONE 0
#define COOKFS_COMPRESSION_ZLIB 1
#define COOKFS_COMPRESSION_BZ2  2

enum {
    cookfsCompressionOptNone,
    cookfsCompressionOptZlib,
#ifdef COOKFS_USEBZ2
    cookfsCompressionOptBz2,
#endif /* COOKFS_USEBZ2 */
    cookfsCompressionOptMax
};

void Cookfs_PagesInitCompr(Cookfs_Pages *rc);

int Cookfs_WritePage(Cookfs_Pages *p, Tcl_Obj *data);
Tcl_Obj *Cookfs_ReadPage(Cookfs_Pages *p, int size);

#endif /* COOKFS_USECPAGES */

#endif /* COOKFS_PAGESCOMPR_H */
