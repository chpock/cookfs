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

Cookfs_PageObj CookfsWritePageBrotli(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog2(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    size_t resultSize = BrotliEncoderMaxCompressedSize((size_t) origSize);
    if (!resultSize) {
        CookfsLog2(printf("ERROR: BrotliEncoderMaxCompressedSize failed"));
        return NULL;
    }
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(resultSize);
    if (rc == NULL) {
        CookfsLog2(printf("ERROR: could not alloc output buffer"));
        return NULL;
    }

    int level = p->currentCompressionLevel;

    if (level < 0) {
        level = 0;
    } else if (level > 11) {
        level = 11;
    }

    CookfsLog2(printf("call BrotliEncoderCompress() level %d ...", level));

    // Leave 4 bytes in the buffer for uncompressed page size
    BROTLI_BOOL res = BrotliEncoderCompress(level, BROTLI_DEFAULT_WINDOW,
        BROTLI_DEFAULT_MODE, (size_t)origSize, bytes, &resultSize, rc->buf);

    if (res != BROTLI_TRUE) {
        CookfsLog2(printf("call got ERROR"));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    }

    CookfsLog2(printf("got encoded size: %zu", resultSize));
    Cookfs_PageObjSetSize(rc, resultSize);

    return rc;

}

int CookfsReadPageBrotli(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog2(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    size_t resultSize = (size_t)sizeUncompressed;

    CookfsLog2(printf("call BrotliDecoderDecompress() ..."));
    BrotliDecoderResult res = BrotliDecoderDecompress(sizeCompressed,
        dataCompressed, &resultSize, dataUncompressed);

    if (res != BROTLI_DECODER_RESULT_SUCCESS) {
        CookfsLog(printf("result: ERROR"));
        return TCL_ERROR;
    }

    CookfsLog2(printf("got %zu bytes", resultSize));

    if (resultSize != (size_t)sizeUncompressed) {
        CookfsLog2(printf("ERROR: result size doesn't match original size"));
        return TCL_ERROR;
    }

    CookfsLog2(printf("return: ok"));
    return TCL_OK;

}

