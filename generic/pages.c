/*
 * pages.c
 *
 * Provides functions for using pages
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"

#define COOKFS_SUFFIX_BYTES 16

/* declarations of static and/or internal functions */
static Tcl_Obj *CookfsPagesPageGetInt(Cookfs_Pages *p, int index);
static void CookfsPagesPageCacheMoveToTop(Cookfs_Pages *p, int index);
static void CookfsPagesPageExtendIfNeeded(Cookfs_Pages *p, int count);
static Tcl_WideInt CookfsPagesGetPageOffset(Cookfs_Pages *p, int idx);
static void CookfsTruncateFileIfNeeded(Cookfs_Pages *p, Tcl_WideInt targetOffset);



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetHandle --
 *
 *	Returns pages handle from provided Tcl command name
 *
 * Results:
 *	Pointer to Cookfs_Pages or NULL in case of failure
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Pages *Cookfs_PagesGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;
    
    /* TODO: verify command suffix etc */

    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
	return NULL;
    }

    /* if we found proper Tcl command, its objClientData is Cookfs_Pages */
    return (Cookfs_Pages *) (cmdInfo.objClientData);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesInit --
 *
 *	Initializes new pages instance
 *
 *	Takes file name as Tcl_Obj
 *
 *	If fileReadOnly is non-zero, file must exist and be a readable
 *	cookfs archive; if fileReadOnly is zero, if file is not a
 *	cookfs archive or does not exist, new one is created/appended at
 *	the end of existing file
 *
 *	fileCompression indicates compression for fsindex storage and
 *	newly created pages;
 *	if compresion is set to COOKFS_COMPRESSION_CUSTOM, compressCommand and
 *	decompressCommand need to be specified and cookfs will invoke these
 *	commands when needed
 *	
 *	fileSignature is only meant for advanced users; it allows specifying
 *	custom pages signature, which can be used to create non-standard
 *	pages storage
 *
 *	If useFoffset is non-zero, foffset is used as indicator to where
 *	end of cookfs archive is; it can be used to store cookfs at location
 *	other than end of file
 *
 * Results:
 *	Pointer to new instance; NULL in case of error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
Cookfs_Pages *Cookfs_PagesInit(Tcl_Interp *interp, Tcl_Obj *fileName, int fileReadOnly, int fileCompression, char *fileSignature, int useFoffset, Tcl_WideInt foffset, int isAside, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand) {
    Cookfs_Pages *rc = (Cookfs_Pages *) Tcl_Alloc(sizeof(Cookfs_Pages));
    int i;

    /* initialize basic information */
    rc->interp = interp;
    rc->isAside = isAside;
    Cookfs_PagesInitCompr(rc);

    if (Cookfs_SetCompressCommands(rc, compressCommand, decompressCommand) != TCL_OK) {
	Tcl_Free((void *) rc);
	return NULL;
    }

    /* initialize structure */
    rc->useFoffset = useFoffset;
    rc->foffset = foffset;
    rc->fileReadOnly = fileReadOnly;
    rc->fileCompression = fileCompression;
    if (fileSignature != NULL)
	memcpy(rc->fileSignature, fileSignature, 7);
    else
	memcpy(rc->fileSignature, "CFS0002", 7);

    
    /* initialize parameters */
    rc->fileLastOp = COOKFS_LASTOP_UNKNOWN;
    rc->fileCompression = fileCompression;
    rc->fileCompressionLevel = 9;
    rc->dataNumPages = 0;
    rc->dataPagesDataSize = 256;
    rc->dataPagesSize = (int *) Tcl_Alloc(rc->dataPagesDataSize * sizeof(int));
    rc->dataPagesMD5 = (unsigned char *) Tcl_Alloc(rc->dataPagesDataSize * 16);
    rc->dataAsidePages = NULL;
    rc->dataPagesIsAside = isAside;
    
    rc->dataIndex = Tcl_NewStringObj("", 0);

    rc->pageHash = COOKFS_HASH_MD5;
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    rc->zipCmdCrc[0] = Tcl_NewStringObj("vfs::crc", -1);
#endif
    
    Tcl_IncrRefCount(rc->dataIndex);

    /* initialize cache */
    for (i = 0; i < COOKFS_MAX_CACHE_PAGES; i++) {
	rc->cachePageIdx[i] = -1;
	rc->cachePageObj[i] = NULL;
    }
    rc->cacheSize = 0;

    CookfsLog(printf("Opening file %s as %s with compression %d", Tcl_GetStringFromObj(fileName, NULL), (rc->fileReadOnly ? "r" : "a+"), fileCompression))

    /* open file for reading / writing */
    CookfsLog(printf("Cookfs_PagesInit - Tcl_FSOpenFileChannel"))
    rc->fileChannel = Tcl_FSOpenFileChannel(NULL, fileName,
	(rc->fileReadOnly ? "r" : "a+"), 0666);
    
    if (rc->fileChannel == NULL) {
        CookfsLog(printf("Cookfs_PagesInit - cleaning up"))
	Cookfs_PagesFini(rc);
	return NULL;
    }
    
    /* initialize binary encoding */
    if (Tcl_SetChannelOption(NULL, rc->fileChannel, "-encoding", "binary") != TCL_OK) {
	CookfsLog(printf("Unable to set -encoding option"))
	Cookfs_PagesFini(rc);
	return NULL;
    }
    if (Tcl_SetChannelOption(NULL, rc->fileChannel, "-translation", "binary") != TCL_OK) {
	CookfsLog(printf("Unable to set -translation option"))
	Cookfs_PagesFini(rc);
	return NULL;
    }

    /* read index or fail */
    if (!CookfsReadIndex(rc)) {
	if (rc->fileReadOnly) {
	    rc->pagesUptodate = 1;
	    rc->indexChanged = 0;
	    rc->shouldTruncate = 0;
	    Cookfs_PagesFini(rc);
	    return NULL;
	}  else  {
	    rc->dataInitialOffset = Tcl_Seek(rc->fileChannel, 0, SEEK_END);
	    rc->dataAllPagesSize = 0;
	    rc->dataNumPages = 0;
	    rc->pagesUptodate = 0;
	    rc->indexChanged = 1;
	    rc->shouldTruncate = 1;
	}
	CookfsLog(printf("Index not read!"))
    }  else  {
	rc->pagesUptodate = 1;
	rc->indexChanged = 0;
	rc->shouldTruncate = 1;
    }
    
    /* force compression since we want to use target compression anyway */
    if (!rc->fileReadOnly) {
	rc->fileCompression = fileCompression;
	rc->fileCompressionLevel = 9;
    }

    CookfsLog(printf("Opening file %s - compression %d", Tcl_GetStringFromObj(fileName, NULL), rc->fileCompression))

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesClose --
 *
 *	Write and close cookfs pages object; object is not yet deleted
 *
 * Results:
 *	Offset to end of data
 *
 * Side effects:
 *	Any attempts to write afterwards might end up in segfault
 *
 *----------------------------------------------------------------------
 */

Tcl_WideInt Cookfs_PagesClose(Cookfs_Pages *p) {
    Tcl_Obj *obj;

    CookfsLog(printf("Cookfs_PagesClose - BEGIN"))
    
    if (p->fileChannel != NULL) {
	CookfsLog(printf("Cookfs_PagesClose - Pages up to date = %d, Index changed = %d", p->pagesUptodate, p->indexChanged))
	/* if changes were made, save them to disk */
	if ((!p->pagesUptodate) || (p->indexChanged)) {
	    int indexSize;
	    unsigned char buf[COOKFS_SUFFIX_BYTES];
	    unsigned char *bufSizes;
	    Tcl_WideInt offset;

	    CookfsLog(printf("Cookfs_PagesClose - Writing index"))
	    offset = CookfsPagesGetPageOffset(p, p->dataNumPages);
 
	    Tcl_Seek(p->fileChannel, offset, SEEK_SET);

	    if (p->dataNumPages > 0) {
		/* add MD5 information */
		obj = Tcl_NewByteArrayObj(p->dataPagesMD5, p->dataNumPages * 16);
		Tcl_IncrRefCount(obj);
		Tcl_WriteObj(p->fileChannel, obj);
		Tcl_DecrRefCount(obj);

		/* add page size information */
		bufSizes = (unsigned char *) Tcl_Alloc(p->dataNumPages * 4);
		Cookfs_Int2Binary(p->dataPagesSize, bufSizes, p->dataNumPages);
		obj = Tcl_NewByteArrayObj(bufSizes, p->dataNumPages * 4);
		Tcl_IncrRefCount(obj);
		Tcl_WriteObj(p->fileChannel, obj);
		Tcl_DecrRefCount(obj);
		Tcl_Free((void *) bufSizes);
	    }

	    /* write index */
	    indexSize = Cookfs_WritePage(p, p->dataIndex);
	    if (indexSize < 0) {
		/* TODO: handle index writing issues better */
		Tcl_Panic("Unable to compress index");
	    }

	    CookfsLog(printf("Cookfs_PagesClose - Offset write: %d", (int) Tcl_Seek(p->fileChannel, 0, SEEK_CUR)))

	    /* provide index size and number of pages */
	    Cookfs_Int2Binary(&indexSize, buf, 1);
	    Cookfs_Int2Binary(&(p->dataNumPages), buf + 4, 1);
	    
	    /* provide compression type and file signature */
	    buf[8] = p->fileCompression;
	    memcpy(buf + 9, p->fileSignature, 7);

	    obj = Tcl_NewByteArrayObj(buf, COOKFS_SUFFIX_BYTES);
	    Tcl_IncrRefCount(obj);
	    Tcl_WriteObj(p->fileChannel, obj);
	    Tcl_DecrRefCount(obj);
	    p->foffset = Tcl_Tell(p->fileChannel);

            CookfsTruncateFileIfNeeded(p, p->foffset);
	}  else  {
	    
	}

	/* close file channel */
	CookfsLog(printf("Cookfs_PagesClose - Closing channel - rc=%d", ((int) ((p->foffset) & 0x7fffffff)) ))
	Tcl_Close(NULL, p->fileChannel);
	
	p->fileChannel = NULL;
    }
    
    CookfsLog(printf("Cookfs_PagesClose - END"))
    
    return p->foffset;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesFini --
 *
 *	Cleanup pages instance
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesFini(Cookfs_Pages *p) {
    int i;

    Cookfs_PagesClose(p);

    /* clean up add-aside pages */
    if (p->dataAsidePages != NULL) {
	Cookfs_PagesFini(p->dataAsidePages);
    }

    /* clean up cache */
    CookfsLog(printf("Cleaning up cache"))
    for (i = 0; i < p->cacheSize; i++) {
	if (p->cachePageObj[i] != NULL) {
	    Tcl_DecrRefCount(p->cachePageObj[i]);
	}
    }
    
    /* clean up compression information */
    Cookfs_PagesFiniCompr(p);
    
    /* clean up index */
    CookfsLog(printf("Cleaning up index data"))
    Tcl_DecrRefCount(p->dataIndex);

    /* clean up pages data */
    CookfsLog(printf("Cleaning up pages MD5/size"))
    Tcl_Free((void *) p->dataPagesSize);
    Tcl_Free((void *) p->dataPagesMD5);

    /* clean up storage */
    Tcl_Free((void *) p);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageAdd --
 *
 *	Add new page or return index of existing page, if page with
 *	same content already exists
 *
 * Results:
 *	Index that can be used in subsequent calls to Cookfs_PageGet()
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PageAdd(Cookfs_Pages *p, Tcl_Obj *dataObj) {
    int idx;
    int dataSize;
    Tcl_WideInt offset;
    unsigned char md5sum[16];
    unsigned char *bytes;
    int objLength;
    unsigned char *pageMD5 = p->dataPagesMD5;

    bytes = Tcl_GetByteArrayFromObj(dataObj, &objLength);
    
    if (p->pageHash == COOKFS_HASH_CRC32) {
	int b[4] = { 0, 0, objLength, 0 };
#ifdef USE_VFS_COMMANDS_FOR_ZIP
	Tcl_Obj *data;
	Tcl_Obj *prevResult;

	Tcl_IncrRefCount(dataObj);
	p->zipCmdCrc[1] = dataObj;

	prevResult = Tcl_GetObjResult(p->interp);
	Tcl_IncrRefCount(prevResult);

	if (Tcl_EvalObjv(p->interp, 2, p->zipCmdCrc, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) == TCL_OK) {
	    data = Tcl_GetObjResult(p->interp);
	    Tcl_IncrRefCount(data);
	    Tcl_GetIntFromObj(NULL, data, &b[3]);
	    Tcl_DecrRefCount(data);
	}

	p->zipCmdCrc[1] = NULL;
	Tcl_DecrRefCount(dataObj);

	Tcl_SetObjResult(p->interp, prevResult);
	Tcl_DecrRefCount(prevResult);
#else
	b[3] = (int) Tcl_ZlibCRC32(Tcl_ZlibCRC32(0,NULL,0), bytes, objLength);
#endif
	/* copy to checksum memory */
	Cookfs_Int2Binary(b, bytes, 4);
    }  else  {
	Cookfs_MD5(bytes, objLength, md5sum);
    }

    /* see if this entry already exists */
    CookfsLog(printf("Cookfs_PageAdd: Matching page (size=%d bytes)", objLength))
    for (idx = 0; idx < p->dataNumPages; idx++, pageMD5 += 16) {
	if (memcmp(pageMD5, md5sum, 16) == 0) {
	    /* even if MD5 checksums are the same, we still need to validate contents of the page */
	    Tcl_Obj *otherPageData;
	    unsigned char *otherBytes;
	    int otherObjLength;
	    int isMatched = 1;

	    otherPageData = Cookfs_PageGet(p, idx);
	    /* fail in case when decompression is not available */
	    if (otherPageData == NULL) {
		CookfsLog(printf("Cookfs_PageAdd: Unable to verify page with same MD5 checksum"))
		return -1;
	    }

	    Tcl_IncrRefCount(otherPageData);
	    otherBytes = Tcl_GetByteArrayFromObj(otherPageData, &otherObjLength);

	    /* if page with same checksum was found, verify its contents as we
	     * do not rely on MD5 checksum - this avoids issue with MD5 collissions */
	    if (otherObjLength != objLength) {
		isMatched = 0;
	    }  else  {
		if (memcmp(bytes, otherBytes, objLength) != 0) {
		    isMatched = 0;
		}
	    }

	    Tcl_DecrRefCount(otherPageData);

	    if (isMatched) {
		CookfsLog(printf("Cookfs_PageAdd: Matched page (size=%d bytes) as %d", objLength, idx))
		if (p->dataPagesIsAside) {
		    idx |= COOKFS_PAGES_ASIDE;
		}
		return idx;
	    }
	}
    }

    /* if this page has an aside page set up, ask it to add new page */
    if (p->dataAsidePages != NULL) {
	CookfsLog(printf("Cookfs_PageAdd: Sending add command to asidePages"))
	return Cookfs_PageAdd(p->dataAsidePages, dataObj);
    }

    /* if file is read only, return page can't be added */
    if (p->fileReadOnly) {
	return -1;
    }

    /* store index for new page, increment number of pages */
    idx = p->dataNumPages;
    (p->dataNumPages)++;

    /* reallocate list of page offsets if exceeded */
    CookfsPagesPageExtendIfNeeded(p, p->dataNumPages);
    offset = CookfsPagesGetPageOffset(p, idx);

    /* if last operation was not write, we need to seek
     * to make sure we're at location where we should be writing */
    if (p->fileLastOp != COOKFS_LASTOP_WRITE) {
	/* TODO: optimize by checking if last operation was a write */
	Tcl_Seek(p->fileChannel, offset, SEEK_SET);
	CookfsLog(printf("Seeking to EOF -> %d",(int) offset))
    }

    memcpy(p->dataPagesMD5 + (idx * 16), md5sum, 16);

    CookfsLog(printf("MD5sum is %08x%08x%08x%08x\n", ((int *) md5sum)[0], ((int *) md5sum)[1], ((int *) md5sum)[2], ((int *) md5sum)[3]))

    dataSize = Cookfs_WritePage(p, dataObj);
    if (dataSize < 0) {
	/* TODO: if writing failed, we can't be certain of archive state - need to handle this at vfs layer somehow */
	CookfsLog(printf("Unable to compress page"))
	return -1;
    }
    p->fileLastOp = COOKFS_LASTOP_WRITE;
    p->pagesUptodate = 0;
    
    p->dataPagesSize[idx] = dataSize;

    if (p->dataPagesIsAside) {
	idx |= COOKFS_PAGES_ASIDE;
    }

    return idx;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGet --
 *
 *	Gets contents of a page at specified index
 *
 * Results:
 *	Tcl_Obj with page data as bytearray object
 *	This Tcl_Obj may also be used by pages cache therefore it is
 *	needed to Tcl_IncrRefCount() / Tcl_DecrRefCount() to properly
 *	handle memory management
 *
 * Side effects:
 *	May remove other pages from pages cache; if reference counter is
 *	not properly managed, objects for other pages might be invalidated
 *	while they are used by caller of this API
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGet(Cookfs_Pages *p, int index) {
    Tcl_Obj *rc;
    int newIdx;
    int i;

    /* TODO: cleanup refcount for cached vs non-cached entries */

    /* if page is disabled, immediately get page */
    if (p->cacheSize <= 0) {
	rc = CookfsPagesPageGetInt(p, index);
	CookfsLog(printf("Returning directly [%s]", rc == NULL ? "NULL" : "SET"))
	return rc;
    }
    
    /* iterate through pages cache and check if it already is in memory */
    for (i = 0; i < p->cacheSize; i++) {
	if (p->cachePageIdx[i] == index) {
	    rc = p->cachePageObj[i];
	    /* if found and is not the first page in cache, move it to front */
	    if (i > 0) {
		CookfsPagesPageCacheMoveToTop(p, i);
	    }
	    CookfsLog(printf("Returning from cache [%s]", rc == NULL ? "NULL" : "SET"))
	    return rc;
	}
    }

    /* get page and store it in cache */
    rc = CookfsPagesPageGetInt(p, index);
    CookfsLog(printf("Returning and caching [%s]", rc == NULL ? "NULL" : "SET"))
    if (rc == NULL) {
	return NULL;
    }

    /* replace page not used the most with new page and move it to top */
    newIdx = p->cacheSize - 1;

    if (p->cachePageObj[newIdx] != NULL) {
	Tcl_DecrRefCount(p->cachePageObj[newIdx]);
    }
    p->cachePageIdx[newIdx] = index;
    p->cachePageObj[newIdx] = rc;
    CookfsPagesPageCacheMoveToTop(p, newIdx);

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetHead --
 *
 *	Get all bytes before beginning of cookfs archive
 *
 * Results:
 *	Tcl_Obj containing read bytes
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetHead(Cookfs_Pages *p) {
    int count;
    Tcl_Obj *data;
    data = Tcl_NewByteArrayObj((unsigned char *) "", 0);
    if (p->dataInitialOffset > 0) {
	p->fileLastOp = COOKFS_LASTOP_UNKNOWN;
	Tcl_Seek(p->fileChannel, 0, SEEK_SET);
	count = Tcl_ReadChars(p->fileChannel, data, p->dataInitialOffset, 0);
	if (count != p->dataInitialOffset) {
	    Tcl_IncrRefCount(data);
	    Tcl_DecrRefCount(data);
	    return NULL;
	}
    }
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetHeadMD5 --
 *
 *	Get MD5 checksum of all bytes before beginning of cookfs archive
 *
 * Results:
 *	Tcl_Obj containing MD5 as hexadecimal string
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetHeadMD5(Cookfs_Pages *p) {
    return Cookfs_MD5FromObj(Cookfs_PageGetHead(p));
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetTail --
 *
 *	Get all bytes of cookfs archive
 *	This should not be called if archive has been modified
 *	after opening it
 *
 * Results:
 *	Tcl_Obj containing data as byte array
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetTail(Cookfs_Pages *p) {
    int count;
    Tcl_Obj *data;
    data = Tcl_NewByteArrayObj((unsigned char *) "", 0);
    if (p->dataInitialOffset > 0) {
	p->fileLastOp = COOKFS_LASTOP_UNKNOWN;
	Tcl_Seek(p->fileChannel, p->dataInitialOffset, SEEK_SET);
	count = Tcl_ReadChars(p->fileChannel, data, -1, 0);
	if (count < 0) {
	    Tcl_IncrRefCount(data);
	    Tcl_DecrRefCount(data);
	    return NULL;
	}
    }
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetTailMD5 --
 *
 *	Get MD5 checksum of all bytes of cookfs archive
 *	This should not be called if archive has been modified
 *	after opening it
 *
 * Results:
 *	Tcl_Obj containing MD5 as hexadecimal string
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetTailMD5(Cookfs_Pages *p) {
    /* TODO: this can consume a lot of memory for large archives */
    return Cookfs_MD5FromObj(Cookfs_PageGetTail(p));
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetAside --
 *
 *	Sets another pages object as commit aside pages for a base
 *	set of pages.
 *
 *	This causes p to add new pages to aside pages object, instead
 *	of appending to its own pages.
 *	It allows saving changes to read-only pages in a separate file.
 *	
 *	If aside pages also contain non-zero length index information,
 *	aside index overwrites main index.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	If p contained another aside pages, these pages are cleaned up
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetAside(Cookfs_Pages *p, Cookfs_Pages *aside) {
    Tcl_Obj *asideIndex;
    int asideIndexLength;
    if (p->dataAsidePages != NULL) {
	Cookfs_PagesFini(p->dataAsidePages);
    }
    p->dataAsidePages = aside;
    if (aside != NULL) {
	CookfsLog(printf("Cookfs_PagesSetAside: Checking if index in add-aside archive should be overwritten."))
	asideIndex = Cookfs_PagesGetIndex(aside);
	Tcl_GetByteArrayFromObj(asideIndex, &asideIndexLength);
	if (asideIndexLength == 0) {
	    CookfsLog(printf("Cookfs_PagesSetAside: Copying index from main archive to add-aside archive."))
	    Cookfs_PagesSetIndex(aside, p->dataIndex);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetIndex --
 *
 *	Sets index information that is stored as part of cookfs archive
 *	metadata
 *
 *	This is usually set with output from Cookfs_FsindexToObject()
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Reference counter for Tcl_Obj storing previous index is
 *	decremented; improper handling of ref count for indexes
 *	might cause crashes
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetIndex(Cookfs_Pages *p, Tcl_Obj *dataIndex) {
    if (p->dataAsidePages != NULL) {
	Cookfs_PagesSetIndex(p->dataAsidePages, dataIndex);
    }  else  {
	Tcl_DecrRefCount(p->dataIndex);
	p->dataIndex = dataIndex;
        p->indexChanged = 1;
	Tcl_IncrRefCount(p->dataIndex);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetIndex --
 *
 *	Gets index information that is stored as part of cookfs archive
 *	metadata
 *
 * Results:
 *	Tcl_Obj storing index as bytearray
 *
 *	This is passed to Cookfs_FsindexFromObject()
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PagesGetIndex(Cookfs_Pages *p) {
    if (p->dataAsidePages != NULL) {
	return Cookfs_PagesGetIndex(p->dataAsidePages);
    }  else  {
	return p->dataIndex;
    }
}


/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesPageGetInt --
 *
 *	Get contents of specified page. This function does not use cache
 *	and always reads page data. It is used by Cookfs_PageGet() which
 *	also manages caching of pages.
 *
 * Results:
 *	Tcl_Obj with page information; NULL otherwise
 *	If non-NULL, returned object already has its reference counter
 *	incremented by 1 (i.e. should not eb incremented again if cached)
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *CookfsPagesPageGetInt(Cookfs_Pages *p, int index) {
    Tcl_Obj *buffer;
    Tcl_WideInt offset;
    int size;
    p->fileLastOp = COOKFS_LASTOP_READ;

    /* if specified index is an aside-index */
    if (COOKFS_PAGES_ISASIDE(index)) {
	CookfsLog(printf("Detected get request for add-aside pages - %08x", index))
	if (p->dataPagesIsAside) {
	    /* if this pages instance is the aside instance, remove the
	     * COOKFS_PAGES_ASIDE flag and proceed */
	    index = index & COOKFS_PAGES_MASK;
	    CookfsLog(printf("New index = %08x", index))
	}  else if (p->dataAsidePages != NULL) {
	    /* if this is not the aside instance, redirect to it */
	    CookfsLog(printf("Redirecting to add-aside pages object"))
	    return CookfsPagesPageGetInt(p->dataAsidePages, index);
	}  else  {
	    /* if no aside instance specified, return NULL */
	    CookfsLog(printf("No add-aside pages defined"))
	    return NULL;
	}
    }

    /* if index is larger than number of pages, fail */
    if (index >= p->dataNumPages) {
	CookfsLog(printf("GetInt failed: %d >= %d", index, p->dataNumPages))
	return NULL;
    }
    
    /* get offset and size */
    offset = CookfsPagesGetPageOffset(p, index);
    size = p->dataPagesSize[index];
    
    CookfsLog(printf("PageGet offset=%d size=%d", (int) offset, size))

    /* seek to specified place and read page */
    /* TODO: optimize by checking if last operation was a read of previous block */
    Tcl_Seek(p->fileChannel, offset, SEEK_SET);

    buffer = Cookfs_ReadPage(p, size);
    if (buffer == NULL) {
	CookfsLog(printf("Unable to read page"))
	return NULL;
    }
    
    return buffer;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesPageCacheMoveToTop --
 *
 *	Move specified entry in page cache to top of page cache
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsPagesPageCacheMoveToTop(Cookfs_Pages *p, int index) {
    Tcl_Obj *to;
    int ti;

    /* if index is 0, do not do anything */
    if (index == 0) {
	return;
    }

    /* get previous values at specified index */
    to = p->cachePageObj[index];
    ti = p->cachePageIdx[index];

    /* move entries from 0 to index-1 to next positions */
    memmove((void *) (p->cachePageIdx + 1), p->cachePageIdx, sizeof(int) * index);
    memmove((void *) (p->cachePageObj + 1), p->cachePageObj, sizeof(Tcl_Obj *) * index);
    
    /* set previous values located at index to position 0 */
    p->cachePageObj[0] = to;
    p->cachePageIdx[0] = ti;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsReadIndex --
 *
 *	Index contents:
 *	  (pagesMD5checksums)(pagesSizes)(indexBinaryData)(indexSuffix)
 *
 *	Page MD5 checksums - 16 bytes * number of pages; contains MD5
 *	  checksum stored as binary data (not hexadecimal)
 *
 *	Page sizes - 4 bytes * number of pages; sizes of each page
 *
 *	Index binary data - archive fsindex stored as binary data
 *	  (accessible via Cookfs_PagesGetIndex() and Cookfs_PagesSetIndex() API)
 *
 *	Index suffix - 16 bytes:
 *	  4 - size of index (compressed, bytes)
 *	  4 - number of pages
 *	  1 - default compression
 *	   7 - signature
 *
 * Results:
 *	non-zero value on success; 0 otherwise
 *
 * Side effects:
 *	May change various attributes in Cookfs_Pages structure
 *
 *----------------------------------------------------------------------
 */

int CookfsReadIndex(Cookfs_Pages *p) {
    unsigned char *bytes = NULL;
    int count = 0;
    int i;
    int pageCount = 0;
    int indexLength = 0;
    int pageCompression = 0;
    Tcl_WideInt fileSize = 0;
    Tcl_WideInt seekOffset = 0;
    Tcl_Obj *buffer = NULL;
    
    CookfsLog(printf("CookfsReadIndex 0 - %d", p->useFoffset))

    /* seek to beginning of suffix */
    if (p->useFoffset) {
	seekOffset = Tcl_Seek(p->fileChannel, p->foffset, SEEK_SET);
    }  else  {
	p->foffset = Tcl_Seek(p->fileChannel, 0, SEEK_END);
    }
    seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES, SEEK_CUR);

    /* if seeking fails, we assume no index exists */
    if (seekOffset < 0) {
	CookfsLog(printf("Unable to seek for index suffix"))
	return 0;
    }
    fileSize = seekOffset + COOKFS_SUFFIX_BYTES;
    CookfsLog(printf("Size=%d", ((int) fileSize)))
    
    /* read 16 bytes from end of cookfs archive */
    buffer = Tcl_NewObj();
    Tcl_IncrRefCount(buffer);
    count = Tcl_ReadChars(p->fileChannel, buffer, COOKFS_SUFFIX_BYTES, 0);
    if (count != COOKFS_SUFFIX_BYTES) {
	Tcl_DecrRefCount(buffer);
	CookfsLog(printf("Failed to read entire index tail: %d / %d", count, COOKFS_SUFFIX_BYTES))
	return 0;
    }
    bytes = Tcl_GetByteArrayFromObj(buffer, NULL);
    if (memcmp(bytes + 9, p->fileSignature, COOKFS_SIGNATURE_LENGTH) != 0) {
	Tcl_DecrRefCount(buffer);
	CookfsLog(printf("Invalid file signature found"))
	return 0;
    }
    
    /* get default compression, index length and number of pages */
    pageCompression = bytes[8] & 0xff;
    p->fileCompression = pageCompression;
    Cookfs_Binary2Int(bytes, &indexLength, 1);
    Cookfs_Binary2Int(bytes + 4, &pageCount, 1);
    CookfsLog(printf("Pages=%d; compression=%d", pageCount, pageCompression))
    Tcl_DecrRefCount(buffer);

    CookfsLog(printf("indexLength=%d pageCount=%d foffset=%d", indexLength, pageCount, p->useFoffset))

    /* read files index */
    
    /* seek to beginning of index, depending on if foffset was specified */
    if (p->useFoffset) {
	Tcl_Seek(p->fileChannel, p->foffset, SEEK_SET);
	seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - indexLength, SEEK_CUR);
    }  else  {
	seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - indexLength, SEEK_END);
    }
    CookfsLog(printf("IndexOffset Read = %d", (int) seekOffset))
    
    buffer = Cookfs_ReadPage(p, indexLength);
    
    if (buffer == NULL) {
	CookfsLog(printf("Unable to read index"))
	return 0;
    }

    /* read page MD5 checksums and pages */
    p->dataIndex = buffer;
    Tcl_IncrRefCount(p->dataIndex);

    /* seek to beginning of data, depending on if foffset was specified */
    if (p->useFoffset) {
	Tcl_Seek(p->fileChannel, p->foffset, SEEK_SET);
	seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - (pageCount * 20) - indexLength, SEEK_CUR);
    }  else  {
	seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - (pageCount * 20) - indexLength, SEEK_END);
    }

    /* if seeking fails, we assume no suffix exists */
    if (seekOffset < 0) {
	CookfsLog(printf("Unable to seek for reading page sizes"))
	return 0;
    }
    
    /* extend pages buffer if needed */
    CookfsPagesPageExtendIfNeeded(p, pageCount);

    /* read MD5 checksums */
    buffer = Tcl_NewObj();
    Tcl_IncrRefCount(buffer);
    count = Tcl_ReadChars(p->fileChannel, buffer, 16 * pageCount, 0);
    bytes = Tcl_GetByteArrayFromObj(buffer, NULL);
    memcpy(p->dataPagesMD5, bytes, 16 * pageCount);
    Tcl_DecrRefCount(buffer);
    
    /* read page sizes */
    buffer = Tcl_NewObj();
    Tcl_IncrRefCount(buffer);
    count = Tcl_ReadChars(p->fileChannel, buffer, 4 * pageCount, 0);
    if (count != (4 * pageCount)) {
	Tcl_DecrRefCount(buffer);
	CookfsLog(printf("Failed to read page buffer"))
	return 0;
    }
    bytes = Tcl_GetByteArrayFromObj(buffer, NULL);
    Cookfs_Binary2Int(bytes, p->dataPagesSize, pageCount);
    Tcl_DecrRefCount(buffer);

    /* set this to 0 so we can calculate actual size of all pages */
    p->dataInitialOffset = 0;
    p->dataNumPages = pageCount;

    /* calculate size of all pages by requesting offset for page after the last existing page */
    p->dataAllPagesSize = CookfsPagesGetPageOffset(p, pageCount);
    
    /* calculate offset from data - offset to end of archive
     * deducted by all index elements size and size of all pages */
    p->dataInitialOffset = fileSize - 
	(COOKFS_SUFFIX_BYTES + p->dataAllPagesSize + (p->dataNumPages * 20) + indexLength);

    CookfsLog(printf("Pages size=%d offset=%d", (int) p->dataAllPagesSize, (int) p->dataInitialOffset))
    for (i = 0; i < pageCount; i++) {
	CookfsLog(printf("Offset %d is %d", i, (int) CookfsPagesGetPageOffset(p, i)))
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesPageExtendIfNeeded --
 *
 *	Reallocate dataPagesSize and dataPagesMD5 to fit count number
 *	of pages; reallocation is only made if current number of memory
 *	is smaller than count
 *
 * Results:
 *	None
 *
 * Side effects:
 *	dataPagesSize and dataPagesMD5 might be moved to new location(s)
 *
 *----------------------------------------------------------------------
 */

static void CookfsPagesPageExtendIfNeeded(Cookfs_Pages *p, int count) {
    int changed = 0;
    CookfsLog(printf("CookfsPagesPageExtendIfNeeded(%d vs %d)", p->dataPagesDataSize, count))
    
    /* find new data size that fits required number of pages */
    while (p->dataPagesDataSize < count) {
	changed = 1;
	p->dataPagesDataSize += p->dataPagesDataSize;
    }
    
    /* if changed, reallocate both structures */
    CookfsLog(printf("CookfsPagesPageExtendIfNeeded(%d vs %d) -> %d", p->dataPagesDataSize, count, changed))
    if (changed) {
	p->dataPagesSize = (int *) Tcl_Realloc((void *) p->dataPagesSize,
	    p->dataPagesDataSize * sizeof(int));
	p->dataPagesMD5 = (unsigned char *) Tcl_Realloc((void *) p->dataPagesMD5,
	    p->dataPagesDataSize * 16);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesGetPageOffset --
 *
 *	Calculate offset of a page from start of file
 *	(not start of cookfs archive)
 *
 * Results:
 *	File offset to beginning of a page
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Tcl_WideInt CookfsPagesGetPageOffset(Cookfs_Pages *p, int idx) {
    /* TODO: optimize by cache'ing each N-th entry and start from there */
    int i;
    Tcl_WideInt rc = p->dataInitialOffset;
    for (i = 0; i < idx; i++) {
	rc += p->dataPagesSize[i];
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsTruncateFileIfNeeded --
 *
 *	Truncate pages file if needed
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsTruncateFileIfNeeded(Cookfs_Pages *p, Tcl_WideInt targetOffset) {
#ifdef USE_TCL_TRUNCATE
    if (p->shouldTruncate == 1) {
	/* TODO: only truncate if current size is larger than what it should be */
	Tcl_TruncateChannel(p->fileChannel, targetOffset);
	/* TODO: truncate is still possible using ftruncate() */
	p->shouldTruncate = 0;
	CookfsLog(printf("Truncating to %d", (int) targetOffset));
    }
#endif
}

