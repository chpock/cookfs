/*
 * pagesCompr.c
 *
 * Provides functions for pages compression
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
#ifdef COOKFS_USEBZ2
#include "pagesComprBz2.h"
#endif
#ifdef COOKFS_USELZMA
#include "pagesComprLzma.h"
#endif
#ifdef COOKFS_USEZSTD
#include "pagesComprZstd.h"
#endif

/* declarations of static and/or internal functions */
static Tcl_Obj **CookfsCreateCompressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr, int additionalElements);
static Cookfs_PageObj CookfsReadPageCustom(Cookfs_Pages *p, int size, Tcl_Obj **err);
static int CookfsWritePageCustom(Cookfs_Pages *p, unsigned char *bytes, int origSize);
#ifdef USE_VFS_COMMANDS_FOR_ZIP
static int CookfsCheckCommandExists(Tcl_Interp *interp, const char *commandName);
#endif
static Tcl_Obj *CookfsRunAsyncCompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);
static Tcl_Obj *CookfsRunAsyncDecompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);

/* compression data */
const char *cookfsCompressionOptions[] = {
    "none",
    "zlib",
#ifdef COOKFS_USEBZ2
    "bz2",
#endif
#ifdef COOKFS_USELZMA
    "lzma",
#endif
#ifdef COOKFS_USEZSTD
    "zstd",
#endif
    "custom",
    NULL
};

/* names for all defined compressions */
const char *cookfsCompressionNames[15] = {
    "none",  /*  0 */
    "zlib",  /*  1 */
    "bz2",   /*  2 */
    "lzma",  /*  3 */
    "zstd",  /*  4 */
    "", "", "", "", "", "", "", "", "", /* 5 - 13 */
    "custom" /* 14 */
};

const unsigned char cookfsCompressionLevels[15] = {
    0,  /* none   -  0 */
    6,  /* zlib   -  1 */
    9,  /* bz2    -  2 */
    5,  /* lzma   -  3 */
    3,  /* zstd   -  4 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, /* 5 - 13 */
    0   /* custom - 14 */
};

const int cookfsCompressionOptionMap[] = {
    COOKFS_COMPRESSION_NONE,
    COOKFS_COMPRESSION_ZLIB,
#ifdef COOKFS_USEBZ2
    COOKFS_COMPRESSION_BZ2,
#endif
#ifdef COOKFS_USELZMA
    COOKFS_COMPRESSION_LZMA,
#endif
#ifdef COOKFS_USEZSTD
    COOKFS_COMPRESSION_ZSTD,
#endif
    COOKFS_COMPRESSION_CUSTOM,
    -1 /* dummy entry */
};

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_CompressionFromObj --
 *
 *      Returns an integer compression ID based on string in the corresponding
 *      Tcl object. If an error occurs, returns TCL_ERROR and an appropriate
 *      error message if the interp is not NULL.
 *
 * Results:
 *      TCL_OK if compression ID was successfully converted from Tcl_Obj.
 *
 * Side effects:
 *      If NULL is specified as the Tcl object, the default compression is
 *      returned.
 *
 *----------------------------------------------------------------------
 */

int Cookfs_CompressionFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    int *compressionPtr, int *compressionLevelPtr)
{
    CookfsLog(printf("Cookfs_CompressionFromObj: from [%s]",
        obj == NULL ? "<NULL>" : Tcl_GetString(obj)));

#ifdef COOKFS_USELZMA
    int compression = COOKFS_COMPRESSION_LZMA;
#else
    int compression = COOKFS_COMPRESSION_ZLIB;
#endif
    int compressionLevel = -1;

    if (obj != NULL) {

        Tcl_Obj *method;
        Tcl_Obj *level = NULL;

        Tcl_Size len;
        char *str = Tcl_GetStringFromObj(obj, &len);
        char *colonPos = strrchr(str, ':');
        if (colonPos == NULL) {
            // If no colon is found, assume obj is a compression method
            method = Tcl_DuplicateObj(obj);
            CookfsLog(printf("Cookfs_CompressionFromObj: only method is"
                " specified"));
        } else if (colonPos[1] == '\0') {
            // If colos is in the last position, create a new object
            // and delete the last character
            method = Tcl_NewStringObj(str, len - 1);
            CookfsLog(printf("Cookfs_CompressionFromObj: method and an empty"
                " level is specified"));
        } else {
            // Split obj to method obj (before the colon) and level obj
            // (after the colon)
            method = Tcl_NewStringObj(str, colonPos - str);
            level = Tcl_NewStringObj(++colonPos, -1);
            Tcl_IncrRefCount(level);
            CookfsLog(printf("Cookfs_CompressionFromObj: method [%s] and"
                " level [%s] are specified", Tcl_GetString(method),
                Tcl_GetString(level)));
        }
        Tcl_IncrRefCount(method);

        if (Tcl_GetIndexFromObj(interp, method,
            (const char **)cookfsCompressionOptions, "compression", 0,
            &compression) != TCL_OK)
        {
            CookfsLog(printf("Cookfs_CompressionFromObj: failed to detect"
                " compression method"));
            goto error;
        }
        /* map compression from cookfsCompressionOptionMap */
        compression = cookfsCompressionOptionMap[compression];
        CookfsLog(printf("Cookfs_CompressionFromObj: detected"
            " compression: %d", compression));

        if (level == NULL) {
            goto nolevel;
        }

        if (Tcl_GetIntFromObj(interp, level, &compressionLevel) != TCL_OK) {
            goto error;
        }
        if (compressionLevel < -1 || compressionLevel > 255) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("the compression level"
                " is expected to be an integer between -1 and 255,"
                " but got \"%d\"", compressionLevel));
            goto error;
        }
        Tcl_DecrRefCount(level);

nolevel:
        Tcl_DecrRefCount(method);
        goto done;

error:
        Tcl_DecrRefCount(method);
        if (level != NULL) {
            Tcl_DecrRefCount(level);
        }
        return TCL_ERROR;
    }

done:
    *compressionPtr = compression;
    if (compressionLevel == -1) {
        compressionLevel = cookfsCompressionLevels[compression];
    }
    *compressionLevelPtr = compressionLevel;
    CookfsLog(printf("Cookfs_CompressionFromObj: return method [%d]"
        " level [%d]", compression, compressionLevel));
    return TCL_OK;
}

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
    if (CookfsCheckCommandExists(rc->interp, "::zlib")) {
        Tcl_Obj *zlibString;
        zlibString = Tcl_NewStringObj("::zlib", -1);
        Tcl_IncrRefCount(zlibString);
        /* use built-in zlib command for (de)compressing */
        rc->zipCmdCompress[0] = zlibString;
        rc->zipCmdCompress[1] = Tcl_NewStringObj("deflate", -1);
        rc->zipCmdDecompress[0] = zlibString;
        rc->zipCmdDecompress[1] = Tcl_NewStringObj("inflate", -1);

        Tcl_IncrRefCount(rc->zipCmdCompress[1]);
        Tcl_IncrRefCount(rc->zipCmdDecompress[1]);

        rc->zipCmdOffset = 2;
        rc->zipCmdLength = 3;
    }  else  {
        /* initialize list for calling vfs::zip command for (de)compressing */
        rc->zipCmdCompress[0] = Tcl_NewStringObj("::vfs::zip", -1);
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

        rc->zipCmdOffset = 5;
        rc->zipCmdLength = 6;
    }
#endif
#if !defined(USE_VFS_COMMANDS_FOR_ZIP)
    UNUSED(rc);
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
    if (rc->zipCmdOffset == 2) {
        Tcl_DecrRefCount(rc->zipCmdCompress[0]);
        Tcl_DecrRefCount(rc->zipCmdCompress[1]);
        Tcl_DecrRefCount(rc->zipCmdDecompress[1]);
    }  else  {
        Tcl_DecrRefCount(rc->zipCmdCompress[0]);
        Tcl_DecrRefCount(rc->zipCmdCompress[1]);
        Tcl_DecrRefCount(rc->zipCmdCompress[2]);
        Tcl_DecrRefCount(rc->zipCmdCompress[3]);
        Tcl_DecrRefCount(rc->zipCmdCompress[4]);

        Tcl_DecrRefCount(rc->zipCmdDecompress[2]);
    }
#endif

    /* clean up compress/decompress commands, if present - for non-aside pages only */
    if (!rc->isAside) {
	if (rc->compressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->compressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    ckfree((void *) rc->compressCommandPtr);
	}
	if (rc->decompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->decompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    ckfree((void *) rc->decompressCommandPtr);
	}
	if (rc->asyncCompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->asyncCompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    ckfree((void *) rc->asyncCompressCommandPtr);
	}
	if (rc->asyncDecompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->asyncDecompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    ckfree((void *) rc->asyncDecompressCommandPtr);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_SetCompressCommands --
 *
 *	Set compress, decompress and async compress commands
 *	for specified pages object
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

int Cookfs_SetCompressCommands(Cookfs_Pages *p, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand, Tcl_Obj *asyncCompressCommand, Tcl_Obj *asyncDecompressCommand) {
    Tcl_Obj **compressPtr = NULL;
    Tcl_Obj **decompressPtr = NULL;
    Tcl_Obj **asyncCompressPtr = NULL;
    Tcl_Obj **asyncDecompressPtr = NULL;
    int compressLen = 0;
    int decompressLen = 0;
    int asyncCompressLen = 0;
    int asyncDecompressLen = 0;

    /* copy compress command */
    if (compressCommand != NULL) {
	compressPtr = CookfsCreateCompressionCommand(NULL, compressCommand, &compressLen, 1);
	if (compressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* copy decompress command */
    if (decompressCommand != NULL) {
	decompressPtr = CookfsCreateCompressionCommand(NULL, decompressCommand, &decompressLen, 1);
	if (decompressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* copy async compress command */
    if (asyncCompressCommand != NULL) {
	asyncCompressPtr = CookfsCreateCompressionCommand(NULL, asyncCompressCommand, &asyncCompressLen, 3);
	if (asyncCompressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* copy async decompress command */
    if (asyncDecompressCommand != NULL) {
	asyncDecompressPtr = CookfsCreateCompressionCommand(NULL, asyncDecompressCommand, &asyncDecompressLen, 3);
	if (asyncDecompressPtr == NULL) {
	    return TCL_ERROR;
	}
    }

    /* store copied list and number of items in lists */
    p->compressCommandPtr = compressPtr;
    p->compressCommandLen = compressLen;
    p->decompressCommandPtr = decompressPtr;
    p->decompressCommandLen = decompressLen;
    p->asyncCompressCommandPtr = asyncCompressPtr;
    p->asyncCompressCommandLen = asyncCompressLen;
    p->asyncDecompressCommandPtr = asyncDecompressPtr;
    p->asyncDecompressCommandLen = asyncDecompressLen;

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_ReadPage --
 *
 *	Read page and invoke proper decompression function, if requested
 *
 * Results:
 *	Page; decompressed if decompress was specified
 *	NOTE: Reference counter for the page is already incremented
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_ReadPage(Cookfs_Pages *p, int idx, int size, int decompress, int compressionType, Tcl_Obj **err) {

    if (idx >= p->dataNumPages) {
	return NULL;
    }

    p->fileLastOp = COOKFS_LASTOP_READ;

    CookfsLog(printf("Cookfs_ReadPage I=%d S=%d C=%d", idx, size, p->fileCompression))
    if (size == 0) {
	/* if page was empty, no need to read anything */
	return Cookfs_PageObjAlloc(0);
    }  else  {
	/* read compression algorithm first */
	Tcl_Obj *byteObj;
	byteObj = Tcl_NewObj();
	if (idx >= 0) {
	    Tcl_WideInt offset = Cookfs_PagesGetPageOffset(p, idx);
	    Tcl_Seek(p->fileChannel, offset, SEEK_SET);
	    if (size == -1) {
		size = p->dataPagesSize[idx];
	    }
	}
	if (Tcl_ReadChars(p->fileChannel, byteObj, 1, 0) != 1) {
	    CookfsLog(printf("Unable to read compression mark"))
	    Tcl_IncrRefCount(byteObj);
	    Tcl_DecrRefCount(byteObj);
	    return NULL;
	}
	int compression = Tcl_GetByteArrayFromObj(byteObj, NULL)[0];
	Tcl_IncrRefCount(byteObj);
	Tcl_DecrRefCount(byteObj);

	/* need to decrease size by 1 byte we just read */
	size = size - 1;

	/* if specific compression was required, exit if did not match */
	if ((compressionType != COOKFS_COMPRESSION_ANY) && (compressionType != compression)) {
	    return NULL;
	}

	if (!decompress) {
	    compression = COOKFS_COMPRESSION_NONE;
	}

	CookfsLog(printf("Cookfs_ReadPage I=%d S=%d C=%d", idx, size, compression))

	/* handle reading based on compression algorithm */
        switch (compression) {
            case COOKFS_COMPRESSION_NONE: {
		/* simply read raw data */
                Cookfs_PageObj data = Cookfs_PageObjAlloc(size);
                int count = Tcl_Read(p->fileChannel, (char *)data, size);
                if (count != size) {
                    CookfsLog(printf("Unable to read - %d != %d", count, size))
                    Cookfs_PageObjIncrRefCount(data);
                    Cookfs_PageObjDecrRefCount(data);
                    return NULL;
                }
		if (!decompress) {
		    CookfsLog(printf("Cookfs_ReadPage retrieved chunk %d", idx));
		}
                return data;
            }
	    /* run proper reading functions */
            case COOKFS_COMPRESSION_ZLIB:
		return CookfsReadPageZlib(p, size, err);
            case COOKFS_COMPRESSION_CUSTOM:
		return CookfsReadPageCustom(p, size, err);
            case COOKFS_COMPRESSION_BZ2:
#ifdef COOKFS_USEBZ2
		return CookfsReadPageBz2(p, size, err);
#else
		return NULL;
#endif /* COOKFS_USEBZ2 */
            case COOKFS_COMPRESSION_LZMA:
#ifdef COOKFS_USELZMA
		return CookfsReadPageLzma(p, size, err);
#else
		return NULL;
#endif /* COOKFS_USELZMA */
            case COOKFS_COMPRESSION_ZSTD:
#ifdef COOKFS_USEZSTD
		return CookfsReadPageZstd(p, size, err);
#else
		return NULL;
#endif /* COOKFS_USEZSTD */
        }
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_SeekToPage --
 *
 *	Seek to offset where specified page is / should be
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_SeekToPage(Cookfs_Pages *p, int idx) {
    Tcl_WideInt offset = Cookfs_PagesGetPageOffset(p, idx);
    Tcl_Seek(p->fileChannel, offset, SEEK_SET);
    CookfsLog(printf("Seeking to EOF -> %d",(int) offset))
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_WritePage --
 *
 *	Optionally compress and write page data
 *
 *	If compressedData is specified, the page is written as
 *	compressed or uncompressed, depending on size
 *
 * Results:
 *	Size of page after compression
 *
 * Side effects:
 *	Data might be freed if its reference counter was originally 0
 *
 *----------------------------------------------------------------------
 */

int Cookfs_WritePage(Cookfs_Pages *p, int idx, unsigned char *bytes, int origSize, Tcl_Obj *compressedData) {
    Tcl_Size size = -1;

    // Add initial stamp if needed
    Cookfs_PageAddStamp(p, 0);

    /* if last operation was not write, we need to seek
     * to make sure we're at location where we should be writing */
    if ((idx >= 0) && (p->fileLastOp != COOKFS_LASTOP_WRITE)) {
	p->fileLastOp = COOKFS_LASTOP_WRITE;
	Cookfs_SeekToPage(p, idx);
    }

    if (origSize > 0) {
	if (compressedData != NULL) {
	    Tcl_GetByteArrayFromObj(compressedData, &size);

	    if (SHOULD_COMPRESS(p, origSize, size)) {
		CookfsWriteCompression(p, p->fileCompression);
		Tcl_WriteObj(p->fileChannel, compressedData);
		size += 1;
	    }  else  {
		CookfsWriteCompression(p, COOKFS_COMPRESSION_NONE);
		Tcl_Write(p->fileChannel, (const char *)bytes, origSize);
		size = origSize + 1;
	    }
	}  else  {
	    /* try to write compressed if compression was enabled */
	    switch (p->fileCompression) {
		case COOKFS_COMPRESSION_ZLIB:
		    size = CookfsWritePageZlib(p, bytes, origSize);
		    break;
		case COOKFS_COMPRESSION_CUSTOM:
		    size = CookfsWritePageCustom(p, bytes, origSize);
		    break;
		case COOKFS_COMPRESSION_BZ2:
#ifdef COOKFS_USEBZ2
		    size = CookfsWritePageBz2(p, bytes, origSize);
#else
		    size = -1;
#endif /* COOKFS_USEBZ2 */
		    break;
		case COOKFS_COMPRESSION_LZMA:
#ifdef COOKFS_USELZMA
		    size = CookfsWritePageLzma(p, bytes, origSize);
#else
		    size = -1;
#endif /* COOKFS_USELZMA */
		    break;
		case COOKFS_COMPRESSION_ZSTD:
#ifdef COOKFS_USEZSTD
		    size = CookfsWritePageZstd(p, bytes, origSize);
#else
		    size = -1;
#endif /* COOKFS_USEZSTD */
		    break;
	    }

	    /* if compression was not enabled or compressor decided it should
	     * be written as raw data, write it uncompressed */
	    if (size == -1) {
		CookfsWriteCompression(p, COOKFS_COMPRESSION_NONE);
		Tcl_Write(p->fileChannel, (const char *)bytes, origSize);
		size = origSize + 1;
	    }  else  {
		/* otherwise add 1 byte for compression */
		size += 1;
	    }
	}
    }  else  {
	size = 0;
    }
    return size;
}


int Cookfs_WritePageObj(Cookfs_Pages *p, int idx, Cookfs_PageObj data, Tcl_Obj *compressedData) {
    CookfsLog(printf("Cookfs_WritePageObj: data: %p", (void *)data));
    return Cookfs_WritePage(p, idx, data, Cookfs_PageObjSize(data), compressedData);
}

int Cookfs_WriteTclObj(Cookfs_Pages *p, int idx, Tcl_Obj *data, Tcl_Obj *compressedData) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(data, &size);
    return Cookfs_WritePage(p, idx, bytes, size, compressedData);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_AsyncPageGet --
 *
 *	Check if page is currently processed as async compression page
 *	or is pending async decompression and return it if it is.
 *
 * Results:
 *	Page contents if found; NULL if not found;
 *	The page contents' ref counter is increased before returning
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_AsyncPageGet(Cookfs_Pages *p, int idx) {
    if ((p->fileCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	int i;
	for (i = 0 ; i < p->asyncPageSize ; i++) {
	    if (p->asyncPage[i].pageIdx == idx) {
		return Cookfs_PageObjNewFromByteArray(p->asyncPage[i].pageContents);
	    }
	}
    }
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	int i;
	for (i = 0 ; i < p->asyncDecompressQueue ; i++) {
	    if (p->asyncDecompressIdx[i] == idx) {
                while (p->asyncDecompressIdx[i] == idx) {
                    Cookfs_AsyncDecompressWait(p, idx, 1);
                }
                /* don't modify here cache entry weight, it will be set by Cookfs_PageGet */
                return Cookfs_PageCacheGet(p, idx, 0, 0);
	    }
	}
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_AsyncPageAdd --
 *
 *	Add page to be asynchronously processed if enabled.
 *
 * Results:
 *	Whether async compression is enabled or not
 *
 * Side effects:
 *	Checks if other pages have been processed and may remove
 *	other pages from the processing queue
 *
 *----------------------------------------------------------------------
 */

int Cookfs_AsyncPageAdd(Cookfs_Pages *p, int idx, unsigned char *bytes, int dataSize) {
    if ((p->fileCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	Tcl_Obj *newObjObj;
	int asyncIdx;
	// retrieve any already processed queues
	while (Cookfs_AsyncCompressWait(p, 0)) {}
	while (p->asyncPageSize >= COOKFS_PAGES_MAX_ASYNC) {
	    Cookfs_AsyncCompressWait(p, 0);
	}
	asyncIdx = p->asyncPageSize++;
	newObjObj = Tcl_NewByteArrayObj(bytes, dataSize);
	// copy the object to avoid increased memory usage
	Tcl_IncrRefCount(newObjObj);
	p->asyncPage[asyncIdx].pageIdx = idx;
	p->asyncPage[asyncIdx].pageContents = newObjObj;
	CookfsRunAsyncCompressCommand(p, p->asyncCommandProcess, idx, newObjObj);
	return 1;
    } else {
	return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_AsyncCompressWait --
 *
 *	Wait / check whether an asynchronous page has already been
 *	processed; if require is set, this indicates cookfs is
 *	finalizing and data is required
 *
 * Results:
 *	Whether further clal to this API is needed or not
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_AsyncCompressWait(Cookfs_Pages *p, int require) {
    // TODO: properly throw errors here?
    if ((p->fileCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	Tcl_Obj *result, *resObj;
	Tcl_Size resultLength;
	int i = 0;
	int idx = -1;

	if (p->asyncPageSize == 0) {
	    if (!require) {
		return 0;
	    }
	}  else  {
	    idx = p->asyncPage[0].pageIdx;
	}

	result = CookfsRunAsyncCompressCommand(p, p->asyncCommandWait, idx, Tcl_NewIntObj(require));
	if (result == NULL) {
	    resultLength = 0;
	} else if (Tcl_ListObjLength(NULL, result, &resultLength) != TCL_OK) {
	    Tcl_DecrRefCount(result);
	    resultLength = 0;
	}

	if (resultLength > 0) {
	    if (Tcl_ListObjIndex(NULL, result, 0, &resObj) != TCL_OK) {
	        Tcl_DecrRefCount(result);
		return 0;
	    }

	    if (Tcl_GetIntFromObj(NULL, resObj, &i) != TCL_OK) {
	        Tcl_DecrRefCount(result);
		return 0;
	    }

	    // TODO: throw error?
	    if (i != idx) {
	        Tcl_DecrRefCount(result);
		return 1;
	    }

	    if (Tcl_ListObjIndex(NULL, result, 1, &resObj) != TCL_OK) {
	        Tcl_DecrRefCount(result);
		return 0;
	    }

	    Tcl_IncrRefCount(resObj);
	    int size = Cookfs_WriteTclObj(p, idx, p->asyncPage[0].pageContents, resObj);
	    p->dataPagesSize[idx] = size;
	    Tcl_DecrRefCount(resObj);
	    Tcl_DecrRefCount(result);

	    (p->asyncPageSize)--;
	    Tcl_DecrRefCount(p->asyncPage[0].pageContents);
	    for (i = 0 ; i < p->asyncPageSize ; i++) {
		p->asyncPage[i].pageIdx = p->asyncPage[i + 1].pageIdx;
		p->asyncPage[i].pageContents = p->asyncPage[i + 1].pageContents;
	    }

	    return (p->asyncPageSize > 0);
	}  else  {
	    if (p->asyncPageSize > 0) {
		return require;
	    }  else  {
		return 0;
	    }
	}
    }  else  {
	return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_AsyncCompressFinalize --
 *
 *	Call code to finalize async compression
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */


void Cookfs_AsyncCompressFinalize(Cookfs_Pages *p) {
    if ((p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	CookfsRunAsyncCompressCommand(p, p->asyncCommandFinalize, -1, Tcl_NewIntObj(1));
    }
}


// TODO: document
int Cookfs_AsyncPagePreload(Cookfs_Pages *p, int idx) {
    CookfsLog(printf("Cookfs_AsyncPagePreload: index [%d]", idx))
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	Tcl_Obj *dataObj;
	int i;

	for (i = 0 ; i < p->asyncDecompressQueue ; i++) {
	    if (p->asyncDecompressIdx[i] == idx) {
		CookfsLog(printf("Cookfs_AsyncPagePreload: return 1 - Page %d already in async decompress queue", i))
		return 1;
	    }
	}

	/* don't modify page weight in cache, as here we are just checking if it is already loaded */
	if (Cookfs_PageCacheGet(p, idx, 0, 0) != NULL) {
	    // page already in cache and we just moved it to top; do nothing
	    CookfsLog(printf("Cookfs_AsyncPagePreload: return 1 - Page already in cache and we just moved it to top"))
	    return 1;
	}

	// if queue is full, do not preload
	if (p->asyncDecompressQueue >= p->asyncDecompressQueueSize) {
	    CookfsLog(printf("Cookfs_AsyncPagePreload: return 0 - Queue is full, do not preload"))
	    return 0;
	}

	CookfsLog(printf("Cookfs_AsyncPagePreload: Reading page %d for async decompress", idx))
	// TODO: do something with possible error message
	Cookfs_PageObj dataPageObj = Cookfs_ReadPage(p, idx, -1, 0, COOKFS_COMPRESSION_CUSTOM, NULL);
	if (dataPageObj == NULL) {
	    CookfsLog(printf("Cookfs_AsyncPagePreload: ERROR: Cookfs_ReadPage returned NULL, return 1"));
	    return 1;
	}

	Cookfs_PageObjIncrRefCount(dataPageObj);
	dataObj = Cookfs_PageObjCopyAsByteArray(dataPageObj);
	Cookfs_PageObjDecrRefCount(dataPageObj);

	if (dataObj == NULL) {
	    CookfsLog(printf("Cookfs_AsyncPagePreload: ERROR: failed to convert Tcl_Obj->PageObj, return 1"));
	    return 1;
	}

	Tcl_IncrRefCount(dataObj);
	p->asyncDecompressIdx[p->asyncDecompressQueue] = idx;
	p->asyncDecompressQueue++;
	CookfsLog(printf("Adding page %d for async decompress", idx))
	CookfsRunAsyncDecompressCommand(p, p->asyncCommandProcess, idx, dataObj);
	Tcl_DecrRefCount(dataObj);

	CookfsLog(printf("Cookfs_AsyncPagePreload: return 1"))
	return 1;
    }
    CookfsLog(printf("Cookfs_AsyncPagePreload: return 0"))
    return 0;
}


// TODO: document
void Cookfs_AsyncDecompressWaitIfLoading(Cookfs_Pages *p, int idx) {
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	int i;

	for (i = 0 ; i < p->asyncDecompressQueue ; i++) {
	    if (p->asyncDecompressIdx[i] == idx) {
		Cookfs_AsyncDecompressWait(p, idx, 1);
		break;
	    }
	}
    }
}


// TODO: document
int Cookfs_AsyncDecompressWait(Cookfs_Pages *p, int idx, int require) {
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	Tcl_Obj *result, *resObj;
	Tcl_Size resultLength;
	int i = 0;

	if (p->asyncDecompressQueue == 0) {
	    if (!require) {
		return 0;
	    }
	}

	CookfsLog(printf("Cookfs_AsyncDecompressWait: calling callback"))

	result = CookfsRunAsyncDecompressCommand(p, p->asyncCommandWait, idx, Tcl_NewIntObj(require));
	if ((result == NULL) || (Tcl_ListObjLength(NULL, result, &resultLength) != TCL_OK)) {
	    resultLength = 0;
	}

	if (resultLength >= 2) {
	    int j, k;
	    if (Tcl_ListObjIndex(NULL, result, 0, &resObj) != TCL_OK) {
		return 0;
	    }

	    if (Tcl_GetIntFromObj(NULL, resObj, &i) != TCL_OK) {
		return 0;
	    }

	    if (Tcl_ListObjIndex(NULL, result, 1, &resObj) != TCL_OK) {
		return 0;
	    }

	    CookfsLog(printf("Cookfs_AsyncDecompressWait: callback returned data for %d", i))
	    Tcl_IncrRefCount(resObj);
	    Cookfs_PageObj pageObj = Cookfs_PageObjNewFromByteArray(resObj);
	    Tcl_DecrRefCount(resObj);
	    if (pageObj != NULL) {
	        Cookfs_PageObjIncrRefCount(pageObj);
	        /*
	            Set the page weight to 1000 because it should be cached and used further.
	            If it will be displaced by other weighty pages, then preloading makes no sense.
	            Real page weight will be set by Cookfs_PageGet
	        */
	        Cookfs_PageCacheSet(p, i, pageObj, 1000);
	        Cookfs_PageObjDecrRefCount(pageObj);
	    }

	    Tcl_DecrRefCount(result);

	    CookfsLog(printf("Cookfs_AsyncDecompressWait: cleaning up decompression queue"))
	    for (j = 0 ; j < p->asyncDecompressQueue ; j++) {
		if (p->asyncDecompressIdx[j] == i) {
		    for (k = j ; k < (p->asyncDecompressQueue - 1) ; k++) {
			p->asyncDecompressIdx[k] = p->asyncDecompressIdx[k + 1];
		    }
		    (p->asyncDecompressQueue)--;
		    // needed to properly detect it in Cookfs_AsyncPageGet
		    p->asyncDecompressIdx[p->asyncDecompressQueue] = -1;
		    break;
		}
	    }

	    CookfsLog(printf("Cookfs_AsyncDecompressWait: cleaning up decompression queue done"))

	    return (p->asyncDecompressQueue > 0);
	}  else  {
	    if (result != NULL) {
		Tcl_DecrRefCount(result);
	    }
	    if (p->asyncDecompressQueue > 0) {
		return require;
	    }  else  {
		return 0;
	    }
	}
    }  else  {
	return 0;
    }
}


// TODO: document
void Cookfs_AsyncDecompressFinalize(Cookfs_Pages *p) {
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	CookfsRunAsyncDecompressCommand(p, p->asyncCommandFinalize, -1, Tcl_NewIntObj(1));
    }
}

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

void CookfsWriteCompression(Cookfs_Pages *p, int compression) {
    Tcl_Obj *byteObj;
    unsigned char byte[4];
    byte[0] = (unsigned char) compression;
    byteObj = Tcl_NewByteArrayObj(byte, 1);
    Tcl_IncrRefCount(byteObj);
    Tcl_WriteObj(p->fileChannel, byteObj);
    Tcl_DecrRefCount(byteObj);
}

/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsCreateCompressionCommand --
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

static Tcl_Obj **CookfsCreateCompressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr, int additionalElements) {
    Tcl_Obj **rc;
    Tcl_Obj **listObjv;
    Tcl_Size listObjc;
    Tcl_Size i;

    /* get command as list of attributes */
    if (Tcl_ListObjGetElements(interp, cmd, &listObjc, &listObjv) != TCL_OK) {
	return NULL;
    }

    rc = (Tcl_Obj **) ckalloc(sizeof(Tcl_Obj *) * (listObjc + additionalElements));
    for (i = 0; i < listObjc; i++) {
	rc[i] = listObjv[i];
	Tcl_IncrRefCount(rc[i]);
    }
    rc[listObjc] = NULL;
    *lenPtr = listObjc + additionalElements;
    return rc;
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

static Cookfs_PageObj CookfsReadPageCustom(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    /* use vfs::zip command for decompression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;
    Tcl_Obj *data;
    int count;

    if (p->decompressCommandPtr == NULL) {
        SET_ERROR_STR("No decompresscommand specified");
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
    Cookfs_PageObj rc = Cookfs_PageObjNewFromByteArray(data);
    Tcl_DecrRefCount(data);
    return rc;
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

static int CookfsWritePageCustom(Cookfs_Pages *p, unsigned char *bytes, int origSize) {
    Tcl_Size size = origSize;
    /* use vfs::zip command for compression */
    Tcl_Obj *prevResult;
    Tcl_Obj *compressed;

    if (p->compressCommandPtr == NULL) {
	return -1;
    }

    Tcl_Obj *data = Tcl_NewByteArrayObj(bytes, origSize);
    Tcl_IncrRefCount(data);
    p->compressCommandPtr[p->compressCommandLen - 1] = data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->compressCommandLen, p->compressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	CookfsLog(printf("Unable to compress: %s", Tcl_GetString(Tcl_GetObjResult(p->interp))));
	p->compressCommandPtr[p->compressCommandLen - 1] = NULL;
	Tcl_SetObjResult(p->interp, prevResult);
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

    if (SHOULD_COMPRESS(p, origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_CUSTOM);
	Tcl_WriteObj(p->fileChannel, compressed);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(compressed);
    return size;
}

#ifdef USE_VFS_COMMANDS_FOR_ZIP

/*
 *----------------------------------------------------------------------
 *
 * CookfsCheckCommandExists --
 *
 *	Checks whether specified command exists, providing Tcl 8.4
 *	backwards compatibility
 *
 * Results:
 *	non-zero if command exists; 0 otherwise
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsCheckCommandExists(Tcl_Interp *interp, const char *commandName)
{
    int rc;
    Tcl_CmdInfo info;

    rc = Tcl_GetCommandInfo(interp, commandName, &info);

    return rc;
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * CookfsRunAsyncCompressCommand --
 *
 *	Helper to run the async compress command with specified
 *	arguments in interp from Cookfs_Pages object
 *
 *	Reverts Tcl interpreter's result to one before
 *	this function was called.
 *
 * Results:
 *	result from command invocation or NULL in case of failure
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *CookfsRunAsyncCompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg) {
    Tcl_Obj *prevResult, *data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 3] = cmd;
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 2] = Tcl_NewIntObj(idx);
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 1] = arg;
    Tcl_IncrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 2]);
    Tcl_IncrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 1]);
    if (Tcl_EvalObjv(p->interp, p->asyncCompressCommandLen, p->asyncCompressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	Tcl_DecrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 2]);
	Tcl_DecrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 1]);
	return NULL;
    }
    Tcl_DecrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 2]);
    Tcl_DecrRefCount(p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 1]);
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 3] = NULL;
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 2] = NULL;
    p->asyncCompressCommandPtr[p->asyncCompressCommandLen - 1] = NULL;
    data = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(data);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsRunAsyncDecompressCommand --
 *
 *	Helper to run the async decompress command with specified
 *	arguments in interp from Cookfs_Pages object
 *
 *	Reverts Tcl interpreter's result to one before
 *	this function was called.
 *
 * Results:
 *	result from command invocation or NULL in case of failure
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *CookfsRunAsyncDecompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg) {
    Tcl_Obj *prevResult, *data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 3] = cmd;
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 2] = Tcl_NewIntObj(idx);
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 1] = arg;
    Tcl_IncrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 2]);
    Tcl_IncrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 1]);
    if (Tcl_EvalObjv(p->interp, p->asyncDecompressCommandLen, p->asyncDecompressCommandPtr, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
	Tcl_DecrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 2]);
	Tcl_DecrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 1]);
	return NULL;
    }
    Tcl_DecrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 2]);
    Tcl_DecrRefCount(p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 1]);
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 3] = NULL;
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 2] = NULL;
    p->asyncDecompressCommandPtr[p->asyncDecompressCommandLen - 1] = NULL;
    data = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(data);
    Tcl_SetObjResult(p->interp, prevResult);
    Tcl_DecrRefCount(prevResult);
    return data;
}
