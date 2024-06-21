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

/*
 *----------------------------------------------------------------------
 *
 * CookfsWritePageLzma --
 *
 *	Write page using lzma compression
 *
 * Results:
 *	Number of bytes written; -1 in case compression failed. This function
 *      doesn't return -1 compressing was not efficient enough.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int CookfsWritePageLzma(Cookfs_Pages *p, unsigned char *bytes, int origSize) {

    CookfsLog(printf("CookfsWritePageLzma: want to compress %d bytes",
        origSize));

    // Output size should be 4 (original size) + 5 (LZMA_PROPS_SIZE)
    // + compressed data
    // So, the minimum compressed size is 9 bytes + compressed data.
    // Let's refuse to compress 16 bytes or less.
    if (origSize <= 16) {
        CookfsLog(printf("CookfsWritePageLzma: too few bytes for"
            " compression"));
        return -1;
    }

    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = p->fileCompressionLevel;
    if (props.level < 0) {
        props.level = 0;
    } else if (props.level >= 255) {
        props.level = 9;
    }
    props.reduceSize = origSize;
    LzmaEncProps_Normalize(&props);

    Tcl_Obj *destObj = Tcl_NewByteArrayObj(NULL, 0);
    Tcl_IncrRefCount(destObj);

    // Let's allocate a destination buffer of the same size as the source
    // buffer. In case of buffer overflow, lzma should return
    // SZ_ERROR_OUTPUT_EOF. Then we'll know that the compression was
    // ineffective.

    unsigned char *dest = Tcl_SetByteArrayLength(destObj, origSize);

    // The destination buffer will also contain the original size + lzma
    // properties. Calculate here what buffer size should be passed to
    // LzmaEncode()
    size_t destLen = origSize - 4 - LZMA_PROPS_SIZE;

    CookfsLog(printf("CookfsWritePageLzma: call LzmaEncode() level %d ...", props.level));
    SizeT propsSize = LZMA_PROPS_SIZE;
    SRes res = LzmaEncode(&dest[4 + LZMA_PROPS_SIZE], &destLen, bytes,
        origSize, &props, &dest[4], &propsSize, 0, NULL, &g_CookfsLzmaAlloc,
        &g_CookfsLzmaAlloc);
    CookfsLog(printf("CookfsWritePageLzma: got: %s",
        (res == SZ_OK ? "SZ_OK" :
        (res == SZ_ERROR_MEM ? "SZ_ERROR_MEM" :
        (res == SZ_ERROR_PARAM ? "SZ_ERROR_PARAM" :
        (res == SZ_ERROR_WRITE ? "SZ_ERROR_WRITE" :
        (res == SZ_ERROR_OUTPUT_EOF ? "SZ_ERROR_OUTPUT_EOF" :
        (res == SZ_ERROR_PROGRESS ? "SZ_ERROR_PROGRESS" :
        (res == SZ_ERROR_THREAD ? "SZ_ERROR_THREAD" : "UNKNOWN")))))))));

    // Continue with the compression only when res == SZ_OK
    if (res == SZ_OK) {

        // Increase output size by pre-defined bytes (origSize + lzma
        // properties)
        destLen += 4 + LZMA_PROPS_SIZE;
        CookfsLog(printf("CookfsWritePageLzma: got encoded size: %zu", destLen));
        if (SHOULD_COMPRESS(p, (unsigned int)origSize, destLen)) {
            // Write the original size to the beginning of the buffer
            CookfsLog(printf("CookfsWritePageLzma: write page"));
            Tcl_SetByteArrayLength(destObj, destLen);
            Cookfs_Int2Binary(&origSize, dest, 1);
            CookfsWriteCompression(p, COOKFS_COMPRESSION_LZMA);
            Tcl_WriteObj(p->fileChannel, destObj);
        } else {
            res = SZ_ERROR_OUTPUT_EOF;
        }

    }

    Tcl_DecrRefCount(destObj);
    if (res == SZ_OK) {
        return destLen;
    } else {
        return -1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsReadPageLzma --
 *
 *	Read lzma compressed page
 *
 * Results:
 *	Binary data as Tcl_Obj
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj CookfsReadPageLzma(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    UNUSED(err);

    CookfsLog(printf("CookfsReadPageLzma: start. Want to read %d bytes.", size));

    Tcl_Obj *data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    int count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    if (count != size) {
        CookfsLog(printf("CookfsReadPageLzma: failed to read, got only %d"
            " bytes", count));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    unsigned char *source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
        CookfsLog(printf("CookfsReadPageLzma: Tcl_GetByteArrayFromObj failed"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    int destSize;
    Cookfs_Binary2Int(source, &destSize, 1);

    Cookfs_PageObj destObj = Cookfs_PageObjAlloc(destSize);
    if (destObj == NULL) {
        CookfsLog(printf("CookfsReadPageLzma: ERROR: failed to alloc"));
        Tcl_DecrRefCount(data);
        return NULL;
    }

    CookfsLog(printf("CookfsReadPageLzma: uncompressed size=%d from %d",
        destSize, size));

    CookfsLog(printf("CookfsReadPageLzma: call LzmaDecode() ..."));
    SizeT destSizeResult = destSize;
    ELzmaStatus status;
    // Source buffer also contains the original size and lzma properties
    SizeT srcLen = size - 4 - LZMA_PROPS_SIZE;
    SRes res = LzmaDecode(destObj, &destSizeResult,
        &source[4 + LZMA_PROPS_SIZE], &srcLen, &source[4], LZMA_PROPS_SIZE,
        LZMA_FINISH_END, &status, &g_CookfsLzmaAlloc);
    CookfsLog(printf("CookfsReadPageLzma: result: %s; status: %s",
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
    CookfsLog(printf("CookfsReadPageLzma: consumed bytes %zu got bytes %zu",
        srcLen, destSizeResult));

    Tcl_DecrRefCount(data);

    if ((res != SZ_OK) || (destSizeResult != (unsigned int)destSize) ||
        (srcLen != ((unsigned int)size - 4 - LZMA_PROPS_SIZE)) ||
        ((status != LZMA_STATUS_FINISHED_WITH_MARK) &&
        (status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)))
    {
        CookfsLog(printf("CookfsReadPageLzma: failed"))
        Cookfs_PageObjIncrRefCount(destObj);
        Cookfs_PageObjDecrRefCount(destObj);
        return NULL;
    }

    return destObj;
}

