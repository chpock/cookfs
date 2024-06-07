/*
 * readerchannel.c
 *
 * Provides implementation for reader channel
 *
 * (c) 2010-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
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

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Cookfs_Fsindex *fsindex, Tcl_Obj *listObj, Cookfs_FsindexEntry *entry, Tcl_Interp *interp, char **channelNamePtr)
{
    Tcl_WideInt fileSize = 0;
    Tcl_Obj **listObjv;
    Tcl_Size listObjc;
    Cookfs_ReaderChannelInstData *instData = NULL;
    Tcl_Size i;

    CookfsLog(printf("Cookfs_CreateReaderchannel: welcome"))

    if (listObj != NULL) {

        if (Tcl_ListObjGetElements(interp, listObj, &listObjc, &listObjv) != TCL_OK) {
            return NULL;
        }

        CookfsLog(printf("Cookfs_CreateReaderchannel: listObjc = %"
            TCL_SIZE_MODIFIER "d", listObjc))

        if ((listObjc % 3) != 0) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid page-offset-size list length", -1));
            }
            return NULL;
        }

    } else if (entry != NULL) {
        CookfsLog(printf("Cookfs_CreateReaderchannel: init by fsindex entry [%p]", (void *)entry));
        listObjc = entry->fileBlocks * 3;
    } else {
        return NULL;
    }

    instData = Cookfs_CreateReaderchannelAlloc(pages, fsindex, listObjc);

    if (instData == NULL) {
        return NULL;
    }

    CookfsLog(printf("Cookfs_CreateReaderchannel: alloc"))

    if (listObj != NULL) {
        for (i = 0; i < listObjc; i++) {
            if (Tcl_GetIntFromObj(interp, listObjv[i], &instData->buf[i]) != TCL_OK) {
                CookfsLog(printf("Cookfs_CreateReaderchannel: buf[%"
                    TCL_SIZE_MODIFIER "d] failed", i))
                Cookfs_CreateReaderchannelFree(instData);
                return NULL;
            }
            CookfsLog(printf("Cookfs_CreateReaderchannel: buf[%"
                TCL_SIZE_MODIFIER "d] = %d", i, instData->buf[i]))
        }

        for (i = 2; i < listObjc; i += 3) {
            fileSize = fileSize + ((Tcl_WideInt) instData->buf[i]);
        }
    } else {
        for (i = 0; i < listObjc; i++) {
            instData->buf[i] = entry->data.fileInfo.fileBlockOffsetSize[i];
        }

        fileSize = entry->data.fileInfo.fileSize;
    }

    CookfsLog(printf("Cookfs_CreateReaderchannel: fileSize=%d", ((int) fileSize)))
    instData->fileSize = fileSize;

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

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages, Cookfs_Fsindex *fsindex, int bufSize) {
    Cookfs_ReaderChannelInstData *result;

    result = (Cookfs_ReaderChannelInstData *) ckalloc(sizeof(Cookfs_ReaderChannelInstData) + bufSize * sizeof(int));
    result->channel = NULL;
    result->event = NULL;

    result->pages = pages;
    result->fsindex = fsindex;

    result->currentOffset = 0;
    result->currentBlock = 0;
    result->currentBlockOffset = 0;

    result->fileSize = 0;
    result->bufSize = bufSize;

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
    ckfree((void *) instData);
}

/* command for creating new objects that deal with pages */
static int CookfsCreateReaderchannelCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Cookfs_Pages *pages;
    Cookfs_Fsindex *fsindex;
    Tcl_Channel channel;
    char *channelName;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "pagesObject fsindexObject chunkList");
        return TCL_ERROR;
    }

    pages = Cookfs_PagesGetHandle(interp, Tcl_GetStringFromObj(objv[1], NULL));

    if (pages == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find pages object", -1));
	return TCL_ERROR;
    }

    fsindex = Cookfs_FsindexGetHandle(interp, Tcl_GetStringFromObj(objv[2], NULL));

    CookfsLog(printf("CookfsCreateReaderchannelCmd: fsindex [%p]", (void *)fsindex));
    if (fsindex == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find fsindex object", -1));
	return TCL_ERROR;
    }

    channel = Cookfs_CreateReaderchannel(pages, fsindex, objv[3], NULL, interp, &channelName);

    if (channel == NULL) {
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(channelName, -1));

    return TCL_OK;
}

int Cookfs_InitReaderchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::c::readerchannel", CookfsCreateReaderchannelCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
}
