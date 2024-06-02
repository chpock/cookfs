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
    CookfsLog(printf("Cookfs_Readerchannel_Close: channel=%s",
        Tcl_GetChannelName(instData->channel)));
    Cookfs_CreateReaderchannelFree(instData);
    return 0;
}

int Cookfs_Readerchannel_Close2(ClientData instanceData, Tcl_Interp *interp, int flags) {
    CookfsLog(printf("Cookfs_Readerchannel_Close2: flags=%d", flags))
    if ((flags & (TCL_CLOSE_READ|TCL_CLOSE_WRITE)) == 0) {
        return Cookfs_Readerchannel_Close(instanceData, interp);
    }
    return EINVAL;
}

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr) {
    Cookfs_ReaderChannelInstData *instData = (Cookfs_ReaderChannelInstData *) instanceData;
    int bytesRead = 0;
    int bytesLeft;
    int blockLeft;
    int blockRead;
    char *pageBuf;
    Tcl_Size pageBufSize;

    CookfsLog(printf("Cookfs_Readerchannel_Input: ===> read %d, current offset: %"
        TCL_LL_MODIFIER "d", bufSize, instData->currentOffset))

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

	if (instData->cachedPageObj != NULL) {
	    if (instData->cachedPageNum == pageIndex) {
		CookfsLog(printf("Cookfs_Readerchannel_Input: use"
		    " the previously retrieved page index#%d", pageIndex));
		goto gotPage;
	    }
	    Tcl_DecrRefCount(instData->cachedPageObj);
	}

	CookfsLog(printf("Cookfs_Readerchannel_Input: reading page index#%d", pageIndex));
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
	instData->cachedPageObj = Cookfs_PageGet(instData->pages, pageIndex, pageWeight);
	// No need to increase refcount on retrieved page, it already has refcount=1
	// after Cookfs_PageGet().
	CookfsLog(printf("Cookfs_Readerchannel_Input: got the page: %p",
	    (void *)instData->cachedPageObj))
	if (instData->cachedPageObj == NULL) {
	    goto error;
	}
	instData->cachedPageNum = pageIndex;

gotPage:

	pageBuf = (char *) Tcl_GetByteArrayFromObj(instData->cachedPageObj, &pageBufSize);

	CookfsLog(printf("Cookfs_Readerchannel_Input: copying %d+%d", instData->buf[instData->currentBlock + 1], instData->currentBlockOffset))
	// validate enough data is available in the buffer
	if (pageBufSize < (instData->buf[instData->currentBlock + 1] + instData->currentBlockOffset + blockRead)) {
	    goto error;
	}
	memcpy(buf + bytesRead, pageBuf + instData->buf[instData->currentBlock + 1] + instData->currentBlockOffset, blockRead);
	instData->currentBlockOffset += blockRead;
	bytesRead += blockRead;
	instData->currentOffset += blockRead;
	CookfsLog(printf("Cookfs_Readerchannel_Input: currentOffset: %"
	    TCL_LL_MODIFIER "d", instData->currentOffset));
	// If we have reached the end of the current page, we probably don't need
	// it anymore and can release it.
	if (instData->currentBlockOffset == instData->buf[instData->currentBlock + 2]) {
	    CookfsLog(printf("Cookfs_Readerchannel_Input: release the page"));
	    Tcl_DecrRefCount(instData->cachedPageObj);
	    instData->cachedPageObj = NULL;
	} else {
	    CookfsLog(printf("Cookfs_Readerchannel_Input: keep the page"));
	}
    }

    CookfsLog(printf("Cookfs_Readerchannel_Input: <=== bytesRead=%d", bytesRead))
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

void Cookfs_Readerchannel_ThreadAction(ClientData instanceData, int action) {
    Cookfs_ReaderChannelInstData *instData =
        (Cookfs_ReaderChannelInstData *) instanceData;

    if (instData->channel == NULL) {
        CookfsLog(printf("Cookfs_Readerchannel_ThreadAction: channel [NULL] at [%p]"
            " action [%d]", (void *)instData, action));
    } else {
        CookfsLog(printf("Cookfs_Readerchannel_ThreadAction: channel [%s] at [%p]"
            " action [%d]", Tcl_GetChannelName(instData->channel),
            (void *)instData, action));
    }

    if (action == TCL_CHANNEL_THREAD_REMOVE) {
        if (instData->event != NULL) {
            instData->event->instData = NULL;
            instData->event = NULL;
        }
        instData->interest = 0;
    }
}

static int Cookfs_Readerchannel_Ready(Tcl_Event* evPtr, int flags) {
    Cookfs_ReaderChannelInstData *instData =
        ((Cookfs_ReaderChannelEvent *)evPtr)->instData;

    if (instData == NULL) {
        CookfsLog(printf("Cookfs_Readerchannel_Ready: NULL data"));
        return 1;
    }

    CookfsLog(printf("Cookfs_Readerchannel_Ready: channel [%s] at [%p]"
        " flags [%d]", Tcl_GetChannelName(instData->channel),
        (void *)instData, flags));

    if (!(flags & TCL_FILE_EVENTS)) {
        CookfsLog(printf("Cookfs_Readerchannel_Ready: not TCL_FILE_EVENTS"));
        return 0;
    }

    instData->event = NULL;

    if (instData->interest) {
        CookfsLog(printf("Cookfs_Readerchannel_Ready: call Tcl_NotifyChannel"
            " with mask [%d]", instData->interest));
        Tcl_NotifyChannel(instData->channel, instData->interest);
    } else {
        CookfsLog(printf("Cookfs_Readerchannel_Ready: interest is zero"));
    }

    return 1;
}

void Cookfs_Readerchannel_Watch(ClientData instanceData, int mask) {

    Cookfs_ReaderChannelInstData *instData =
        (Cookfs_ReaderChannelInstData *)instanceData;

    CookfsLog(printf("Cookfs_Readerchannel_Watch: channel=%s mask=%08x",
        Tcl_GetChannelName(instData->channel), mask))

    instData->interest = mask;

    if ((mask & TCL_READABLE) == 0) {
        if (instData->event != NULL) {
            instData->event->instData = NULL;
            instData->event = NULL;
        }
        return;
    }

    if (instData->event == NULL) {
        instData->event = (Cookfs_ReaderChannelEvent*)ckalloc(
            sizeof(Cookfs_ReaderChannelEvent));
        if (instData->event == NULL) {
            return;
        }
        instData->event->header.proc = Cookfs_Readerchannel_Ready;
        instData->event->instData = instData;
        Tcl_QueueEvent(&instData->event->header, TCL_QUEUE_TAIL);
    }
    CookfsLog(printf("Cookfs_Readerchannel_Watch: ok"));

}

