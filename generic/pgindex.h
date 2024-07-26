/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PGINDEX_H
#define COOKFS_PGINDEX_H 1

#include "pageObj.h"

typedef struct _Cookfs_PgIndex Cookfs_PgIndex;

Cookfs_PgIndex *Cookfs_PgIndexInit(unsigned int initialPagesCount);
void Cookfs_PgIndexFini(Cookfs_PgIndex *pgi);

int Cookfs_PgIndexAddPage(Cookfs_PgIndex *pgi,
    Cookfs_CompressionType compression, int compressionLevel, int encryption,
    int sizeCompressed, int sizeUncompressed, unsigned char *hashMD5);

int Cookfs_PgIndexGetLength(Cookfs_PgIndex *pgi);

int Cookfs_PgIndexSearchByMD5(Cookfs_PgIndex *pgi, unsigned char *hashMD5,
    int sizeUncompressed, int *index);

Tcl_WideInt Cookfs_PgIndexGetEndOffset(Cookfs_PgIndex *pgi, int num);
Tcl_WideInt Cookfs_PgIndexGetStartOffset(Cookfs_PgIndex *pgi, int num);

void Cookfs_PgIndexSetEncryption(Cookfs_PgIndex *pgi, int num, int encryption);
void Cookfs_PgIndexSetCompression(Cookfs_PgIndex *pgi, int num,
    Cookfs_CompressionType compression, int compressionLevel);
void Cookfs_PgIndexSetSizeCompressed(Cookfs_PgIndex *pgi, int num,
    int sizeCompressed);

int Cookfs_PgIndexGetSizeUncompressed(Cookfs_PgIndex *pgi, int num);
int Cookfs_PgIndexGetSizeCompressed(Cookfs_PgIndex *pgi, int num);
int Cookfs_PgIndexGetCompressionLevel(Cookfs_PgIndex *pgi, int num);
Cookfs_CompressionType Cookfs_PgIndexGetCompression(Cookfs_PgIndex *pgi,
    int num);
int Cookfs_PgIndexGetEncryption(Cookfs_PgIndex *pgi, int num);
unsigned char *Cookfs_PgIndexGetHashMD5(Cookfs_PgIndex *pgi, int num);

Cookfs_PgIndex *Cookfs_PgIndexImport(unsigned char *bytes, int size,
    Tcl_Obj **err);

Cookfs_PageObj Cookfs_PgIndexExport(Cookfs_PgIndex *pgi);

#endif /* COOKFS_PGINDEX_H */
