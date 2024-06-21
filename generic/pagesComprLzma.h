/*
 * pagesComprLzma.h
 *
 * Provides lzma functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRLZMA_H
#define COOKFS_PAGESCOMPRLZMA_H 1

Cookfs_PageObj CookfsReadPageLzma(Cookfs_Pages *p, int size, Tcl_Obj **err);
int CookfsWritePageLzma(Cookfs_Pages *p, unsigned char *bytes, int origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */