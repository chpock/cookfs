/*
 * pagesComprZlib.h
 *
 * Provides zlib functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_PAGESCOMPRZLIB_H
#define COOKFS_PAGESCOMPRZLIB_H 1

#define COOKFS_DEFAULT_COMPRESSION_LEVEL_ZLIB 6

Cookfs_PageObj CookfsReadPageZlib(Cookfs_Pages *p, int size, Tcl_Obj **err);
int CookfsWritePageZlib(Cookfs_Pages *p, unsigned char *bytes, int origSize);

#endif /* COOKFS_PAGESCOMPRZLIB_H */
