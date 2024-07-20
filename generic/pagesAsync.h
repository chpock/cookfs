/*
  (c) 2010-2014 Wojciech Kocjan, Pawel Salawa
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGESASYNC_H
#define COOKFS_PAGESASYNC_H 1

#include "pages.h"

Cookfs_PageObj Cookfs_AsyncPageGet(Cookfs_Pages *p, int idx);
int Cookfs_AsyncPageAdd(Cookfs_Pages *p, int idx, unsigned char *bytes, int dataSize);
void Cookfs_AsyncDecompressWaitIfLoading(Cookfs_Pages *p, int idx);
int Cookfs_AsyncCompressWait(Cookfs_Pages *p, int require);
void Cookfs_AsyncCompressFinalize(Cookfs_Pages *p);
int Cookfs_AsyncPagePreload(Cookfs_Pages *p, int idx);
int Cookfs_AsyncDecompressWait(Cookfs_Pages *p, int idx, int require);
void Cookfs_AsyncDecompressFinalize(Cookfs_Pages *p);

#endif /* COOKFS_PAGESASYNC_H */
