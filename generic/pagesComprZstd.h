/*
 * pagesComprZstd.h
 *
 * Provides Zstandard functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRZSTD_H
#define COOKFS_PAGESCOMPRZSTD_H 1

Cookfs_PageObj CookfsReadPageZstd(Cookfs_Pages *p, int size, Tcl_Obj **err);
int CookfsWritePageZstd(Cookfs_Pages *p, unsigned char *bytes, int origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */
