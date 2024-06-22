/*
 * pagesComprBrotli.h
 *
 * Provides brotli functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRBROTLI_H
#define COOKFS_PAGESCOMPRBROTLI_H 1

Cookfs_PageObj CookfsReadPageBrotli(Cookfs_Pages *p, int size, Tcl_Obj **err);
int CookfsWritePageBrotli(Cookfs_Pages *p, unsigned char *bytes, int origSize);

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_BROTLI 6

#endif /* COOKFS_PAGESCOMPRBROTLI_H */
