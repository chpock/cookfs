/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#include "cookfs.h"
#ifdef COOKFS_USEBZ2
#include "../bzip2/bzlib.h"
#endif

#define COOKFS_SUFFIX_BYTES 16

static Tcl_Obj *CookfsReadPageZlib(Cookfs_Pages *p, int size);
static int CookfsWritePageZlib(Cookfs_Pages *p, Tcl_Obj *data, int size);

static Tcl_Obj *CookfsReadPageBz2(Cookfs_Pages *p, int size);
static int CookfsWritePageBz2(Cookfs_Pages *p, Tcl_Obj *data, int size);

Tcl_Obj *Cookfs_ReadPage(Cookfs_Pages *p, int size) {
    int count;
    int compression = p->fileCompression;

    CookfsLog(printf("Cookfs_ReadPage S=%d C=%d", size, p->fileCompression))
    if (size == 0) {
        return Tcl_NewByteArrayObj((unsigned char *) "", 0);
    }  else  {
	Tcl_Obj *byteObj;
	byteObj = Tcl_NewObj();
	if (Tcl_ReadChars(p->fileChannel, byteObj, 1, 0) != 1) {
	    CookfsLog(printf("Unable to read compression mark"))
	    Tcl_IncrRefCount(byteObj);
	    Tcl_DecrRefCount(byteObj);
	    return NULL;
	}
	compression = Tcl_GetByteArrayFromObj(byteObj, NULL)[0];
	Tcl_IncrRefCount(byteObj);
	Tcl_DecrRefCount(byteObj);

	/* need to decrease size by 1 byte we just read */
	size = size - 1;

        switch (compression) {
            case cookfsCompressionNone: {
                Tcl_Obj *data;
                
                data = Tcl_NewObj();
                count = Tcl_ReadChars(p->fileChannel, data, size, 0);
                if (count != size) {
                    CookfsLog(printf("Unable to read - %d != %d", count, size))
                    Tcl_IncrRefCount(data);
                    Tcl_DecrRefCount(data);
                    return NULL;
                }

                Tcl_IncrRefCount(data);
                return data;
            }
            case cookfsCompressionZlib:
		return CookfsReadPageZlib(p, size);

            case cookfsCompressionBz2:
		return CookfsReadPageBz2(p, size);
        }
    }
    return NULL;
}

int Cookfs_WritePage(Cookfs_Pages *p, Tcl_Obj *data) {
    unsigned char *bytes;
    Tcl_Obj *byteObj;
    int size;

    bytes = Tcl_GetByteArrayFromObj(data, &size);
    Tcl_IncrRefCount(data);
    if (size > 0) {
	/* write compression algorithm */
	unsigned char byte[4];
	byte[0] = (unsigned char) p->fileCompression;
	byteObj = Tcl_NewByteArrayObj(byte, 1);
	Tcl_IncrRefCount(byteObj);
	Tcl_WriteObj(p->fileChannel, byteObj);
	Tcl_DecrRefCount(byteObj);

        switch (p->fileCompression) {
            case cookfsCompressionNone:
                CookfsLog(printf("Cookfs_WritePage writing %d byte(s) as raw data", size))
                Tcl_WriteObj(p->fileChannel, data);
		break;

            case cookfsCompressionZlib:
		size = CookfsWritePageZlib(p, data, size);
		break;

            case cookfsCompressionBz2:
		size = CookfsWritePageBz2(p, data, size);
		break;
        }

	if (size != -1) {
	    /* add 1 byte needed to store compression algorithm */
	    size += 1;
	}
    }
    Tcl_DecrRefCount(data);
    return size;
}

static Tcl_Obj *CookfsReadPageZlib(Cookfs_Pages *p, int size) {
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
    while (!Tcl_ZlibStreamEof(zshandle)) {
	if (Tcl_ZlibStreamGet(zshandle, cobj, -1) != TCL_OK) {
	    Tcl_IncrRefCount(cobj);
	    Tcl_DecrRefCount(cobj);
	    Tcl_ZlibStreamClose(zshandle);
	    CookfsLog(printf("Unable to decompress - reading"))
	    return NULL;
	}
    }
    Tcl_IncrRefCount(cobj);
    CookfsLog(printf("Returning = [%s]", cobj == NULL ? "NULL" : "SET"))
    Tcl_ZlibStreamClose(zshandle);
    return cobj;
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
    p->zipCmdDecompress[5] = compressed;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, 6, p->zipCmdDecompress, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	CookfsLog(printf("Unable to decompress"))
	printf("TEST0x - %s\n", Tcl_GetStringFromObj(Tcl_GetObjResult(p->interp), NULL));
	Tcl_DecrRefCount(compressed);
	return NULL;
    }
    Tcl_DecrRefCount(compressed);
    data = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(data);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    return data;
#endif
}

static int CookfsWritePageZlib(Cookfs_Pages *p, Tcl_Obj *data, int size) {
#ifdef USE_ZLIB_TCL86
    /* use Tcl 8.6 API for zlib compression */
    Tcl_Obj *cobj;
    Tcl_ZlibStream zshandle;

    if (Tcl_ZlibStreamInit(NULL, TCL_ZLIB_STREAM_DEFLATE, TCL_ZLIB_FORMAT_RAW, 9, NULL, &zshandle) != TCL_OK) {
	CookfsLog(printf("Cookfs_WritePage: Tcl_ZlibStreamInit failed!"))
	return -1;
    }

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

    Tcl_WriteObj(p->fileChannel, cobj);
    Tcl_DecrRefCount(cobj);
#else
    /* use vfs::zip command for compression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;

    Tcl_IncrRefCount(data);
    p->zipCmdCompress[5] = data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, 6, p->zipCmdCompress, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	Tcl_SetObjResult(p->interp, prevResult);
	CookfsLog(printf("Unable to compress"))
	Tcl_DecrRefCount(data);
	return -1;
    }
    Tcl_DecrRefCount(data);
    compressed = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(compressed);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    Tcl_GetByteArrayFromObj(compressed, &size);
    Tcl_WriteObj(p->fileChannel, compressed);
    Tcl_DecrRefCount(compressed);
#endif
    return size;
}

static Tcl_Obj *CookfsReadPageBz2(Cookfs_Pages *p, int size) {
#ifdef COOKFS_USEBZ2
    int destSize;
    int count;
    unsigned char *source;
    unsigned char *dest;
    Tcl_Obj *destObj;
    Tcl_Obj *data;

    data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    if (count != size) {
	Tcl_DecrRefCount(data);
	return NULL;
    }

    source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
	CookfsLog(printf("Cookfs_ReadPage: Tcl_GetByteArrayFromObj failed"))
	return NULL;
    }

    destObj = Tcl_NewByteArrayObj(NULL, 0);
    Tcl_IncrRefCount(destObj);
    Cookfs_Binary2Int(source, &destSize, 1);

    CookfsLog(printf("Cookfs_ReadPage: uncompressed size=%d from %d", destSize, size))
    Tcl_SetByteArrayLength(destObj, destSize);
    dest = Tcl_GetByteArrayFromObj(destObj, NULL);

    if (BZ2_bzBuffToBuffDecompress((char *) dest, (unsigned int *) &destSize, (char *) source + 4, (unsigned int) size - 4, 0, 0) != BZ_OK) {
	Tcl_DecrRefCount(data);
	Tcl_IncrRefCount(destObj);
	Tcl_DecrRefCount(destObj);
	CookfsLog(printf("Cookfs_ReadPage: BZ2_bzBuffToBuffDecompress failed"))
	return NULL;
    }

    Tcl_DecrRefCount(data);

    return destObj;
#else
    return NULL;
#endif
}

static int CookfsWritePageBz2(Cookfs_Pages *p, Tcl_Obj *data, int size) {
#ifdef COOKFS_USEBZ2
    int sourceSize;
    int destSize;
    unsigned char *source;
    unsigned char *dest;
    Tcl_Obj *destObj;

    source = Tcl_GetByteArrayFromObj(data, &sourceSize);
    if (source == NULL) {
	CookfsLog(printf("Cookfs_WritePage: Tcl_GetByteArrayFromObj failed"))
	return -1;
    }

    destObj = Tcl_NewByteArrayObj(NULL, 0);
    destSize = sourceSize * 2 + 1024;
    Tcl_SetByteArrayLength(destObj, destSize + 4);
    dest = Tcl_GetByteArrayFromObj(destObj, NULL);

    Cookfs_Int2Binary(&sourceSize, (unsigned char *) dest, 1);
    if (BZ2_bzBuffToBuffCompress((char *) (dest + 4), (unsigned int *) &destSize, (char *) source, (unsigned int) sourceSize, 5, 0, 0) != BZ_OK) {
	CookfsLog(printf("Cookfs_WritePage: BZ2_bzBuffToBuffCompress failed"))
	return -1;
    }

    CookfsLog(printf("Cookfs_WritePage: size=%d (to %d)", sourceSize, destSize))
    Tcl_SetByteArrayLength(destObj, destSize + 4);

    Tcl_IncrRefCount(destObj);
    Tcl_WriteObj(p->fileChannel, destObj);
    Tcl_DecrRefCount(destObj);

    return destSize + 4;
#else
    return -1;
#endif
}


