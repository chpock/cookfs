/*
 * vfs.c
 *
 * Provides implementation for writer
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

static void Cookfs_WriterSetLastErrorObj(Cookfs_Writer *w, Tcl_Obj *msg) {
    if (w->lastErrorObj != NULL) {
        Tcl_DecrRefCount(w->lastErrorObj);
    }
    if (msg == NULL) {
        w->lastErrorObj = NULL;
    } else {
        w->lastErrorObj = msg;
        Tcl_IncrRefCount(w->lastErrorObj);
    }
}

static void Cookfs_WriterSetLastError(Cookfs_Writer *w, const char *msg) {
    Cookfs_WriterSetLastErrorObj(w, Tcl_NewStringObj(msg, -1));
}

Tcl_Obj *Cookfs_WriterGetLastError(Cookfs_Writer *w) {
    return w->lastErrorObj;
}

static Cookfs_WriterBuffer *Cookfs_WriterWriterBufferAlloc(Tcl_Obj *pathObj,
    Tcl_WideInt mtime)
{
    Cookfs_WriterBuffer *wb = ckalloc(sizeof(Cookfs_WriterBuffer));
    if (wb != NULL) {
        wb->buffer = NULL;
        wb->bufferSize = 0;
        wb->mtime = mtime;
        wb->pathObj = pathObj;
        Tcl_IncrRefCount(pathObj);
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
        Tcl_DecrRefCount(wb->pathObj);
    }
    if (wb->sortKey != NULL) {
        Tcl_DecrRefCount(wb->sortKey);
    }
    ckfree(wb);
}

Cookfs_Writer *Cookfs_WriterInit(Tcl_Interp* interp,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Tcl_WideInt smallfilebuffer, Tcl_WideInt smallfilesize,
    Tcl_WideInt pagesize, int writetomemory)
{

    CookfsLog(printf("Cookfs_WriterInit: init mount in interp [%p];"
        " pages:%p index:%p smbuf:%ld sms:%ld pagesize:%ld writetomem:%d",
        (void *)interp, (void *)pages, (void *)index,
        smallfilebuffer, smallfilesize, pagesize, writetomemory));

    if (interp == NULL || pages == NULL || index == NULL) {
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

    w->lastErrorObj = NULL;
    w->commandToken = NULL;
    w->interp = interp;
    w->fatalError = 0;
    w->isDead = 0;

    w->pages = pages;
    w->index = index;

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

void Cookfs_WriterFini(Cookfs_Writer *w) {

    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterFini: ERROR: writer is NULL"));
        return;
    }

    if (w->isDead) {
        return;
    }
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
        Cookfs_WriterWriterBufferFree(wb);
        wb = next;
    }

    CookfsLog(printf("Cookfs_WriterFini: free all"));

    if (w->lastErrorObj != NULL) {
        Tcl_DecrRefCount(w->lastErrorObj);
    }

    ckfree(w);

    return;
}

int Cookfs_WriterAddBufferToSmallFiles(Cookfs_Writer *w, Tcl_Obj *pathObj,
    Tcl_WideInt mtime, void *buffer, Tcl_WideInt bufferSize)
{
    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: add buf [%p],"
        " size: %ld", buffer, bufferSize));

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: alloc"
        " WriterBuffer"));
    Cookfs_WriterBuffer *wb = Cookfs_WriterWriterBufferAlloc(pathObj, mtime);
    if (wb == NULL) {
        CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: failed to"
            " alloc"));
        Cookfs_WriterSetLastError(w, "failed to alloc WriterBuffer");
        return TCL_ERROR;
    }

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: create an entry"
        " in fsindex..."));
    wb->entry = Cookfs_FsindexSet(w->index, pathObj, 1);
    if (wb->entry == NULL) {
        CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: failed to create"
            " the entry"));
        Cookfs_WriterSetLastError(w, "Unable to create entry");
        Cookfs_WriterWriterBufferFree(wb);
        return TCL_ERROR;
    }

    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: set fsindex"
        " entry values"));
    wb->entry->data.fileInfo.fileBlockOffsetSize[0] = -w->bufferCount - 1;
    wb->entry->data.fileInfo.fileBlockOffsetSize[1] = 0;
    wb->entry->data.fileInfo.fileBlockOffsetSize[2] = bufferSize;
    wb->entry->data.fileInfo.fileSize = bufferSize;
    wb->entry->fileTime = mtime;

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
        " %d buffers, total size: %ld", w->bufferCount, w->bufferSize));
    CookfsLog(printf("Cookfs_WriterAddBufferToSmallFiles: ok"));
    return TCL_OK;
}

static Tcl_WideInt Cookfs_WriterReadChannel(char *buffer,
    Tcl_WideInt bufferSize, Tcl_Channel channel)
{

    CookfsLog(printf("Cookfs_WriterReadChannel: want to read %ld bytes from"
        " channel %p", bufferSize, (void *)channel));

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
        CookfsLog(printf("Cookfs_WriterReadChannel: got %ld bytes from"
            " the channel", readSize));
    }
    CookfsLog(printf("Cookfs_WriterReadChannel: return %ld bytes from"
            " the channel", readSize));

    return readSize;

}

int Cookfs_WriterRemoveFile(Cookfs_Writer *w, Cookfs_FsindexEntry *entry) {
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
            Cookfs_WriterWriterBufferFree(wb);

            // Shift block number for the following files and their entries
            while (next != NULL) {
                CookfsLog(printf("Cookfs_WriterRemoveFile: shift buffer number"
                    " for buffer [%p]", (void *)next));
                next->entry->data.fileInfo.fileBlockOffsetSize[0]++;
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

int Cookfs_WriterAddFile(Cookfs_Writer *w, Tcl_Obj *pathObj,
    Cookfs_WriterDataSource dataType, void *data, Tcl_WideInt dataSize)
{
    CookfsLog(printf("Cookfs_WriterAddFile: enter [%p] [%s] size: %ld", data,
        (dataType == COOKFS_WRITER_SOURCE_BUFFER ? "buffer" :
        (dataType == COOKFS_WRITER_SOURCE_FILE ? "file" :
        (dataType == COOKFS_WRITER_SOURCE_CHANNEL ? "channel" :
        (dataType == COOKFS_WRITER_SOURCE_OBJECT ? "object" : "unknown")))),
        dataSize));

    // Check if a fatal error has occurred previously
    if (w->fatalError) {
        CookfsLog(printf("Cookfs_WriterAddFile: ERROR: writer in a fatal"
            " error state: [%s]",
            Tcl_GetString(Cookfs_WriterGetLastError(w))));
        return TCL_ERROR;
    }

    int result = TCL_OK;
    void *readBuffer = NULL;
    Tcl_WideInt mtime = -1;
    Tcl_DString chanTranslation, chanEncoding;
    Cookfs_FsindexEntry *entry = NULL;

    // Check if we have the file in the small file buffer. We will try to get
    // the fsindex entry for this file and see if it is a pending file.
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


    switch (dataType) {

    case COOKFS_WRITER_SOURCE_BUFFER:

        // No need to do anything. It is here to simply avoid the compiler warning.

        break;

    case COOKFS_WRITER_SOURCE_FILE:

        CookfsLog(printf("Cookfs_WriterAddFile: alloc statbuf"));

        Tcl_StatBuf *sb = Tcl_AllocStatBuf();
        if (sb == NULL) {
            Cookfs_WriterSetLastError(w, "could not alloc statbuf");
            return TCL_ERROR;
        }

        CookfsLog(printf("Cookfs_WriterAddFile: get file stat for [%s]",
            Tcl_GetString(DATA_FILE)));
        if (Tcl_FSStat(DATA_FILE, sb) != TCL_OK) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed, return error"));
            ckfree(sb);
            Cookfs_WriterSetLastError(w, "could get stat for the file");
            return TCL_ERROR;
        }

        if (dataSize < 0) {
            dataSize = Tcl_GetSizeFromStat(sb);
            CookfsLog(printf("Cookfs_WriterAddFile: got file size: %ld",
                dataSize));
        } else {
            CookfsLog(printf("Cookfs_WriterAddFile: use specified size"));
        }

        mtime = Tcl_GetModificationTimeFromStat(sb);
        CookfsLog(printf("Cookfs_WriterAddFile: got mtime from the file: %ld",
            mtime));

        ckfree(sb);

        CookfsLog(printf("Cookfs_WriterAddFile: open the file"));
        data = (void *)Tcl_FSOpenFileChannel(NULL, DATA_FILE, "rb", 0);
        if (data == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to open the file"));
            Cookfs_WriterSetLastError(w, "could not open the file");
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
            CookfsLog(printf("Cookfs_WriterAddFile: got data size: %ld",
                dataSize));
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

        int length;
        data = (void *)Tcl_GetByteArrayFromObj(DATA_OBJECT, &length);

        if (dataSize < 0) {
            CookfsLog(printf("Cookfs_WriterAddFile: get datasize from"
                " the object"));
            dataSize = length;
            CookfsLog(printf("Cookfs_WriterAddFile: got data size: %ld",
                dataSize));
        } else if (dataSize > length) {
            dataSize = length;
            CookfsLog(printf("Cookfs_WriterAddFile: WARNING: data size was"
                " corrected to %ld avoid overflow", dataSize));
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
            " mtime: %ld", mtime));
    }

    // If the file is empty, then just add it to the index and skip
    // everything else
    if (dataSize == 0) {
        // Create an entry
        CookfsLog(printf("Cookfs_WriterAddFile: create an entry"
            " in fsindex for empty file with 1 block..."));
        entry = Cookfs_FsindexSet(w->index, pathObj, 1);
        if (entry == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to create"
                " the entry"));
            Cookfs_WriterSetLastError(w, "Unable to create entry");
            goto error;
        }
        // Set entry block information
        Cookfs_FsindexUpdateEntryBlock(w->index, entry, 0, -1, 0, 0);
        Cookfs_FsindexUpdateEntryFileSize(entry, 0);
        // Unset entry to avoid releasing it in the final part of this function
        entry = NULL;
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
                Cookfs_WriterSetLastError(w, "failed to alloc buffer");
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
                    Cookfs_WriterSetLastError(w, "could not read specified"
                        " amount of bytes from the file");
                    goto error;
                }

            }

        }

        CookfsLog(printf("Cookfs_WriterAddFile: add to small file buf..."));
        int ret = Cookfs_WriterAddBufferToSmallFiles(w, pathObj, mtime,
            (dataType == COOKFS_WRITER_SOURCE_BUFFER ? data : readBuffer),
            dataSize);
        if (ret != TCL_OK) {
            goto error;
        }

        // readBuffer now owned by small file buffer. Set it to NULL to
        // avoid releasing it.
        readBuffer = NULL;

        if (!w->isWriteToMemory && (w->bufferSize >= w->maxBufferSize)) {
            CookfsLog(printf("Cookfs_WriterAddFile: need to purge"));
            result = Cookfs_WriterPurge(w);
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
                Cookfs_WriterSetLastError(w, "failed to alloc buffer");
                goto error;
            }
        }

        // Calculate number of blocks
        int numBlocks = dataSize / w->pageSize;
        if (dataSize % w->pageSize) {
            numBlocks++;
        }

        // Create an entry
        CookfsLog(printf("Cookfs_WriterAddFile: create an entry"
            " in fsindex with %d blocks...", numBlocks));
        entry = Cookfs_FsindexSet(w->index, pathObj, numBlocks);
        if (entry == NULL) {
            CookfsLog(printf("Cookfs_WriterAddFile: failed to create"
                " the entry"));
            Cookfs_WriterSetLastError(w, "Unable to create entry");
            goto error;
        }
        Cookfs_FsindexUpdateEntryFileSize(entry, dataSize);
        entry->fileTime = mtime;

        Tcl_WideInt currentOffset = 0;
        int currentBlockNumber = 0;
        Tcl_WideInt bytesLeft = dataSize;

        while (bytesLeft) {

            Tcl_WideInt bytesToWrite = (bytesLeft > w->pageSize ?
                w->pageSize : bytesLeft);

            CookfsLog(printf("Cookfs_WriterAddFile: want to write %ld bytes...",
                bytesToWrite));

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
                    Cookfs_WriterSetLastError(w, "could not read specified"
                        " amount of bytes from the file");
                    goto error;
                }
            }

            // Try to add page
            CookfsLog(printf("Cookfs_WriterAddFile: add page..."));
            int block = Cookfs_PageAddRaw(w->pages,
                (readBuffer == NULL ?
                (char *)data + currentOffset : readBuffer), bytesToWrite);
            CookfsLog(printf("Cookfs_WriterAddFile: got block index: %d", block));

            if (block < 0) {
                Tcl_Obj *err = Cookfs_PagesGetLastError(w->pages);
                Cookfs_WriterSetLastErrorObj(w, Tcl_ObjPrintf("error while adding"
                    " page: %s",
                    (err == NULL ? "unknown error" : Tcl_GetString(err))));
                w->fatalError = 1;
                goto error;
            }

            CookfsLog(printf("Cookfs_WriterAddFile: update block number %d"
                " of fsindex entry...", currentBlockNumber));
            Cookfs_FsindexUpdateEntryBlock(w->index, entry, currentBlockNumber,
                block, 0, bytesToWrite);

            currentBlockNumber++;
            currentOffset += bytesToWrite;
            bytesLeft -= bytesToWrite;

        }

        // Unset entry to avoid releasing it at the end
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
        Cookfs_FsindexUnset(w->index, pathObj);
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
    Cookfs_WriterBuffer *wba = *(Cookfs_WriterBuffer **)a;
    Cookfs_WriterBuffer *wbb = *(Cookfs_WriterBuffer **)b;
    return strcmp(wba->sortKeyStr, wbb->sortKeyStr);
}

int Cookfs_WriterPurge(Cookfs_Writer *w) {

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
        Cookfs_WriterSetLastError(w, "failed to alloc sortedWB");
        goto fatalError;
    }

    char *justDot = ".";

    // Fill the buffer
    for (i = 0, wb = w->bufferFirst; wb != NULL; i++, wb = wb->next) {

        CookfsLog(printf("Cookfs_WriterPurge: add buffer [%p] size %ld"
                " to sort buffer at #%d", (void *)wb->buffer,
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
                CookfsLog(printf("Cookfs_WriterPurge: check if [%p] equals"
                    " to [%p]", (void *)wb->buffer,
                    (void *)wbc->buffer));
                if ((wb->bufferSize == wbc->bufferSize)
                    && memcmp(wb->buffer, wbc->buffer, wb->bufferSize) == 0)
                {
                    // We found the same buffer
                    CookfsLog(printf("Cookfs_WriterPurge: the same buffer"
                        " has been found"));
                    // Let's use its sort key
                    wb->sortKey = wbc->sortKey;
                    Tcl_IncrRefCount(wb->sortKey);
                    wb->sortKeyStr = wbc->sortKeyStr;
                    wb->sortKeyLen = wbc->sortKeyLen;
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

        // Let's generate a sort key for the file
        int pathLength;
        if (Tcl_ListObjLength(NULL, wb->pathObj, &pathLength) != TCL_OK ||
            pathLength < 1
        ) {
            goto fatalErrorCantHappened;
        }

        // Get file name
        Tcl_Obj *pathTail;
        if (Tcl_ListObjIndex(NULL, wb->pathObj, pathLength - 1, &pathTail)
            != TCL_OK || pathTail == NULL)
        {
            goto fatalErrorCantHappened;
        }

        Tcl_Obj *sortPrefix;
        pathTail = Tcl_DuplicateObj(pathTail);
        Tcl_IncrRefCount(pathTail);

        // Let's move extension to front of filename
        int pathTailLength;
        char *pathTailStr = Tcl_GetStringFromObj(pathTail, &pathTailLength);
        // Check to see if we have an empty filename, this shouldn't
        // happen, but who knows?
        if (!pathTailLength) {
            goto skipExtension;
        }
        char *dotPosition = strrchr(pathTailStr, '.');
        // No dot or dot at the first position? then skip
        if (dotPosition == NULL || dotPosition == pathTailStr) {
            goto skipExtension;
        }
        int nameLength = dotPosition - pathTailStr;
        // Create a new object with extension only
        sortPrefix = Tcl_NewStringObj(++dotPosition,
            pathTailLength - nameLength - 1);
        Tcl_IncrRefCount(sortPrefix);
        // Append dot symbol
        Tcl_AppendToObj(sortPrefix, justDot, 1);
        // Append file name without extension
        Tcl_AppendToObj(sortPrefix, pathTailStr, nameLength);

        // pathTail is not needed anymore
        Tcl_DecrRefCount(pathTail);

        goto generateSortKey;

skipExtension:

        // If we have no extension, than just use the filename as
        // the sortPrefix. We don't create a new object and we don't decrement
        // refcount on pathTail. Later, we will reduce the refcount for
        // sortPrefix, which essentially means reducing the refcount
        // for pathTail.
        sortPrefix = pathTail;

generateSortKey: ; // empty statement

        // Let's generate our sort key. First, use the split path
        // as the source.
        Tcl_Obj *sortKeySplit = Tcl_DuplicateObj(wb->pathObj);
        Tcl_IncrRefCount(sortKeySplit);

        // Add sortPrefix to the beggining
        if (Tcl_ListObjReplace(NULL, sortKeySplit, 0, 0, 1, &sortPrefix)
            != TCL_OK)
        {
            // Don't forget to cleanup
            Tcl_DecrRefCount(sortPrefix);
            Tcl_DecrRefCount(sortKeySplit);
            goto fatalErrorCantHappened;
        }

        // sortPrefix is not needed anymore
        Tcl_DecrRefCount(sortPrefix);

        // Generate the sort key by compining split sort key
        wb->sortKey = Tcl_FSJoinPath(sortKeySplit, -1);
        Tcl_IncrRefCount(wb->sortKey);

        // We don't need split sort key anymore
        Tcl_DecrRefCount(sortKeySplit);

        // Optimize a bit
        wb->sortKeyStr = Tcl_GetStringFromObj(wb->sortKey, &wb->sortKeyLen);

        CookfsLog(printf("Cookfs_WriterPurge: generated the sort key [%s]",
            Tcl_GetString(wb->sortKey)));

    }

    // If we have more than 2 buffers, then sort them
    if (w->bufferCount > 2) {
        CookfsLog( \
            printf("Cookfs_WriterPurge: == entries ===========> \n"); \
            for (i = 0; i < w->bufferCount; i++) { \
                printf("Cookfs_WriterPurge: %p %s\n", \
                    (void *)sortedWB[i]->buffer, \
                    (char *)sortedWB[i]->sortKeyStr); \
            }; \
            printf("Cookfs_WriterPurge: <======================")
        );
        CookfsLog(printf("Cookfs_WriterPurge: sort buffers..."));
        qsort(sortedWB, w->bufferCount, sizeof(Cookfs_WriterBuffer *),
            Cookfs_WriterPurgeSortFunc);
        CookfsLog( \
            printf("Cookfs_WriterPurge: == entries ===========> \n"); \
            for (i = 0; i < w->bufferCount; i++) { \
                printf("Cookfs_WriterPurge: %p %s\n", \
                    (void *)sortedWB[i]->buffer, \
                    (char *)sortedWB[i]->sortKeyStr); \
            }; \
            printf("Cookfs_WriterPurge: <======================")
        );
    } else {
        CookfsLog(printf("Cookfs_WriterPurge: no need to sort buffers"));
    }

    // If our small buffer has fewer bytes than the page size, we will
    // allocate in the page buffer only what is needed to store all
    // the small buffer files.
    CookfsLog(printf("Cookfs_WriterPurge: alloc page buffer for %ld bytes",
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

            CookfsLog(printf("Cookfs_WriterPurge: add buffer [%p] size %ld"
                " to page buffer", (void *)wb->buffer, wb->bufferSize));

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
            pageBlock = Cookfs_PageAddRaw(w->pages, pageBuffer, pageBufferSize);
            CookfsLog(printf("Cookfs_WriterPurge: got block index: %d",
                pageBlock));

            if (pageBlock < 0) {
                Tcl_Obj *err = Cookfs_PagesGetLastError(w->pages);
                Cookfs_WriterSetLastErrorObj(w, Tcl_ObjPrintf("error while adding"
                    " page of small files: %s",
                    (err == NULL ? "unknown error" : Tcl_GetString(err))));
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

            Cookfs_FsindexUpdatePendingEntry(w->index, wb->entry,
                wb->pageBlock, wb->pageOffset);

        }

    }

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

fatalErrorCantHappened:

    Cookfs_WriterSetLastError(w, "this case doesn't have to happen");

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
        " data [%p] size [%ld]", (void *)wb, (void *)wb->buffer,
        wb->bufferSize));

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
    return w->isWriteToMemory;
}

void Cookfs_WriterSetWritetomemory(Cookfs_Writer *w, int status) {
    w->isWriteToMemory = status;
    return;
}

Tcl_WideInt Cookfs_WriterGetSmallfilebuffersize(Cookfs_Writer *w) {
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
