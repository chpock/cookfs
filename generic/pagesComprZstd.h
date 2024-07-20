/*
 * pagesComprZstd.h
 *
 * Provides Zstandard functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRZSTD_H
#define COOKFS_PAGESCOMPRZSTD_H 1

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_ZSTD 3

int CookfsReadPageZstd(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err);

Cookfs_PageObj CookfsWritePageZstd(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */
