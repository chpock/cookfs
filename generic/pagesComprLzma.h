/*
 * pagesComprLzma.h
 *
 * Provides lzma functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRLZMA_H
#define COOKFS_PAGESCOMPRLZMA_H 1

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_LZMA 5

int CookfsReadPageLzma(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err);

Cookfs_PageObj CookfsWritePageLzma(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */
