/*
 * readerchannelIO.c
 *
 * Provides API implementation for reader channel
 *
 * (c) 2010-2014 Wojciech Kocjan
 */

#include "cookfs.h"
#include <errno.h>

int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp) {
    UNUSED(interp);
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;
    CookfsLog(printf("Cookfs_Readerchannel_Close: channel=%s\n", instData->channelName))
    Cookfs_CreateReaderchannelFree(instData);
    return 0;
}

int Cookfs_Readerchannel_Close2(ClientData instanceData, Tcl_Interp *interp, int flags) {
    CookfsLog(printf("Cookfs_Readerchannel_Close2: flags=%d", flags))
    if (flags == 0) {
        return Cookfs_Readerchannel_Close(instanceData, interp);
    }  else  {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid operation", -1));
        }
	return EINVAL;
    }
}

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr) {
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;
    Tcl_Obj *pageObj;
    int bytesRead = 0;
    int bytesLeft;
    int blockLeft;
    int blockRead;
    char *pageBuf;
    int pageBufSize;

    CookfsLog(printf("Cookfs_Readerchannel_Input: read %d, current offset: %ld", bufSize, instData->currentOffset))

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

	int pageIndex = instData->buf[instData->currentBlock + 0];
	CookfsLog(printf("Cookfs_Readerchannel_Input: reading page %d", pageIndex))
	int pageUsage = Cookfs_FsindexGetBlockUsage(instData->fsindex, pageIndex);
	/* If page contains only one file, set its weight to 0. Otherwise, set its weight to 1. */
	int pageWeight = (pageUsage <= 1) ? 0 : 1;
	/*
	   Check if we need to do a tick-tock on the page cache. This should only be done when
	   we are reading a file for the first time. This will avoid tick-tocks when reading
	   a large file with multiple pages.
	*/
	if (instData->firstTimeRead) {
	    /*
	       Check to see if the page we are interested in is cached. This will avoid tick-tocks
	       when reading multiple small files from one page, or when one file is requested
	       multiple times.
	    */
	    if (!Cookfs_PagesIsCached(instData->pages, pageIndex)) {
	        Cookfs_PagesTickTock(instData->pages);
	    }
	    instData->firstTimeRead = 0;
	}
	pageObj = Cookfs_PageGet(instData->pages, pageIndex, pageWeight);
	CookfsLog(printf("Cookfs_Readerchannel_Input: result %s", (pageObj ? "SET" : "NULL")))

	if (pageObj == NULL) {
	    goto error;
	}

	CookfsLog(printf("Cookfs_Readerchannel_Input: before incr"))
	Tcl_IncrRefCount(pageObj);
	CookfsLog(printf("Cookfs_Readerchannel_Input: after incr"))
	pageBuf = (char *) Tcl_GetByteArrayFromObj(pageObj, &pageBufSize);

	CookfsLog(printf("Cookfs_Readerchannel_Input: copying %d+%d", instData->buf[instData->currentBlock + 1], instData->currentBlockOffset))
	// validate enough data is available in the buffer
	if (pageBufSize < (instData->buf[instData->currentBlock + 1] + instData->currentBlockOffset + blockRead)) {
	    goto error;
	}
	memcpy(buf + bytesRead, pageBuf + instData->buf[instData->currentBlock + 1] + instData->currentBlockOffset, blockRead);
	CookfsLog(printf("Cookfs_Readerchannel_Input: before decr"))
	Tcl_DecrRefCount(pageObj);
	CookfsLog(printf("Cookfs_Readerchannel_Input: after decr"))

	instData->currentBlockOffset += blockRead;
	bytesRead += blockRead;
	instData->currentOffset += blockRead;
	CookfsLog(printf("Cookfs_Readerchannel_Input: currentOffset: %ld", instData->currentOffset))
    }

    CookfsLog(printf("Cookfs_Readerchannel_Input: bytesRead=%d", bytesRead))
    return bytesRead;

error:
    *errorCodePtr = EIO;
    return -1;
}

int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr) {
    UNUSED(instanceData);
    UNUSED(buf);
    UNUSED(toWrite);
    *errorCodePtr = ENODEV;
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
		instData->currentOffset += instData->buf[instData->currentBlock + 2];
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
    *errorCodePtr = 0;
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
    CookfsLog(printf("Cookfs_Readerchannel_Watch: channel=%s mask=%08x\n", chan->channelName, mask))
    if (mask & TCL_READABLE) {
        if (chan->watchTimer == NULL) {
    	    chan->watchTimer = Tcl_CreateTimerHandler(5, CookfsReaderchannelTimerProc, instanceData);
        }
    } else if (chan->watchTimer != NULL) {
	Tcl_DeleteTimerHandler(chan->watchTimer);
	chan->watchTimer = NULL;
    }
}

