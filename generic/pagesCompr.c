/*
 * pagesCompr.c
 *
 * Provides functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 */

#include "cookfs.h"
#ifdef COOKFS_USEBZ2
#include "../bzip2/bzlib.h"
#endif

#define COOKFS_SUFFIX_BYTES 16

/* let's gain at least 16 bytes and/or 5% to compress it */
#define SHOULD_COMPRESS(p, origSize, size) ((p->alwaysCompress) || ((size < (origSize - 16)) && ((size) <= (origSize - (origSize / 20)))))

/* declarations of static and/or internal functions */
static Tcl_Obj **CookfsCreateCompressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr, int additionalElements);
static void CookfsWriteCompression(Cookfs_Pages *p, int compression);
static Tcl_Obj *CookfsReadPageZlib(Cookfs_Pages *p, int size);
static int CookfsWritePageZlib(Cookfs_Pages *p, Tcl_Obj *data, int origSize);
static Tcl_Obj *CookfsReadPageBz2(Cookfs_Pages *p, int size);
static int CookfsWritePageBz2(Cookfs_Pages *p, Tcl_Obj *data, int origSize);
static Tcl_Obj *CookfsReadPageCustom(Cookfs_Pages *p, int size);
static int CookfsWritePageCustom(Cookfs_Pages *p, Tcl_Obj *data, int origSize);
static int CookfsCheckCommandExists(Tcl_Interp *interp, const char *commandName);
static Tcl_Obj *CookfsRunAsyncCompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);
static Tcl_Obj *CookfsRunAsyncDecompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);

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

/* names for all defined compressions */
const char *cookfsCompressionNames[256] = {
    "none",
    "zlib",
    "bz2",
    "", "", "", "", "", "", "", "", "", "", "", "", "",
    /* 0x10 - 0x1f */
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    /* 0x10 - 0x1f */
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    /* 0xf0 - 0xfe */
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "custom"
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
	    Tcl_Free((void *) rc->compressCommandPtr);
	}
	if (rc->decompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->decompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    Tcl_Free((void *) rc->decompressCommandPtr);
	}
	if (rc->asyncCompressCommandPtr != NULL) {
	    Tcl_Obj **ptr;
	    for (ptr = rc->asyncCompressCommandPtr; *ptr; ptr++) {
		Tcl_DecrRefCount(*ptr);
	    }
	    Tcl_Free((void *) rc->asyncCompressCommandPtr);
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

Tcl_Obj *Cookfs_ReadPage(Cookfs_Pages *p, int idx, int size, int decompress, int compressionType) {
    int count;
    int compression = p->fileCompression;
    
    if (idx >= p->dataNumPages) {
	return NULL;
    }

    p->fileLastOp = COOKFS_LASTOP_READ;

    CookfsLog(printf("Cookfs_ReadPage I=%d S=%d C=%d", idx, size, p->fileCompression))
    if (size == 0) {
	/* if page was empty, no need to read anything */
	Tcl_Obj *obj = Tcl_NewByteArrayObj((unsigned char *) "", 0);
	Tcl_IncrRefCount(obj);
        return obj;
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
	compression = Tcl_GetByteArrayFromObj(byteObj, NULL)[0];
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
                Tcl_Obj *data;
                data = Tcl_NewObj();
                count = Tcl_ReadChars(p->fileChannel, data, size, 0);
                if (count != size) {
                    CookfsLog(printf("Unable to read - %d != %d", count, size))
                    Tcl_IncrRefCount(data);
                    Tcl_DecrRefCount(data);
                    return NULL;
                }

		if (!decompress) {
		    CookfsLog(printf("Cookfs_ReadPage retrieved chunk %d", idx));
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

int Cookfs_WritePage(Cookfs_Pages *p, int idx, Tcl_Obj *data, Tcl_Obj *compressedData) {
    int origSize;
    int size = -1;

    /* get binary data */
    Tcl_GetByteArrayFromObj(data, &origSize);
    Tcl_IncrRefCount(data);
    
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
		Tcl_WriteObj(p->fileChannel, data);
		size = origSize + 1;
	    }
	}  else  {
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
	}
    }  else  {
	size = 0;
    }
    Tcl_DecrRefCount(data);
    return size;
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

Tcl_Obj *Cookfs_AsyncPageGet(Cookfs_Pages *p, int idx) {
    if ((p->fileCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	int i;
	for (i = 0 ; i < p->asyncPageSize ; i++) {
	    if (p->asyncPage[i].pageIdx == idx) {
		Tcl_IncrRefCount(p->asyncPage[i].pageContents);
		return p->asyncPage[i].pageContents;
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
		return Cookfs_PageCacheGet(p, idx);
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

int Cookfs_AsyncPageAdd(Cookfs_Pages *p, int idx, Tcl_Obj *data) {
    if ((p->fileCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	Tcl_Obj *newObjObj;
	int newObjSize;
	unsigned char *newObjData;
	int asyncIdx;
	// retrieve any already processed queues
	while (Cookfs_AsyncCompressWait(p, 0)) {}
	while (p->asyncPageSize >= COOKFS_PAGES_MAX_ASYNC) {
	    Cookfs_AsyncCompressWait(p, 0);
	}
	asyncIdx = p->asyncPageSize++;
	newObjData = Tcl_GetByteArrayFromObj(data, &newObjSize);
	newObjObj = Tcl_NewByteArrayObj(newObjData, newObjSize);
	// copy the object to avoid increased memory usage
	Tcl_IncrRefCount(newObjObj);
	Tcl_IncrRefCount(data);
	Tcl_DecrRefCount(data);
	p->asyncPage[asyncIdx].pageIdx = idx;
	p->asyncPage[asyncIdx].pageContents = newObjObj;
	CookfsRunAsyncCompressCommand(p, p->asyncCommandProcess, idx, data);
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
	int resultLength, size;
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
	if ((result == NULL) || (Tcl_ListObjLength(NULL, result, &resultLength) != TCL_OK)) {
	    resultLength = 0;
	}

	if (resultLength > 0) {
	    if (Tcl_ListObjIndex(NULL, result, 0, &resObj) != TCL_OK) {
		return 0;
	    }
	    
	    if (Tcl_GetIntFromObj(NULL, resObj, &i) != TCL_OK) {
		return 0;
	    }

	    // TODO: throw error?
	    if (i != idx) {
		return 1;
	    }

	    if (Tcl_ListObjIndex(NULL, result, 1, &resObj) != TCL_OK) {
		return 0;
	    }

	    Tcl_IncrRefCount(resObj);
	    size = Cookfs_WritePage(p, idx, p->asyncPage[0].pageContents, resObj);
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
	    Tcl_DecrRefCount(result);
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
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	Tcl_Obj *dataObj;
	int i;

	for (i = 0 ; i < p->asyncDecompressQueue ; i++) {
	    if (p->asyncDecompressIdx[i] == idx) {
		CookfsLog(printf("Page %d already in async decompress queue", i))
		return 1;
	    }
	}
	
	if (Cookfs_PageCacheGet(p, idx) != NULL) {
	    // page already in cache and we just moved it to top; do nothing
	    return 1;
	}
	
	// if queue is full, do not preload
	if (p->asyncDecompressQueue >= p->asyncDecompressQueueSize) {
	    return 0;
	}

	CookfsLog(printf("Reading page %d for async decompress", idx))
	dataObj = Cookfs_ReadPage(p, idx, -1, 0, COOKFS_COMPRESSION_CUSTOM);
	
	if (dataObj != NULL) {
	    p->asyncDecompressIdx[p->asyncDecompressQueue] = idx;
	    p->asyncDecompressQueue++;
	    CookfsLog(printf("Adding page %d for async decompress", idx))
	    CookfsRunAsyncDecompressCommand(p, p->asyncCommandProcess, idx, dataObj);
	    Tcl_DecrRefCount(dataObj);
	}

	return 1;
    }
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
	int resultLength;
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
	    Cookfs_PageCacheSet(p, i, resObj);
	    
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
    int listObjc;
    int i;

    /* get command as list of attributes */
    if (Tcl_ListObjGetElements(interp, cmd, &listObjc, &listObjv) != TCL_OK) {
	return NULL;
    }

    rc = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * (listObjc + additionalElements));
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

    Tcl_IncrRefCount(data);
    p->zipCmdCompress[p->zipCmdOffset] = data;
    prevResult = Tcl_GetObjResult(p->interp);
    Tcl_IncrRefCount(prevResult);
    if (Tcl_EvalObjv(p->interp, p->zipCmdLength, p->zipCmdCompress, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
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
    if (SHOULD_COMPRESS(p, origSize, size)) {
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
    
    if (SHOULD_COMPRESS(p, origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_CUSTOM);
	Tcl_WriteObj(p->fileChannel, compressed);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(compressed);
    return size;
}


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
