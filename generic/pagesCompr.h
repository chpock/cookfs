/*
  (c) 2010-2014 Wojciech Kocjan, Pawel Salawa
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGESCOMPR_H
#define COOKFS_PAGESCOMPR_H 1

/* let's gain at least 16 bytes and/or 5% to compress it */
#define SHOULD_COMPRESS(p, origSize, size) ((p->alwaysCompress) || ((size < (origSize - 16)) && ((size) <= (origSize - (origSize / 20)))))

const char *Cookfs_CompressionGetName(Cookfs_CompressionType compression);

int Cookfs_CompressionFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    Cookfs_CompressionType *compressionPtr, int *compressionLevelPtr);

void Cookfs_PagesInitCompr(Cookfs_Pages *rc);
void Cookfs_PagesFiniCompr(Cookfs_Pages *rc);

int Cookfs_SetCompressCommands(Cookfs_Pages *p,
    Tcl_Obj *compressCommand,
    Tcl_Obj *decompressCommand,
    Tcl_Obj *asyncCompressCommand,
    Tcl_Obj *asyncDecompressCommand);

void Cookfs_SeekToPage(Cookfs_Pages *p, int idx);

Tcl_Size Cookfs_WritePage(Cookfs_Pages *p, int idx, unsigned char *bytes,
    Tcl_Size sizeUncompressed, unsigned char *md5hash,
    Cookfs_PageObj pgCompressed);

int Cookfs_WritePageObj(Cookfs_Pages *p, int idx, Cookfs_PageObj data,
    unsigned char *md5hash);

int Cookfs_WriteTclObj(Cookfs_Pages *p, int idx, Tcl_Obj *data, Tcl_Obj *compressedData);

Cookfs_PageObj Cookfs_ReadPage(Cookfs_Pages *p, int idx,
    Cookfs_CompressionType compression, int sizeCompressed,
    int sizeUncompressed, unsigned char *md5hash, int decompress,
    int encrypted, Tcl_Obj **err);

#endif /* COOKFS_PAGESCOMPR_H */
