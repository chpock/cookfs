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

Cookfs_PageObj CookfsReadPageBz2(Cookfs_Pages *p, int size, Tcl_Obj **err);
int CookfsWritePageBz2(Cookfs_Pages *p, unsigned char *bytes, int origSize);

#endif /* COOKFS_PAGESCOMPRBZ2_H */
