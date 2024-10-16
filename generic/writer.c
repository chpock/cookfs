/*
 * vfs.c
 *
 * Provides implementation for writer
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "writer.h"
#include "writerInt.h"

typedef struct Cookfs_WriterPageMapEntry {

    int pageSize;
    int pageNum;
    int pageOffset;

    int is_md5_initialized;
    unsigned char md5[16];

    struct Cookfs_WriterPageMapEntry *nextByPage;
    struct Cookfs_WriterPageMapEntry *nextBySize;

} Cookfs_WriterPageMapEntry;

static void Cookfs_WriterFree(Cookfs_Writer *w);

int Cookfs_WriterLockRW(int isWrite, Cookfs_Writer *w, Tcl_Obj **err) {
    int ret = 1;
#ifdef TCL_THREADS
    CookfsLog(printf("try to %s lock...", isWrite ? "WRITE" : "READ"));
    if (isWrite) {
        ret = Cookfs_RWMutexLockWrite(w->mx);
    } else {
        ret = Cookfs_RWMutexLockRead(w->mx);
    }
    if (ret && w->isDead == 1) {
        // If object is terminated, don't allow everything.
        ret = 0;
        Cookfs_RWMutexUnlock(w->mx);
    }
    if (!ret) {
        CookfsLog(printf("FAILED to %s lock", isWrite ? "WRITE" : "READ"));
        if (err != NULL) {
            *err = Tcl_NewStringObj("stalled writer object detected", -1);
        }
    } else {
        CookfsLog(printf("ok - %s lock (%d)", isWrite ? "WRITE" : "READ",
            Cookfs_RWMutexGetLocks(w->mx)));
    }
#else
    UNUSED(isWrite);
    UNUSED(w);
    UNUSED(err);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_WriterUnlock(Cookfs_Writer *w) {
#ifdef TCL_THREADS
    Cookfs_RWMutexUnlock(w->mx);
    CookfsLog(printf("ok (%d)", Cookfs_RWMutexGetLocks(w->mx)));
#else
    UNUSED(w);
#endif /* TCL_THREADS */
    return 1;
}

int Cookfs_WriterLockSoft(Cookfs_Writer *w) {
    int ret = 1;
#ifdef TCL_THREADS
    Tcl_MutexLock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    if (w->isDead) {
        ret = 0;
    } else {
        w->lockSoft++;
    }
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_WriterUnlockSoft(Cookfs_Writer *w) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    assert(w->lockSoft > 0);
    w->lockSoft--;
    if (w->isDead == 1) {
        Cookfs_WriterFree(w);
    } else {
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    }
    return 1;
}

void Cookfs_WriterLockExclusive(Cookfs_Writer *w) {
#ifdef TCL_THREADS
    CookfsLog(printf("try to lock exclusive..."));
    Cookfs_RWMutexLockExclusive(w->mx);
    CookfsLog(printf("ok"));
#else
    UNUSED(w);
#endif /* TCL_THREADS */
}

static Cookfs_WriterBuffer *Cookfs_WriterWriterBufferAlloc(
    Cookfs_PathObj *pathObj, Tcl_WideInt mtime)
{
    Cookfs_WriterBuffer *wb = ckalloc(sizeof(Cookfs_WriterBuffer));
    if (wb != NULL) {
        wb->buffer = NULL;
        wb->bufferSize = 0;
        wb->mtime = mtime;
        wb->pathObj = pathObj;
        Cookfs_PathObjIncrRefCount(pathObj);
        wb->entry = NULL;
        wb->sortKey = NULL;
        wb->next = NULL;
    }
    CookfsLog(printf("buffer [%p]", (void *)wb));
    return wb;
}

static void Cookfs_WriterWriterBufferFree(Cookfs_WriterBuffer *wb) {
    CookfsLog(printf("buffer [%p]", (void *)wb));
    if (wb->buffer != NULL) {
        ckfree(wb->buffer);
    }
    if (wb->pathObj != NULL) {
        Cookfs_PathObjDecrRefCount(wb->pathObj);
    }
    if (wb->sortKey != NULL) {
        Cookfs_PathObjDecrRefCount(wb->sortKey);
    }
    ckfree(wb);
}

Cookfs_Writer *Cookfs_WriterInit(Tcl_Interp* interp,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Tcl_WideInt smallfilebuffer, Tcl_WideInt smallfilesize,
    Tcl_WideInt pagesize, int writetomemory)
{

    CookfsLog(printf("init mount in interp [%p]; pages:%p index:%p smbuf:%"
        TCL_LL_MODIFIER "d sms:%" TCL_LL_MODIFIER "d pagesize:%"
        TCL_LL_MODIFIER "d writetomem:%d", (void *)interp, (void *)pages,
        (void *)index, smallfilebuffer, smallfilesize, pagesize,
        writetomemory));

    if (interp == NULL || (pages == NULL && !writetomemory) || index == NULL) {
        CookfsLog(printf("failed, something is NULL"));
        return NULL;
    }

    Cookfs_Writer *w = (Cookfs_Writer *)ckalloc(sizeof(Cookfs_Writer));
    memset(w, 0, sizeof(Cookfs_Writer));

    // Since we have cleared w, we don't need to set NULL / null values.
    // However, let's leave these values here as comments so that we have
    // a clear idea of the default values.

    // w->commandToken = NULL;
    w->interp = interp;
    // w->fatalError = 0;
    // w->isDead = 0;
    // w->lockSoft = 0;

#ifdef TCL_THREADS
    /* initialize thread locks */
    w->mx = Cookfs_RWMutexInit();
    w->threadId = Tcl_GetCurrentThread();
    // w->mxLockSoft = NULL;
#endif /* TCL_THREADS */

    w->pages = pages;
    if (pages != NULL) {
        Cookfs_PagesLockSoft(pages);
    }
    w->index = index;
    Cookfs_FsindexLockSoft(index);

    w->isWriteToMemory = writetomemory;
    w->smallFileSize = smallfilesize;
    w->maxBufferSize = smallfilebuffer;
    w->pageSize = pagesize;

    // w->bufferFirst = NULL;
    // w->bufferLast = NULL;
    // w->bufferSize = 0;
    // w->bufferCount = 0;

    // w->pageMapByPage = NULL;
    // w->pageMapBySize = NULL;

    CookfsLog(printf("ok [%p]", (void *)w));
    return w;

}

static void Cookfs_WriterFree(Cookfs_Writer *w) {
    CookfsLog(printf("Cleaning up writer"))
#ifdef TCL_THREADS
    CookfsLog(printf("Cleaning up thread locks"));
    Cookfs_RWMutexFini(w->mx);
    Tcl_MutexUnlock(&w->mxLockSoft);
    Tcl_MutexFinalize(&w->mxLockSoft);
#endif /* TCL_THREADS */
    /* clean up storage */
    ckfree((void *)w);
}

void Cookfs_WriterFini(Cookfs_Writer *w) {

    if (w == NULL) {
        CookfsLog(printf("ERROR: writer is NULL"));
        return;
    }

    if (w->isDead == 1) {
        return;
    }

    Cookfs_WriterLockExclusive(w);

    CookfsLog(printf("aquire mutex"));
    // By acquisition the lockSoft mutex, we will be sure that no other
    // thread calls Cookfs_WriterUnlockSoft() that can release this object
    // while this function is running.
#ifdef TCL_THREADS
    Tcl_MutexLock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    w->isDead = 1;

    CookfsLog(printf("enter [%p]", (void *)w));

    if (w->commandToken != NULL) {
        CookfsLog(printf("Cleaning tcl command"));
        Tcl_DeleteCommandFromToken(w->interp, w->commandToken);
    } else {
        CookfsLog(printf("No tcl command"));
    }

    CookfsLog(printf("free buffers"));
    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (wb != NULL) {
        Cookfs_WriterBuffer *next = wb->next;
        if (wb->entry != NULL) {
            Cookfs_FsindexEntryUnlock(wb->entry);
        }
        Cookfs_WriterWriterBufferFree(wb);
        wb = next;
    }

    CookfsLog(printf("free all"));
    Cookfs_FsindexUnlockSoft(w->index);
    if (w->pages != NULL) {
        Cookfs_PagesUnlockSoft(w->pages);
    }

    if (w->pageMapByPage != NULL) {

        CookfsLog(printf("free page map"));

        Tcl_HashSearch hSearch;

        for (Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(w->pageMapByPage, &hSearch);
            hPtr != NULL; hPtr = Tcl_NextHashEntry(&hSearch))
        {

            Cookfs_WriterPageMapEntry *pme =
                (Cookfs_WriterPageMapEntry *)Tcl_GetHashValue(hPtr);
            while(pme != NULL) {
                Cookfs_WriterPageMapEntry *next = pme->nextByPage;
                CookfsLog(printf("free pme: %p", (void *)pme));
                ckfree(pme);
                pme = next;
            }

        }

        Tcl_DeleteHashTable(w->pageMapByPage);
        ckfree(w->pageMapByPage);
        Tcl_DeleteHashTable(w->pageMapBySize);
        ckfree(w->pageMapBySize);

    }

    // Unlock writer now. It is possible that some threads are waiting for
    // read/write events. Let them go on and fail because of a dead object.
    Cookfs_WriterUnlock(w);

    if (w->lockSoft) {
        CookfsLog(printf("The writer object is soft-locked"))
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    } else {
        Cookfs_WriterFree(w);
    }

    return;
}

static Cookfs_WriterPageMapEntry *Cookfs_WriterPageMapEntryAlloc(int pageNum,
    int pageOffset, int pageSize)
{

    Cookfs_WriterPageMapEntry *pme =
        ckalloc(sizeof(Cookfs_WriterPageMapEntry));

    pme->pageNum = pageNum;
    pme->pageOffset = pageOffset;
    pme->pageSize = pageSize;
    pme->is_md5_initialized = 0;
    pme->nextByPage = NULL;
    pme->nextBySize = NULL;
    CookfsLog(printf("return: %p (pageNum: %d; pageOffset: %d; pageSize: %d)",
        (void *)pme, pageNum, pageOffset, pageSize));
    return pme;

}

static int Cookfs_WriterPageMapEntryIsExists(Cookfs_Writer *w,
    int pageNum, int pageOffset, int pageSize)
{

    // cppcheck-suppress redundantInitialization
    Tcl_HashEntry *hashEntry = Tcl_FindHashEntry(w->pageMapByPage,
        INT2PTR(pageNum));

    if (hashEntry == NULL) {
        return 0;
    }

    Cookfs_WriterPageMapEntry *pme =
        (Cookfs_WriterPageMapEntry *)Tcl_GetHashValue(hashEntry);

    for (; pme != NULL; pme = pme->nextByPage) {

        if (pme->pageOffset < pageOffset) {
            continue;
        }

        // Page map entries are sorted by offset when linked by page number.
        // Thus, if we find a page map entry with an offset greater than what
        // we are looking for, we can confidently say that such a page map
        //entry does not exist.
        if (pme->pageOffset > pageOffset) {
            return 0;
        }

        // If we are here, then we have found an entry on the page map with
        // an offset equal to the one we are looking for. Let's check its size.
        // If size is equal to the one we are looking for, then return true.

        if (pme->pageSize == pageSize) {
            return 1;
        }

        // If we are here, then we found a page map entry with equal offset,
        // but with wrong size. This should be impossible. Let's catch this
        // case with assert().
        assert(0 && "Cookfs_WriterPageMapEntryIsExists(): we are looking for"
            " PME of one size and offset, but we found another PME with"
            " the same offset, but with other size");

    }

    return 0;

}

static void Cookfs_WriterPageMapEntryAddBySize(Cookfs_Writer *w,
    Cookfs_WriterPageMapEntry *pme)
{

    int is_new;
    // cppcheck-suppress redundantInitialization
    Tcl_HashEntry *hPtr = Tcl_CreateHashEntry(w->pageMapBySize,
        INT2PTR(pme->pageSize), &is_new);
    if (is_new) {
        // CookfsLog(printf("new hash value by size"));
    } else {
        // CookfsLog(printf("add hash value by size"));
        pme->nextBySize = Tcl_GetHashValue(hPtr);
    }
    Tcl_SetHashValue(hPtr, pme);

}

static void Cookfs_WriterPageMapEntryAdd(Cookfs_Writer *w, int pageNum,
    int pageOffset, int pageSize)
{

    CookfsLog(printf("pageNum:%d pageOffset:%d pageSize:%d", pageNum,
        pageOffset, pageSize));

    if (Cookfs_WriterPageMapEntryIsExists(w, pageNum, pageOffset, pageSize)) {
        CookfsLog(printf("return: ok (the page map entry already exists)"));
    }

    Cookfs_WriterPageMapEntry *pme = Cookfs_WriterPageMapEntryAlloc(pageNum,
        pageOffset, pageSize);

    Cookfs_WriterPageMapEntryAddBySize(w, pme);

    int is_new;
    Tcl_HashEntry *hPtr;
    Cookfs_WriterPageMapEntry *current_pme;

    // We insert the new hash in pageMapByPage in a different way because we
    // want to keep the linked list in this hash map in pageOffset sorted
    // order. This will be useful for finding gaps in the map for
    // a particular page.

    // cppcheck-suppress redundantAssignment
    hPtr = Tcl_CreateHashEntry(w->pageMapByPage, INT2PTR(pageNum), &is_new);
    if (is_new) {
        // CookfsLog(printf("new hash value by page"));
        Tcl_SetHashValue(hPtr, pme);
    } else {
        // CookfsLog(printf("add hash value by page"));
        current_pme = Tcl_GetHashValue(hPtr);
        if (current_pme->pageOffset > pageOffset) {
            // CookfsLog(printf("insert value as a head because the first pme"
            //     " has offset: %d", current_pme->pageOffset));
            pme->nextByPage = current_pme;
            Tcl_SetHashValue(hPtr, pme);
        } else {
            // CookfsLog(printf("head pme has offset: %d",
            //     current_pme->pageOffset));
            while (current_pme->nextByPage != NULL &&
                current_pme->nextByPage->pageOffset < pageOffset)
            {
                current_pme = current_pme->nextByPage;
                // CookfsLog(printf("go to the next pme with offset %d",
                //     current_pme->pageOffset));
            }
            if (current_pme->nextByPage == NULL) {
                // CookfsLog(printf("insert value here as we have reached"
                //     " the end of the list"));
            } else {
                // CookfsLog(printf("insert value here"));
            }
            pme->nextByPage = current_pme->nextByPage;
            current_pme->nextByPage = pme;
        }
    }

}

static void Cookfs_WriterFillMap(Cookfs_FsindexEntry *e, ClientData clientData) {

    if (Cookfs_FsindexEntryIsDirectory(e)) {
        // CookfsLog(printf("return: entry [%s] is a directory", Cookfs_FsindexEntryGetFileName(e, NULL)));
        return;
    }

    if (Cookfs_FsindexEntryIsPending(e)) {
        // CookfsLog(printf("return: entry [%s] is pending", Cookfs_FsindexEntryGetFileName(e, NULL)));
        return;
    }

    if (Cookfs_FsindexEntryGetBlockCount(e) > 1) {
        // CookfsLog(printf("return: entry [%s] uses more than 1 block", Cookfs_FsindexEntryGetFileName(e, NULL)));
        return;
    }

    int pageNum, pageOffset, pageSize;
    Cookfs_FsindexEntryGetBlock(e, 0, &pageNum, &pageOffset, &pageSize);
    if (pageSize == 0) {
        // CookfsLog(printf("return: entry [%s] has zero size", Cookfs_FsindexEntryGetFileName(e, NULL)));
        return;
    }

    Cookfs_Writer *w = (Cookfs_Writer *)clientData;

    if (Cookfs_PagesIsEncrypted(w->pages, pageNum)) {
        // CookfsLog(printf("return: entry [%s] is for encrypted file", Cookfs_FsindexEntryGetFileName(e, NULL)));
        return;
    }

    CookfsLog(printf("new entry %p : [%s]", (void *)e, Cookfs_FsindexEntryGetFileName(e, NULL)));

    Cookfs_WriterPageMapEntryAdd(w, pageNum, pageOffset, pageSize);

}

static int Cookfs_WriterInitPageMap(Cookfs_Writer *w, Tcl_Obj **err) {

    if (!Cookfs_PagesLockRead(w->pages, NULL)) {
        CookfsLog(printf("failed to lock pages"));
        SET_ERROR_STR("failed to lock pages");
        return TCL_ERROR;
    };

    CookfsLog(printf("initialize page map hash tables"));

    w->pageMapByPage = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(w->pageMapByPage, TCL_ONE_WORD_KEYS);

    w->pageMapBySize = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(w->pageMapBySize, TCL_ONE_WORD_KEYS);

    Cookfs_FsindexForeach(w->index, Cookfs_WriterFillMap, (ClientData)w);
    Cookfs_PagesUnlock(w->pages);

    // Try to detect gaps in page maps

    Tcl_HashSearch hSearch;

    for (Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(w->pageMapByPage, &hSearch);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&hSearch))
    {

        int size, offset;
        Cookfs_WriterPageMapEntry *tmp, *pme =
            (Cookfs_WriterPageMapEntry *)Tcl_GetHashValue(hPtr);

        if (pme->pageOffset != 0) {

            size = pme->pageOffset;

            CookfsLog(printf("page %d: found a gap at beggining of page,"
                " size:%d", pme->pageNum, size));

            tmp = pme;
            pme = Cookfs_WriterPageMapEntryAlloc(pme->pageNum, 0, size);

            Cookfs_WriterPageMapEntryAddBySize(w, pme);

            pme->nextByPage = tmp;
            Tcl_SetHashValue(hPtr, pme);

        }

        for (; pme->nextByPage != NULL; pme = pme->nextByPage) {

            offset = pme->pageOffset + pme->pageSize;
            size = pme->nextByPage->pageOffset - offset;

            if (size == 0) {
                continue;
            }

            CookfsLog(printf("page %d: found a gap at offset %d, size:%d",
                pme->pageNum, offset, size));

            tmp = pme;
            pme = Cookfs_WriterPageMapEntryAlloc(pme->pageNum, offset, size);

            Cookfs_WriterPageMapEntryAddBySize(w, pme);

            pme->nextByPage = tmp->nextByPage;
            tmp->nextByPage = pme;

        }

        // If we are here, then pme is a pointer to the latest page map entry.
        // Let's check if we have something till the and of page.

        int pageSizeEntire = Cookfs_PagesGetPageSize(w->pages, pme->pageNum);

        // Check for error (-1) here. It is possible to get an error if page
        // index is from aside pages object that is not connected.
        if (pageSizeEntire != -1) {

            offset = pme->pageOffset + pme->pageSize;
            size = pageSizeEntire - offset;

            if (size != 0) {

                CookfsLog(printf("page %d: found a gap at the end, offset %d,"
                    " size:%d", pme->pageNum, offset, size));

                tmp = pme;
                pme = Cookfs_WriterPageMapEntryAlloc(pme->pageNum, offset, size);

                Cookfs_WriterPageMapEntryAddBySize(w, pme);

                tmp->nextByPage = pme;

            }

        }

    }

    CookfsLog(printf("return: ok"));
    return TCL_OK;

}

static void Cookfs_WriterPageMapInitializePage(Cookfs_Writer *w, int pageNum,
    unsigned char *pageDataBuffer)
{

    CookfsLog(printf("initialize hashes on pageNum:%d", pageNum));

    // cppcheck-suppress redundantInitialization
    Tcl_HashEntry *hashEntry = Tcl_FindHashEntry(w->pageMapByPage, INT2PTR(pageNum));
    Cookfs_WriterPageMapEntry *pme =
        (Cookfs_WriterPageMapEntry *)Tcl_GetHashValue(hashEntry);

    for (; pme != NULL; pme = pme->nextByPage) {

        Cookfs_MD5(&pageDataBuffer[pme->pageOffset], pme->pageSize, pme->md5);
        pme->is_md5_initialized = 1;

        CookfsLog(printf("pageOffset:%d pageSize:%d to md5:" PRINTF_MD5_FORMAT,
            pme->pageOffset, pme->pageSize, PRINTF_MD5_VAR(pme->md5)));

    }

}

static int Cookfs_WriterCheckDuplicate(Cookfs_Writer *w, void *buffer,
    Tcl_WideInt bufferSize, Cookfs_FsindexEntry *entry)
{

    int rc = 0;
    int is_pages_locked = 0;

    // cppcheck-suppress redundantInitialization
    Tcl_HashEntry *hashEntry = Tcl_FindHashEntry(w->pageMapBySize,
        INT2PTR((int)bufferSize));

    if (hashEntry == NULL) {
        CookfsLog(printf("return: no files of suitable size"));
        return 0;
    }

    unsigned char md5[MD5_DIGEST_SIZE];
    Cookfs_MD5(buffer, (Tcl_Size)bufferSize, md5);
    CookfsLog(printf("source file size %" TCL_LL_MODIFIER "d md5:"
        PRINTF_MD5_FORMAT, bufferSize, PRINTF_MD5_VAR(md5)));

    for (Cookfs_WriterPageMapEntry *pme = Tcl_GetHashValue(hashEntry);
        pme != NULL; pme = pme->nextBySize)
    {

        CookfsLog(printf("check file on pageNum:%d with pageOffset:%d",
            pme->pageNum, pme->pageOffset));

        // Here we first try to compare the md5 of the current file with
        // the md5 of the candidate duplicate, if known. If md5 doesn't match,
        // then we skip this duplicate candidate.
        //
        // We then attempt to load the page data from disk and directly
        // compare the current file with the candidate duplicate.
        // We do this even if the md5 hashes match because we don't really
        // trust this hash comparison and md5 collisions are possible.
        //
        // If we have successfully loaded the page data and the md5 hashes of
        // the files on the page are unknown, we save the md5 hashes on
        // the page so that we can compare other files against them in
        // the future.

        if (pme->is_md5_initialized) {

            // If we have md5 of duplicate candidate, then compare it first
            if (memcmp(md5, pme->md5, MD5_DIGEST_SIZE) != 0) {
                CookfsLog(printf("duplicate candidate has different md5 hash"));
                continue;
            }

            CookfsLog(printf("md5 matches, load the page"));

        } else {
            CookfsLog(printf("md5 is unknown, load the page"));
        }


        if (!is_pages_locked) {
            if (!Cookfs_PagesLockRead(w->pages, NULL)) {
                // We are not able to lock pages. Perhaps it is in terminating
                // state. Stop processing.
                goto done;
            }
            is_pages_locked = 1;
        };

        // use -1000 weight as it is temporary page and we don't really
        // need it in cache
        Cookfs_PageObj page = Cookfs_PageGet(w->pages, pme->pageNum,
            -1000, NULL);

        if (page == NULL) {
            CookfsLog(printf("failed to load the page, skip it"));
            continue;
        }

        if (!pme->is_md5_initialized) {
            CookfsLog(printf("need to update md5 for the chunks on the page"));
            Cookfs_WriterPageMapInitializePage(w, pme->pageNum, page->buf);
        }

        // Directly compare the current file
        int cmp = memcmp(&page->buf[pme->pageOffset], buffer, bufferSize);

        Cookfs_PageObjDecrRefCount(page);

        if (cmp == 0) {
            CookfsLog(printf("duplicate has been found"));
            Cookfs_FsindexEntrySetBlock(entry, 0, pme->pageNum, pme->pageOffset,
                bufferSize);
            rc = 1;
            goto done;
        }

        CookfsLog(printf("the duplicate candidate doesn't match"
            " the current file"));

    }

    CookfsLog(printf("return: no suitable files"));

done:
    if (is_pages_locked) {
        Cookfs_PagesUnlock(w->pages);
    }
    return rc;

}

static int Cookfs_WriterAddBufferToSmallFiles(Cookfs_Writer *w,
    Cookfs_PathObj *pathObj, Tcl_WideInt mtime, void *buffer,
    Tcl_WideInt bufferSize, Tcl_Obj **err)
{
    CookfsLog(printf("add buf [%p], size: %" TCL_LL_MODIFIER "d",
        buffer, bufferSize));

    CookfsLog(printf("alloc WriterBuffer"));
    Cookfs_WriterBuffer *wb = Cookfs_WriterWriterBufferAlloc(pathObj, mtime);
    if (wb == NULL) {
        CookfsLog(printf("failed to alloc"));
        SET_ERROR_STR("failed to alloc WriterBuffer");
        return TCL_ERROR;
    }

    CookfsLog(printf("create an entry in fsindex..."));
    if (!Cookfs_FsindexLockWrite(w->index, err)) {
        Cookfs_WriterWriterBufferFree(wb);
        return TCL_ERROR;
    };
    wb->entry = Cookfs_FsindexSet(w->index, pathObj, 1);
    if (wb->entry == NULL) {
        CookfsLog(printf("failed to create the entry"));
        SET_ERROR_STR("Unable to create entry");
        Cookfs_FsindexUnlock(w->index);
        Cookfs_WriterWriterBufferFree(wb);
        return TCL_ERROR;
    }
    Cookfs_FsindexEntryLock(wb->entry);

    CookfsLog(printf("set fsindex entry values"));
    Cookfs_FsindexEntrySetFileSize(wb->entry, bufferSize);
    Cookfs_FsindexEntrySetFileTime(wb->entry, mtime);

    // Here we check to see if encryption is active before checking for
    // duplicates. This is to ensure that we do not check for duplicates of
    // encrypted data (files) so as not to complicate the logic of
    // the de-duplicator.
    //
    // Looking at the code, it may seem that there is a critical error because
    // we do not lock pages. First we check the encryption status and then we
    // check for duplicates. Another thread may have enabled encryption before
    // we start checking for duplicates. And in that case, checking for
    // duplicates might succeed when it shouldn't (when encryption is in use).
    // But this scenario is impossible. To change the encryption status,
    // writer's buffer must be reset. And to reset the writer buffer - we need
    // to write-lock it. And if we are in this function, then write lock
    // is already acquired. So, as long as we are in this function - we don't
    // have to worry that the encryption mode can be changed by another thread.

    if (!w->isWriteToMemory &&
#ifdef COOKFS_USECCRYPTO
        !Cookfs_PagesIsEncryptionActive(w->pages) &&
#endif /* COOKFS_USECCRYPTO */
        Cookfs_WriterCheckDuplicate(w, buffer, bufferSize, wb->entry))
    {
        CookfsLog(printf("return: duplicate has been found"));
        // We must free the buffer, since the caller expects us to own it
        // in case of a successful return.
        ckfree(buffer);
        Cookfs_WriterWriterBufferFree(wb);
        Cookfs_FsindexUnlock(w->index);
        return TCL_OK;
    }

    Cookfs_FsindexEntrySetBlock(wb->entry, 0, -w->bufferCount - 1, 0,
        bufferSize);

    Cookfs_FsindexUnlock(w->index);

    CookfsLog(printf("set WritterBuffer values and add to the chain"));
    wb->buffer = buffer;
    wb->bufferSize = bufferSize;

    if (w->bufferFirst == NULL) {
        w->bufferFirst = wb;
    } else {
        w->bufferLast->next = wb;
    }

    w->bufferLast = wb;

    w->bufferCount++;
    w->bufferSize += bufferSize;

    CookfsLog(printf("currently have %d buffers, total size: %"
        TCL_LL_MODIFIER "d", w->bufferCount, w->bufferSize));
    CookfsLog(printf("ok"));
    return TCL_OK;
}

static Tcl_WideInt Cookfs_WriterReadChannel(char *buffer,
    Tcl_WideInt bufferSize, Tcl_Channel channel)
{

    CookfsLog(printf("want to read %" TCL_LL_MODIFIER "d bytes from"
        " channel %p", bufferSize, (void *)channel));

    Tcl_WideInt readSize = 0;

    while (readSize < bufferSize) {
        if (Tcl_Eof(channel)) {
            CookfsLog(printf("EOF reached"));
            break;
        }
        CookfsLog(printf("read bytes from the channel"));
        readSize += Tcl_Read(channel, buffer + readSize,
            bufferSize - readSize);
        CookfsLog(printf("got %" TCL_LL_MODIFIER "d bytes from the channel",
            readSize));
    }
    CookfsLog(printf("return %" TCL_LL_MODIFIER "d bytes from the channel",
        readSize));

    return readSize;

}

int Cookfs_WriterRemoveFile(Cookfs_Writer *w, Cookfs_FsindexEntry *entry) {
    Cookfs_WriterWantWrite(w);
    CookfsLog(printf("enter"));
    Cookfs_WriterBuffer *wbPrev = NULL;
    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (wb != NULL) {
        if (wb->entry == entry) {

            CookfsLog(printf("found the buffer to remove [%p]", (void *)wb));
            Cookfs_WriterBuffer *next = wb->next;
            if (wbPrev == NULL) {
                w->bufferFirst = next;
            } else {
                wbPrev->next = next;
            }
            if (w->bufferLast == wb) {
                w->bufferLast = wbPrev;
            }
            w->bufferCount--;
            w->bufferSize -= wb->bufferSize;
            Cookfs_FsindexEntryUnlock(entry);
            Cookfs_WriterWriterBufferFree(wb);

            // Shift block number for the following files and their entries
            while (next != NULL) {
                CookfsLog(printf("shift buffer number for buffer [%p]",
                    (void *)next));
                Cookfs_FsindexEntryIncrBlockPageIndex(next->entry, 0, 1);
                next = next->next;
            }

            return 1;
        }
        wbPrev = wb;
        wb = wb->next;
    }
    CookfsLog(printf("could not find the buffer to remove"));
    return 0;
}

#define DATA_FILE    (Tcl_Obj *)data
#define DATA_CHANNEL (Tcl_Channel)data
#define DATA_OBJECT  (Tcl_Obj *)data

int Cookfs_WriterAddFile(Cookfs_Writer *w, Cookfs_PathObj *pathObj,
    Cookfs_FsindexEntry *oldEntry, Cookfs_WriterDataSource dataType,
    void *data, Tcl_WideInt dataSize, Tcl_Obj **err)
{
    Cookfs_WriterWantWrite(w);
    CookfsLog(printf("enter [%p] [%s] size: %" TCL_LL_MODIFIER "d", data,
        dataType == COOKFS_WRITER_SOURCE_BUFFER ? "buffer" :
        dataType == COOKFS_WRITER_SOURCE_FILE ? "file" :
        dataType == COOKFS_WRITER_SOURCE_CHANNEL ? "channel" :
        dataType == COOKFS_WRITER_SOURCE_OBJECT ? "object" : "unknown",
        dataSize));

    // Check if a fatal error has occurred previously
    if (w->fatalError) {
        CookfsLog(printf("ERROR: writer in a fatal error state"));
        return TCL_ERROR;
    }

    int result = TCL_OK;
    void *readBuffer = NULL;
    Tcl_WideInt mtime = -1;
    Tcl_DString chanTranslation, chanEncoding;
    Cookfs_FsindexEntry *entry = NULL;

    // Check if we have the file in the small file buffer. We will try to get
    // the fsindex entry for this file and see if it is a pending file.
    if (!Cookfs_FsindexLockRead(w->index, err)) {
        return TCL_ERROR;
    };
    // Check the previous entry if it is inactive. Can be true if the file has
    // already been deleted. Don't write anything in this case. But free data
    // buffer if dataType is COOKFS_WRITER_SOURCE_BUFFER, because our caller
    // expects that buffer is owned by us now.
    if (oldEntry != NULL && Cookfs_FsindexEntryIsInactive(oldEntry)) {
        CookfsLog(printf("dead entry is detected, return ok without"
            " writing"));
        if (dataType == COOKFS_WRITER_SOURCE_BUFFER) {
            ckfree(data);
        }
        Cookfs_FsindexUnlock(w->index);
        return TCL_OK;
    }
    entry = Cookfs_FsindexGet(w->index, pathObj);
    if (entry != NULL) {
        CookfsLog(printf("an existing entry for the file was found"));
        if (Cookfs_FsindexEntryIsPending(entry)) {
            CookfsLog(printf("the entry is pending, remove it from small"
                " file buffer"));
            Cookfs_WriterRemoveFile(w, entry);
        } else {
            CookfsLog(printf("the entry is not pending"));
        }
        entry = NULL;
    }

    // We're populating the page map here because we need a read lock on
    // fsindex to do that, and this is one of the good places with
    // a locked fsindex. But do this only if writeToMemory is not enabled,
    // i.e. we are going to write the file to pages.
    if (!w->isWriteToMemory) {
        if (w->pageMapByPage == NULL) {
            CookfsLog(printf("page map is not initialized"));
            if (Cookfs_WriterInitPageMap(w, err) != TCL_OK) {
                return TCL_ERROR;
            }
        } else {
            CookfsLog(printf("page map has already been initialized"));
        }
    }

    Cookfs_FsindexUnlock(w->index);

    switch (dataType) {

    case COOKFS_WRITER_SOURCE_BUFFER:

        // No need to do anything. It is here to simply avoid the compiler warning.

        break;

    case COOKFS_WRITER_SOURCE_FILE:

        CookfsLog(printf("alloc statbuf"));

        Tcl_StatBuf *sb = Tcl_AllocStatBuf();
        if (sb == NULL) {
            SET_ERROR_STR("could not alloc statbuf");
            return TCL_ERROR;
        }

        CookfsLog(printf("get file stat for [%s]", Tcl_GetString(DATA_FILE)));
        if (Tcl_FSStat(DATA_FILE, sb) != TCL_OK) {
            CookfsLog(printf("failed, return error"));
            ckfree(sb);
            SET_ERROR_STR("could get stat for the file");
            return TCL_ERROR;
        }

        if (dataSize < 0) {
            dataSize = Tcl_GetSizeFromStat(sb);
            CookfsLog(printf("got file size: %" TCL_LL_MODIFIER "d",
                dataSize));
        } else {
            CookfsLog(printf("use specified size"));
        }

        mtime = Tcl_GetModificationTimeFromStat(sb);
        CookfsLog(printf("got mtime from the file: %" TCL_LL_MODIFIER "d",
            mtime));

        ckfree(sb);

        CookfsLog(printf("open the file"));
        data = (void *)Tcl_FSOpenFileChannel(NULL, DATA_FILE, "rb", 0);
        if (data == NULL) {
            CookfsLog(printf("failed to open the file"));
            SET_ERROR_STR("could not open the file");
            return TCL_ERROR;
        }

        // Let's believe that "rb" for file open mode worked
        //Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-translation", "binary");

        break;

    case COOKFS_WRITER_SOURCE_CHANNEL:

        if (dataSize < 0) {
            CookfsLog(printf("get datasize from the channel"));
            Tcl_WideInt pos = Tcl_Tell(DATA_CHANNEL);
            dataSize = Tcl_Seek(DATA_CHANNEL, 0, SEEK_END);
            Tcl_Seek(DATA_CHANNEL, pos, SEEK_SET);
            CookfsLog(printf("got data size: %" TCL_LL_MODIFIER "d",
                dataSize));
        } else {
            CookfsLog(printf("use specified size"));
        }

        Tcl_DStringInit(&chanTranslation);
        Tcl_DStringInit(&chanEncoding);
        Tcl_GetChannelOption(NULL, DATA_CHANNEL, "-encoding", &chanEncoding);
        Tcl_GetChannelOption(NULL, DATA_CHANNEL, "-translation",
            &chanTranslation);
        Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-translation", "binary");

        break;

    case COOKFS_WRITER_SOURCE_OBJECT: ; // an empty statement

        Tcl_Size length;
        data = (void *)Tcl_GetByteArrayFromObj(DATA_OBJECT, &length);

        if (dataSize < 0) {
            CookfsLog(printf("get datasize from the object"));
            dataSize = length;
            CookfsLog(printf("got data size: %" TCL_LL_MODIFIER "d",
                dataSize));
        } else if (dataSize > length) {
            dataSize = length;
            CookfsLog(printf("WARNING: data size was corrected to %"
                TCL_LL_MODIFIER "d avoid overflow", dataSize));
        } else {
            CookfsLog(printf("use specified size"));
        }

        break;
    }

    if (mtime == -1) {
        Tcl_Time now;
        Tcl_GetTime(&now);
        mtime = now.sec;
        CookfsLog(printf("use current time for mtime: %" TCL_LL_MODIFIER "d",
            mtime));
    }

    // If the file is empty, then just add it to the index and skip
    // everything else
    if (dataSize == 0) {
        // Create an entry
        if (!Cookfs_FsindexLockWrite(w->index, err)) {
            // Make sure we don't try to remove the file in fsindex
            // when terminating
            entry = NULL;
            goto error;
        };
        CookfsLog(printf("create an entry in fsindex for empty file with"
            " 1 block..."));
        entry = Cookfs_FsindexSet(w->index, pathObj, 1);
        if (entry == NULL) {
            CookfsLog(printf("failed to create the entry"));
            SET_ERROR_STR("Unable to create entry");
            goto error;
        }
        // Set entry block information
        Cookfs_FsindexEntrySetBlock(entry, 0, -1, 0, 0);
        Cookfs_FsindexEntrySetFileSize(entry, 0);
        // Unset entry to avoid releasing it in the final part of this function
        entry = NULL;
        Cookfs_FsindexUnlock(w->index);
        goto done;
    }

    if (((dataSize <= w->smallFileSize) && (dataSize <= w->pageSize)) ||
        w->isWriteToMemory)
    {

        CookfsLog(printf("write file to small file buffer"));

        if (!w->isWriteToMemory &&
            (w->bufferSize + dataSize > w->maxBufferSize))
        {
            CookfsLog(printf("need to purge"));
            result = Cookfs_WriterPurge(w, 1, err);
            if (result != TCL_OK) {
                CookfsLog(printf("ERROR: failed to purge"));
                goto error;
            }
        } else {
            CookfsLog(printf("no need to purge"));
        }

        if (dataType != COOKFS_WRITER_SOURCE_BUFFER) {

            CookfsLog(printf("alloc buffer"));
            readBuffer = ckalloc(dataSize);
            if (readBuffer == NULL) {
                CookfsLog(printf("failed to alloc buffer"));
                SET_ERROR_STR("failed to alloc buffer");
                goto error;
            }

            if (dataType == COOKFS_WRITER_SOURCE_OBJECT) {
                CookfsLog(printf("copy object's bytes to the buffer"));
                memcpy(readBuffer, data, dataSize);
            } else {

                CookfsLog(printf("read bytes from the channel"));
                Tcl_WideInt readSize = Cookfs_WriterReadChannel(
                    (char *)readBuffer, dataSize, DATA_CHANNEL);

                if (readSize < dataSize) {
                    CookfsLog(printf("ERROR: got less bytes than required"));
                    SET_ERROR_STR("could not read specified amount of bytes"
                        " from the file");
                    goto error;
                }

            }

        }

        CookfsLog(printf("add to small file buf..."));
        result = Cookfs_WriterAddBufferToSmallFiles(w, pathObj, mtime,
            (dataType == COOKFS_WRITER_SOURCE_BUFFER ? data : readBuffer),
            dataSize, err);
        if (result != TCL_OK) {
            goto error;
        }

        // readBuffer now owned by small file buffer. Set it to NULL to
        // avoid releasing it.
        readBuffer = NULL;

        if (!w->isWriteToMemory && (w->bufferSize >= w->maxBufferSize)) {
            CookfsLog(printf("need to purge"));
            result = Cookfs_WriterPurge(w, 1, err);
        } else {
            CookfsLog(printf("no need to purge"));
        }

    } else {

        CookfsLog(printf("write big file"));

        if (dataType == COOKFS_WRITER_SOURCE_CHANNEL ||
            dataType == COOKFS_WRITER_SOURCE_FILE)
        {
            CookfsLog(printf("alloc page buffer"));
            readBuffer = ckalloc(w->pageSize);
            if (readBuffer == NULL) {
                CookfsLog(printf("failed to alloc"));
                SET_ERROR_STR("failed to alloc buffer");
                goto error;
            }
        }

        // Calculate number of blocks
        int numBlocks = dataSize / w->pageSize;
        if (dataSize % w->pageSize) {
            numBlocks++;
        }

        // Create an entry
        if (!Cookfs_FsindexLockWrite(w->index, err)) {
            // Make sure we don't try to remove the file in fsindex
            // when terminating
            entry = NULL;
            goto error;
        };
        CookfsLog(printf("create an entry in fsindex with %d blocks...",
            numBlocks));
        entry = Cookfs_FsindexSet(w->index, pathObj, numBlocks);
        if (entry == NULL) {
            CookfsLog(printf("failed to create the entry"));
            SET_ERROR_STR("Unable to create entry");
            goto error;
        }
        Cookfs_FsindexEntrySetFileSize(entry, dataSize);
        Cookfs_FsindexEntrySetFileTime(entry, mtime);
        Cookfs_FsindexEntryLock(entry);
        Cookfs_FsindexUnlock(w->index);

        Tcl_WideInt currentOffset = 0;
        int currentBlockNumber = 0;
        Tcl_WideInt bytesLeft = dataSize;

        while (bytesLeft) {

            Tcl_WideInt bytesToWrite = (bytesLeft > w->pageSize ?
                w->pageSize : bytesLeft);

            CookfsLog(printf("want to write %" TCL_LL_MODIFIER "d bytes...",
                bytesToWrite));

            if (dataType == COOKFS_WRITER_SOURCE_CHANNEL ||
                dataType == COOKFS_WRITER_SOURCE_FILE)
            {
                CookfsLog(printf("read bytes from the channel"));
                Tcl_WideInt readSize = Cookfs_WriterReadChannel(
                    (char *)readBuffer, bytesToWrite, DATA_CHANNEL);

                if (readSize < bytesToWrite) {
                    CookfsLog(printf("ERROR: got less bytes than required"));
                    SET_ERROR_STR("could not read specified amount of bytes"
                        " from the file");
                    goto error;
                }
            }

            // Try to add page
            if (!Cookfs_PagesLockWrite(w->pages, err)) {
                goto error;
            };
            CookfsLog(printf("add page..."));
            Tcl_Obj *pgerr = NULL;
            int block = Cookfs_PageAddRaw(w->pages,
                (readBuffer == NULL ?
                (char *)data + currentOffset : readBuffer), bytesToWrite, &pgerr);
            CookfsLog(printf("got block index: %d", block));
            Cookfs_PagesUnlock(w->pages);

            if (block < 0) {
                SET_ERROR(Tcl_ObjPrintf("error while adding page: %s",
                    (pgerr == NULL ? "unknown error" : Tcl_GetString(pgerr))));
                if (pgerr != NULL) {
                    Tcl_IncrRefCount(pgerr);
                    Tcl_DecrRefCount(pgerr);
                }
                w->fatalError = 1;
                goto error;
            }

            if (!Cookfs_FsindexLockWrite(w->index, err)) {
                // Make sure we don't try to remove the file in fsindex
                // when terminating
                entry = NULL;
                goto error;
            };
            CookfsLog(printf("update block number %d of fsindex entry...",
                currentBlockNumber));
            Cookfs_FsindexEntrySetBlock(entry, currentBlockNumber, block, 0,
                bytesToWrite);
            Cookfs_FsindexUnlock(w->index);

            currentBlockNumber++;
            currentOffset += bytesToWrite;
            bytesLeft -= bytesToWrite;

        }

        // Unset entry to avoid releasing it at the end
        Cookfs_FsindexEntryUnlock(entry);
        entry = NULL;

        // If we add a buffer, the caller expects that the writer now owns
        // the buffer. Since we already store the file (its buffer) in pages,
        // we don't need this data anymore.
        if (dataType == COOKFS_WRITER_SOURCE_BUFFER) {
            ckfree(data);
        }

    }

    goto done;

error:

    result = TCL_ERROR;

done:

    if (readBuffer != NULL) {
        CookfsLog(printf("free readBuffer"));
        ckfree(readBuffer);
    }

    // Unset entry for the file if an error occurred while adding it
    if (entry != NULL) {
        CookfsLog(printf("unset fsindex entry"));
        if (Cookfs_FsindexLockWrite(w->index, err)) {
            Cookfs_FsindexEntryUnlock(entry);
            Cookfs_FsindexUnset(w->index, pathObj);
            Cookfs_FsindexUnlock(w->index);
        };
    }

    if (dataType == COOKFS_WRITER_SOURCE_CHANNEL) {
        CookfsLog(printf("restore chan translation/encoding"));
        Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-translation",
            Tcl_DStringValue(&chanTranslation));
        Tcl_DStringFree(&chanTranslation);
        Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-encoding",
            Tcl_DStringValue(&chanEncoding));
        Tcl_DStringFree(&chanEncoding);
    } else if (dataType == COOKFS_WRITER_SOURCE_FILE) {
        CookfsLog(printf("close channel"));
        Tcl_Close(NULL, DATA_CHANNEL);
    }

    if (result == TCL_ERROR) {
        CookfsLog(printf("return ERROR"));
    } else {
        CookfsLog(printf("ok"));
    }

    return result;

}

static int Cookfs_WriterPurgeSortFunc(const void *a, const void *b) {
    int rc;
    Cookfs_WriterBuffer *wba = *(Cookfs_WriterBuffer **)a;
    Cookfs_WriterBuffer *wbb = *(Cookfs_WriterBuffer **)b;
    rc = strcmp(wba->sortKeyExt, wbb->sortKeyExt);
    if (!rc) {
        rc = strcmp(wba->pathObj->tailName, wbb->pathObj->tailName);
        if (!rc) {
            rc = strcmp(wba->pathObj->fullName, wbb->pathObj->fullName);
        }
    }
    return rc;
}

int Cookfs_WriterPurge(Cookfs_Writer *w, int lockIndex, Tcl_Obj **err) {

    Cookfs_WriterWantWrite(w);

    CookfsLog(printf("enter [%p]", (void *)w));
    if (w->bufferCount == 0) {
        CookfsLog(printf("nothing to purge"));
        return TCL_OK;
    }

    int result = TCL_OK;
    unsigned char *pageBuffer = NULL;
    Cookfs_WriterBuffer **sortedWB = NULL;
    Cookfs_WriterBuffer *wb;
    int i;

    // Sort our files

    // A few words about sorting:
    //
    // Below we use the qsort() function to sort the buffers according to
    // the sort key.
    //
    // A sort key is "file extension + '.' + file name + full path". Thus,
    // the files in the archive will be sorted first by extension, then
    // by name, then by full path.
    //
    // However, we also want to consider files with the same content.
    // When adding files to the page, we will compare the contents of
    // the current buffer with the previous buffer. If they are the same,
    // then duplicate data will not be added. In order for this logic to work,
    // the same files must come one after the other. This means that when
    // sorting, we must first consider the sameness of the files and then
    // the sort key.
    //
    // Quicksort does not work in this case. There may be a pivot whose buffer
    // does not match any file. In this case, identical buffers will be
    // placed to different partitions and will never be next to each other in
    // the sort results.
    //
    // To solve this problem, we will check the buffers and if they are
    // identical, then we will use the same sort key for those buffers.

    CookfsLog(printf("have total %d entries", w->bufferCount));
    // Create array for all our entries
    sortedWB = ckalloc(w->bufferCount * sizeof(Cookfs_WriterBuffer *));
    if (sortedWB == NULL) {
        CookfsLog(printf("unable to alloc sortedWB"));
        SET_ERROR_STR("failed to alloc sortedWB");
        goto fatalError;
    }

    // Fill the buffer
    for (i = 0, wb = w->bufferFirst; wb != NULL; i++, wb = wb->next) {

        CookfsLog(printf("add buffer [%p] size %" TCL_LL_MODIFIER "d to"
            " sort buffer at #%d", (void *)wb->buffer, wb->bufferSize, i));
        sortedWB[i] = wb;

        // If we have less than 3 buffers, then we will not sort them
        if (w->bufferCount < 3) {
            continue;
        }

        // First we check previously processed buffers, trying to find out
        // if there is already exactly the same buffer in the queue. If such
        // a buffer is found, then we will use its sorting key.
        if (i > 0) {
            int j;
            for (j = 0; j < i; j++) {
                Cookfs_WriterBuffer *wbc = sortedWB[j];
                // TODO: comparing the current buffer with all previous buffers
                // is extremely expensive. Here we first compare the buffers
                // size and this partially mitigates the problem. However,
                // since all our buffers are already in memory, it will be easy
                // to calculate the crc/hash for the buffer and compare
                // the buffers by hashes. If hashes are equal, then compare
                // their contents.
                //
                // Another thing in which md5 hash for buffer will be useful is
                // activity related to finding duplicates.
                // See: Cookfs_WriterCheckDuplicate()
                //
                // CookfsLog(printf("check if [%p] equals to [%p]",
                //     (void *)wb->buffer, (void *)wbc->buffer));
                if ((wb->bufferSize == wbc->bufferSize)
                    && memcmp(wb->buffer, wbc->buffer, wb->bufferSize) == 0)
                {
                    // We found the same buffer
                    CookfsLog(printf("the same buffer has been found"));
                    // Let's use its sort key
                    wb->sortKey = wbc->sortKey;
                    Cookfs_PathObjIncrRefCount(wb->sortKey);
                    wb->sortKeyExt = wbc->sortKeyExt;
                    wb->sortKeyExtLen = wbc->sortKeyExtLen;
                    // Stop processing
                    break;
                }
            }
            // If 'j' did not reach 'i', then we have found a matching buffer.
            // No need to anything else with this buffer. Let's continue with
            // the next buffer.
            if (j != i) {
                continue;
            }
        }

        // Copy existing pathObj as a sortKey
        wb->sortKey = wb->pathObj;
        Cookfs_PathObjIncrRefCount(wb->sortKey);

        // TODO: this will not work correctly if there are null bytes in
        // the tailName, since strrchr will start searching from the first null
        // byte rather than from the beginning of the string.
        char *dotPosition = strrchr(wb->pathObj->tailName, '.');
        // No dot or dot at the first position? then skip
        if (dotPosition == NULL || dotPosition == wb->pathObj->tailName) {
            wb->sortKeyExt = wb->pathObj->tailName;
            wb->sortKeyExtLen = wb->pathObj->tailNameLength;
        } else {
            // +1 for dot position to skip the dot itself
            wb->sortKeyExt = ++dotPosition;
            // (dotPosition - wb->pathObj->tailName) - filename without extension
            // filename length - length of the filename without extension = ext length
            wb->sortKeyExtLen = wb->pathObj->tailNameLength -
                (dotPosition - wb->pathObj->tailName);
        }

        CookfsLog(printf("generated the sort key [%s]+[%s]+[%s]",
            wb->sortKeyExt, wb->pathObj->tailName, wb->pathObj->fullName));

    }

    if (w->isWriteToMemory) {
        goto skipAll;
    }

    // If we have more than 2 buffers, then sort them
    if (w->bufferCount > 2) {
        CookfsLog( \
            printf("== entries ===========> \n"); \
            for (i = 0; i < w->bufferCount; i++) { \
                printf("Cookfs_WriterPurge: %p [%s]+[%s]+[%s]\n", \
                    (void *)sortedWB[i]->buffer, \
                    sortedWB[i]->sortKeyExt, \
                    sortedWB[i]->pathObj->tailName, \
                    sortedWB[i]->pathObj->fullName); \
            }; \
            printf("Cookfs_WriterPurge: <======================")
        );
        CookfsLog(printf("sort buffers..."));
        qsort(sortedWB, w->bufferCount, sizeof(Cookfs_WriterBuffer *),
            Cookfs_WriterPurgeSortFunc);
        CookfsLog( \
            printf("== entries ===========> \n"); \
            for (i = 0; i < w->bufferCount; i++) { \
                printf("Cookfs_WriterPurge: %p [%s]+[%s]+[%s]\n", \
                    (void *)sortedWB[i]->buffer, \
                    sortedWB[i]->sortKeyExt, \
                    sortedWB[i]->pathObj->tailName, \
                    sortedWB[i]->pathObj->fullName); \
            }; \
            printf("Cookfs_WriterPurge: <======================")
        );
    } else {
        CookfsLog(printf("no need to sort buffers"));
    }

    // If our small buffer has fewer bytes than the page size, we will
    // allocate in the page buffer only what is needed to store all
    // the small buffer files.
    CookfsLog(printf("alloc page buffer for %" TCL_LL_MODIFIER "d bytes",
        w->bufferSize < w->pageSize ? w->bufferSize : w->pageSize));

    pageBuffer = ckalloc(w->bufferSize < w->pageSize ?
        w->bufferSize : w->pageSize);

    for (int bufferIdx = 0; bufferIdx < w->bufferCount;) {

        int firstBufferIdx = bufferIdx;
        // The first stage. Let's go through the buffers, fill pageBuffer
        // and determine which buffer we have reached the page size limit.
        Tcl_WideInt pageBufferSize = 0;
        wb = sortedWB[bufferIdx];
        while (1) {

            CookfsLog(printf("add buffer [%p] size %" TCL_LL_MODIFIER "d to"
                " page buffer", (void *)wb->buffer, wb->bufferSize));

            // Check to see if the exact same block has already been added
            // before
            if (bufferIdx != 0) {
                int found = 0;
                Cookfs_WriterBuffer *prevWB = sortedWB[bufferIdx - 1];
                if ((wb->bufferSize == prevWB->bufferSize)
                    && memcmp(wb->buffer, prevWB->buffer, wb->bufferSize) == 0)
                {
                    CookfsLog(printf("this buffer is equal to the previous"
                        " buffer [%p]", (void *)prevWB->buffer));
                    // Copy block-offset from the equal buffer
                    wb->pageBlock = prevWB->pageBlock;
                    wb->pageOffset = prevWB->pageOffset;
                    found = 1;
                }
                // We don't need the previous buffer and free it now
                // to optimize memory usage.
                CookfsLog(printf("free data from the previous buffer [%p]"
                    " as it is no longer needed", (void *)prevWB->buffer));
                ckfree(prevWB->buffer);
                prevWB->buffer = NULL;
                prevWB->bufferSize = 0;
                // Skip adding data from this buffer to the page, since we have
                // already found the same buffer
                if (found) {
                    goto next;
                }
            }

            wb->pageBlock = -1;
            wb->pageOffset = pageBufferSize;

            memcpy((void *)(pageBuffer + pageBufferSize), wb->buffer,
                wb->bufferSize);
            pageBufferSize += wb->bufferSize;

next:

            if (++bufferIdx >= w->bufferCount) {
                CookfsLog(printf("reached the end of buffers"));
                break;
            }
            wb = sortedWB[bufferIdx];
            if ((pageBufferSize + wb->bufferSize) > w->pageSize) {
                CookfsLog(printf("the next buffer will cause a page buffer"
                    " overflow, the page buffer must be flushed"));
                break;
            }

        }

        int pageBlock;

        // Try to add page if we need to save something from pageBuffer
        if (pageBufferSize) {
            CookfsLog(printf("add page..."));
            if (!Cookfs_PagesLockWrite(w->pages, err)) {
                goto fatalError;
            };
            Tcl_Obj *pgerr = NULL;
            pageBlock = Cookfs_PageAddRaw(w->pages, pageBuffer, pageBufferSize,
                &pgerr);
            CookfsLog(printf("got block index: %d", pageBlock));
            Cookfs_PagesUnlock(w->pages);

            if (pageBlock < 0) {
                SET_ERROR(Tcl_ObjPrintf("error while adding page of small"
                    " files: %s", (pgerr == NULL ? "unknown error" :
                    Tcl_GetString(pgerr))));
                if (pgerr != NULL) {
                    Tcl_IncrRefCount(pgerr);
                    Tcl_DecrRefCount(pgerr);
                }
                goto fatalError;
            }
            // Reduce size by already saved buffers size
            w->bufferSize -= pageBufferSize;
        } else {
            pageBlock = -1;
        }

        // The second state. Update entries in fsindex. The bufferIdx variable
        // corresponds to the last not saved buffer.

        CookfsLog(printf("modify %d files", bufferIdx - firstBufferIdx));
        if (lockIndex && !Cookfs_FsindexLockWrite(w->index, err)) {
            goto fatalError;
        };
        for (i = firstBufferIdx; i < bufferIdx; i++) {

            wb = sortedWB[i];

            // Update pageBlock to the new index of the saved page. It is
            // possible that pageBlock is already set. This can happen if we
            // add some file that uses a previously added page. Thus, update
            // pageBlock only if it is not defined yet, i.e. equal to -1.
            if (wb->pageBlock == -1) {
                wb->pageBlock = pageBlock;
            }

            if (pageBlock != -1) {
                // In Cookfs_WriterBuffer we set bufferSize to zero when free
                // its buffer. Thus, we can not rely on wb->bufferSize here.
                // Let's take the file size from fsindex entry.
                Cookfs_WriterPageMapEntryAdd(w, pageBlock, wb->pageOffset,
                    Cookfs_FsindexEntryGetFilesize(wb->entry));
            }

            // Update entry
            CookfsLog(printf("update fsindex entry for buffer:%p"
                " pageBlock:%d pageOffset:%d", (void *)wb, wb->pageBlock,
                wb->pageOffset));

            Cookfs_FsindexEntrySetBlock(wb->entry, 0, wb->pageBlock,
                wb->pageOffset, -1);

            Cookfs_FsindexEntryUnlock(wb->entry);

        }
        if (lockIndex) {
            Cookfs_FsindexUnlock(w->index);
        }

        if (pageBlock != -1) {
            Cookfs_WriterPageMapInitializePage(w, pageBlock, pageBuffer);
        }

    }

skipAll:

    // Cleanup small file buffer
    CookfsLog(printf("cleanup small file buffer"));
    for (i = 0; i < w->bufferCount; i++) {
        Cookfs_WriterWriterBufferFree(sortedWB[i]);
    }

    w->bufferFirst = NULL;
    w->bufferLast = NULL;
    w->bufferSize = 0;
    w->bufferCount = 0;

    goto done;

fatalError:

    CookfsLog(printf("!!! SET FATAL ERROR STATE !!!"));
    w->fatalError = 1;
    result = TCL_ERROR;

done:

    if (sortedWB != NULL) {
        CookfsLog(printf("free sortedWB"));
        ckfree(sortedWB);
    }

    if (pageBuffer != NULL) {
        CookfsLog(printf("free pageBuffer"));
        ckfree(pageBuffer);
    }

    if (result == TCL_ERROR) {
        CookfsLog(printf("return ERROR"));
    } else {
        CookfsLog(printf("ok"));
    }

    return result;
}

const void *Cookfs_WriterGetBuffer(Cookfs_Writer *w, int blockNumber,
    Tcl_WideInt *blockSize)
{
    Cookfs_WriterWantRead(w);

    CookfsLog(printf("enter [%p] block: %d", (void *)w, blockNumber));

    if (blockNumber < 0) {
        blockNumber = -blockNumber - 1;
    }
    CookfsLog(printf("real block number: %d; current number of blocks: %d",
        blockNumber, w->bufferCount));

    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (blockNumber && wb != NULL) {
        wb = wb->next;
        blockNumber--;
    }

    if (wb == NULL) {
        CookfsLog(printf("ERROR: block number is incorrect"));
        return NULL;
    }

    CookfsLog(printf("the block has been found [%p] data [%p] size [%"
        TCL_LL_MODIFIER "d]", (void *)wb, (void *)wb->buffer, wb->bufferSize));

    *blockSize = wb->bufferSize;
    return wb->buffer;
}

Tcl_Obj *Cookfs_WriterGetBufferObj(Cookfs_Writer *w, int blockNumber)
{
    CookfsLog(printf("enter [%p] block: %d", (void *)w, blockNumber));

    Tcl_WideInt blockSize;
    const void *blockData = Cookfs_WriterGetBuffer(w, blockNumber, &blockSize);
    if (blockData == NULL) {
        CookfsLog(printf("ERROR: block number is incorrect"));
        return NULL;
    }
    Tcl_Obj *rc = Tcl_NewByteArrayObj(blockData, blockSize);
    CookfsLog(printf("return obj [%p]", (void *)rc));
    return rc;
}

int Cookfs_WriterGetWritetomemory(Cookfs_Writer *w) {
    Cookfs_WriterWantRead(w);
    return w->isWriteToMemory;
}

void Cookfs_WriterSetWritetomemory(Cookfs_Writer *w, int status) {
    Cookfs_WriterWantWrite(w);
    w->isWriteToMemory = status;
    return;
}

Tcl_WideInt Cookfs_WriterGetSmallfilebuffersize(Cookfs_Writer *w) {
    Cookfs_WriterWantRead(w);
    return w->bufferSize;
}

Cookfs_Writer *Cookfs_WriterGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;
    CookfsLog(printf("get handle from cmd [%s]", cmdName));
    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
        return NULL;
    }
    CookfsLog(printf("return [%p]", cmdInfo.objClientData));
    return (Cookfs_Writer *)(cmdInfo.objClientData);
}
