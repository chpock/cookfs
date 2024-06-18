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

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Cookfs_Fsindex *fsindex, Cookfs_FsindexEntry *entry, Tcl_Interp *interp, char **channelNamePtr)
{
    Cookfs_ReaderChannelInstData *instData = NULL;

    CookfsLog(printf("Cookfs_CreateReaderchannel: welcome"))

    instData = Cookfs_CreateReaderchannelAlloc(pages, fsindex, entry);

    if (instData == NULL) {
        return NULL;
    }

    CookfsLog(printf("Cookfs_CreateReaderchannel: alloc"))

    if (Cookfs_CreateReaderchannelCreate(instData, interp) != TCL_OK) {
	CookfsLog(printf("Cookfs_CreateReaderchannel: Cookfs_CreateReaderchannelCreate failed"))
	Cookfs_CreateReaderchannelFree(instData);
	return NULL;
    }

    if (channelNamePtr != NULL)
    {
	*channelNamePtr = (char *)Tcl_GetChannelName(instData->channel);
    }

    return instData->channel;
}

int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData, Tcl_Interp *interp) {
    char channelName[128];
    sprintf(channelName, "cookfsreader%p", (void *)instData);

    instData->channel = Tcl_CreateChannel(&cookfsReaderChannel, channelName,
        (ClientData) instData, TCL_READABLE);

    if (instData->channel == NULL) {
	CookfsLog(printf("Cookfs_CreateReaderchannelCreate: Unable to create channel"))
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
    CookfsLog(printf("Cookfs_CreateReaderchannelFree: freeing channel=%s",
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
