/*
 * pagesComprLzma.c
 *
 * Provides lzma functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "../7zip/C/LzmaEnc.h"
#include "../7zip/C/LzmaDec.h"
#include "../7zip/C/Alloc.h"
#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprLzma.h"

static void *CookfsLzmaAlloc(ISzAllocPtr p, size_t size) {
    UNUSED(p);
    return ckalloc(size);
}

static void CookfsLzmaFree(ISzAllocPtr p, void *address) {
    UNUSED(p);
    ckfree(address);
}

const ISzAlloc g_CookfsLzmaAlloc = {
    CookfsLzmaAlloc,
    CookfsLzmaFree
};

Cookfs_PageObj CookfsWritePageLzma(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog2(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    // The destination buffer should contain lzma properties + compression
    // data.
    size_t resultSize = LZMA_PROPS_SIZE + (size_t)origSize +
        (size_t)origSize / 1024 + 128;
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(resultSize);
    if (rc == NULL) {
        CookfsLog2(printf("ERROR: could not alloc output buffer"));
        return NULL;
    }

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = p->currentCompressionLevel;
    if (props.level < 0) {
        props.level = 0;
    } else if (props.level >= 255) {
        props.level = 9;
    }
    props.reduceSize = origSize;
    LzmaEncProps_Normalize(&props);

    SizeT propsSize = LZMA_PROPS_SIZE;

    // Reduce the size of the destination buffer by the size of the lzma
    // properties.
    resultSize -= LZMA_PROPS_SIZE;
    CookfsLog(printf("CookfsWritePageLzma: call LzmaEncode() level %d ...",
        props.level));
    SRes res = LzmaEncode(&rc->buf[LZMA_PROPS_SIZE], &resultSize, bytes,
        origSize, &props, rc->buf, &propsSize, 0, NULL, &g_CookfsLzmaAlloc,
        &g_CookfsLzmaAlloc);

    CookfsLog(printf("CookfsWritePageLzma: got: %s",
        (res == SZ_OK ? "SZ_OK" :
        (res == SZ_ERROR_MEM ? "SZ_ERROR_MEM" :
        (res == SZ_ERROR_PARAM ? "SZ_ERROR_PARAM" :
        (res == SZ_ERROR_WRITE ? "SZ_ERROR_WRITE" :
        (res == SZ_ERROR_OUTPUT_EOF ? "SZ_ERROR_OUTPUT_EOF" :
        (res == SZ_ERROR_PROGRESS ? "SZ_ERROR_PROGRESS" :
        (res == SZ_ERROR_THREAD ? "SZ_ERROR_THREAD" : "UNKNOWN")))))))));

    if (res != SZ_OK) {
        CookfsLog2(printf("return: ERROR"));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    }

    // Increase the size of the destination buffer by the size of the lzma
    // properties.
    resultSize += LZMA_PROPS_SIZE;

    CookfsLog2(printf("got encoded size: %zu", resultSize));
    Cookfs_PageObjSetSize(rc, resultSize);

    return rc;

}

int CookfsReadPageLzma(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog2(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    SizeT destSizeResult = (SizeT)sizeUncompressed;
    ELzmaStatus status;
    // Source buffer also contains the original size and lzma properties
    SizeT srcLen = (SizeT)sizeCompressed - LZMA_PROPS_SIZE;

    CookfsLog2(printf("call LzmaDecode() ..."));
    SRes res = LzmaDecode(dataUncompressed, &destSizeResult,
        &dataCompressed[LZMA_PROPS_SIZE], &srcLen, dataCompressed,
        LZMA_PROPS_SIZE, LZMA_FINISH_END, &status, &g_CookfsLzmaAlloc);

    CookfsLog2(printf("result: %s; status: %s",
        (res == SZ_OK ? "SZ_OK" :
        (res == SZ_ERROR_DATA ? "SZ_ERROR_DATA" :
        (res == SZ_ERROR_MEM ? "SZ_ERROR_MEM" :
        (res == SZ_ERROR_UNSUPPORTED ? "SZ_ERROR_UNSUPPORTED" :
        (res == SZ_ERROR_INPUT_EOF ? "SZ_ERROR_INPUT_EOF" :
        (res == SZ_ERROR_FAIL ? "SZ_ERROR_FAIL" : "UNKNOWN")))))),
        (res == SZ_OK ?
            (status == LZMA_STATUS_FINISHED_WITH_MARK ? "FINISHED_WITH_MARK" :
            (status == LZMA_STATUS_NOT_FINISHED ? "NOT_FINISHED" :
            (status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK ?
            "MAYBE_FINISHED_WITHOUT_MARK" : "UNKNOWN-OK"))) : "UNKNOWN")));

    CookfsLog2(printf("consumed bytes %zu got bytes %zu", srcLen, destSizeResult));

    if ((res != SZ_OK) || (destSizeResult != (unsigned int)sizeUncompressed) ||
        (srcLen != ((unsigned int)sizeCompressed - LZMA_PROPS_SIZE)) ||
        ((status != LZMA_STATUS_FINISHED_WITH_MARK) &&
        (status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)))
    {
        CookfsLog2(printf("return: ERROR"));
        return TCL_ERROR;
    }

    CookfsLog2(printf("return: ok"));
    return TCL_OK;

}

