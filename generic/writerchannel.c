/*
 * writerchannel.c
 *
 * Provides implementation for writer channel
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "writerchannel.h"
#include "writerchannelIO.h"

static int Cookfs_CreateWriterchannelCreate(Cookfs_WriterChannelInstData
    *instData, Tcl_Interp *interp)
{
    char channelName[128];
    sprintf(channelName, "cookfswriter%p", (void *)instData);
    instData->channel = Tcl_CreateChannel(CookfsWriterChannel(),
        channelName, (ClientData) instData, TCL_READABLE | TCL_WRITABLE);

    if (instData->channel == NULL) {
        CookfsLog(printf("Cookfs_CreateWriterchannelCreate: Unable to create"
            " channel"));
        return TCL_ERROR;
    }

    Tcl_RegisterChannel(interp, instData->channel);
    Tcl_CreateCloseHandler(instData->channel,
        Cookfs_Writerchannel_CloseHandler, (ClientData)instData);

    Tcl_SetChannelOption(interp, instData->channel, "-blocking", "0");

    return TCL_OK;
}

static Cookfs_WriterChannelInstData *Cookfs_CreateWriterchannelAlloc(
    Cookfs_Pages *pages, Cookfs_Fsindex *index, Cookfs_Writer *writer,
    Cookfs_PathObj *pathObj, Cookfs_FsindexEntry *entry, Tcl_Interp *interp,
    Tcl_WideInt initialBufferSize)
{

    CookfsLog(printf("Cookfs_CreateWriterchannelAlloc: start,"
        " initial buff size [%" TCL_LL_MODIFIER "d]", initialBufferSize));

    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *)ckalloc(
        sizeof(Cookfs_WriterChannelInstData));

    if (instData == NULL) {
        CookfsLog(printf("Cookfs_CreateWriterchannelAlloc: failed"));
        return NULL;
    }

    if (initialBufferSize) {
        instData->bufferSize = initialBufferSize;
        instData->buffer = ckalloc(initialBufferSize);
        if (instData->buffer == NULL) {
            CookfsLog(printf("Cookfs_CreateWriterchannelAlloc: failed"
                " to alloc buffer"));
            ckfree(instData);
            return NULL;
        }
    } else {
        instData->buffer = NULL;
        instData->bufferSize = 0;
    }

    instData->channel = NULL;
    instData->event = NULL;
    instData->interp = interp;
    instData->closeResult = NULL;

    instData->pages = pages;
    if (pages != NULL) {
        Cookfs_PagesLockSoft(pages);
    }
    instData->index = index;
    Cookfs_FsindexLockSoft(index);
    instData->writer = writer;
    Cookfs_WriterLockSoft(writer);
    instData->entry = entry;
    if (entry != NULL) {
        Cookfs_FsindexEntryLock(entry);
    }

    instData->pathObj = pathObj;
    if (pathObj != NULL) {
        Cookfs_PathObjIncrRefCount(pathObj);
    }

    instData->currentOffset = 0;
    instData->currentSize = 0;

    CookfsLog(printf("Cookfs_CreateWriterchannelAlloc: ok [%p]",
        (void *)instData));

    return instData;
}

void Cookfs_CreateWriterchannelFree(Cookfs_WriterChannelInstData *instData) {

    CookfsLog(printf("Cookfs_CreateWriterchannelFree: freeing channel"
        " [%s] at [%p]", Tcl_GetChannelName(instData->channel),
        (void *)instData));

    if (instData->event != NULL) {
        instData->event->instData = NULL;
        instData->event = NULL;
    }

    if (instData->closeResult != NULL) {
        Tcl_DecrRefCount(instData->closeResult);
    }

    if (instData->buffer != NULL) {
        ckfree(instData->buffer);
    }
    if (instData->pathObj != NULL) {
        Cookfs_PathObjDecrRefCount(instData->pathObj);
    }
    if (instData->entry != NULL) {
        Cookfs_FsindexEntryUnlock(instData->entry);
    }

    Cookfs_FsindexUnlockSoft(instData->index);
    if (instData->pages != NULL) {
        Cookfs_PagesUnlockSoft(instData->pages);
    }
    Cookfs_WriterUnlockSoft(instData->writer);

    ckfree((void *)instData);
    CookfsLog(printf("Cookfs_CreateWriterchannelFree: ok"))

}

Tcl_Channel Cookfs_CreateWriterchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *index, Cookfs_Writer *writer, Cookfs_PathObj *pathObj,
    Cookfs_FsindexEntry *entry, Tcl_Interp *interp)
{

    CookfsLog(printf("Cookfs_CreateWriterchannel: start"));

    Cookfs_WriterChannelInstData *instData = Cookfs_CreateWriterchannelAlloc(
        pages, index, writer, pathObj, entry, interp,
        (entry == NULL ? 0 : Cookfs_FsindexEntryGetFilesize(entry)));

    Tcl_Obj *err = NULL;

    if (instData == NULL) {
        err = Tcl_NewStringObj("failed to alloc", -1);
        goto errorReturn;
    }

    Cookfs_PageObj blockObj = NULL;

    if (Cookfs_CreateWriterchannelCreate(instData, interp) != TCL_OK) {
        CookfsLog(printf("Cookfs_CreateWriterchannel: "
            "Cookfs_CreateWriterchannelCreate failed"))
        Cookfs_CreateWriterchannelFree(instData);
        err = Tcl_NewStringObj("failed to create a channel", -1);
        goto errorReturn;
    }

    // We now have a channel that was associated with instData. This channel
    // will attempt to release instData when closed. This means that we
    // should not release instData manually (e.g. on an error), as this would
    // result in a double release when closing the channel. Instead, we have
    // to close the channel.

    if (entry == NULL) {
        goto done;
    }

    CookfsLog(printf("Cookfs_CreateWriterchannel: reading existing data..."));
    if (!Cookfs_FsindexLockRead(index, &err)) {
        goto error;
    }
    if (!Cookfs_WriterLockRead(writer, &err)) {
        goto error;
    }
    int firstTimeRead = 1;
    int blockCount = Cookfs_FsindexEntryGetBlockCount(entry);
    for (int i = 0; i < blockCount; i++) {

        int pageIndex, pageOffset, pageSize;
        Cookfs_FsindexEntryGetBlock(entry, i, &pageIndex, &pageOffset,
            &pageSize);

        CookfsLog(printf("Cookfs_CreateWriterchannel: reading block [%d]"
            " offset [%d] size [%d]", pageIndex, pageOffset, pageSize));

        // Nothing to read from this block
        if (pageSize <= 0) {
            continue;
        }

        Tcl_Size blockSize;
        const char *blockBuffer;

        if (pageIndex < 0) {

            // If block is < 0, this block is in the writer's smallfilebuffer
            CookfsLog(printf("Cookfs_CreateWriterchannel: reading the block"
                " from writer"));

            Tcl_WideInt blockSizeWide;
            blockBuffer = Cookfs_WriterGetBuffer(writer, pageIndex, &blockSizeWide);

            // Returns an error if the pages was unable to retrieve the block
            if (blockBuffer == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as writer failed"));
                err = Tcl_NewStringObj("failed to get a page from writer", -1);
                goto errorAndUnlock;
            }

            blockSize = blockSizeWide;

        } else {

            // If block is >= 0 then get it from pages
            CookfsLog(printf("Cookfs_CreateWriterchannel: reading the block"
                " from pages"));

            int pageUsage = Cookfs_FsindexGetBlockUsage(index, pageIndex);

            if (!Cookfs_PagesLockRead(pages, &err)) {
                goto errorAndUnlock;
            }
            int pageWeight = (pageUsage <= 1) ? 0 : 1;
            if (firstTimeRead) {
                if (!Cookfs_PagesIsCached(pages, pageIndex)) {
                    Cookfs_PagesTickTock(pages);
                }
                firstTimeRead = 0;
            }

            blockObj = Cookfs_PageGet(pages, pageIndex, pageWeight, &err);
            // Do not increate refcount for blockObj as Cookfs_PageGet() returns
            // pages with refcount=1.
            Cookfs_PagesUnlock(pages);

            // Returns an error if the pages was unable to retrieve the block
            if (blockObj == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as pages failed"));
                // We have an error from Cookfs_PageGet() in the err variable
                goto errorAndUnlock;
            }

            blockSize = Cookfs_PageObjSize(blockObj);
            blockBuffer = (const char *)blockObj->buf;

        }

        CookfsLog(printf("Cookfs_CreateWriterchannel: got block size [%"
            TCL_SIZE_MODIFIER "d]", blockSize));

        // Check if we have enough bytes in the block
        if ((pageOffset + pageSize) > blockSize) {
            CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: not enough"
                " bytes in block, return an error"));
            err = Tcl_NewStringObj("got malformed page", -1);
            goto errorAndUnlock;
        }

        CookfsLog(printf("Cookfs_CreateWriterchannel: push [%d] bytes"
            " from retrieved block to the channel", pageSize));

        int errorCode;
        int writtenBytes = Tcl_ChannelOutputProc(CookfsWriterChannel())(
            (ClientData)instData, blockBuffer + pageOffset, pageSize, &errorCode);

        if (writtenBytes != pageSize) {
            CookfsLog(printf("Cookfs_CreateWriterchannel: only [%d]"
                " bytes were written to the channel, consider this"
                " an error", writtenBytes));
            err = Tcl_NewStringObj("failed to write to the buffer", -1);
            goto errorAndUnlock;
        }

        // We don't need the blockObj object anymore
        if (blockObj != NULL) {
            Cookfs_PageObjDecrRefCount(blockObj);
            blockObj = NULL;
        }

    }
    Cookfs_FsindexUnlock(index);
    Cookfs_WriterUnlock(writer);
    // Set current position to the start of file
    instData->currentOffset = 0;
    CookfsLog(printf("Cookfs_CreateWriterchannel: reading of existing data"
        " is completed"));

done:

    CookfsLog(printf("Cookfs_CreateWriterchannel: ok [%s]",
        Tcl_GetChannelName(instData->channel)));

    return instData->channel;

errorAndUnlock:
    Cookfs_FsindexUnlock(index);
    Cookfs_WriterUnlock(writer);

error:

    if (blockObj != NULL) {
        Cookfs_PageObjDecrRefCount(blockObj);
    }

    if (instData->channel != NULL) {
        // If we encounter an error, we must close the channel. However,
        // the close procedure may attempt to write the file from the buffer
        // to the archive. We have to prevent this because the file could
        // have been read incorrectly. To do this, we will unset pathObj
        // from instData. Then the close procedure will think we opened
        // the file in read-only mode and will not try to modify it in archive.
        if (instData->pathObj != NULL) {
            Cookfs_PathObjDecrRefCount(instData->pathObj);
            instData->pathObj = NULL;
        }
        Tcl_UnregisterChannel(interp, instData->channel);
    }

errorReturn:

    if (interp != NULL) {

        if (err == NULL) {
            err = Tcl_NewStringObj("unknown error", -1);
        }

        Tcl_SetObjResult(interp, err);

    } else if (err != NULL) {
        Tcl_BounceRefCount(err);
    }

    return NULL;

}

