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
    Cookfs_Pages *pages, Cookfs_Fsindex *index, Tcl_Obj *writerObjCmd,
    Tcl_Obj *pathObj, int pathObjLen, Tcl_Interp *interp,
    Tcl_WideInt initialBufferSize)
{

    CookfsLog(printf("Cookfs_CreateWriterchannelAlloc: start,"
        " initial buff size [%ld]", initialBufferSize));

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
    instData->writerObjCmd = writerObjCmd;
    Tcl_IncrRefCount(writerObjCmd);
    instData->pathObjLen = pathObjLen;
    instData->pathObj = pathObj;
    if (pathObj != NULL) {
        Tcl_IncrRefCount(pathObj);
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
        Tcl_DecrRefCount(instData->pathObj);
    }
    Tcl_DecrRefCount(instData->writerObjCmd);

    ckfree((void *)instData);
    CookfsLog(printf("Cookfs_CreateWriterchannelFree: ok"))

}

Tcl_Channel Cookfs_CreateWriterchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *index, Tcl_Obj *writerObjCmd, Tcl_Obj *pathObj,
    int pathObjLen, Cookfs_FsindexEntry *entry, Tcl_Interp *interp)
{

    CookfsLog(printf("Cookfs_CreateWriterchannel: start"));

    Cookfs_WriterChannelInstData *instData = Cookfs_CreateWriterchannelAlloc(
        pages, index, writerObjCmd, pathObj, pathObjLen, interp,
        (entry == NULL ? 0 : entry->data.fileInfo.fileSize));

    if (instData == NULL) {
        return NULL;
    }

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

        Tcl_Obj *blockObj;

        // If block is < 0, this block is in the writer's smallfilebuffer
        if (block < 0) {

            // Create the writer command to retrieve the required block
            Tcl_Obj *writerCmd = Tcl_NewListObj(0, NULL);
            Tcl_IncrRefCount(writerCmd);

            Tcl_ListObjAppendElement(interp, writerCmd, writerObjCmd);
            Tcl_ListObjAppendElement(interp, writerCmd,
                Tcl_NewStringObj("getbuf", -1));
            Tcl_ListObjAppendElement(interp, writerCmd,
                Tcl_NewIntObj(block));

            // Exec the writer command
            CookfsLog(printf("Cookfs_CreateWriterchannel: exec writer to get"
                " block [%d]", block));
            int ret = Tcl_EvalObjEx(instData->interp, writerCmd,
                TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
            CookfsLog(printf("Cookfs_CreateWriterchannel: writer"
                " returned: %d", ret));

            // We don't need the writer command anymore
            Tcl_DecrRefCount(writerCmd);

            // Returns an error if the writer was unable to retrieve the block
            if (ret != TCL_OK) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as writer failed"));
                goto error;
            }

            blockObj = Tcl_GetObjResult(interp);
            if (blockObj == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: failed to get"
                    " result from Tcl"));
                goto error;
            }

            Tcl_IncrRefCount(blockObj);

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

            blockObj = Cookfs_PageGet(pages, block, pageWeight);

            // Returns an error if the pages was unable to retrieve the block
            if (blockObj == NULL) {
                CookfsLog(printf("Cookfs_CreateWriterchannel: return an error"
                    " as pages failed"));
                goto error;
            }

            Tcl_IncrRefCount(blockObj);

        }

        int blockSize;
        const char *blockBuffer = (char *)Tcl_GetByteArrayFromObj(blockObj,
            &blockSize);

        CookfsLog(printf("Cookfs_CreateWriterchannel: got block size [%d]",
            blockSize));

        // Check if we have enough bytes in the block
        if ((offset + size) > blockSize) {
            CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: not enough"
                " bytes in block, return an error"));
            Tcl_DecrRefCount(blockObj);
            goto error;
        }

        CookfsLog(printf("Cookfs_CreateWriterchannel: push [%d] bytes"
            " from retrieved block to the channel", size));

        int errorCode;
        int writtenBytes = Tcl_ChannelOutputProc(CookfsWriterChannel())(
            (ClientData)instData, blockBuffer + offset, size, &errorCode);

        // We don't need the blockObj object anymore
        Tcl_DecrRefCount(blockObj);

        if (writtenBytes != size) {
            CookfsLog(printf("Cookfs_CreateWriterchannel: only [%d]"
                " bytes were written to the channel, consider this"
                " an error", writtenBytes));
            goto error;
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

    // TODO: Validate somehow the specified writer object? May be try to execute
    // some known method?
    Tcl_Obj *writerObjCmd = objv[3];

    int pathObjLen;
    Tcl_Obj *pathObj = Tcl_FSSplitPath(objv[4], &pathObjLen);
    if (pathObj == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to use"
            " file name \"%s\"", Tcl_GetString(objv[4])));
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(pathObj);

    // If pathObjLen is 0, it is the root directory. We cannot open
    // the root directory as a writable file.
    if (pathObjLen == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Could not open an empty"
            " file name for writing", -1));
        goto error;
    }

    // Verify that the entry exists and is not a directory, or the entry
    // does not exist but can be created (i.e. it has a parent directory).
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, pathObj);

    // If entry exists, make sure it is not a directory
    if (entry != NULL) {
        int isDirectory = entry->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY ?
            1 : 0;
        if (isDirectory) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("file \"%s\" exists"
                " and it is a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    } else {
        // Make sure that parent directory exists
        Cookfs_FsindexEntry *entryParent = CookfsFsindexFindElement(index,
            pathObj, pathObjLen - 1);
        if (entryParent == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since the parent directory"
                " does not exist", Tcl_GetString(objv[4])));
            goto error;
        }

        // Check if parent is a directory
        if (entryParent->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since its parent"
                " is not a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    }

    // Everything looks good. Let's create a channel. If we don't want
    // to read previous data from the file and it should be created
    // from scratch, then pass NULL as entry.

    Tcl_Channel channel = Cookfs_CreateWriterchannel(pages, index,
        writerObjCmd, pathObj, pathObjLen, (wantToRead ? entry : NULL),
        interp);

    if (channel == NULL) {
        goto error;
    }

    // We don't need the pathObj anymore
    Tcl_DecrRefCount(pathObj);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        Tcl_GetChannelName(channel), -1));
    return TCL_OK;

error:

    Tcl_DecrRefCount(pathObj);
    return TCL_ERROR;

}

int Cookfs_InitWriterchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::c::writerchannel",
        CookfsCreateWriterchannelCmd, (ClientData) NULL, NULL);
    return TCL_OK;
}
