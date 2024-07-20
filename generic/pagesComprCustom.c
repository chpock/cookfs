/*
 * pagesComprCustom.c
 *
 * Provides functions for custom pages compression
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprCustom.h"

int CookfsReadPageCustom(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    CookfsLog2(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    if (p->decompressCommandPtr == NULL) {
        CookfsLog2(printf("ERROR: No decompresscommand specified"));
        SET_ERROR_STR("No decompresscommand specified");
        return TCL_ERROR;
    }

    int res;

    Tcl_Obj *sourceObj = Tcl_NewByteArrayObj(dataCompressed, sizeCompressed);
    Tcl_IncrRefCount(sourceObj);

    CookfsLog2(printf("p = %p", (void *)p));
    p->decompressCommandPtr[p->decompressCommandLen - 1] = sourceObj;

    CookfsLog2(printf("call custom decompression command ..."));
    res = Tcl_EvalObjv(p->interp, p->decompressCommandLen,
        p->decompressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);

    p->decompressCommandPtr[p->decompressCommandLen - 1] = NULL;
    Tcl_DecrRefCount(sourceObj);

    if (res != TCL_OK) {
        CookfsLog2(printf("return: ERROR"));
        return TCL_ERROR;
    }

    Tcl_Obj *destObj = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(destObj);
    Tcl_ResetResult(p->interp);

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

}

Cookfs_PageObj CookfsWritePageCustom(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog2(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    if (p->compressCommandPtr == NULL) {
        CookfsLog2(printf("ERROR: No compresscommand specified"));
        return NULL;
    }

    int res;

    Tcl_Obj *inputData = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(inputData);

    p->compressCommandPtr[p->compressCommandLen - 1] = inputData;
    CookfsLog2(printf("call custom compression command ..."));
    res = Tcl_EvalObjv(p->interp, p->compressCommandLen, p->compressCommandPtr,
        TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL);

    p->compressCommandPtr[p->compressCommandLen - 1] = NULL;
    Tcl_DecrRefCount(inputData);

    if (res != TCL_OK) {
        CookfsLog2(printf("return: ERROR"));
        return NULL;
    }

    Tcl_Obj *outputObj = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(outputObj);
    Tcl_ResetResult(p->interp);

    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(outputObj);
    Tcl_DecrRefCount(outputObj);
    if (rc == NULL) {
        CookfsLog2(printf("return: ERROR (failed to alloc)"));
        return NULL;
    }

    CookfsLog2(printf("got encoded size: %" TCL_SIZE_MODIFIER "d",
        Cookfs_PageObjSize(rc)));

    return rc;

}
