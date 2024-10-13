/*
 * readerchannel.c
 *
 * Provides implementation for reader channel
 *
 * (c) 2010-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "readerchannel.h"
#include "readerchannelIO.h"
#include <errno.h>

static Tcl_ChannelType cookfsReaderChannel = {
    "cookfsreader",
    TCL_CHANNEL_VERSION_5,
#if TCL_MAJOR_VERSION < 9
    Cookfs_Readerchannel_Close,
#else
    NULL,
#endif
    Cookfs_Readerchannel_Input,
    Cookfs_Readerchannel_Output,
#if TCL_MAJOR_VERSION < 9
    Cookfs_Readerchannel_Seek,
#else
    NULL,
#endif
    NULL,
    NULL,
    Cookfs_Readerchannel_Watch,
    NULL,
    Cookfs_Readerchannel_Close2,
    NULL,
    NULL,
    NULL,
    Cookfs_Readerchannel_WideSeek,
    Cookfs_Readerchannel_ThreadAction,
    NULL,
};

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *fsindex, Cookfs_FsindexEntry *entry,
    Tcl_Interp *interp, char **channelNamePtr)
{

    CookfsLog(printf("welcome"))

    Tcl_Obj *err = NULL;

    CookfsLog(printf("alloc..."));
    Cookfs_ReaderChannelInstData *instData =
        Cookfs_CreateReaderchannelAlloc(pages, fsindex, entry);

    if (instData == NULL) {
        CookfsLog(printf("failed to alloc"));
        err = Tcl_NewStringObj("failed to alloc", -1);
        goto error;
    }

    if (Cookfs_CreateReaderchannelCreate(instData, interp) != TCL_OK) {
        CookfsLog(printf("Cookfs_CreateReaderchannelCreate failed"));
        Cookfs_CreateReaderchannelFree(instData);
        err = Tcl_NewStringObj("failed to create a channel", -1);
        goto error;
    }

    if (entry == NULL) {
        CookfsLog(printf("skip encryption check, entry is NULL"));
        goto done;
    }

    if (!Cookfs_FsindexLockRead(fsindex, &err)) {
        goto error;
    }

    if (Cookfs_FsindexEntryGetBlockCount(entry) < 1) {
        CookfsLog(printf("skip encryption check, block count < 1"));
        goto doneAndUnlock;
    }

    int pageIndex;
    Cookfs_FsindexEntryGetBlock(entry, 0, &pageIndex, NULL, NULL);

    if (pageIndex < 0) {
        CookfsLog(printf("skip encryption check, pageIndex < 0"));
        goto doneAndUnlock;
    }

    if (!Cookfs_PagesLockRead(pages, &err)) {
        CookfsLog(printf("ERROR: failed to lock pages"));
        goto errorAndUnlock;
    }

    if (!Cookfs_PagesIsEncrypted(pages, pageIndex)) {
        CookfsLog(printf("skip encryption check, the page is not encrypted"));
        Cookfs_PagesUnlock(pages);
        goto doneAndUnlock;
    }

    // We know that this is the first read for the file/channel
    if (!Cookfs_PagesIsCached(pages, pageIndex)) {
        Cookfs_PagesTickTock(pages);
    }

    int pageUsage = Cookfs_FsindexGetBlockUsage(fsindex, pageIndex);
    int pageWeight = (pageUsage <= 1) ? 0 : 1;

    instData->cachedPageObj = Cookfs_PageGet(pages, pageIndex, pageWeight,
        &err);

    Cookfs_PagesUnlock(pages);

    // If page read failed, then return an error
    if (instData->cachedPageObj == NULL) {
        CookfsLog(printf("ERROR: encryption check failed"));
        goto errorAndUnlock;
    }

    // We have successfully read the page. Let's save it to instData so we
    // don't have to read it again when a read operation is requested.

    instData->firstTimeRead = 0;
    instData->cachedPageNum = pageIndex;

doneAndUnlock:

    Cookfs_FsindexUnlock(fsindex);

done:

    if (channelNamePtr != NULL) {
        *channelNamePtr = (char *)Tcl_GetChannelName(instData->channel);
    }

    return instData->channel;

errorAndUnlock:

    Cookfs_FsindexUnlock(fsindex);

error:

    if (instData->channel != NULL) {
        Tcl_UnregisterChannel(interp, instData->channel);
    }

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

int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData, Tcl_Interp *interp) {
    char channelName[128];
    sprintf(channelName, "cookfsreader%p", (void *)instData);

    instData->channel = Tcl_CreateChannel(&cookfsReaderChannel, channelName,
        (ClientData) instData, TCL_READABLE);

    if (instData->channel == NULL) {
        CookfsLog(printf("Unable to create channel"))
        return TCL_ERROR;
    }

    Tcl_RegisterChannel(interp, instData->channel);
    Tcl_SetChannelOption(interp, instData->channel, "-buffering", "none");
    Tcl_SetChannelOption(interp, instData->channel, "-blocking", "0");

    return TCL_OK;
}

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages, Cookfs_Fsindex *fsindex, Cookfs_FsindexEntry *entry) {
    Cookfs_ReaderChannelInstData *result;

    result = (Cookfs_ReaderChannelInstData *) ckalloc(sizeof(Cookfs_ReaderChannelInstData));
    result->channel = NULL;
    result->event = NULL;

    result->pages = pages;
    Cookfs_PagesLockSoft(pages);
    result->fsindex = fsindex;
    Cookfs_FsindexLockSoft(fsindex);
    result->entry = entry;
    Cookfs_FsindexEntryLock(entry);

    result->currentOffset = 0;
    result->currentBlock = 0;
    result->currentBlockOffset = 0;

    result->cachedPageObj = NULL;

    result->firstTimeRead = 1;

    return result;
}

void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData) {
    CookfsLog(printf("freeing channel=%s",
        Tcl_GetChannelName(instData->channel)));
    if (instData->event != NULL) {
        instData->event->instData = NULL;
        instData->event = NULL;
    }
    if (instData->cachedPageObj != NULL) {
        Cookfs_PageObjDecrRefCount(instData->cachedPageObj);
    }
    Cookfs_FsindexEntryUnlock(instData->entry);
    Cookfs_FsindexUnlockSoft(instData->fsindex);
    Cookfs_PagesUnlockSoft(instData->pages);
    ckfree((void *) instData);
}
