/*
 * pagesComprZlib.c
 *
 * Provides zlib functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprZlib.h"

/*
 *----------------------------------------------------------------------
 *
 * CookfsReadPageZlib --
 *
 *	Read zlib compressed page
 *
 * Results:
 *	Binary data as Tcl_Obj
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj CookfsReadPageZlib(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    UNUSED(err);
#ifdef USE_ZLIB_TCL86
    /* use Tcl 8.6 API for decompression */
    Tcl_Obj *data;
    Tcl_Obj *cobj;
    Tcl_ZlibStream zshandle;
    int count;

    if (Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_INFLATE, TCL_ZLIB_FORMAT_RAW, 9, NULL, &zshandle) != TCL_OK) {
	CookfsLog(printf("Unable to initialize zlib"))
	return NULL;
    }
    data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    CookfsLog(printf("Reading - %d vs %d", count, size))
    if (count != size) {
	CookfsLog(printf("Unable to read - %d != %d", count, size))
	Tcl_DecrRefCount(data);
	return NULL;
    }

    CookfsLog(printf("Writing"))
    /* write compressed information */
    if (Tcl_ZlibStreamPut(zshandle, data, TCL_ZLIB_FINALIZE) != TCL_OK) {
	CookfsLog(printf("Unable to decompress - writing"))
	Tcl_ZlibStreamClose(zshandle);
	Tcl_DecrRefCount(data);
	return NULL;
    }
    Tcl_DecrRefCount(data);

    CookfsLog(printf("Reading"))
    /* read resulting object */
    cobj = Tcl_NewObj();
    Tcl_IncrRefCount(cobj);
    while (!Tcl_ZlibStreamEof(zshandle)) {
	if (Tcl_ZlibStreamGet(zshandle, cobj, -1) != TCL_OK) {
	    Tcl_DecrRefCount(cobj);
	    Tcl_ZlibStreamClose(zshandle);
	    CookfsLog(printf("Unable to decompress - reading"))
	    return NULL;
	}
    }
    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(cobj);
    Tcl_DecrRefCount(cobj);
    Tcl_ZlibStreamClose(zshandle);
    CookfsLog(printf("Returning = [%s]", rc == NULL ? "NULL" : "SET"))
    return rc;
#else
    /* use vfs::zip command for decompression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;
    Tcl_Obj *data;
    int count;

    compressed = Tcl_NewObj();
    Tcl_IncrRefCount(compressed);
    count = Tcl_ReadChars(p->fileChannel, compressed, size, 0);

    CookfsLog(printf("Reading - %d vs %d", count, size))
    if (count != size) {
	CookfsLog(printf("Unable to read - %d != %d", count, size))
	Tcl_DecrRefCount(compressed);
	return NULL;
    }
    p->zipCmdDecompress[p->zipCmdOffset] = compressed;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->zipCmdLength, p->zipCmdDecompress, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	CookfsLog(printf("Unable to decompress"))
	Tcl_DecrRefCount(compressed);
	return NULL;
    }
    Tcl_DecrRefCount(compressed);
    data = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(data);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(data);
    Tcl_DecrRefCount(data);
    return rc;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsWritePageZlib --
 *
 *	Write page using zlib compression
 *
 * Results:
 *	Number of bytes written; -1 in case compression failed or
 *	compressing was not efficient enough (see SHOULD_COMPRESS macro)
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int CookfsWritePageZlib(Cookfs_Pages *p, unsigned char *bytes, int origSize) {
#ifdef USE_ZLIB_TCL86
    Tcl_Size size = origSize;
    /* use Tcl 8.6 API for zlib compression */
    Tcl_Obj *cobj;
    Tcl_ZlibStream zshandle;

    int level = p->fileCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level >= 255) {
        level = 9;
    }

    if (Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_DEFLATE, TCL_ZLIB_FORMAT_RAW, level, NULL, &zshandle) != TCL_OK) {
	CookfsLog(printf("Cookfs_WritePage: Tcl_ZlibStreamInit failed!"))
	return -1;
    }

    Tcl_Obj *data = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(data);
    if (Tcl_ZlibStreamPut(zshandle, data, TCL_ZLIB_FINALIZE) != TCL_OK) {
	Tcl_DecrRefCount(data);
	Tcl_ZlibStreamClose(zshandle);
	CookfsLog(printf("Cookfs_WritePage: Tcl_ZlibStreamPut failed"))
	return -1;
    }
    Tcl_DecrRefCount(data);

    cobj = Tcl_NewObj();
    if (Tcl_ZlibStreamGet(zshandle, cobj, -1) != TCL_OK) {
	Tcl_IncrRefCount(cobj);
	Tcl_DecrRefCount(cobj);
	Tcl_ZlibStreamClose(zshandle);
	CookfsLog(printf("Cookfs_WritePage: Tcl_ZlibStreamGet failed"))
	return -1;
    }
    Tcl_ZlibStreamClose(zshandle);
    Tcl_IncrRefCount(cobj);
    Tcl_GetByteArrayFromObj(cobj, &size);

    if (SHOULD_COMPRESS(p, origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_ZLIB);
	Tcl_WriteObj(p->fileChannel, cobj);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(cobj);
#else
    int size = origSize;
    /* use vfs::zip command for compression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;

    Tcl_Obj data = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(data);
    p->zipCmdCompress[p->zipCmdOffset] = data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->zipCmdLength, p->zipCmdCompress, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	CookfsLog(printf("Unable to compress: %s", Tcl_GetString(Tcl_GetObjResult(p->interp))));
	Tcl_SetObjResult(p->interp, prevResult);
	Tcl_DecrRefCount(data);
	return -1;
    }
    Tcl_DecrRefCount(data);
    compressed = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(compressed);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    Tcl_GetByteArrayFromObj(compressed, &size);

    if (SHOULD_COMPRESS(p, origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_ZLIB);
	Tcl_WriteObj(p->fileChannel, compressed);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(compressed);
#endif
    return size;
}
