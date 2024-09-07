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

static void Cookfs_WriterFree(Cookfs_Writer *w);

int Cookfs_WriterLockRW(int isWrite, Cookfs_Writer *w, Tcl_Obj **err) {
    int ret = 1;
#ifdef TCL_THREADS
    if (isWrite) {
        CookfsLog(printf("Cookfs_WriterLockWrite: try to lock..."));
        ret = Cookfs_RWMutexLockWrite(w->mx);
    } else {
        CookfsLog(printf("Cookfs_WriterLockRead: try to lock..."));
        ret = Cookfs_RWMutexLockRead(w->mx);
    }
    if (ret && w->isDead == 1) {
        // If object is terminated, don't allow everything.
        ret = 0;
        Cookfs_RWMutexUnlock(w->mx);
    }
    if (!ret) {
        CookfsLog(printf("%s: FAILED", isWrite ? "Cookfs_WriterLockWrite" :
            "Cookfs_WriterLockRead"));
        if (err != NULL) {
            *err = Tcl_NewStringObj("stalled fsindex object detected", -1);
        }
    } else {
        CookfsLog(printf("%s: ok", isWrite ? "Cookfs_WriterLockWrite" :
            "Cookfs_WriterLockRead"));
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
    CookfsLog(printf("Cookfs_WriterUnlock: ok"));
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
    CookfsLog2(printf("try to lock exclusive..."));
    Cookfs_RWMutexLockExclusive(w->mx);
    CookfsLog2(printf("ok"));
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
    CookfsLog(printf("Cookfs_WriterWriterBufferAlloc: buffer [%p]",
        (void *)wb));
    return wb;
}

static void Cookfs_WriterWriterBufferFree(Cookfs_WriterBuffer *wb) {
    CookfsLog(printf("Cookfs_WriterWriterBufferFree: buffer [%p]",
        (void *)wb));
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

    CookfsLog(printf("Cookfs_WriterInit: init mount in interp [%p];"
        " pages:%p index:%p smbuf:%" TCL_LL_MODIFIER "d sms:%" TCL_LL_MODIFIER
        "d pagesize:%" TCL_LL_MODIFIER "d writetomem:%d",
        (void *)interp, (void *)pages, (void *)index,
        smallfilebuffer, smallfilesize, pagesize, writetomemory));

    if (interp == NULL || (pages == NULL && !writetomemory) || index == NULL) {
        CookfsLog(printf("Cookfs_WriterInit: failed, something is NULL"));
        return NULL;
    }

    // Double check that smallfilesize is less than pagesize
    if (smallfilesize > pagesize) {
        CookfsLog(printf("Cookfs_WriterInit: failed,"
            " smallfilesize > pagesize"));
        return NULL;
    }

    Cookfs_Writer *w = (Cookfs_Writer *)ckalloc(sizeof(Cookfs_Writer));
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterInit: failed, OOM"));
        return NULL;
    }

    w->commandToken = NULL;
    w->interp = interp;
    w->fatalError = 0;
    w->isDead = 0;
    w->lockSoft = 0;

#ifdef TCL_THREADS
    /* initialize thread locks */
    w->mx = Cookfs_RWMutexInit();
    w->threadId = Tcl_GetCurrentThread();
    w->mxLockSoft = NULL;
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

    w->bufferFirst = NULL;
    w->bufferLast = NULL;
    w->bufferSize = 0;
    w->bufferCount = 0;

    CookfsLog(printf("Cookfs_WriterInit: ok [%p]", (void *)w));
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
        CookfsLog(printf("Cookfs_WriterFini: ERROR: writer is NULL"));
        return;
    }

    if (w->isDead == 1) {
        return;
    }

    Cookfs_WriterLockExclusive(w);

    CookfsLog(printf("Cookfs_WriterFini: aquire mutex"));
    // By acquisition the lockSoft mutex, we will be sure that no other
    // thread calls Cookfs_WriterUnlockSoft() that can release this object
    // while this function is running.
#ifdef TCL_THREADS
    Tcl_MutexLock(&w->mxLockSoft);
#endif /* TCL_THREADS */
    w->isDead = 1;

    CookfsLog(printf("Cookfs_WriterFini: enter [%p]", (void *)w));

    if (w->commandToken != NULL) {
        CookfsLog(printf("Cookfs_WriterFini: Cleaning tcl command"));
        Tcl_DeleteCommandFromToken(w->interp, w->commandToken);
    } else {
        CookfsLog(printf("Cookfs_WriterFini: No tcl command"));
    }

    CookfsLog(printf("Cookfs_WriterFini: free buffers"));
    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (wb != NULL) {
        Cookfs_WriterBuffer *next = wb->next;
        if (wb->entry != NULL) {
            Cookfs_FsindexEntryUnlock(wb->entry);
        }
        Cookfs_WriterWriterBufferFree(wb);
        wb = next;
    }

    CookfsLog(printf("Cookfs_WriterFini: free all"));
    Cookfs_FsindexUnlockSoft(w->index);
    if (w->pages != NULL) {
        Cookfs_PagesUnlockSoft(w->pages);
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

static int Cookfs_WriterAddBufferToSmallFiles(Cookfs_Writer *w,
    Cookfs_PathObj *pathObj, Tcl_WideInt mtime, void *buffer,
    Tcl_WideInt bufferSize, Tcl_Obj **err)
{
    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: add buf [%p],"
        " size: %" TCL_LL_MODIFIER "d", buffer, bufferSize));

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: alloc"
        " WriterBuffer"));
    Cookfs_WriterBuffer *wb = Cookfs_WriterWriterBufferAlloc(pathObj, mtime);
    if (wb == NULL) {
        CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: failed to"
            " alloc"));
        SET_ERROR_STR("failed to alloc WriterBuffer");
        return TCL_ERROR;
    }

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: create an entry"
        " in fsindex..."));
    if (!Cookfs_FsindexLockWrite(w->index, err)) {
        Cookfs_WriterWriterBufferFree(wb);
        return TCL_ERROR;
    };
    wb->entry = Cookfs_FsindexSet(w->index, pathObj, 1);
    if (wb->entry == NULL) {
        CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: failed to create"
            " the entry"));
        SET_ERROR_STR("Unable to create entry");
        Cookfs_FsindexUnlock(w->index);
        Cookfs_WriterWriterBufferFree(wb);
        return TCL_ERROR;
    }
    Cookfs_FsindexEntryLock(wb->entry);

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: set fsindex"
        " entry values"));
    Cookfs_FsindexEntrySetBlock(wb->entry, 0, -w->bufferCount - 1, 0,
        bufferSize);
    Cookfs_FsindexEntrySetFileSize(wb->entry, bufferSize);
    Cookfs_FsindexEntrySetFileTime(wb->entry, mtime);
    Cookfs_FsindexUnlock(w->index);

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: set WritterBuffer"
        " values and add to the chain"));
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

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: currently have"
        " %d buffers, total size: %" TCL_LL_MODIFIER "d", w->bufferCount,
        w->bufferSize));
    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: ok"));
    return TCL_OK;
}

static Tcl_WideInt Cookfs_WriterReadChannel(char *buffer,
    Tcl_WideInt bufferSize, Tcl_Channel channel)
{

    CookfsLog(printf("Cookfs_WriterReadChannel: want to read %" TCL_LL_MODIFIER
        "d bytes from channel %p", bufferSize, (void *)channel));

    Tcl_WideInt readSize = 0;

    while (readSize < bufferSize) {
        if (Tcl_Eof(channel)) {
            CookfsLog(printf("Cookfs_WriterReadChannel: EOF reached"));
            break;
        }
        CookfsLog(printf("Cookfs_WriterReadChannel: read bytes from"
            " the channel"));
        readSize += Tcl_Read(channel, buffer + readSize,
            bufferSize - readSize);
        CookfsLog(printf("Cookfs_WriterReadChannel: got %" TCL_LL_MODIFIER "d"
            " bytes from the channel", readSize));
    }
    CookfsLog(printf("Cookfs_WriterReadChannel: return %" TCL_LL_MODIFIER "d"
        " bytes from the channel", readSize));

    return readSize;

}

int Cookfs_WriterRemoveFile(Cookfs_Writer *w, Cookfs_FsindexEntry *entry) {
    Cookfs_WriterWantWrite(w);
    CookfsLog(printf("Cookfs_WriterRemoveFile: enter"));
    Cookfs_WriterBuffer *wbPrev = NULL;
    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (wb != NULL) {
        if (wb->entry == entry) {

            CookfsLog(printf("Cookfs_WriterRemoveFile: found the buffer"
                " to remove [%p]", (void *)wb));
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
                CookfsLog(printf("Cookfs_WriterRemoveFile: shift buffer number"
                    " for buffer [%p]", (void *)next));
                Cookfs_FsindexEntryIncrBlockPageIndex(next->entry, 0, 1);
                next = next->next;
            }

            return 1;
        }
        wbPrev = wb;
        wb = wb->next;
    }
    CookfsLog(printf("Cookfs_WriterRemoveFile: could not find the buffer"
        " to remove"));
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
    CookfsLog(printf("Cookfs_WriterAddFile: enter [%p] [%s] size: %"
        TCL_LL_MODIFIER "d", data,
        (dataType == COOKFS_WRITER_SOURCE_BUFFER ? "buffer" :
        (dataType == COOKFS_WRITER_SOURCE_FILE ? "file" :
        (dataType == COOKFS_WRITER_SOURCE_CHANNEL ? "channel" :
        (dataType == COOKFS_WRITER_SOURCE_OBJECT ? "object" : "unknown")))),
        dataSize));

    // Check if a fatal error has occurred previously
    if (w->fatalError) {
        CookfsLog(printf("Cookfs_WriterAddFile: ERROR: writer in a fatal"
            " error state"));
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
        CookfsLog(printf("Cookfs_WriterAddFile: dead entry is detected, return"
            " ok without writing"));
        if (dataType == COOKFS_WRITER_SOURCE_BUFFER) {
            ckfree(data);
        }
        Cookfs_FsindexUnlock(w->index);
        return TCL_OK;
    }
    entry = Cookfs_FsindexGet(w->index, pathObj);
    if (entry != NULL) {
        CookfsLog(printf("Cookfs_WriterAddFile: an existing entry for the file"
            " was found"));
        if (Cookfs_FsindexEntryIsPending(entry)) {
            CookfsLog(printf("Cookfs_WriterAddFile: the entry is pending,"
                " remove it from small file buffer"));
            Cookfs_WriterRemoveFile(w, entry);
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: the entry is"
                " not pending"));
        }
        entry = NULL;
    }
    Cookfs_FsindexUnlock(w->index);

    switch (dataType) {

    case COOKFS_WRITER_SOURCE_BUFFER:

        // No need to do anything. It is here to simply avoid the compiler warning.

        break;

    case COOKFS_WRITER_SOURCE_FILE:

        CookfsLog(printf("Cookfs_WriterAddFile: alloc statbuf"));

        Tcl_StatBuf *sb = Tcl_AllocStatBuf();
        if (sb == NULL) {
            SET_ERROR_STR("could not alloc statbuf");
            return TCL_ERROR;
        }

        CookfsLog(printf("Cookfs_WriterAddFile: get file stat for [%s]",
            Tcl_GetString(DATA_FILE)));
        if (Tcl_FSStat(DATA_FILE, sb) != TCL_OK) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed, return error"));
            ckfree(sb);
            SET_ERROR_STR("could get stat for the file");
            return TCL_ERROR;
        }

        if (dataSize < 0) {
            dataSize = Tcl_GetSizeFromStat(sb);
            CookfsLog(printf("Cookfs_WriterAddFile: got file size: %"
                TCL_LL_MODIFIER "d", dataSize));
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: use specified size"));
        }

        mtime = Tcl_GetModificationTimeFromStat(sb);
        CookfsLog(printf("Cookfs_WriterAddFile: got mtime from the file: %"
            TCL_LL_MODIFIER "d", mtime));

        ckfree(sb);

        CookfsLog(printf("Cookfs_WriterAddFile: open the file"));
        data = (void *)Tcl_FSOpenFileChannel(NULL, DATA_FILE, "rb", 0);
        if (data == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to open the file"));
            SET_ERROR_STR("could not open the file");
            return TCL_ERROR;
        }

        // Let's believe that "rb" for file open mode worked
        //Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-translation", "binary");

        break;

    case COOKFS_WRITER_SOURCE_CHANNEL:

        if (dataSize < 0) {
            CookfsLog(printf("Cookfs_WriterAddFile: get datasize from"
                " the channel"));
            Tcl_WideInt pos = Tcl_Tell(DATA_CHANNEL);
            dataSize = Tcl_Seek(DATA_CHANNEL, 0, SEEK_END);
            Tcl_Seek(DATA_CHANNEL, pos, SEEK_SET);
            CookfsLog(printf("Cookfs_WriterAddFile: got data size: %"
                TCL_LL_MODIFIER "d", dataSize));
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: use specified size"));
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
            CookfsLog(printf("Cookfs_WriterAddFile: get datasize from"
                " the object"));
            dataSize = length;
            CookfsLog(printf("Cookfs_WriterAddFile: got data size: %"
                TCL_LL_MODIFIER "d", dataSize));
        } else if (dataSize > length) {
            dataSize = length;
            CookfsLog(printf("Cookfs_WriterAddFile: WARNING: data size was"
                " corrected to %" TCL_LL_MODIFIER "d avoid overflow",
                dataSize));
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: use specified size"));
        }

        break;
    }

    if (mtime == -1) {
        Tcl_Time now;
        Tcl_GetTime(&now);
        mtime = now.sec;
        CookfsLog(printf("Cookfs_WriterAddFile: use current time for"
            " mtime: %" TCL_LL_MODIFIER "d", mtime));
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
        CookfsLog(printf("Cookfs_WriterAddFile: create an entry"
            " in fsindex for empty file with 1 block..."));
        entry = Cookfs_FsindexSet(w->index, pathObj, 1);
        if (entry == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to create"
                " the entry"));
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

        CookfsLog(printf("Cookfs_WriterAddFile: write file to small file"
            " buffer"));

        if (dataType != COOKFS_WRITER_SOURCE_BUFFER) {

            CookfsLog(printf("Cookfs_WriterAddFile: alloc buffer"));
            readBuffer = ckalloc(dataSize);
            if (readBuffer == NULL) {
                CookfsLog(printf("Cookfs_WriterAddFile: failed to alloc"
                    " buffer"));
                SET_ERROR_STR("failed to alloc buffer");
                goto error;
            }

            if (dataType == COOKFS_WRITER_SOURCE_OBJECT) {
                CookfsLog(printf("Cookfs_WriterAddFile: copy object's bytes"
                    " to the buffer"));
                memcpy(readBuffer, data, dataSize);
            } else {

                CookfsLog(printf("Cookfs_WriterAddFile: read bytes from"
                    " the channel"));
                Tcl_WideInt readSize = Cookfs_WriterReadChannel(
                    (char *)readBuffer, dataSize, DATA_CHANNEL);

                if (readSize < dataSize) {
                    CookfsLog(printf("Cookfs_WriterAddFile: ERROR: got less"
                        " bytes than required"));
                    SET_ERROR_STR("could not read specified amount of bytes"
                        " from the file");
                    goto error;
                }

            }

        }

        CookfsLog(printf("Cookfs_WriterAddFile: add to small file buf..."));
        int ret = Cookfs_WriterAddBufferToSmallFiles(w, pathObj, mtime,
            (dataType == COOKFS_WRITER_SOURCE_BUFFER ? data : readBuffer),
            dataSize, err);
        if (ret != TCL_OK) {
            goto error;
        }

        // readBuffer now owned by small file buffer. Set it to NULL to
        // avoid releasing it.
        readBuffer = NULL;

        if (!w->isWriteToMemory && (w->bufferSize >= w->maxBufferSize)) {
            CookfsLog(printf("Cookfs_WriterAddFile: need to purge"));
            result = Cookfs_WriterPurge(w, err);
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: no need to purge"));
        }

    } else {

        CookfsLog(printf("Cookfs_WriterAddFile: write big file"));

        if (dataType == COOKFS_WRITER_SOURCE_CHANNEL ||
            dataType == COOKFS_WRITER_SOURCE_FILE)
        {
            CookfsLog(printf("Cookfs_WriterAddFile: alloc page buffer"));
            readBuffer = ckalloc(w->pageSize);
            if (readBuffer == NULL) {
                CookfsLog(printf("Cookfs_WriterAddFile: failed to alloc"));
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
        CookfsLog(printf("Cookfs_WriterAddFile: create an entry"
            " in fsindex with %d blocks...", numBlocks));
        entry = Cookfs_FsindexSet(w->index, pathObj, numBlocks);
        if (entry == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to create"
                " the entry"));
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

            CookfsLog(printf("Cookfs_WriterAddFile: want to write %"
                TCL_LL_MODIFIER "d bytes...", bytesToWrite));

            if (dataType == COOKFS_WRITER_SOURCE_CHANNEL ||
                dataType == COOKFS_WRITER_SOURCE_FILE)
            {
                CookfsLog(printf("Cookfs_WriterAddFile: read bytes from"
                    " the channel"));
                Tcl_WideInt readSize = Cookfs_WriterReadChannel(
                    (char *)readBuffer, bytesToWrite, DATA_CHANNEL);

                if (readSize < bytesToWrite) {
                    CookfsLog(printf("Cookfs_WriterAddFile: ERROR: got less"
                        " bytes than required"));
                    SET_ERROR_STR("could not read specified amount of bytes"
                        " from the file");
                    goto error;
                }
            }

            // Try to add page
            if (!Cookfs_PagesLockWrite(w->pages, err)) {
                goto error;
            };
            CookfsLog(printf("Cookfs_WriterAddFile: add page..."));
            Tcl_Obj *pgerr = NULL;
            int block = Cookfs_PageAddRaw(w->pages,
                (readBuffer == NULL ?
                (char *)data + currentOffset : readBuffer), bytesToWrite, &pgerr);
            CookfsLog(printf("Cookfs_WriterAddFile: got block index: %d", block));
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
            CookfsLog(printf("Cookfs_WriterAddFile: update block number %d"
                " of fsindex entry...", currentBlockNumber));
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
        CookfsLog(printf("Cookfs_WriterAddFile: free readBuffer"));
        ckfree(readBuffer);
    }

    // Unset entry for the file if an error occurred while adding it
    if (entry != NULL) {
        CookfsLog(printf("Cookfs_WriterAddFile: unset fsindex entry"));
        if (Cookfs_FsindexLockWrite(w->index, err)) {
            Cookfs_FsindexEntryUnlock(entry);
            Cookfs_FsindexUnset(w->index, pathObj);
            Cookfs_FsindexUnlock(w->index);
        };
    }

    if (dataType == COOKFS_WRITER_SOURCE_CHANNEL) {
        CookfsLog(printf("Cookfs_WriterAddFile: restore chan"
            " translation/encoding"));
        Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-translation",
            Tcl_DStringValue(&chanTranslation));
        Tcl_DStringFree(&chanTranslation);
        Tcl_SetChannelOption(NULL, DATA_CHANNEL, "-encoding",
            Tcl_DStringValue(&chanEncoding));
        Tcl_DStringFree(&chanEncoding);
    } else if (dataType == COOKFS_WRITER_SOURCE_FILE) {
        CookfsLog(printf("Cookfs_WriterAddFile: close channel"));
        Tcl_Close(NULL, DATA_CHANNEL);
    }

    if (result == TCL_ERROR) {
        CookfsLog(printf("Cookfs_WriterAddFile: return ERROR"));
    } else {
        CookfsLog(printf("Cookfs_WriterAddFile: ok"));
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

int Cookfs_WriterPurge(Cookfs_Writer *w, Tcl_Obj **err) {

    Cookfs_WriterWantWrite(w);

    CookfsLog(printf("Cookfs_WriterPurge: enter [%p]", (void *)w));
    if (w->bufferCount == 0) {
        CookfsLog(printf("Cookfs_WriterPurge: nothing to purge"));
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

    CookfsLog(printf("Cookfs_WriterPurge: have total %d entries",
        w->bufferCount));
    // Create array for all our entries
    sortedWB = ckalloc(w->bufferCount * sizeof(Cookfs_WriterBuffer *));
    if (sortedWB == NULL) {
        CookfsLog(printf("Cookfs_WriterPurge: unable to alloc sortedWB"));
        SET_ERROR_STR("failed to alloc sortedWB");
        goto fatalError;
    }

    // Fill the buffer
    for (i = 0, wb = w->bufferFirst; wb != NULL; i++, wb = wb->next) {

        CookfsLog(printf("Cookfs_WriterPurge: add buffer [%p] size %"
            TCL_LL_MODIFIER "d to sort buffer at #%d", (void *)wb->buffer,
            wb->bufferSize, i));
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
                // CookfsLog(printf("Cookfs_WriterPurge: check if [%p] equals"
                //     " to [%p]", (void *)wb->buffer,
                //     (void *)wbc->buffer));
                if ((wb->bufferSize == wbc->bufferSize)
                    && memcmp(wb->buffer, wbc->buffer, wb->bufferSize) == 0)
                {
                    // We found the same buffer
                    CookfsLog(printf("Cookfs_WriterPurge: the same buffer"
                        " has been found"));
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

        CookfsLog(printf("Cookfs_WriterPurge: generated the sort key"
            " [%s]+[%s]+[%s]", wb->sortKeyExt, wb->pathObj->tailName,
            wb->pathObj->fullName));

    }

    if (w->isWriteToMemory) {
        goto skipAll;
    }

    // If we have more than 2 buffers, then sort them
    if (w->bufferCount > 2) {
        CookfsLog( \
            printf("Cookfs_WriterPurge: == entries ===========> \n"); \
            for (i = 0; i < w->bufferCount; i++) { \
                printf("Cookfs_WriterPurge: %p [%s]+[%s]+[%s]\n", \
                    (void *)sortedWB[i]->buffer, \
                    sortedWB[i]->sortKeyExt, \
                    sortedWB[i]->pathObj->tailName, \
                    sortedWB[i]->pathObj->fullName); \
            }; \
            printf("Cookfs_WriterPurge: <======================")
        );
        CookfsLog(printf("Cookfs_WriterPurge: sort buffers..."));
        qsort(sortedWB, w->bufferCount, sizeof(Cookfs_WriterBuffer *),
            Cookfs_WriterPurgeSortFunc);
        CookfsLog( \
            printf("Cookfs_WriterPurge: == entries ===========> \n"); \
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
        CookfsLog(printf("Cookfs_WriterPurge: no need to sort buffers"));
    }

    // If our small buffer has fewer bytes than the page size, we will
    // allocate in the page buffer only what is needed to store all
    // the small buffer files.
    CookfsLog(printf("Cookfs_WriterPurge: alloc page buffer for %"
        TCL_LL_MODIFIER "d bytes",
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

            CookfsLog(printf("Cookfs_WriterPurge: add buffer [%p] size %"
                TCL_LL_MODIFIER "d to page buffer", (void *)wb->buffer,
                wb->bufferSize));

            // Check to see if the exact same block has already been added
            // before
            if (bufferIdx != 0) {
                int found = 0;
                Cookfs_WriterBuffer *prevWB = sortedWB[bufferIdx - 1];
                if ((wb->bufferSize == prevWB->bufferSize)
                    && memcmp(wb->buffer, prevWB->buffer, wb->bufferSize) == 0)
                {
                    CookfsLog(printf("Cookfs_WriterPurge: this buffer is equal"
                        " to the previous buffer [%p]",
                        (void *)prevWB->buffer));
                    // Copy block-offset from the equal buffer
                    wb->pageBlock = prevWB->pageBlock;
                    wb->pageOffset = prevWB->pageOffset;
                    found = 1;
                }
                // We don't need the previous buffer and free it now
                // to optimize memory usage.
                CookfsLog(printf("Cookfs_WriterPurge: free data from"
                    " the previous buffer [%p] as it is no longer needed",
                    (void *)prevWB->buffer));
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
                CookfsLog(printf("Cookfs_WriterPurge: reached the end of"
                    " buffers"));
                break;
            }
            wb = sortedWB[bufferIdx];
            if ((pageBufferSize + wb->bufferSize) > w->pageSize) {
                CookfsLog(printf("Cookfs_WriterPurge: the next buffer will"
                    " cause a page buffer overflow, the page buffer must"
                    " be flushed"));
                break;
            }

        }

        int pageBlock;

        // Try to add page if we need to save something from pageBuffer
        if (pageBufferSize) {
            CookfsLog(printf("Cookfs_WriterPurge: add page..."));
            if (!Cookfs_PagesLockWrite(w->pages, err)) {
                goto fatalError;
            };
            Tcl_Obj *pgerr = NULL;
            pageBlock = Cookfs_PageAddRaw(w->pages, pageBuffer, pageBufferSize,
                &pgerr);
            CookfsLog(printf("Cookfs_WriterPurge: got block index: %d",
                pageBlock));
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

        CookfsLog(printf("Cookfs_WriterPurge: modify %d files",
            bufferIdx - firstBufferIdx));
        if (!Cookfs_FsindexLockWrite(w->index, err)) {
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

            // Update entry
            CookfsLog(printf("Cookfs_WriterPurge: update fsindex entry for"
                " buffer %p: pageBlock:%d pageOffset:%d", (void *)wb,
                wb->pageBlock, wb->pageOffset));

            Cookfs_FsindexEntrySetBlock(wb->entry, 0, wb->pageBlock,
                wb->pageOffset, -1);
            Cookfs_FsindexEntryUnlock(wb->entry);

        }
        Cookfs_FsindexUnlock(w->index);

    }

skipAll:

    // Cleanup small file buffer
    CookfsLog(printf("Cookfs_WriterPurge: cleanup small file buffer"));
    for (i = 0; i < w->bufferCount; i++) {
        Cookfs_WriterWriterBufferFree(sortedWB[i]);
    }

    w->bufferFirst = NULL;
    w->bufferLast = NULL;
    w->bufferSize = 0;
    w->bufferCount = 0;

    goto done;

fatalError:


    CookfsLog(printf("Cookfs_WriterPurge: !!! SET FATAL ERROR STATE !!!"));
    w->fatalError = 1;
    result = TCL_ERROR;

done:

    if (sortedWB != NULL) {
        CookfsLog(printf("Cookfs_WriterPurge: free sortedWB"));
        ckfree(sortedWB);
    }

    if (pageBuffer != NULL) {
        CookfsLog(printf("Cookfs_WriterPurge: free pageBuffer"));
        ckfree(pageBuffer);
    }

    if (result == TCL_ERROR) {
        CookfsLog(printf("Cookfs_WriterPurge: return ERROR"));
    } else {
        CookfsLog(printf("Cookfs_WriterPurge: ok"));
    }

    return result;
}

// IMPLEMENTED

const void *Cookfs_WriterGetBuffer(Cookfs_Writer *w, int blockNumber,
    Tcl_WideInt *blockSize)
{
    Cookfs_WriterWantRead(w);

    CookfsLog(printf("Cookfs_WriterGetBuffer: enter [%p] block: %d",
        (void *)w, blockNumber));

    if (blockNumber < 0) {
        blockNumber = -blockNumber - 1;
    }
    CookfsLog(printf("Cookfs_WriterGetBuffer: real block number: %d;"
        " current number of blocks: %d", blockNumber, w->bufferCount));

    Cookfs_WriterBuffer *wb = w->bufferFirst;
    while (blockNumber && wb != NULL) {
        wb = wb->next;
        blockNumber--;
    }

    if (wb == NULL) {
        CookfsLog(printf("Cookfs_WriterGetBuffer: ERROR: block number"
            " is incorrect"));
        return NULL;
    }

    CookfsLog(printf("Cookfs_WriterGetBuffer: the block has been found [%p]"
        " data [%p] size [%" TCL_LL_MODIFIER "d]", (void *)wb,
        (void *)wb->buffer, wb->bufferSize));

    *blockSize = wb->bufferSize;
    return wb->buffer;
}

Tcl_Obj *Cookfs_WriterGetBufferObj(Cookfs_Writer *w, int blockNumber)
{
    CookfsLog(printf("Cookfs_WriterGetBufferObj: enter [%p] block: %d",
        (void *)w, blockNumber));

    Tcl_WideInt blockSize;
    const void *blockData = Cookfs_WriterGetBuffer(w, blockNumber, &blockSize);
    if (blockData == NULL) {
        CookfsLog(printf("Cookfs_WriterGetBufferObj: ERROR: block number"
            " is incorrect"));
        return NULL;
    }
    Tcl_Obj *rc = Tcl_NewByteArrayObj(blockData, blockSize);
    CookfsLog(printf("Cookfs_WriterGetBuffer: return obj [%p]", (void *)rc));
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
    CookfsLog(printf("Cookfs_WriterGetHandle: get handle from cmd [%s]", cmdName));
    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
        return NULL;
    }
    CookfsLog(printf("Cookfs_WriterGetHandle: return [%p]", cmdInfo.objClientData));
    return (Cookfs_Writer *)(cmdInfo.objClientData);
}
