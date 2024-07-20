/*
 * pagesComprBrotli.h
 *
 * Provides brotli functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRBROTLI_H
#define COOKFS_PAGESCOMPRBROTLI_H 1

int CookfsReadPageBrotli(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err);

Cookfs_PageObj CookfsWritePageBrotli(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize);

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_BROTLI 6

#endif /* COOKFS_PAGESCOMPRBROTLI_H */
