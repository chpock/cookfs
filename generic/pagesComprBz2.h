/*
 * pagesComprBz2.h
 *
 * Provides bzip2 functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRBZ2_H
#define COOKFS_PAGESCOMPRBZ2_H 1

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_BZ2 9

int CookfsReadPageBz2(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err);

Cookfs_PageObj CookfsWritePageBz2(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */
