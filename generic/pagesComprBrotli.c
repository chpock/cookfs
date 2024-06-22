/*
 * pagesComprBrotli.c
 *
 * Provides brotli functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "../brotli/c/include/brotli/decode.h"
#include "../brotli/c/include/brotli/encode.h"
#include "../brotli/c/include/brotli/types.h"
#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprBrotli.h"

int CookfsWritePageBrotli(Cookfs_Pages *p, unsigned char *bytes, int origSize) {

    CookfsLog(printf("CookfsWritePageBrotli: want to compress %d bytes",
        origSize));

    Tcl_Obj *destObj = Tcl_NewByteArrayObj(NULL, 0);
    Tcl_IncrRefCount(destObj);

    size_t resultSize = BrotliEncoderMaxCompressedSize((size_t) origSize);
    if (!resultSize) {
        CookfsLog(printf("CookfsWritePageBrotli:"
            " BrotliEncoderMaxCompressedSize failed"));
        resultSize = 0;
        goto done;
    }

    // Allocate additional 4 bytes for uncompressed page size
    unsigned char *dest = Tcl_SetByteArrayLength(destObj, resultSize + 4);

    int level = p->fileCompressionLevel;

    if (level < 0) {
        level = 0;
    } else if (level > 11) {
        level = 11;
    }

    CookfsLog(printf("CookfsWritePageBrotli: call BrotliEncoderCompress()"
        " level %d ...", level));

    // Leave 4 bytes in the buffer for uncompressed page size
    BROTLI_BOOL res = BrotliEncoderCompress(level, BROTLI_DEFAULT_WINDOW,
        BROTLI_DEFAULT_MODE, (size_t)origSize, bytes, &resultSize, dest + 4);

    if (res != BROTLI_TRUE) {
        CookfsLog(printf("CookfsWritePageBrotli: call got ERROR"));
        resultSize = 0;
        goto done;
    }

    // Add 4 bytes to resultSize, which is the size of the uncompressed page
    resultSize += 4;
    CookfsLog(printf("CookfsWritePageBrotli: got encoded size: %zu",
        resultSize));
    if (SHOULD_COMPRESS(p, (unsigned int)origSize, resultSize)) {
        CookfsLog(printf("CookfsWritePageBrotli: write page"));
        Tcl_SetByteArrayLength(destObj, resultSize);
        // Write the original size to the beginning of the buffer
        Cookfs_Int2Binary(&origSize, dest, 1);
        CookfsWriteCompression(p, COOKFS_COMPRESSION_BROTLI);
        Tcl_WriteObj(p->fileChannel, destObj);
    } else {
        CookfsLog(printf("CookfsWritePageBrotli: compression is inefficient"));
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

Cookfs_PageObj CookfsReadPageBrotli(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    UNUSED(err);

    CookfsLog(printf("CookfsReadPageBrotli: start. Want to read %d bytes.", size));

    Tcl_Obj *data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    int count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    if (count != size) {
        CookfsLog(printf("CookfsReadPageBrotli: failed to read, got only %d"
            " bytes", count));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    unsigned char *source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
        CookfsLog(printf("CookfsReadPageBrotli: Tcl_GetByteArrayFromObj failed"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    int destSize;
    Cookfs_Binary2Int(source, &destSize, 1);

    Cookfs_PageObj destObj = Cookfs_PageObjAlloc(destSize);
    if (destObj == NULL) {
        CookfsLog(printf("CookfsReadPageBrotli: ERROR: failed to alloc"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    CookfsLog(printf("CookfsReadPageBrotli: uncompressed size=%d from %d",
        destSize, size));

    CookfsLog(printf("CookfsReadPageBrotli: call BrotliDecoderDecompress()"
        " ..."));
    size_t resultSize = destSize;
    BrotliDecoderResult res = BrotliDecoderDecompress(size - 4, source + 4,
        &resultSize, destObj);
    Tcl_DecrRefCount(data);

    if (res != BROTLI_DECODER_RESULT_SUCCESS) {
        CookfsLog(printf("CookfsReadPageBrotli: call got ERROR"));
        goto unpackError;
    }

    CookfsLog(printf("CookfsReadPageBrotli: got %zu bytes", resultSize));

    if (resultSize != (unsigned int)destSize) {
        CookfsLog(printf("CookfsReadPageBrotli: ERROR: result size doesn't"
            " match original size"));
        goto unpackError;
    }

    return destObj;

unpackError:

    Cookfs_PageObjIncrRefCount(destObj);
    Cookfs_PageObjDecrRefCount(destObj);
    return NULL;
}

