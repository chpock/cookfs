/* (c) 2010 Pawel Salawa */

#include "cookfs.h"

#define COOKFS_SUFFIX_BYTES 16


const char *cookfsCompressionOptions[] = {
    "none",
    "zlib",
    NULL
};

static Tcl_Interp *GetThreadInterp(void);
static Tcl_Obj *PageGetInt(Cookfs_Pages *p, int index);
static void PageCacheMoveToTop(Cookfs_Pages *p, int index);
static void CookfsPageExtendIfNeeded(Cookfs_Pages *p, int count);
static Tcl_WideInt CookfsGetOffset(Cookfs_Pages *p, int idx);
static int CookfsCompressPage(Cookfs_Pages *p, Tcl_Obj *data);
static Tcl_Obj *CookfsDecompressPage(Cookfs_Pages *p, int size);

static Tcl_Interp *GetThreadInterp(void) {
    /* TODO: make it thread safe */
    static Tcl_Interp *interp = NULL;
    if (interp == NULL) {
        interp = Tcl_CreateInterp();
    }
    return interp;
}

Cookfs_Pages *Cookfs_PagesInit(Tcl_Obj *fileName, int fileReadOnly, int fileCompression, char *fileSignature) {
    Cookfs_Pages *rc = (Cookfs_Pages *) Tcl_Alloc(sizeof(Cookfs_Pages));
    int i;

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
    rc->dataPagesDataSize = 1024;
    rc->dataPagesSize = (int *) Tcl_Alloc(rc->dataPagesDataSize * sizeof(int));
    rc->dataPagesMD5 = (unsigned char *) Tcl_Alloc(rc->dataPagesDataSize * 16);

    CookfsLog(printf("Opening file %s as %s with compression %d", Tcl_GetStringFromObj(fileName, NULL), (rc->fileReadOnly ? "r" : "a+"), fileCompression))

    rc->dataIndex = Tcl_NewStringObj("", 0);
    Tcl_IncrRefCount(rc->dataIndex);
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
    
    /* initialize cache */
    for (i = 0; i < COOKFS_MAX_CACHE_PAGES; i++) {
        rc->cachePageIdx[i] = -1;
        rc->cachePageObj[i] = NULL;
    }
    rc->cacheSize = 0;

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
            indexSize = CookfsCompressPage(p, p->dataIndex);
            if (indexSize < 0) {
                Tcl_Panic("Unable to compress index");
            }

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
        Tcl_Close(NULL, p->fileChannel);
    }
    
    /* clean up cache */
    for (i = 0; i < p->cacheSize; i++) {
        if (p->cachePageObj[i] != NULL) {
            Tcl_DecrRefCount(p->cachePageObj[i]);
        }
    }

    /* clean up index */
    Tcl_DecrRefCount(p->dataIndex);

    /* clean up pages data */
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
    for (idx = 0; idx < p->dataNumPages; idx++, pageMD5 += 16) {
        if (memcmp(pageMD5, md5sum, 16) == 0) {
            return idx;
        }
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

    dataSize = CookfsCompressPage(p, dataObj);
    if (dataSize < 0) {
        Tcl_Panic("Unable to compress page");
    }
    p->fileLastOp = COOKFS_LASTOP_WRITE;
    
    p->dataPagesSize[idx] = dataSize;
    if (p->indexUptodate) {
        /* only truncate if index might have been there */
        p->indexUptodate = 0;
        Tcl_TruncateChannel(p->fileChannel, offset + dataSize);
        CookfsLog(printf("Truncating to %d",(int) (offset + dataSize)))
    }

    // Tcl_MutexUnlock(&p->pagesLock);
    
    return idx;
}

static Tcl_Obj *PageGetInt(Cookfs_Pages *p, int index) {
    Tcl_Obj *buffer;
    Tcl_WideInt offset;
    int size;
    p->fileLastOp = COOKFS_LASTOP_READ;
    
    if (index >= p->dataNumPages) {
        return NULL;
    }
    
    offset = CookfsGetOffset(p, index);
    size = p->dataPagesSize[index];
    
    CookfsLog(printf("PageGet offset=%d size=%d", (int) offset, size))

    /* TODO: optimize by checking if last operation was a read of previous block */
    Tcl_Seek(p->fileChannel, offset, SEEK_SET);

    buffer = CookfsDecompressPage(p, size);
    if (buffer == NULL) {
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
        return PageGetInt(p, index);
    }
    
    for (i = 0; i < p->cacheSize; i++) {
        if (p->cachePageIdx[i] == index) {
            rc = p->cachePageObj[i];
            if (i > 0) {
                PageCacheMoveToTop(p, i);
            }
            return rc;
        }
        
    }

    rc = PageGetInt(p, index);
    Tcl_IncrRefCount(rc);

    newIdx = p->cacheSize - 1;

    if (p->cachePageObj[newIdx] != NULL) {
        Tcl_DecrRefCount(p->cachePageObj[newIdx]);
    }
    p->cachePageIdx[newIdx] = index;
    p->cachePageObj[newIdx] = rc;
    PageCacheMoveToTop(p, newIdx);

    return rc;
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
    
    CookfsLog(printf("CookfsReadIndex 0"))

    seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES, SEEK_END);
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

    CookfsLog(printf("indexLength=%d pageCount=%d", indexLength, pageCount))

    /* read files index */
    seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - indexLength, SEEK_END);
    
    buffer = CookfsDecompressPage(p, indexLength);
    
    if (buffer == NULL) {
        CookfsLog(printf("Unable to read index"))
        return 0;
    }

    p->dataIndex = buffer;
    Tcl_IncrRefCount(p->dataIndex);

    /* read page sizes */
    seekOffset = Tcl_Seek(p->fileChannel, -COOKFS_SUFFIX_BYTES - (pageCount * 20) - indexLength, SEEK_END);

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
        p->dataPagesMD5 = (unsigned char *) Tcl_Realloc((void *) p->dataPagesSize,
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

static int CookfsCompressPage(Cookfs_Pages *p, Tcl_Obj *data) {
    unsigned char *bytes;
    int size;
    Tcl_IncrRefCount(data);
    bytes = Tcl_GetByteArrayFromObj(data, &size);
    if (size > 0) {
        switch (p->fileCompression) {
            case cookfsCompressionNone: {
                CookfsLog(printf("CookfsCompressPage writing %d byte(s) as raw data", size))
                Tcl_WriteObj(p->fileChannel, data);
                break;
            }
            case cookfsCompressionZip: {
                Tcl_Interp *tinterp = GetThreadInterp();
                Tcl_Obj *cobj;

                Tcl_SetObjResult(tinterp, Tcl_NewObj());
                CookfsLog(printf("CookfsCompressPage before deflate"))
                if (Tcl_ZlibDeflate(tinterp, TCL_ZLIB_FORMAT_RAW, data, p->fileCompressionLevel, NULL) == TCL_ERROR) {
                    CookfsLog(printf("CookfsCompressPage deflate fail"))
                    return -1;
                }
                CookfsLog(printf("CookfsCompressPage after deflate"))

                cobj = Tcl_GetObjResult(tinterp);

                if (cobj == NULL) {
                    return -1;
                }

                Tcl_IncrRefCount(cobj);
                Tcl_GetByteArrayFromObj(cobj, &size);

                Tcl_WriteObj(p->fileChannel, cobj);
                Tcl_DecrRefCount(cobj);
                break;
            }
        }
        
    }
    Tcl_DecrRefCount(data);
    return size;
}

// static int CookfsDecompressPage(Cookfs_Pages *p, Tcl_Obj *data, int size) {
static Tcl_Obj *CookfsDecompressPage(Cookfs_Pages *p, int size) {
    int count;
    CookfsLog(printf("CookfsDecompressPage S=%d C=%d", size, p->fileCompression))
    if (size == 0) {
        return Tcl_NewByteArrayObj((unsigned char *) "", 0);
    }  else  {
        switch (p->fileCompression) {
            case cookfsCompressionNone: {
                Tcl_Obj *data;
                
                data = Tcl_NewObj();
                count = Tcl_ReadChars(p->fileChannel, data, size, 0);
                if (count != size) {
                    Tcl_IncrRefCount(data);
                    Tcl_DecrRefCount(data);
                    return NULL;
                }

                return data;
            }
            case cookfsCompressionZip: {
                Tcl_Obj *data;
                Tcl_Obj *cobj;
                Tcl_Interp *tinterp = GetThreadInterp();

                data = Tcl_NewObj();
                Tcl_IncrRefCount(data);
                count = Tcl_ReadChars(p->fileChannel, data, size, 0);

                if (count != size) {
                    Tcl_DecrRefCount(data);
                    return NULL;
                }

                Tcl_SetObjResult(tinterp, Tcl_NewObj());
                CookfsLog(printf("CookfsDecompressPage before inflate"))
                if (Tcl_ZlibInflate(tinterp, TCL_ZLIB_FORMAT_RAW, data, 0, NULL) == TCL_ERROR) {
                    CookfsLog(printf("CookfsDecompressPage inflate FAILED"))
                    return NULL;
                }
                CookfsLog(printf("CookfsDecompressPage after inflate"))

                Tcl_DecrRefCount(data);
                cobj = Tcl_GetObjResult(tinterp);

                return cobj;
                break;
            }
        }
    }
    return NULL;
}
