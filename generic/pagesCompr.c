/*
 * pagesCompre.c
 *
 * Provides functions for pages compression
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"
#ifdef COOKFS_USEBZ2
#include "../bzip2/bzlib.h"
#endif

#define COOKFS_SUFFIX_BYTES 16

/* let's gain at least 16 bytes and/or 5% to compress it */
#define SHOULD_COMPRESS(origSize, size) ((size < (origSize - 16)) && ((size * 100) <= (origSize * 95)))

/* declarations of static and/or internal functions */
static Tcl_Obj **CookfsCreateCopressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr);
static void CookfsWriteCompression(Cookfs_Pages *p, int compression);
static Tcl_Obj *CookfsReadPageZlib(Cookfs_Pages *p, int size);
static int CookfsWritePageZlib(Cookfs_Pages *p, Tcl_Obj *data, int origSize);
static Tcl_Obj *CookfsReadPageBz2(Cookfs_Pages *p, int size);
static int CookfsWritePageBz2(Cookfs_Pages *p, Tcl_Obj *data, int origSize);
static Tcl_Obj *CookfsReadPageCustom(Cookfs_Pages *p, int size);
static int CookfsWritePageCustom(Cookfs_Pages *p, Tcl_Obj *data, int origSize);

/* compression data */
const char *cookfsCompressionOptions[] = {
    "none",
    "zlib",
#ifdef COOKFS_USEBZ2
    "bz2",
#endif
    "custom",
    NULL
};

const int cookfsCompressionOptionMap[] = {
    COOKFS_COMPRESSION_NONE,
    COOKFS_COMPRESSION_ZLIB,
#ifdef COOKFS_USEBZ2
    COOKFS_COMPRESSION_BZ2,
#endif
    COOKFS_COMPRESSION_CUSTOM,
    -1 /* dummy entry */
};


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesInitCompr --
 *
 *	Initializes pages compression handling for specified pages object
 *
 *	Invoked as part of Cookfs_PagesInit()
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesInitCompr(Cookfs_Pages *rc) {
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    /* initialize list for calling vfs::zip command for (de)compressing */
    rc->zipCmdCompress[0] = Tcl_NewStringObj("vfs::zip", -1);
    rc->zipCmdCompress[1] = Tcl_NewStringObj("-mode", -1);
    rc->zipCmdCompress[2] = Tcl_NewStringObj("compress", -1);
    rc->zipCmdCompress[3] = Tcl_NewStringObj("-nowrap", -1);
    rc->zipCmdCompress[4] = Tcl_NewIntObj(1);

    rc->zipCmdDecompress[0] = rc->zipCmdCompress[0];
    rc->zipCmdDecompress[1] = rc->zipCmdCompress[1];
    rc->zipCmdDecompress[2] = Tcl_NewStringObj("decompress", -1);
    rc->zipCmdDecompress[3] = rc->zipCmdCompress[3];
    rc->zipCmdDecompress[4] = rc->zipCmdCompress[4];

    Tcl_IncrRefCount(rc->zipCmdCompress[0]);
    Tcl_IncrRefCount(rc->zipCmdCompress[1]);
    Tcl_IncrRefCount(rc->zipCmdCompress[2]);
    Tcl_IncrRefCount(rc->zipCmdCompress[3]);
    Tcl_IncrRefCount(rc->zipCmdCompress[4]);

    Tcl_IncrRefCount(rc->zipCmdDecompress[2]);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesFiniCompr --
 *
 *	Cleans up pages compression handling for specified pages object
 *
 *	Invoked as part of Cookfs_PagesFini()
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesFiniCompr(Cookfs_Pages *rc) {
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    /* free up memory for invoking commands */
    Tcl_DecrRefCount(rc->zipCmdCompress[0]);
    Tcl_DecrRefCount(rc->zipCmdCompress[1]);
    Tcl_DecrRefCount(rc->zipCmdCompress[2]);
    Tcl_DecrRefCount(rc->zipCmdCompress[3]);
    Tcl_DecrRefCount(rc->zipCmdCompress[4]);

    Tcl_DecrRefCount(rc->zipCmdDecompress[2]);
#endif

    /* clean up compress/decompress commands, if present - for non-aside pages only */
    if (!rc->isAside) {
	if (rc->compressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->compressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    Tcl_Free((void *) rc->compressCommandPtr);
	}
	if (rc->decompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->decompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    Tcl_Free((void *) rc->decompressCommandPtr);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_SetCompressCommands --
 *
 *	Set compress and decompress commands for specified pages object
 *
 *	Creates a copy of objects' list elements 
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR otherwise
 *
 * Side effects:
 *	May set interpreter's result to error message, if error occurred
 *
 *----------------------------------------------------------------------
 */

int Cookfs_SetCompressCommands(Cookfs_Pages *p, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand) {
    Tcl_Obj **compressPtr = NULL;
    Tcl_Obj **decompressPtr = NULL;
    int compressLen = 0;
    int decompressLen = 0;

    /* copy compress command */
    if (compressCommand != NULL) {
	compressPtr = CookfsCreateCopressionCommand(NULL, compressCommand, &compressLen);
	if (compressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* copy decompress command */
    if (decompressCommand != NULL) {
	decompressPtr = CookfsCreateCopressionCommand(NULL, decompressCommand, &decompressLen);
	if (decompressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* store copied list and number of items in lists */
    p->compressCommandPtr = compressPtr;
    p->compressCommandLen = compressLen;
    p->decompressCommandPtr = decompressPtr;
    p->decompressCommandLen = decompressLen;
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_ReadPage --
 *
 *	Read page and invoke proper decompression function, if needed
 *
 * Results:
 *	Decompressed page as
 *	NOTE: Reference counter for the page is already incremented
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_ReadPage(Cookfs_Pages *p, int size) {
    int count;
    int compression = p->fileCompression;
    

    CookfsLog(printf("Cookfs_ReadPage S=%d C=%d", size, p->fileCompression))
    if (size == 0) {
	/* if page was empty, no need to read anything */
	Tcl_Obj *obj = Tcl_NewByteArrayObj((unsigned char *) "", 0);
	Tcl_IncrRefCount(obj);
        return obj;
    }  else  {
	/* read compression algorithm first */
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

	/* handle reading based on compression algorithm */
        switch (compression) {
            case COOKFS_COMPRESSION_NONE: {
		/* simply read raw data */
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
	    /* run proper reading functions */
            case COOKFS_COMPRESSION_ZLIB:
		return CookfsReadPageZlib(p, size);
            case COOKFS_COMPRESSION_CUSTOM:
		return CookfsReadPageCustom(p, size);
            case COOKFS_COMPRESSION_BZ2:
		return CookfsReadPageBz2(p, size);
        }
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_WritePage --
 *
 *	Compress and write page data
 *
 * Results:
 *	Size of page after compression
 *
 * Side effects:
 *	Data might be freed if its reference counter was originally 0
 *
 *----------------------------------------------------------------------
 */

int Cookfs_WritePage(Cookfs_Pages *p, Tcl_Obj *data) {
    unsigned char *bytes;
    int origSize;
    int size = -1;

    /* get binary data */
    bytes = Tcl_GetByteArrayFromObj(data, &origSize);
    Tcl_IncrRefCount(data);

    if (origSize > 0) {
	/* try to write compressed if compression was enabled */
        switch (p->fileCompression) {
            case COOKFS_COMPRESSION_ZLIB:
		size = CookfsWritePageZlib(p, data, origSize);
		break;

            case COOKFS_COMPRESSION_CUSTOM:
		size = CookfsWritePageCustom(p, data, origSize);
		break;

            case COOKFS_COMPRESSION_BZ2:
		size = CookfsWritePageBz2(p, data, origSize);
		break;
        }

	/* if compression was not enabled or compressor decided it should
	 * be written as raw data, write it uncompressed */
	if (size == -1) {
	    CookfsWriteCompression(p, COOKFS_COMPRESSION_NONE);
	    Tcl_WriteObj(p->fileChannel, data);
	    size = origSize + 1;
	}  else  {
	    /* otherwise add 1 byte for compression */
	    size += 1;
	}
    }  else  {
	size = 0;
    }
    Tcl_DecrRefCount(data);
    return size;
}

/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsWriteCompression --
 *
 *	Write compression id to file as 1 byte
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsWriteCompression(Cookfs_Pages *p, int compression) {
    Tcl_Obj *byteObj;
    unsigned char byte[4];
    byte[0] = (unsigned char) compression;
    byteObj = Tcl_NewByteArrayObj(byte, 1);
    Tcl_IncrRefCount(byteObj);
    Tcl_WriteObj(p->fileChannel, byteObj);
    Tcl_DecrRefCount(byteObj);
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsCreateCopressionCommand --
 *
 *	Copy specified command and store length of entire command
 *
 * Results:
 * 	Pointer to array of Tcl_Obj for the command; placeholder for
 * 	actual data to (de)compress is added as last element of the list
 *
 * Side effects:
 *	In case of error, interp will be set with proper error message
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj **CookfsCreateCopressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr) {
    Tcl_Obj **rc;
    Tcl_Obj **listObjv;
    int listObjc;
    int i;

    /* get command as list of attributes */
    if (Tcl_ListObjGetElements(interp, cmd, &listObjc, &listObjv) != TCL_OK) {
	return NULL;
    }

    rc = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * (listObjc + 1));
    for (i = 0; i < listObjc; i++) {
	rc[i] = listObjv[i];
	Tcl_IncrRefCount(rc[i]);
    }
    rc[listObjc] = NULL;
    *lenPtr = listObjc + 1;
    return rc;
}


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

static int CookfsWritePageZlib(Cookfs_Pages *p, Tcl_Obj *data, int origSize) {
#ifdef USE_ZLIB_TCL86
    int size = origSize;
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

    if (SHOULD_COMPRESS(origSize, size)) {
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

    if (SHOULD_COMPRESS(origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_ZLIB);
	Tcl_WriteObj(p->fileChannel, compressed);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(compressed);
#endif
    return size;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsReadPageBz2 --
 *
 *	Read bzip2 compressed page
 *
 * Results:
 *	Binary data as Tcl_Obj
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

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


/*
 *----------------------------------------------------------------------
 *
 * CookfsWritePageBz2 --
 *
 *	Write page using bzip2 compression
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

static int CookfsWritePageBz2(Cookfs_Pages *p, Tcl_Obj *data, int origSize) {
#ifdef COOKFS_USEBZ2
    int size;
    unsigned char *source;
    unsigned char *dest;
    Tcl_Obj *destObj;

    source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
	CookfsLog(printf("Cookfs_WritePage: Tcl_GetByteArrayFromObj failed"))
	return -1;
    }

    destObj = Tcl_NewByteArrayObj(NULL, 0);
    size = origSize * 2 + 1024;
    Tcl_SetByteArrayLength(destObj, size + 4);
    dest = Tcl_GetByteArrayFromObj(destObj, NULL);

    Cookfs_Int2Binary(&origSize, (unsigned char *) dest, 1);
    if (BZ2_bzBuffToBuffCompress((char *) (dest + 4), (unsigned int *) &size, (char *) source, (unsigned int) origSize, 5, 0, 0) != BZ_OK) {
	CookfsLog(printf("Cookfs_WritePage: BZ2_bzBuffToBuffCompress failed"))
	return -1;
    }

    CookfsLog(printf("Cookfs_WritePage: size=%d (to %d)", origSize, size))
    size += 4;
    Tcl_SetByteArrayLength(destObj, size);

    Tcl_IncrRefCount(destObj);
    if (SHOULD_COMPRESS(origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_BZ2);
	Tcl_WriteObj(p->fileChannel, destObj);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(destObj);

    return size;
#else
    return -1;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsReadPageCustom --
 *
 *	Read page stored using custom compression
 *
 * Results:
 *	Binary data as Tcl_Obj
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *CookfsReadPageCustom(Cookfs_Pages *p, int size) {
    /* use vfs::zip command for decompression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;
    Tcl_Obj *data;
    int count;

    if (p->decompressCommandPtr == NULL) {
	return NULL;
    }

    compressed = Tcl_NewObj();
    Tcl_IncrRefCount(compressed);
    count = Tcl_ReadChars(p->fileChannel, compressed, size, 0);

    CookfsLog(printf("Reading - %d vs %d", count, size))
    if (count != size) {
	CookfsLog(printf("Unable to read - %d != %d", count, size))
	Tcl_DecrRefCount(compressed);
	return NULL;
    }
    p->decompressCommandPtr[p->decompressCommandLen - 1] = compressed;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->decompressCommandLen, p->decompressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	p->decompressCommandPtr[p->decompressCommandLen - 1] = NULL;
	CookfsLog(printf("Unable to decompress"))
	Tcl_DecrRefCount(compressed);
        Tcl_SetObjResult(p->interp, prevResult);
        Tcl_DecrRefCount(prevResult);
	return NULL;
    }
    p->decompressCommandPtr[p->decompressCommandLen - 1] = NULL;
    Tcl_DecrRefCount(compressed);
    data = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(data);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsWritePageCustom --
 *
 *	Write page using custom compression
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

static int CookfsWritePageCustom(Cookfs_Pages *p, Tcl_Obj *data, int origSize) {
    int size = origSize;
    /* use vfs::zip command for compression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;

    if (p->compressCommandPtr == NULL) {
	return -1;
    }

    Tcl_IncrRefCount(data);
    p->compressCommandPtr[p->compressCommandLen - 1] = data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->compressCommandLen, p->compressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	p->compressCommandPtr[p->compressCommandLen - 1] = NULL;
	Tcl_SetObjResult(p->interp, prevResult);
	CookfsLog(printf("Unable to compress"))
	Tcl_DecrRefCount(data);
	return -1;
    }
    p->compressCommandPtr[p->compressCommandLen - 1] = NULL;
    Tcl_DecrRefCount(data);
    compressed = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(compressed);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    Tcl_GetByteArrayFromObj(compressed, &size);

    if (SHOULD_COMPRESS(origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_CUSTOM);
	Tcl_WriteObj(p->fileChannel, compressed);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(compressed);
    return size;
}
