/*
 * pagesComprZstd.c
 *
 * Provides Zstandard functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "../zstd/lib/zstd.h"
#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprZstd.h"

int CookfsWritePageZstd(Cookfs_Pages *p, unsigned char *bytes, int origSize) {

    CookfsLog(printf("CookfsWritePageZstd: want to compress %d bytes",
        origSize));

    size_t resultSize;

    Tcl_Obj *destObj = Tcl_NewByteArrayObj(NULL, 0);
    Tcl_IncrRefCount(destObj);

    size_t destSize = ZSTD_compressBound((size_t) origSize);
    if (ZSTD_isError(destSize)) {
        CookfsLog(printf("CookfsWritePageZstd: ZSTD_compressBound()"
            " failed with: %s", ZSTD_getErrorName(destSize)));
        resultSize = 0;
        goto done;
    }

    // Allocate additional 4 bytes for uncompressed page size
    unsigned char *dest = Tcl_SetByteArrayLength(destObj, destSize + 4);

    int level = p->fileCompressionLevel;

    if (level < 1) {
        level = 1;
    } else if (level > 22) {
        level = 22;
    }

    CookfsLog(printf("CookfsWritePageZstd: call ZSTD_compress() level %d ...",
        level));

    // Leave 4 bytes in the buffer for uncompressed page size
    resultSize = ZSTD_compress(dest + 4, destSize, bytes, origSize, level);

    if (ZSTD_isError(resultSize)) {
        CookfsLog(printf("CookfsWritePageZstd: call got error: %s",
            ZSTD_getErrorName(resultSize)));
        resultSize = 0;
        goto done;
    }

    // Add 4 bytes to resultSize, which is the size of the uncompressed page
    resultSize += 4;
    CookfsLog(printf("CookfsWritePageZstd: got encoded size: %zu", resultSize));
    if (SHOULD_COMPRESS(p, (unsigned int)origSize, resultSize)) {
        CookfsLog(printf("CookfsWritePageZstd: write page"));
        Tcl_SetByteArrayLength(destObj, resultSize);
        // Write the original size to the beginning of the buffer
        Cookfs_Int2Binary(&origSize, dest, 1);
        CookfsWriteCompression(p, COOKFS_COMPRESSION_ZSTD);
        Tcl_WriteObj(p->fileChannel, destObj);
    } else {
        CookfsLog(printf("CookfsWritePageZstd: compression is inefficient"));
        resultSize = 0;
    }

done:
    Tcl_DecrRefCount(destObj);
    if (resultSize) {
        return resultSize;
    } else {
        return -1;
    }
}

Cookfs_PageObj CookfsReadPageZstd(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    UNUSED(err);

    CookfsLog(printf("CookfsReadPageZstd: start. Want to read %d bytes.", size));

    Tcl_Obj *data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    int count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    if (count != size) {
        CookfsLog(printf("CookfsReadPageZstd: failed to read, got only %d"
            " bytes", count));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    unsigned char *source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
        CookfsLog(printf("CookfsReadPageZstd: Tcl_GetByteArrayFromObj failed"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    int destSize;
    Cookfs_Binary2Int(source, &destSize, 1);

    Cookfs_PageObj destObj = Cookfs_PageObjAlloc(destSize);
    if (destObj == NULL) {
        CookfsLog(printf("CookfsReadPageZstd: ERROR: failed to alloc"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    CookfsLog(printf("CookfsReadPageZstd: uncompressed size=%d from %d",
        destSize, size));

    CookfsLog(printf("CookfsReadPageZstd: call ZSTD_decompress() ..."));
    size_t resultSize = ZSTD_decompress(destObj, destSize, source + 4,
        size - 4);
    Tcl_DecrRefCount(data);

    if (ZSTD_isError(resultSize)) {
        CookfsLog(printf("CookfsReadPageZstd: call got error: %s",
            ZSTD_getErrorName(resultSize)));
        goto unpackError;
    }

    CookfsLog(printf("CookfsReadPageZstd: got %zu bytes", resultSize));

    if (resultSize != (unsigned int)destSize) {
        CookfsLog(printf("CookfsReadPageZstd: ERROR: result size doesn't"
            " match original size"));
        goto unpackError;
    }

    return destObj;

unpackError:

    Cookfs_PageObjIncrRefCount(destObj);
    Cookfs_PageObjDecrRefCount(destObj);
    return NULL;
}

