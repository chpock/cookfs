/*
  (c) 2010-2014 Wojciech Kocjan, Pawel Salawa
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGESCOMPR_H
#define COOKFS_PAGESCOMPR_H 1

#define COOKFS_COMPRESSION_NONE      0
#define COOKFS_COMPRESSION_ZLIB      1
#define COOKFS_COMPRESSION_BZ2       2
#define COOKFS_COMPRESSION_LZMA      3
#define COOKFS_COMPRESSION_ZSTD      4
#define COOKFS_COMPRESSION_BROTLI    5
#define COOKFS_COMPRESSION_CUSTOM  254
#define COOKFS_COMPRESSION_ANY     255

/* let's gain at least 16 bytes and/or 5% to compress it */
#define SHOULD_COMPRESS(p, origSize, size) ((p->alwaysCompress) || ((size < (origSize - 16)) && ((size) <= (origSize - (origSize / 20)))))

const char *Cookfs_CompressionGetName(int compression);

void CookfsWriteCompression(Cookfs_Pages *p, int compression);

int Cookfs_CompressionFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    int *compressionPtr, int *compressionLevelPtr);

void Cookfs_PagesInitCompr(Cookfs_Pages *rc);
void Cookfs_PagesFiniCompr(Cookfs_Pages *rc);

int Cookfs_SetCompressCommands(Cookfs_Pages *p, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand, Tcl_Obj *asyncCompressCommand, Tcl_Obj *asyncDecompressCommand);

void Cookfs_SeekToPage(Cookfs_Pages *p, int idx);
int Cookfs_WritePage(Cookfs_Pages *p, int idx, unsigned char *bytes, int origSize, Tcl_Obj *compressedData);
int Cookfs_WritePageObj(Cookfs_Pages *p, int idx, Cookfs_PageObj data, Tcl_Obj *compressedData);
int Cookfs_WriteTclObj(Cookfs_Pages *p, int idx, Tcl_Obj *data, Tcl_Obj *compressedData);
Cookfs_PageObj Cookfs_ReadPage(Cookfs_Pages *p, int idx, int size, int decompress, int compressionType, Tcl_Obj **err);
Cookfs_PageObj Cookfs_AsyncPageGet(Cookfs_Pages *p, int idx);
int Cookfs_AsyncPageAdd(Cookfs_Pages *p, int idx, unsigned char *bytes, int dataSize);
void Cookfs_AsyncDecompressWaitIfLoading(Cookfs_Pages *p, int idx);
int Cookfs_AsyncCompressWait(Cookfs_Pages *p, int require);
void Cookfs_AsyncCompressFinalize(Cookfs_Pages *p);
int Cookfs_AsyncPagePreload(Cookfs_Pages *p, int idx);
int Cookfs_AsyncDecompressWait(Cookfs_Pages *p, int idx, int require);
void Cookfs_AsyncDecompressFinalize(Cookfs_Pages *p);

#endif /* COOKFS_PAGESCOMPR_H */
