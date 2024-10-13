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
#if HAVE_ZLIB
#include <zlib.h>
#endif /* HAVE_ZLIB */

int CookfsReadPageZlib(Cookfs_Pages *p, unsigned char *dataCompressed,
    Tcl_Size sizeCompressed, unsigned char *dataUncompressed,
    Tcl_Size sizeUncompressed, Tcl_Obj **err)
{

    UNUSED(err);
    UNUSED(p);

    CookfsLog(printf("input buffer %p (%" TCL_SIZE_MODIFIER "d bytes) ->"
        " output buffer %p (%" TCL_SIZE_MODIFIER "d bytes)",
        (void *)dataCompressed, sizeCompressed,
        (void *)dataUncompressed, sizeUncompressed));

    int res;

#if HAVE_ZLIB

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    stream.avail_in = (uInt)sizeCompressed;
    stream.next_in = dataCompressed;
    stream.avail_out = (uInt)sizeUncompressed;
    stream.next_out = dataUncompressed;

    CookfsLog(printf("call inflateInit2() ..."));
    res = inflateInit2(&stream, -MAX_WBITS);

    CookfsLog(printf("result: %s",
        res == Z_OK ? "Z_OK" :
        res == Z_MEM_ERROR ? "Z_MEM_ERROR" :
        res == Z_BUF_ERROR ? "Z_BUF_ERROR" :
        res == Z_DATA_ERROR ? "Z_DATA_ERROR" :
        "UNKNOWN"));

    if (res != Z_OK) {
        CookfsLog(printf("return: ERROR"));
        return TCL_ERROR;
    }

    CookfsLog(printf("call inflate() ..."));
    res = inflate(&stream, Z_FINISH);

    inflateEnd(&stream);

    if (res != Z_STREAM_END) {
        CookfsLog(printf("return: ERROR (not Z_STREAM_END)"));
        return TCL_ERROR;
    }

    CookfsLog(printf("got: Z_STREAM_END"));

    if (stream.total_out != (uInt)sizeUncompressed) {
        CookfsLog(printf("return: ERROR (uncompressed size doesn't match"
            " %u != %u)", (unsigned int)stream.total_out,
            (unsigned int)sizeUncompressed));
        return TCL_ERROR;
    }

#elif defined(USE_ZLIB_TCL86)
    /* use Tcl 8.6 API for decompression */
    Tcl_ZlibStream zshandle;

    CookfsLog(printf("initialize zlib handle"))
    res = Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_INFLATE,
        TCL_ZLIB_FORMAT_RAW, 9, NULL, &zshandle);
    if (res != TCL_OK) {
        CookfsLog(printf("Unable to initialize zlib"));
        return TCL_ERROR;
    }

    Tcl_Obj *sourceObj = Tcl_NewByteArrayObj(dataCompressed, sizeCompressed);
    Tcl_IncrRefCount(sourceObj);

    CookfsLog(printf("call Tcl_ZlibStreamPut() ..."));
    res = Tcl_ZlibStreamPut(zshandle, sourceObj, TCL_ZLIB_FINALIZE);
    Tcl_DecrRefCount(sourceObj);

    if (res != TCL_OK) {
        CookfsLog(printf("return: ERROR"));
        Tcl_ZlibStreamClose(zshandle);
        return TCL_ERROR;
    }

    Tcl_Obj *destObj = Tcl_NewObj();
    Tcl_IncrRefCount(destObj);

    CookfsLog(printf("reading from the handle..."));
    while (!Tcl_ZlibStreamEof(zshandle)) {
        if (Tcl_ZlibStreamGet(zshandle, destObj, -1) != TCL_OK) {
            Tcl_DecrRefCount(destObj);
            Tcl_ZlibStreamClose(zshandle);
            CookfsLog(printf("return: ERROR (while reading)"));
            return TCL_ERROR;
        }
    }

    Tcl_ZlibStreamClose(zshandle);

    Tcl_Size destObjSize;
    unsigned char *destStr = Tcl_GetByteArrayFromObj(destObj, &destObjSize);

    if (destObjSize != sizeUncompressed) {
        CookfsLog(printf("ERROR: result size doesn't match original size"));
        Tcl_DecrRefCount(destObj);
        return TCL_ERROR;
    }

    CookfsLog(printf("copy data to the output buffer"));
    memcpy(dataUncompressed, destStr, destObjSize);

    Tcl_DecrRefCount(destObj);

#else
#error Only Tcl8.6 with zlib is supported.
#endif /* USE_ZLIB_TCL86 */

    CookfsLog(printf("return: ok"));
    return TCL_OK;

}


Cookfs_PageObj CookfsWritePageZlib(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size origSize)
{

    CookfsLog(printf("want to compress %" TCL_SIZE_MODIFIER "d bytes",
        origSize));

    int res;

    int level = p->currentCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level >= 255 ) {
        level = 9;
    }

#if HAVE_ZLIB

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    stream.avail_in = (uInt)origSize;
    stream.next_in = bytes;

    CookfsLog(printf("call deflateInit2() level %d ...", level));
    res = deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL,
        Z_DEFAULT_STRATEGY);

    CookfsLog(printf("got: %s",
        res == Z_OK ? "Z_OK" :
        res == Z_MEM_ERROR ? "Z_MEM_ERROR" :
        res == Z_BUF_ERROR ? "Z_BUF_ERROR" :
        res ==  Z_STREAM_ERROR ? " Z_STREAM_ERROR" :
        "UNKNOWN"));

    if (res != Z_OK) {
        CookfsLog(printf("return: ERROR"));
        return NULL;
    }

    stream.avail_out = deflateBound(&stream, (uInt)origSize);
    CookfsLog(printf("want output buffer size: %u",
        (unsigned int)stream.avail_out));

    Cookfs_PageObj rc = Cookfs_PageObjAlloc(stream.avail_out);
    if (rc == NULL) {
        deflateEnd(&stream);
        CookfsLog(printf("ERROR: could not alloc output buffer"));
        return NULL;
    }

    stream.next_out = rc->buf;

    CookfsLog(printf("call deflate() ..."));
    res = deflate(&stream, Z_FINISH);

    if (res != Z_STREAM_END) {
        deflateEnd(&stream);
        CookfsLog(printf("ERROR: got not Z_STREAM_END"));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    } else {
        CookfsLog(printf("got: Z_STREAM_END"));
    }

    res = deflateEnd(&stream);

    CookfsLog(printf("got: %s",
        res == Z_OK ? "Z_OK" :
        res == Z_MEM_ERROR ? "Z_MEM_ERROR" :
        res == Z_BUF_ERROR ? "Z_BUF_ERROR" :
        res ==  Z_STREAM_ERROR ? " Z_STREAM_ERROR" :
        "UNKNOWN"));

    if (res != Z_OK) {
        CookfsLog(printf("ERROR: got not Z_OK"));
        Cookfs_PageObjBounceRefCount(rc);
        return NULL;
    }

    Cookfs_PageObjSetSize(rc, stream.total_out);

#elif defined(USE_ZLIB_TCL86)

    /* use Tcl 8.6 API for zlib compression */
    Tcl_ZlibStream zshandle;

    CookfsLog(printf("initialize zlib handle"))
    res = Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_DEFLATE,
        TCL_ZLIB_FORMAT_RAW, level, NULL, &zshandle);
    if (res != TCL_OK) {
        CookfsLog(printf("ERROR: Tcl_ZlibStreamInit failed"));
        return NULL;
    }

    Tcl_Obj *inputData = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(inputData);

    CookfsLog(printf("call Tcl_ZlibStreamPut() ..."));
    res = Tcl_ZlibStreamPut(zshandle, inputData, TCL_ZLIB_FINALIZE);
    Tcl_DecrRefCount(inputData);
    if (res != TCL_OK) {
        CookfsLog(printf("ERROR: failed"));
        Tcl_ZlibStreamClose(zshandle);
        return NULL;
    }

    //Tcl_Obj *outputObj = Tcl_NewObj();
    Tcl_Obj *outputObj = Tcl_NewByteArrayObj(NULL, 0) ;
    Tcl_IncrRefCount(outputObj);

    CookfsLog(printf("reading from the handle..."));
    res = Tcl_ZlibStreamGet(zshandle, outputObj, -1);
    Tcl_ZlibStreamClose(zshandle);
    if (res != TCL_OK) {
        CookfsLog(printf("return: ERROR (while reading)"));
        Tcl_DecrRefCount(outputObj);
        return NULL;
    }

    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(outputObj);
    Tcl_DecrRefCount(outputObj);

    if (rc == NULL) {
        CookfsLog(printf("return: ERROR (failed to alloc)"));
        return NULL;
    }

#else
#error Only Tcl8.6 with zlib is supported.
#endif /* USE_ZLIB_TCL86 */

    CookfsLog(printf("got encoded size: %" TCL_SIZE_MODIFIER "d",
        Cookfs_PageObjSize(rc)));

    return rc;

}
