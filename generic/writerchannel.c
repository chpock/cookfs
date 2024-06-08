/*
 * writerchannel.c
 *
 * Provides implementation for writer channel
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

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
    Cookfs_PathObj *pathObj, Tcl_Interp *interp,
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
    instData->index = index;
    instData->writer = writer;

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

    ckfree((void *)instData);
    CookfsLog(printf("Cookfs_CreateWriterchannelFree: ok"))

}

Tcl_Channel Cookfs_CreateWriterchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *index, Cookfs_Writer *writer, Cookfs_PathObj *pathObj,
    Cookfs_FsindexEntry *entry, Tcl_Interp *interp)
{

    CookfsLog(printf("Cookfs_CreateWriterchannel: start"));

    Cookfs_WriterChannelInstData *instData = Cookfs_CreateWriterchannelAlloc(
        pages, index, writer, pathObj, interp,
        (entry == NULL ? 0 : entry->data.fileInfo.fileSize));

    if (instData == NULL) {
        return NULL;
    }

    Cookfs_PageObj blockObj = NULL;

    if (Cookfs_CreateWriterchannelCreate(instData, interp) != TCL_OK) {
        CookfsLog(printf("Cookfs_CreateWriterchannel: "
            "Cookfs_CreateWriterchannelCreate failed"))
        goto error;
    }

    if (entry == NULL) {
        goto done;
    }

    CookfsLog(printf("Cookfs_CreateWriterchannel: reading existing data..."));
    int firstTimeRead = 1;
    for (int i = 0; i < (entry->fileBlocks * 3); i++) {

        int block = entry->data.fileInfo.fileBlockOffsetSize[i++];
        int offset = entry->data.fileInfo.fileBlockOffsetSize[i++];
        int size = entry->data.fileInfo.fileBlockOffsetSize[i];
        CookfsLog(printf("Cookfs_CreateWriterchannel: reading block [%d]"
            " offset [%d] size [%d]", block, offset, size));

        // Nothing to read from this block
        if (size <= 0) {
            continue;
        }

        Tcl_Size blockSize;
        const char *blockBuffer;

        if (block < 0) {

            // If block is < 0, this block is in the writer's smallfilebuffer
            CookfsLog(printf("Cookfs_CreateWriterchannel: reading the block"
                " from writer"));

            Tcl_WideInt blockSizeWide;
            blockBuffer = Cookfs_WriterGetBuffer(writer, block, &blockSizeWide);

            // Returns an error if the pages was unable to retrieve the block
            if (blockBuffer == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as writer failed"));
                goto error;
            }

            blockSize = blockSizeWide;

        } else {

            // If block is >= 0 then get it from pages
            CookfsLog(printf("Cookfs_CreateWriterchannel: reading the block"
                " from pages"));

            int pageUsage = Cookfs_FsindexGetBlockUsage(index, block);
            int pageWeight = (pageUsage <= 1) ? 0 : 1;
            if (firstTimeRead) {
                if (!Cookfs_PagesIsCached(pages, block)) {
                    Cookfs_PagesTickTock(pages);
                }
                firstTimeRead = 0;
            }

            // TODO: pass a pointer to err variable instead of NULL and produce
            // the corresponding error message
            blockObj = Cookfs_PageGet(pages, block, pageWeight, NULL);

            // Returns an error if the pages was unable to retrieve the block
            if (blockObj == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as pages failed"));
                goto error;
            }

            Cookfs_PageObjIncrRefCount(blockObj);
            blockSize = Cookfs_PageObjSize(blockObj);
            blockBuffer = (const char *)blockObj;

        }

        CookfsLog(printf("Cookfs_CreateWriterchannel: got block size [%"
            TCL_SIZE_MODIFIER "d]", blockSize));

        // Check if we have enough bytes in the block
        if ((offset + size) > blockSize) {
            CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: not enough"
                " bytes in block, return an error"));
            goto error;
        }

        CookfsLog(printf("Cookfs_CreateWriterchannel: push [%d] bytes"
            " from retrieved block to the channel", size));

        int errorCode;
        int writtenBytes = Tcl_ChannelOutputProc(CookfsWriterChannel())(
            (ClientData)instData, blockBuffer + offset, size, &errorCode);

        if (writtenBytes != size) {
            CookfsLog(printf("Cookfs_CreateWriterchannel: only [%d]"
                " bytes were written to the channel, consider this"
                " an error", writtenBytes));
            goto error;
        }

        // We don't need the blockObj object anymore
        if (blockObj != NULL) {
            Cookfs_PageObjDecrRefCount(blockObj);
            blockObj = NULL;
        }

    }
    // Set current position to the start of file
    instData->currentOffset = 0;
    CookfsLog(printf("Cookfs_CreateWriterchannel: reading of existing data"
        " is completed"));

done:

    CookfsLog(printf("Cookfs_CreateWriterchannel: ok [%s]",
        Tcl_GetChannelName(instData->channel)));

    return instData->channel;

error:

    if (blockObj != NULL) {
        Cookfs_PageObjDecrRefCount(blockObj);
    }

    if (instData != NULL) {
        Cookfs_CreateWriterchannelFree(instData);
    }
    return NULL;

}

/* command for creating new objects that deal with pages */
static int CookfsCreateWriterchannelCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if (objc != 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "pagesObject fsindexObject"
            " writerObject relativePath readflag");
        return TCL_ERROR;
    }

    int wantToRead;
    if (Tcl_GetBooleanFromObj(interp, objv[5], &wantToRead) != TCL_OK) {
        return TCL_ERROR;
    }

    Cookfs_Pages *pages = Cookfs_PagesGetHandle(interp,
        Tcl_GetStringFromObj(objv[1], NULL));
    CookfsLog(printf("CookfsCreateWriterchannelCmd: pages [%p]",
        (void *)pages));

    if (pages == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " pages object", -1));
        return TCL_ERROR;
    }

    Cookfs_Fsindex *index = Cookfs_FsindexGetHandle(interp,
        Tcl_GetStringFromObj(objv[2], NULL));
    CookfsLog(printf("CookfsCreateWriterchannelCmd: index [%p]",
        (void *)index));

    if (index == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " fsindex object", -1));
        return TCL_ERROR;
    }

    Cookfs_Writer *writer = Cookfs_WriterGetHandle(interp,
        Tcl_GetStringFromObj(objv[3], NULL));
    CookfsLog(printf("CookfsCreateWriterchannelCmd: writer [%p]",
        (void *)writer));

    if (writer == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " writer object", -1));
        return TCL_ERROR;
    }

    Cookfs_PathObj *pathObj = Cookfs_PathObjNewFromTclObj(objv[4]);
    Cookfs_PathObjIncrRefCount(pathObj);

    // If pathObjLen is 0, it is the root directory. We cannot open
    // the root directory as a writable file.
    if (pathObj->elementCount == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Could not open an empty"
            " file name for writing", -1));
        goto error;
    }

    // Verify that the entry exists and is not a directory, or the entry
    // does not exist but can be created (i.e. it has a parent directory).
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, pathObj);

    // If entry exists, make sure it is not a directory
    if (entry != NULL) {
        if (Cookfs_FsindexEntryIsDirectory(entry)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("file \"%s\" exists"
                " and it is a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    } else {
        // Make sure that parent directory exists
        Cookfs_FsindexEntry *entryParent = CookfsFsindexFindElement(index,
            pathObj, pathObj->elementCount - 1);
        if (entryParent == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since the parent directory"
                " does not exist", Tcl_GetString(objv[4])));
            goto error;
        }

        // Check if parent is a directory
        if (!Cookfs_FsindexEntryIsDirectory(entryParent)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since its parent"
                " is not a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    }

    // Everything looks good. Let's create a channel. If we don't want
    // to read previous data from the file and it should be created
    // from scratch, then pass NULL as entry.

    Tcl_Channel channel = Cookfs_CreateWriterchannel(pages, index, writer,
        pathObj, (wantToRead ? entry : NULL), interp);

    if (channel == NULL) {
        goto error;
    }

    // We don't need the pathObj anymore
    Cookfs_PathObjDecrRefCount(pathObj);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        Tcl_GetChannelName(channel), -1));
    return TCL_OK;

error:

    Cookfs_PathObjDecrRefCount(pathObj);
    return TCL_ERROR;

}

int Cookfs_InitWriterchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::c::writerchannel",
        CookfsCreateWriterchannelCmd, (ClientData) NULL, NULL);
    return TCL_OK;
}
