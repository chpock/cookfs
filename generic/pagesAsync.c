/*
 * pagesAsync.c
 *
 * Provides functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pagesAsync.h"
#include "pagesInt.h"
#include "pagesCompr.h"

static Tcl_Obj *CookfsRunAsyncCompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);
static Tcl_Obj *CookfsRunAsyncDecompressCommand(Cookfs_Pages *p, Tcl_Obj *cmd, int idx, Tcl_Obj *arg);

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
    if ((p->currentCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
	int i;
	for (i = 0 ; i < p->asyncPageSize ; i++) {
	    if (p->asyncPage[i].pageIdx == idx) {
		return Cookfs_PageObjNewFromByteArray(p->asyncPage[i].pageContents);;
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
    if ((p->currentCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
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
    if ((p->currentCompression == COOKFS_COMPRESSION_CUSTOM) && (p->asyncCompressCommandPtr != NULL) && (p->asyncCompressCommandLen > 3)) {
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
	    Cookfs_WriteTclObj(p, idx, p->asyncPage[0].pageContents, resObj);
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
    CookfsLog(printf("index [%d]", idx))
    if ((p->asyncDecompressQueueSize > 0) && (p->asyncDecompressCommandPtr != NULL) && (p->asyncDecompressCommandLen > 3)) {
	Tcl_Obj *dataObj;
	int i;

	for (i = 0 ; i < p->asyncDecompressQueue ; i++) {
	    if (p->asyncDecompressIdx[i] == idx) {
		CookfsLog(printf("return 1 - Page %d already in async decompress queue", i))
		return 1;
	    }
	}

	/* don't modify page weight in cache, as here we are just checking if it is already loaded */
	if (Cookfs_PageCacheGet(p, idx, 0, 0) != NULL) {
	    // page already in cache and we just moved it to top; do nothing
	    CookfsLog(printf("return 1 - Page already in cache and we just moved it to top"))
	    return 1;
	}

	// if queue is full, do not preload
	if (p->asyncDecompressQueue >= p->asyncDecompressQueueSize) {
	    CookfsLog(printf("return 0 - Queue is full, do not preload"))
	    return 0;
	}

	CookfsLog(printf("Reading page %d for async decompress", idx))
	// TODO: do something with possible error message
	Cookfs_PageObj dataPageObj = Cookfs_ReadPage(p,
	    Cookfs_PagesGetPageOffset(p, idx),
	    Cookfs_PgIndexGetCompression(p->pagesIndex, idx),
	    Cookfs_PgIndexGetSizeCompressed(p->pagesIndex, idx),
	    Cookfs_PgIndexGetSizeUncompressed(p->pagesIndex, idx),
	    Cookfs_PgIndexGetHashMD5(p->pagesIndex, idx),
	    0, 0, NULL);
	if (dataPageObj == NULL) {
	    CookfsLog(printf("ERROR: Cookfs_ReadPage returned NULL, return 1"));
	    return 1;
	}

	dataObj = Cookfs_PageObjCopyAsByteArray(dataPageObj);
	Cookfs_PageObjDecrRefCount(dataPageObj);

	if (dataObj == NULL) {
	    CookfsLog(printf("ERROR: failed to convert Tcl_Obj->PageObj, return 1"));
	    return 1;
	}

	Tcl_IncrRefCount(dataObj);
	p->asyncDecompressIdx[p->asyncDecompressQueue] = idx;
	p->asyncDecompressQueue++;
	CookfsLog(printf("Adding page %d for async decompress", idx))
	CookfsRunAsyncDecompressCommand(p, p->asyncCommandProcess, idx, dataObj);
	Tcl_DecrRefCount(dataObj);

	CookfsLog(printf("return 1"))
	return 1;
    }
    CookfsLog(printf("return 0"))
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

	CookfsLog(printf("calling callback"))

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

	    CookfsLog(printf("callback returned data for %d", i))
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

	    CookfsLog(printf("cleaning up decompression queue"))
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

	    CookfsLog(printf("cleaning up decompression queue done"))

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
