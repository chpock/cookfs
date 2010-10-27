/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#include "cookfs.h"

#define COOKFS_SUFFIX_BYTES 16

static Tcl_Obj *PageGetInt(Cookfs_Pages *p, int index);
static void PageCacheMoveToTop(Cookfs_Pages *p, int index);
static void CookfsPageExtendIfNeeded(Cookfs_Pages *p, int count);
static Tcl_WideInt CookfsGetOffset(Cookfs_Pages *p, int idx);

Cookfs_Pages *Cookfs_PagesGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;
    
    /* TODO: verify command suffix etc */

    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
	return NULL;
    }

    return (Cookfs_Pages *) (cmdInfo.objClientData);
}

Cookfs_Pages *Cookfs_PagesInit(Tcl_Interp *interp, Tcl_Obj *fileName, int fileReadOnly, int fileCompression, char *fileSignature, int useFoffset, Tcl_WideInt foffset, int isAside, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand) {
    Cookfs_Pages *rc = (Cookfs_Pages *) Tcl_Alloc(sizeof(Cookfs_Pages));
    int i;

    rc->interp = interp;
    rc->isAside = isAside;
    Cookfs_PagesInitCompr(rc);

    if (Cookfs_SetCompressCommands(rc, compressCommand, decompressCommand) != TCL_OK) {
	Tcl_Free((void *) rc);
	return NULL;
    }

    rc->useFoffset = useFoffset;
    rc->foffset = foffset;
    rc->fileReadOnly = fileReadOnly;
    rc->fileCompression = fileCompression;
    if (fileSignature != NULL)
        memcpy(rc->fileSignature, fileSignature, 7);
    else
        memcpy(rc->fileSignature, "CFS0002", 7);

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
    Tcl_IncrRefCount(rc->dataIndex);

    /* initialize cache */
    for (i = 0; i < COOKFS_MAX_CACHE_PAGES; i++) {
        rc->cachePageIdx[i] = -1;
        rc->cachePageObj[i] = NULL;
    }
    rc->cacheSize = 0;

    CookfsLog(printf("Opening file %s as %s with compression %d", Tcl_GetStringFromObj(fileName, NULL), (rc->fileReadOnly ? "r" : "a+"), fileCompression))

    rc->fileChannel = Tcl_FSOpenFileChannel(NULL, fileName,
        (rc->fileReadOnly ? "r" : "a+"), 0666);
    
    if (rc->fileChannel == NULL) {
        Cookfs_PagesFini(rc);
        return NULL;
    }
    
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
            rc->indexUptodate = 1;
            Cookfs_PagesFini(rc);
            return NULL;
        }  else  {
            rc->dataInitialOffset = Tcl_Seek(rc->fileChannel, 0, SEEK_END);
            rc->dataAllPagesSize = 0;
            rc->dataNumPages = 0;
            rc->indexUptodate = 0;
        }
        CookfsLog(printf("Index not read!"))
    }
    
    /* force compression since we want to use target compression anyway */
    if (!rc->fileReadOnly) {
	rc->fileCompression = fileCompression;
	rc->fileCompressionLevel = 9;
    }

    CookfsLog(printf("Opening file %s - compression %d", Tcl_GetStringFromObj(fileName, NULL), rc->fileCompression))

    return rc;
}

void Cookfs_PagesFini(Cookfs_Pages *p) {
    Tcl_Obj *obj;
    int i;

    if (p->fileChannel != NULL) {
        CookfsLog(printf("Index up to date = %d", p->indexUptodate))
        /* if changes were made, save them to disk */
        if (!p->indexUptodate) {
            int indexSize;
            unsigned char buf[COOKFS_SUFFIX_BYTES];
            unsigned char *bufSizes;
            Tcl_WideInt offset;

            CookfsLog(printf("Writing index"))
            offset = CookfsGetOffset(p, p->dataNumPages);
 
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

            CookfsLog(printf("Offset write: %d", (int) Tcl_Seek(p->fileChannel, 0, SEEK_CUR)))

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
        }
        /* close file channel */
        CookfsLog(printf("Closing channel"))
        Tcl_Close(NULL, p->fileChannel);
    }

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
    Tcl_Free((void *) p);
}

int Cookfs_PageAdd(Cookfs_Pages *p, Tcl_Obj *dataObj) {
    int idx;
    int dataSize;
    Tcl_WideInt offset;
    unsigned char md5sum[16];
    unsigned char *bytes;
    int objLength;
    unsigned char *pageMD5 = p->dataPagesMD5;

    bytes = Tcl_GetByteArrayFromObj(dataObj, &objLength);
    Cookfs_MD5(bytes, objLength, md5sum);

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

	    Tcl_IncrRefCount(otherPageData);
	    otherBytes = Tcl_GetByteArrayFromObj(otherPageData, &otherObjLength);

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

    if (p->dataAsidePages != NULL) {
        CookfsLog(printf("Cookfs_PageAdd: Sending add command to asidePages"))
        return Cookfs_PageAdd(p->dataAsidePages, dataObj);
    }

    if (p->fileReadOnly) {
        return -1;
    }

    idx = p->dataNumPages;
    (p->dataNumPages)++;

    /* reallocate list of page offsets if exceeded */
    CookfsPageExtendIfNeeded(p, p->dataNumPages);
    offset = CookfsGetOffset(p, idx);

    if (p->fileLastOp != COOKFS_LASTOP_WRITE) {
        /* TODO: optimize by checking if last operation was a write */
        Tcl_Seek(p->fileChannel, offset, SEEK_SET);
        CookfsLog(printf("Seeking to EOF -> %d",(int) offset))
    }

    memcpy(p->dataPagesMD5 + (idx * 16), md5sum, 16);

    CookfsLog(printf("MD5sum is %08x%08x%08x%08x\n", ((int *) md5sum)[0], ((int *) md5sum)[1], ((int *) md5sum)[2], ((int *) md5sum)[3]))

    dataSize = Cookfs_WritePage(p, dataObj);
    if (dataSize < 0) {
        CookfsLog(printf("Unable to compress page"))
        return -1;
    }
    p->fileLastOp = COOKFS_LASTOP_WRITE;
    
    p->dataPagesSize[idx] = dataSize;
    if (p->indexUptodate) {
        /* only truncate if index might have been there */
#ifdef USE_TCL_TRUNCATE
        Tcl_TruncateChannel(p->fileChannel, offset + dataSize);
#else
	/* TODO: truncate is still possible using ftruncate() */
#endif
        p->indexUptodate = 0;
        CookfsLog(printf("Truncating to %d",(int) (offset + dataSize)))
    }

    // Tcl_MutexUnlock(&p->pagesLock);

    if (p->dataPagesIsAside) {
        idx |= COOKFS_PAGES_ASIDE;
    }

    return idx;
}

static Tcl_Obj *PageGetInt(Cookfs_Pages *p, int index) {
    Tcl_Obj *buffer;
    Tcl_WideInt offset;
    int size;
    p->fileLastOp = COOKFS_LASTOP_READ;

    if (COOKFS_PAGES_ISASIDE(index)) {
        CookfsLog(printf("Detected get request for add-aside pages - %08x", index))
        if (p->dataPagesIsAside) {
            index = index & COOKFS_PAGES_MASK;
            CookfsLog(printf("New index = %08x", index))
        }  else if (p->dataAsidePages != NULL) {
            CookfsLog(printf("Redirecting to add-aside pages object"))
            return PageGetInt(p->dataAsidePages, index);
        }  else  {
            CookfsLog(printf("No add-aside pages defined"))
            return NULL;
        }
    }

    if (index >= p->dataNumPages) {
        CookfsLog(printf("GetInt failed: %d >= %d", index, p->dataNumPages))
        return NULL;
    }
    
    offset = CookfsGetOffset(p, index);
    size = p->dataPagesSize[index];
    
    CookfsLog(printf("PageGet offset=%d size=%d", (int) offset, size))

    /* TODO: optimize by checking if last operation was a read of previous block */
    Tcl_Seek(p->fileChannel, offset, SEEK_SET);

    buffer = Cookfs_ReadPage(p, size);
    if (buffer == NULL) {
        CookfsLog(printf("Unable to read page"))
        return NULL;
    }
    
    return buffer;
}

static void PageCacheMoveToTop(Cookfs_Pages *p, int index) {
    Tcl_Obj *to;
    int ti;

    if (index == 0) {
        return;
    }

    to = p->cachePageObj[index];
    ti = p->cachePageIdx[index];
    memmove((void *) (p->cachePageIdx + 1), p->cachePageIdx, sizeof(int) * index);
    memmove((void *) (p->cachePageObj + 1), p->cachePageObj, sizeof(Tcl_Obj *) * index);
    p->cachePageObj[0] = to;
    p->cachePageIdx[0] = ti;
}

Tcl_Obj *Cookfs_PageGet(Cookfs_Pages *p, int index) {
    Tcl_Obj *rc;
    int newIdx;
    int i;

    if (p->cacheSize <= 0) {
        rc = PageGetInt(p, index);
        CookfsLog(printf("Returning directly [%s]", rc == NULL ? "NULL" : "SET"))
        return rc;
    }
    
    for (i = 0; i < p->cacheSize; i++) {
        if (p->cachePageIdx[i] == index) {
            rc = p->cachePageObj[i];
            if (i > 0) {
                PageCacheMoveToTop(p, i);
            }
            CookfsLog(printf("Returning from cache [%s]", rc == NULL ? "NULL" : "SET"))
            return rc;
        }
        
    }

    rc = PageGetInt(p, index);
    CookfsLog(printf("Returning and caching [%s]", rc == NULL ? "NULL" : "SET"))
    if (rc == NULL) {
        return NULL;
    }

    newIdx = p->cacheSize - 1;

    if (p->cachePageObj[newIdx] != NULL) {
        Tcl_DecrRefCount(p->cachePageObj[newIdx]);
    }
    p->cachePageIdx[newIdx] = index;
    p->cachePageObj[newIdx] = rc;
    PageCacheMoveToTop(p, newIdx);

    return rc;
}

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

Tcl_Obj *Cookfs_PageGetHeadMD5(Cookfs_Pages *p) {
    return Cookfs_MD5FromObj(Cookfs_PageGetHead(p));
}

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

Tcl_Obj *Cookfs_PageGetTailMD5(Cookfs_Pages *p) {
    return Cookfs_MD5FromObj(Cookfs_PageGetTail(p));
}


/* Index */

/*
 *  4 - number of pages
 *  1 - compression
 *  7 - signature
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

    if (p->useFoffset) {
        seekOffset = Tcl_Seek(p->fileChannel, p->foffset, SEEK_SET);
        seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES, SEEK_CUR);
    }  else  {
        seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES, SEEK_END);
    }

    /* if seeking fails, we assume no index exists */
    if (seekOffset < 0) {
        CookfsLog(printf("Unable to seek for index suffix"))
        return 0;
    }
    fileSize = seekOffset + COOKFS_SUFFIX_BYTES;
    CookfsLog(printf("Size=%d", ((int) fileSize)))
    
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
    
    pageCompression = bytes[8] & 0xff;
    p->fileCompression = pageCompression;
    Cookfs_Binary2Int(bytes, &indexLength, 1);
    Cookfs_Binary2Int(bytes + 4, &pageCount, 1);
    CookfsLog(printf("Pages=%d; compression=%d", pageCount, pageCompression))
    Tcl_DecrRefCount(buffer);

    CookfsLog(printf("indexLength=%d pageCount=%d foffset=%d", indexLength, pageCount, p->useFoffset))

    /* read files index */
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

    p->dataIndex = buffer;
    Tcl_IncrRefCount(p->dataIndex);

    /* read page sizes */
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
    
    CookfsPageExtendIfNeeded(p, pageCount);

    buffer = Tcl_NewObj();
    Tcl_IncrRefCount(buffer);
    count = Tcl_ReadChars(p->fileChannel, buffer, 16 * pageCount, 0);
    bytes = Tcl_GetByteArrayFromObj(buffer, NULL);
    memcpy(p->dataPagesMD5, bytes, 16 * pageCount);
    Tcl_DecrRefCount(buffer);
    
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
    p->dataNumPages = pageCount;
    p->dataInitialOffset = 0;
    p->dataAllPagesSize = CookfsGetOffset(p, pageCount);
    p->dataInitialOffset = fileSize - 
        (COOKFS_SUFFIX_BYTES + p->dataAllPagesSize + (p->dataNumPages * 20) + indexLength);
        
    p->indexUptodate = 1;

    CookfsLog(printf("Pages size=%d offset=%d", (int) p->dataAllPagesSize, (int) p->dataInitialOffset))
    for (i = 0; i < pageCount; i++) {
        CookfsLog(printf("Offset %d is %d", i, (int) CookfsGetOffset(p, i)))
    }
    return 1;
}

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

void Cookfs_PagesSetIndex(Cookfs_Pages *p, Tcl_Obj *dataIndex) {
    if (p->dataAsidePages != NULL) {
        Cookfs_PagesSetIndex(p->dataAsidePages, dataIndex);
    }  else  {
        Tcl_DecrRefCount(p->dataIndex);
        p->dataIndex = dataIndex;
        Tcl_IncrRefCount(p->dataIndex);
    }
}

Tcl_Obj *Cookfs_PagesGetIndex(Cookfs_Pages *p) {
    if (p->dataAsidePages != NULL) {
        return Cookfs_PagesGetIndex(p->dataAsidePages);
    }  else  {
        return p->dataIndex;
    }
}

static void CookfsPageExtendIfNeeded(Cookfs_Pages *p, int count) {
    int changed = 0;
    CookfsLog(printf("CookfsPageExtendIfNeeded(%d vs %d)", p->dataPagesDataSize, count))
    while (p->dataPagesDataSize < count) {
        changed = 1;
        p->dataPagesDataSize += p->dataPagesDataSize;
    }
    CookfsLog(printf("CookfsPageExtendIfNeeded(%d vs %d) -> %d", p->dataPagesDataSize, count, changed))
    if (changed) {
        p->dataPagesSize = (int *) Tcl_Realloc((void *) p->dataPagesSize,
            p->dataPagesDataSize * sizeof(int));
        p->dataPagesMD5 = (unsigned char *) Tcl_Realloc((void *) p->dataPagesMD5,
            p->dataPagesDataSize * 16);
    }
}

static Tcl_WideInt CookfsGetOffset(Cookfs_Pages *p, int idx) {
    /* TODO: optimize by cache'ing each N-th entry and start from there */
    int i;
    Tcl_WideInt rc = p->dataInitialOffset;
    for (i = 0; i < idx; i++) {
        rc += p->dataPagesSize[i];
    }
    return rc;
}


