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

Cookfs_PageObj CookfsWritePageZstd(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    size_t resultSize = ZSTD_compressBound((size_t) origSize);
    if (ZSTD_isError(resultSize)) {
        CookfsLog(printf("ZSTD_compressBound() failed with: %s",
            ZSTD_getErrorName(resultSize)));
        return NULL;
    }
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(resultSize);
    if (rc == NULL) {
        CookfsLog(printf("ERROR: could not alloc output buffer"));
        return NULL;
    }

    int level = p->currentCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level > 22) {
        level = 22;
    }

    CookfsLog(printf("call ZSTD_compress() level %d ...", level));
    resultSize = ZSTD_compress(rc->buf, resultSize, bytes, origSize, level);

    if (ZSTD_isError(resultSize)) {
        CookfsLog(printf("got error: %s", ZSTD_getErrorName(resultSize)));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    }

    CookfsLog(printf("got encoded size: %zu", resultSize));
    Cookfs_PageObjSetSize(rc, resultSize);

    return rc;

}

int CookfsReadPageZstd(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    CookfsLog(printf("call ZSTD_decompress() ..."));
    size_t resultSize = ZSTD_decompress(dataUncompressed, sizeUncompressed,
        dataCompressed, sizeCompressed);

    if (ZSTD_isError(resultSize)) {
        CookfsLog(printf("call got error: %s", ZSTD_getErrorName(resultSize)));
        return TCL_ERROR;
    }

    CookfsLog(printf("got %zu bytes", resultSize));

    if (resultSize != (size_t)sizeUncompressed) {
        CookfsLog(printf("ERROR: result size doesn't match original size"));
        return TCL_ERROR;
    }

    CookfsLog(printf("return: ok"));
    return TCL_OK;

}

