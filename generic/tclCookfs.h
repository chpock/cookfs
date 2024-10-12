/*
 * tclCookfs.h
 *
 * Global header file for the Tcl cookfs extension
 *
 * (c) 2024 Konstantin Kushnir
 */

#ifndef _TCLCOOKFS_H_
#define _TCLCOOKFS_H_

#include <tcl.h>
#include <stdint.h>

typedef enum {
    COOKFS_PROP_PAGESOBJ,
    COOKFS_PROP_FSINDEXOBJ,
    COOKFS_PROP_NOREGISTER,
    COOKFS_PROP_BOOTSTRAP,
    COOKFS_PROP_NOCOMMAND,
    COOKFS_PROP_COMPRESSION,
    COOKFS_PROP_COMPRESSIONLEVEL,
    COOKFS_PROP_ALWAYSCOMPRESS,
    COOKFS_PROP_COMPRESSCOMMAND,
    COOKFS_PROP_DECOMPRESSCOMMAND,
    COOKFS_PROP_ASYNCCOMPRESSCOMMAND,
    COOKFS_PROP_ASYNCDECOMPRESSCOMMAND,
    COOKFS_PROP_ASYNCDECOMPRESSQUEUESIZE,
    COOKFS_PROP_ENDOFFSET,
    COOKFS_PROP_SETMETADATA,
    COOKFS_PROP_READONLY,
    COOKFS_PROP_WRITETOMEMORY,
    COOKFS_PROP_PAGECACHESIZE,
    COOKFS_PROP_VOLUME,
    COOKFS_PROP_PAGESIZE,
    COOKFS_PROP_SMALLFILESIZE,
    COOKFS_PROP_SMALLFILEBUFFER,
    COOKFS_PROP_NODIRECTORYMTIME,
    COOKFS_PROP_PAGEHASH,
    COOKFS_PROP_SHARED,
    COOKFS_PROP_PASSWORD,
    COOKFS_PROP_ENCRYPTKEY,
    COOKFS_PROP_ENCRYPTLEVEL,
    COOKFS_PROP_FILESET
} Cookfs_VfsPropertiesType;

typedef enum {
    COOKFS_COMPRESSION_DEFAULT = -1,
    COOKFS_COMPRESSION_NONE    =  0,
    COOKFS_COMPRESSION_ZLIB    =  1,
    COOKFS_COMPRESSION_BZ2     =  2,
    COOKFS_COMPRESSION_LZMA    =  3,
    COOKFS_COMPRESSION_ZSTD    =  4,
    COOKFS_COMPRESSION_BROTLI  =  5,
    COOKFS_COMPRESSION_CUSTOM  =  254
} Cookfs_CompressionType;

typedef enum {
    COOKFS_HASH_DEFAULT = -1,
    COOKFS_HASH_MD5     =  0,
    COOKFS_HASH_CRC32   =  1
} Cookfs_HashType;

typedef struct _Cookfs_VfsProps Cookfs_VfsProps;

Cookfs_VfsProps *Cookfs_VfsPropsInit(void);

void Cookfs_VfsPropsFree(Cookfs_VfsProps *p);

// This is an internal function that should not be used
void Cookfs_VfsPropSet(Cookfs_VfsProps *p, Cookfs_VfsPropertiesType type,
    intptr_t value);

static inline void Cookfs_VfsPropSetPagesObject(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_PAGESOBJ, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetFsindexObject(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_FSINDEXOBJ, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetNoRegister(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_NOREGISTER, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetBootstrap(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_BOOTSTRAP, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetNoCommand(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_NOCOMMAND, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetCompression(Cookfs_VfsProps *p,
    Cookfs_VfsPropertiesType v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_COMPRESSION, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetCompressionLevel(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_COMPRESSIONLEVEL, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetAlwaysCompress(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ALWAYSCOMPRESS, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetCompressCommand(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_COMPRESSCOMMAND, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetDecompressCommand(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_DECOMPRESSCOMMAND, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetAsyncCompressCommand(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ASYNCCOMPRESSCOMMAND, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetAsyncDecompressCommand(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ASYNCDECOMPRESSCOMMAND, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetAsyncDecompressQueueSize(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ASYNCDECOMPRESSQUEUESIZE, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetEndOffset(Cookfs_VfsProps *p,
    Tcl_WideInt v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ENDOFFSET, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetSetMetadata(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_SETMETADATA, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetReadonly(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_READONLY, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetWriteToMemory(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_WRITETOMEMORY, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetPageCacheSize(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_PAGECACHESIZE, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetVolume(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_VOLUME, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetPageSize(Cookfs_VfsProps *p,
    Tcl_WideInt v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_PAGESIZE, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetSmallFileSize(Cookfs_VfsProps *p,
    Tcl_WideInt v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_SMALLFILESIZE, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetSmallFileBuffer(Cookfs_VfsProps *p,
    Tcl_WideInt v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_SMALLFILEBUFFER, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetNoDirectoryMtime(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_NODIRECTORYMTIME, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetPageHash(Cookfs_VfsProps *p,
    Cookfs_HashType v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_PAGEHASH, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetShared(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_SHARED, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetPassword(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_PASSWORD, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetEncryptKey(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ENCRYPTKEY, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetEncryptLevel(Cookfs_VfsProps *p,
    int v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_ENCRYPTLEVEL, (intptr_t)v);
}

static inline void Cookfs_VfsPropSetFileset(Cookfs_VfsProps *p,
    Tcl_Obj *v)
{
    Cookfs_VfsPropSet(p, COOKFS_PROP_FILESET, (intptr_t)v);
}

int Cookfs_Mount(Tcl_Interp *interp, Tcl_Obj *archive, Tcl_Obj *local,
    Cookfs_VfsProps *props);

#ifdef __cplusplus
extern "C" {
#endif

// Exported from cookfs.c file.
DLLEXPORT int Cookfs_Init(Tcl_Interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* _TCLCOOKFS_H_ */

