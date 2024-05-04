/*
 * readerchannel.c
 *
 * Provides implementation for reader channel
 *
 * (c) 2010-2014 Wojciech Kocjan
 */

#include "cookfs.h"
#include <errno.h>

static int cookfsChannelId = 0;

static Tcl_ChannelType cookfsReaderChannel = {
    "cookfsreader",
    TCL_CHANNEL_VERSION_5,
    Cookfs_Readerchannel_Close,
    Cookfs_Readerchannel_Input,
    Cookfs_Readerchannel_Output,
    Cookfs_Readerchannel_Seek,
    NULL,
    NULL,
    Cookfs_Readerchannel_Watch,
    NULL,
    Cookfs_Readerchannel_Close2,
    NULL,
    NULL,
    NULL,
    Cookfs_Readerchannel_WideSeek,
    NULL,
    NULL,
};

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Cookfs_Fsindex *fsindex, Tcl_Obj *listObj, Tcl_Interp *interp, char **channelNamePtr)
{
    Tcl_WideInt fileSize = 0;
    Tcl_Obj **listObjv;
    int listObjc;
    Cookfs_ReaderChannelInstData *instData = NULL;
    int i;

    CookfsLog(printf("Cookfs_CreateReaderchannel: welcome"))

    if (Tcl_ListObjGetElements(interp, listObj, &listObjc, &listObjv) != TCL_OK) {
	return NULL;
    }

    CookfsLog(printf("Cookfs_CreateReaderchannel: listObjc = %d", listObjc))

    if ((listObjc % 3) != 0) {
	if (interp != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid page-offset-size list length", -1));
	}
	return NULL;
    }

    instData = Cookfs_CreateReaderchannelAlloc(pages, fsindex, listObjc);

    if (instData == NULL) {
	return NULL;
    }

    CookfsLog(printf("Cookfs_CreateReaderchannel: alloc"))

    for (i = 0; i < listObjc; i++) {
	if (Tcl_GetIntFromObj(interp, listObjv[i], &instData->buf[i]) != TCL_OK) {
	    CookfsLog(printf("Cookfs_CreateReaderchannel: buf[%d] failed", i))
	    Cookfs_CreateReaderchannelFree(instData);
	    return NULL;
	}
	CookfsLog(printf("Cookfs_CreateReaderchannel: buf[%d] = %d", i, instData->buf[i]))
    }

    for (i = 2; i < listObjc; i += 3) {
	fileSize = fileSize + ((Tcl_WideInt) instData->buf[i]);
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
	*channelNamePtr = instData->channelName;
    }

    return instData->channel;
}

int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData, Tcl_Interp *interp) {
    /* TODO: mutex */
    sprintf(instData->channelName, "cookfsreader%d", ++cookfsChannelId);
    instData->channel = Tcl_CreateChannel(&cookfsReaderChannel, instData->channelName, (ClientData) instData, TCL_READABLE);

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

    result = (Cookfs_ReaderChannelInstData *) Tcl_Alloc(sizeof(Cookfs_ReaderChannelInstData) + bufSize * sizeof(int));
    result->channel = NULL;
    result->watchTimer = NULL;

    result->pages = pages;
    result->fsindex = fsindex;

    result->currentOffset = 0;
    result->currentBlock = 0;
    result->currentBlockOffset = 0;

    result->fileSize = 0;
    result->bufSize = bufSize;

    result->firstTimeRead = 1;

    return result;
}

void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData) {
    CookfsLog(printf("Cookfs_CreateReaderchannelFree: freeing channel=%s\n", instData->channelName))
    strcpy(instData->channelName, "INVALID");
    if (instData->watchTimer != NULL) {
    	CookfsLog(printf("Cookfs_CreateReaderchannelFree: deleting target"))
	Tcl_DeleteTimerHandler(instData->watchTimer);
    }
    CookfsLog(printf("Cookfs_CreateReaderchannelFree: before free"))
    Tcl_Free((void *) instData);
    CookfsLog(printf("Cookfs_CreateReaderchannelFree: after free"))
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

    CookfsLog(printf("CookfsCreateReaderchannelCmd: fsindex [%p]", fsindex));
    if (fsindex == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find fsindex object", -1));
	return TCL_ERROR;
    }

    channel = Cookfs_CreateReaderchannel(pages, fsindex, objv[3], interp, &channelName);

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
