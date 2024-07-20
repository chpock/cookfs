/*
 * pagesComprCustom.h
 *
 * Provides custom functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRCUSTOM_H
#define COOKFS_PAGESCOMPRCUSTOM_H 1

int CookfsReadPageCustom(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err);

Cookfs_PageObj CookfsWritePageCustom(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize);

#endif /* COOKFS_PAGESCOMPRCUSTOM_H */
