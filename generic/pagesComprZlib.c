/*
 * pagesComprZlib.c
 *
 * Provides zlib functions for pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprZlib.h"

int CookfsReadPageZlib(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog2(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

#ifdef USE_ZLIB_TCL86
    /* use Tcl 8.6 API for decompression */
    int res;
    Tcl_ZlibStream zshandle;

    CookfsLog2(printf("initialize zlib handle"))
    res = Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_INFLATE,
        TCL_ZLIB_FORMAT_RAW, 9, NULL, &zshandle);
    if (res != TCL_OK) {
        CookfsLog2(printf("Unable to initialize zlib"));
        return TCL_ERROR;
    }

    Tcl_Obj *sourceObj = Tcl_NewByteArrayObj(dataCompressed, sizeCompressed);
    Tcl_IncrRefCount(sourceObj);

    CookfsLog2(printf("call Tcl_ZlibStreamPut() ..."));
    res = Tcl_ZlibStreamPut(zshandle, sourceObj, TCL_ZLIB_FINALIZE);
    Tcl_DecrRefCount(sourceObj);

    if (res != TCL_OK) {
        CookfsLog2(printf("return: ERROR"));
        Tcl_ZlibStreamClose(zshandle);
        return TCL_ERROR;
    }

    Tcl_Obj *destObj = Tcl_NewObj();
    Tcl_IncrRefCount(destObj);

    CookfsLog2(printf("reading from the handle..."));
    while (!Tcl_ZlibStreamEof(zshandle)) {
        if (Tcl_ZlibStreamGet(zshandle, destObj, -1) != TCL_OK) {
            Tcl_DecrRefCount(destObj);
            Tcl_ZlibStreamClose(zshandle);
            CookfsLog2(printf("return: ERROR (while reading)"));
            return TCL_ERROR;
        }
    }

    Tcl_ZlibStreamClose(zshandle);

    Tcl_Size destObjSize;
    unsigned char *destStr = Tcl_GetByteArrayFromObj(destObj, &destObjSize);

    if (destObjSize != sizeUncompressed) {
        CookfsLog2(printf("ERROR: result size doesn't match original size"));
        Tcl_DecrRefCount(destObj);
        return TCL_ERROR;
    }

    CookfsLog2(printf("copy data to the output buffer"));
    memcpy(dataUncompressed, destStr, destObjSize);

    Tcl_DecrRefCount(destObj);

    CookfsLog2(printf("return: ok"));
    return TCL_OK;

#else
#error Only Tcl8.6 with zlib is supported.
#endif /* USE_ZLIB_TCL86 */
}


Cookfs_PageObj CookfsWritePageZlib(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog2(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

#ifdef USE_ZLIB_TCL86
    /* use Tcl 8.6 API for zlib compression */
    Tcl_ZlibStream zshandle;
    int res;

    int level = p->currentCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level >= 255) {
        level = 9;
    }

    CookfsLog2(printf("initialize zlib handle"))
    res = Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_DEFLATE,
        TCL_ZLIB_FORMAT_RAW, level, NULL, &zshandle);
    if (res != TCL_OK) {
        CookfsLog2(printf("ERROR: Tcl_ZlibStreamInit failed"));
        return NULL;
    }

    Tcl_Obj *inputData = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(inputData);

    CookfsLog2(printf("call Tcl_ZlibStreamPut() ..."));
    res = Tcl_ZlibStreamPut(zshandle, inputData, TCL_ZLIB_FINALIZE);
    Tcl_DecrRefCount(inputData);
    if (res != TCL_OK) {
        CookfsLog2(printf("ERROR: failed"));
        Tcl_ZlibStreamClose(zshandle);
        return NULL;
    }

    Tcl_Obj *outputObj = Tcl_NewObj();
    Tcl_IncrRefCount(outputObj);

    CookfsLog2(printf("reading from the handle..."));
    res = Tcl_ZlibStreamGet(zshandle, outputObj, -1);
    Tcl_ZlibStreamClose(zshandle);
    if (res != TCL_OK) {
        CookfsLog2(printf("return: ERROR (while reading)"));
        Tcl_DecrRefCount(outputObj);
        return NULL;
    }

    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(outputObj);
    Tcl_DecrRefCount(outputObj);
    if (rc == NULL) {
        CookfsLog2(printf("return: ERROR (failed to alloc)"));
        return NULL;
    }

    CookfsLog2(printf("got encoded size: %" TCL_SIZE_MODIFIER "d",
        Cookfs_PageObjSize(rc)));

    return rc;

#else
#error Only Tcl8.6 with zlib is supported.
#endif /* USE_ZLIB_TCL86 */
}
