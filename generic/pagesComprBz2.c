/*
 * pagesComprBz2.c
 *
 * Provides bzip2 functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "../bzip2/bzlib.h"
#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprBz2.h"

int CookfsReadPageBz2(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog2(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    unsigned int resultSize = (unsigned int)sizeUncompressed;

    CookfsLog2(printf("call BZ2_bzBuffToBuffDecompress() ..."));
    int res = BZ2_bzBuffToBuffDecompress((char *)dataUncompressed, &resultSize,
        (char *)dataCompressed, (unsigned int)sizeCompressed, 0, 0);

    if (res != BZ_OK) {
        CookfsLog(printf("result: ERROR"));
        return TCL_ERROR;
    }

    CookfsLog2(printf("got %u bytes", resultSize));

    if (resultSize != (unsigned int)sizeUncompressed) {
        CookfsLog2(printf("ERROR: result size doesn't match original size"));
        return TCL_ERROR;
    }

    CookfsLog2(printf("return: ok"));
    return TCL_OK;

}

Cookfs_PageObj CookfsWritePageBz2(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog2(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    unsigned int resultSize = (unsigned int)origSize * 2 + 1024;
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(resultSize);
    if (rc == NULL) {
        CookfsLog2(printf("ERROR: could not alloc output buffer"));
        return NULL;
    }

    int level = p->currentCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level >= 255) {
        level = 9;
    }

    CookfsLog2(printf("call BZ2_bzBuffToBuffCompress() level %d ...", level));
    int res = BZ2_bzBuffToBuffCompress((char *)rc, &resultSize, (char *)bytes,
        (unsigned int)origSize, level, 0, 0);

    if (res != BZ_OK) {
        CookfsLog2(printf("call got ERROR"));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    }

    CookfsLog2(printf("got encoded size: %u", resultSize));
    Cookfs_PageObjSetSize(rc, resultSize);

    return rc;

}

