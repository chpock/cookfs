/* (c) 2010 Wojciech Kocjan */

#include "cookfs.h"
#include <errno.h>

static int cookfsChannelId = 0;

static Tcl_ChannelType cookfsReaderChannel = {
    "cookfsreader",
    TCL_CHANNEL_VERSION_4,
    Cookfs_Readerchannel_Close,
    Cookfs_Readerchannel_Input,
    Cookfs_Readerchannel_Output,
    Cookfs_Readerchannel_Seek,
    NULL,
    NULL,
    Cookfs_Readerchannel_Watch,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    Cookfs_Readerchannel_WideSeek,
    NULL,
};

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Tcl_Obj *listObj, Tcl_Interp *interp) {
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

    instData = Cookfs_CreateReaderchannelAlloc(pages, listObjc);
    
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

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages, int bufSize) {
    Cookfs_ReaderChannelInstData *result;
    
    result = (Cookfs_ReaderChannelInstData *) Tcl_Alloc(sizeof(Cookfs_ReaderChannelInstData) + bufSize * sizeof(int));
    result->channel = NULL;
    result->watchTimer = NULL;

    result->pages = pages;
    
    result->currentOffset = 0;
    result->currentBlock = 0;
    result->currentBlockOffset = 0;

    result->fileSize = 0;
    result->bufSize = bufSize;

    return result;
}

void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData) {
    Tcl_Free((void *) instData);
}

int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp) {
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;
    Cookfs_CreateReaderchannelFree(instData);
    return 0;
}

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr) {
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;
    Tcl_Obj *pageObj;
    int bytesRead = 0;
    int bytesLeft;
    int blockLeft;
    int blockRead;
    char *pageBuf;

    CookfsLog(printf("Cookfs_Readerchannel_Input: read %d", bufSize))

    if (instData->currentBlock >= instData->bufSize) {
	CookfsLog(printf("Cookfs_Readerchannel_Input: EOF reached"))
	return 0;
    }

    while (bytesRead < bufSize) {
	bytesLeft = bufSize - bytesRead;
	blockLeft = instData->buf[instData->currentBlock + 2] - instData->currentBlockOffset;
	CookfsLog(printf("Cookfs_Readerchannel_Input: blockLeft = %d, bytesRead = %d", blockLeft, bytesRead))

	if (blockLeft == 0) {
	    instData->currentBlock += 3;
	    CookfsLog(printf("Cookfs_Readerchannel_Input: block %d - %d", instData->currentBlock, instData->bufSize))
	    if (instData->currentBlock >= instData->bufSize) {
		break;
	    }
	    instData->currentBlockOffset = 0;
	}
	
	/* read as many bytes as left in chunk, or as many as requested */
	if (bytesLeft > blockLeft) {
	    blockRead = blockLeft;
	}  else  {
	    blockRead = bytesLeft;
	}

	CookfsLog(printf("Cookfs_Readerchannel_Input: reading page %d", instData->buf[instData->currentBlock + 0]))
	pageObj = Cookfs_PageGet(instData->pages, instData->buf[instData->currentBlock + 0]);
	
	if (pageObj == NULL) {
	    goto error;
	}
	
	pageBuf = (char *) Tcl_GetByteArrayFromObj(pageObj, NULL);

	CookfsLog(printf("Cookfs_Readerchannel_Input: copying %d+%d", instData->buf[instData->currentBlock + 1], instData->currentBlockOffset))
	memcpy(buf + bytesRead, pageBuf + instData->buf[instData->currentBlock + 1] + instData->currentBlockOffset, blockRead);
	instData->currentBlockOffset += blockRead;
	bytesRead += blockRead;
	instData->currentOffset += bytesRead;
    }
    
    // CookfsLog(printf("Cookfs_Readerchannel_Input: "))

    return bytesRead;

error:
    *errorCodePtr = EIO;
    return -1;
}

int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr) {
    return -1;
}

Tcl_WideInt Cookfs_Readerchannel_WideSeek(ClientData instanceData, Tcl_WideInt offset, int seekMode, int *errorCodePtr) {
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;

    CookfsLog(printf("Cookfs_Readerchannel_WideSeek: current=%d offset=%d mode=%d", ((int) instData->currentOffset), ((int) offset), seekMode))
    if (seekMode == SEEK_CUR) {
	offset += instData->currentOffset;
    }  else if (seekMode == SEEK_END) {
	offset += instData->fileSize;
    }
    CookfsLog(printf("Cookfs_Readerchannel_WideSeek: step 1 offset=%d", (int) offset))
    if (offset < 0) {
	offset = 0;
    }  else if (offset > instData->fileSize) {
	offset = instData->fileSize;
    }
    CookfsLog(printf("Cookfs_Readerchannel_WideSeek: step 2 offset=%d", (int) offset))

    if (offset != instData->currentOffset) {
	Tcl_WideInt bytesLeft = offset;
	Tcl_WideInt blockLeft;
	CookfsLog(printf("Cookfs_Readerchannel_WideSeek: resetting offset"))

	instData->currentOffset = 0;
	instData->currentBlock = 0;
	instData->currentBlockOffset = 0;
	
	while (bytesLeft > 0) {
	    /* either block is larger than left bytes or not */
	    CookfsLog(printf("Cookfs_Readerchannel_WideSeek: compare %d < %d", instData->buf[instData->currentBlock + 2], (int) bytesLeft))
	    if (instData->buf[instData->currentBlock + 2] < bytesLeft) {
		/* move to next block */
		bytesLeft -= instData->buf[instData->currentBlock + 2];
		instData->currentBlock += 3;
		
		if (instData->currentBlock >= instData->bufSize) {
		    break;
		}
	    }  else  {
		instData->currentBlockOffset += bytesLeft;
		instData->currentOffset += bytesLeft;
		bytesLeft = 0;
		break;
	    }
	}

	CookfsLog(printf("Cookfs_Readerchannel_WideSeek: end offset: block=%d blockoffset=%d offset=%d", instData->currentBlock, instData->currentBlockOffset, ((int) instData->currentOffset)))
    }
    return instData->currentOffset;
}

int Cookfs_Readerchannel_Seek(ClientData instanceData, long offset, int seekMode, int *errorCodePtr) {
    return Cookfs_Readerchannel_WideSeek(instanceData, offset, seekMode, errorCodePtr);
}

/* watch implementation taken from rechan.c */
static void CookfsReaderchannelTimerProc(ClientData instanceData)
{
    Cookfs_ReaderChannelInstData *chan = (Cookfs_ReaderChannelInstData *) instanceData;
    if (chan->watchTimer != NULL) {
	Tcl_DeleteTimerHandler(chan->watchTimer);
	chan->watchTimer = NULL;
    }
    Tcl_NotifyChannel(chan->channel, TCL_READABLE);
}

void Cookfs_Readerchannel_Watch(ClientData instanceData, int mask) {
    Cookfs_ReaderChannelInstData *chan = (Cookfs_ReaderChannelInstData *) instanceData;
    if (mask && TCL_READABLE) {
	chan->watchTimer = Tcl_CreateTimerHandler(5, CookfsReaderchannelTimerProc, instanceData);
    } else if (chan->watchTimer != NULL) {
	Tcl_DeleteTimerHandler(chan->watchTimer);
	chan->watchTimer = NULL;
    }
}

/* command for creating new objects that deal with pages */
static int CookfsCreateReaderchannelCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    return TCL_ERROR;
}

int Cookfs_InitReaderchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::readerchannel", CookfsCreateReaderchannelCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
}
