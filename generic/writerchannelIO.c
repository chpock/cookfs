/*
 * writerchannelIO.c
 *
 * Provides API implementation for writer channel
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include <errno.h>

static Tcl_DriverCloseProc Cookfs_Writerchannel_Close;
static Tcl_DriverInputProc Cookfs_Writerchannel_Input;
static Tcl_DriverOutputProc Cookfs_Writerchannel_Output;
static Tcl_DriverSeekProc Cookfs_Writerchannel_Seek;
// Tcl_DriverSetOptionProc *Cookfs_Writerchannel_SetOptionProc;
// Tcl_DriverGetOptionProc *Cookfs_Writerchannel_GetOptionProc;
static Tcl_DriverWatchProc Cookfs_Writerchannel_Watch;
// Tcl_DriverGetHandleProc *Cookfs_Writerchannel_GetHandleProc;
// static Tcl_DriverClose2Proc Cookfs_Writerchannel_Close2;
static Tcl_DriverBlockModeProc Cookfs_Writerchannel_BlockMode;
// Tcl_DriverFlushProc *Cookfs_Writerchannel_FlushProc;
// Tcl_DriverHandlerProc *Cookfs_Writerchannel_HandlerProc;
static Tcl_DriverWideSeekProc Cookfs_Writerchannel_WideSeek;
static Tcl_DriverThreadActionProc Cookfs_Writerchannel_ThreadAction;
static Tcl_DriverTruncateProc Cookfs_Writerchannel_Truncate;

static Tcl_ChannelType cookfsWriterChannel = {
    "cookfswriter",
    TCL_CHANNEL_VERSION_5,
    Cookfs_Writerchannel_Close,
    Cookfs_Writerchannel_Input,
    Cookfs_Writerchannel_Output,
    Cookfs_Writerchannel_Seek,
    NULL,
    NULL,
    Cookfs_Writerchannel_Watch,
    NULL,
    NULL,
    Cookfs_Writerchannel_BlockMode,
    NULL,
    NULL,
    Cookfs_Writerchannel_WideSeek,
    Cookfs_Writerchannel_ThreadAction,
    Cookfs_Writerchannel_Truncate,
};

const Tcl_ChannelType *CookfsWriterChannel(void) {
    return &cookfsWriterChannel;
}

static int Cookfs_Writerchannel_Realloc(Cookfs_WriterChannelInstData *instData,
    Tcl_WideInt newBufferSize, int clear)
{
    CookfsLog(printf("Cookfs_Writerchannel_Realloc: channel [%s] at [%p]"
        " resize buffer from [%ld] to [%ld] clear?%d",
        Tcl_GetChannelName(instData->channel), (void *)instData,
        instData->bufferSize, newBufferSize, clear));

    void *newBuffer;

    // Simply clear the buffer if zero size is specified
    if (newBufferSize == 0) {
        newBuffer = NULL;
        if (instData->buffer != NULL) {
            ckfree(instData->buffer);
        }
        goto done;
    }

    // How many more bytes do we want
    Tcl_WideInt diff = newBufferSize - instData->bufferSize;
    Tcl_WideInt diffActual;

    if (diff == 0) {
        CookfsLog(printf("Cookfs_Writerchannel_Realloc: nothing to do"));
        return 1;
    } else if (diff > 0) {
        // We need to increase the buffer size
        if (diff < 1024) {
            // If we need less than 1k more, then add 1k
            diffActual = 1024;
        } else if (diff < 131072) {
            // If we need less than 128k more, then add 128k
            diffActual = 131072;
        } else {
            // If we need more than 128k, then use this size, but rundup to 1024
            diffActual = (diff + 1023) & ~(1023);
        }
        newBufferSize = instData->bufferSize + diffActual;
    } else {
        // We need to decrease the buffer size
        diffActual = newBufferSize;
        // rundup to 1024
        newBufferSize = (newBufferSize + 1023) & ~(1023);
        // As for now:
        // diffActual - requester buffer size
        // newBufferSize - the size we want to set
        // Calculate the difference:
        diffActual = newBufferSize - diffActual;
        // WARNING: don't use clear=0 here, because then we will then below
        // clear memory based on diff, but not based on diffActual.
        clear = 1;
    }

    CookfsLog(printf("Cookfs_Writerchannel_Realloc: try to realloc to [%ld]",
        newBufferSize));

    if (instData->buffer == NULL) {
        newBuffer = ckalloc(newBufferSize);
    } else {
        newBuffer = ckrealloc(instData->buffer, newBufferSize);
    }

    if (newBuffer == NULL) {
        CookfsLog(printf("Cookfs_Writerchannel_Realloc: failed"));
        return 0;
    }

    if (clear == 0) {
        CookfsLog(printf("Cookfs_Writerchannel_Realloc: cleanup from offset"
            " [%ld] count bytes [%ld]", instData->bufferSize + diff,
            diffActual - diff));
        memset((void *)((char *)newBuffer + instData->bufferSize + diff),
            0, diffActual - diff);
    } else if (diffActual != diff) {
        CookfsLog(printf("Cookfs_Writerchannel_Realloc: cleanup from offset"
            " [%ld] count bytes [%ld]", instData->bufferSize, diffActual));
        memset((void *)((char *)newBuffer + instData->bufferSize),
            0, diffActual);
    }

done:
    instData->buffer = newBuffer;
    instData->bufferSize = newBufferSize;
    return 1;
}

/*
 *
 * Warning from tclvfs:
 *
 * IMPORTANT: This procedure must *not* modify the interpreter's result
 * this leads to the objResultPtr being corrupted (somehow), and curious
 * crashes in the future (which are very hard to debug ;-).
 *
 * This is particularly important since we are evaluating arbitrary
 * Tcl code in the callback.
 *
 * Also note we are relying on the close-callback to occur just before
 * the channel is about to be properly closed, but after all output
 * has been flushed.  That way we can, in the callback, read in the
 * entire contents of the channel and, say, compress it for storage
 * into a tclkit or zip archive.
 */

void Cookfs_Writerchannel_CloseHandler(ClientData clientData) {
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *)clientData;

    // We don't need to do anything if channel is in RO mode (when pathObj
    // is NULL)
    if (instData->pathObj == NULL) {
        CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: channel [%s]"
            " at [%p] is in RO mode", Tcl_GetChannelName(instData->channel),
            (void *)instData));
        return;
    }

    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: channel [%s] at [%p]",
        Tcl_GetChannelName(instData->channel), (void *)instData));

    // Move current offset to the start of channel. instData->currentOffset
    // should not be used here. The channel can be buffered, and the Tcl core
    // must reset the internal buffer before we change the current position.
    // If we do it hacky way by directly changing the position, the Tcl core
    // will push data from the buffer into the channel via Output(), and this
    // will break the current position.
    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: move current offset"
        " to the start"));
    Tcl_Seek(instData->channel, 0, SEEK_SET);

    // Save interp result, see the notice before this function
    Tcl_SavedResult savedResult;
    Tcl_SaveResult(instData->interp, &savedResult);

    // Construct the writer command
    Tcl_Obj *writerCmd = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(writerCmd);

    Tcl_ListObjAppendElement(instData->interp, writerCmd,
        instData->writerObjCmd);
    Tcl_ListObjAppendElement(instData->interp, writerCmd,
        Tcl_NewStringObj("write", -1));
    Tcl_ListObjAppendElement(instData->interp, writerCmd,
        Tcl_FSJoinPath(instData->pathObj, instData->pathObjLen));
    Tcl_ListObjAppendElement(instData->interp, writerCmd,
        Tcl_NewStringObj("channel", -1));
    Tcl_ListObjAppendElement(instData->interp, writerCmd,
        Tcl_NewStringObj(Tcl_GetChannelName(instData->channel), -1));
    Tcl_ListObjAppendElement(instData->interp, writerCmd, Tcl_NewObj());

    /*
     * Another magic from tclvfs:
     *
     * The interpreter needs to know about the channel, else the Tcl
     * callback will fail, so we register the channel (this allows
     * the Tcl code to use the channel's string-name).
     */
    Tcl_RegisterChannel(instData->interp, instData->channel);

    // The channel must be in raw mode before saving
    Tcl_SetChannelOption(instData->interp, instData->channel,
        "-translation", "binary");

    // Exec the writer command
    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: exec writer..."));
    int ret = Tcl_EvalObjEx(instData->interp, writerCmd,
        TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: writer"
        " returned: %d", ret));

    /*
     * One more magic from tclvfs:
     *
     * More complications; we can't just unregister the channel,
     * because it is in the middle of being cleaned up, and the cleanup
     * code doesn't like a channel to be closed again while it is
     * already being closed.  So, we do the same trick as above to
     * unregister it without cleanup.
     */
    Tcl_DetachChannel(instData->interp, instData->channel);

    if (instData->closeResult != NULL) {
        Tcl_DecrRefCount(instData->closeResult);
    }

    if (ret != TCL_OK) {
        instData->closeResult = Tcl_GetObjResult(instData->interp);
        Tcl_IncrRefCount(instData->closeResult);
    } else {
        instData->closeResult = NULL;
    }

    // Restore interp result
    Tcl_RestoreResult(instData->interp, &savedResult);

    // We don't need the writer command anymore
    Tcl_DecrRefCount(writerCmd);
}

static int Cookfs_Writerchannel_Close(ClientData instanceData,
    Tcl_Interp *interp)
{
    UNUSED(interp);
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;

    CookfsLog(printf("Cookfs_Writerchannel_Close: channel [%s] at [%p]",
        Tcl_GetChannelName(instData->channel), (void *)instData));

    int closeResult;

    if (instData->closeResult == NULL) {
        closeResult = TCL_OK;
    } else {
        closeResult = TCL_ERROR;
        Tcl_SetObjResult(interp, instData->closeResult);
        Tcl_DecrRefCount(instData->closeResult);
        instData->closeResult = NULL;
    }

    Cookfs_CreateWriterchannelFree(instData);

    return closeResult;
}

static int Cookfs_Writerchannel_BlockMode(ClientData instanceData, int mode) {
    UNUSED(instanceData);
    UNUSED(mode);
    return 0;
}

static int Cookfs_Writerchannel_Input(ClientData instanceData, char *buf,
    int toRead, int *errorCodePtr)
{
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;
    *errorCodePtr = 0;

    CookfsLog(printf("Cookfs_Writerchannel_Input: channel [%s] at [%p]"
        " want to read [%d] bytes", Tcl_GetChannelName(instData->channel),
        (void *)instData, toRead));

    if (toRead == 0) {
        CookfsLog(printf("Cookfs_Writerchannel_Input: want to read zero bytes"));
        return 0;
    }

    Tcl_WideInt available = instData->currentSize - instData->currentOffset;
    CookfsLog(printf("Cookfs_Writerchannel_Input: have [%ld] data available",
        available));

    if (available <= 0) {
        CookfsLog(printf("Cookfs_Writerchannel_Input: return EOF"));
        return 0;
    }

    if (available < toRead) {
        toRead = available;
        CookfsLog(printf("Cookfs_Writerchannel_Input: return only"
            " available data"));
    }

    memcpy((void*)buf, (void*)((char*)instData->buffer + instData->currentOffset),
        toRead);
    instData->currentOffset += toRead;

    return toRead;
}

static int Cookfs_Writerchannel_Output(ClientData instanceData,
    const char *buf, int toWrite, int *errorCodePtr)
{
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;
    *errorCodePtr = 0;

    CookfsLog(printf("Cookfs_Writerchannel_Output: channel [%s] at [%p]"
        " want to write [%d] bytes", Tcl_GetChannelName(instData->channel),
        (void *)instData, toWrite));

    if (toWrite == 0) {
        goto done;
    }

    Tcl_WideInt endOffset = toWrite + instData->currentOffset;

    if (endOffset > instData->bufferSize) {
        if (!Cookfs_Writerchannel_Realloc(instData, endOffset, 0)) {
            CookfsLog(printf("Cookfs_Writerchannel_Output: failed"));
            *errorCodePtr = ENOSPC;
            return -1;
        }
    }

    memcpy((void *)((char *)instData->buffer + instData->currentOffset),
        (void*)buf, toWrite);
    instData->currentOffset = endOffset;

    if (endOffset > instData->currentSize) {
        instData->currentSize = endOffset;
        CookfsLog(printf("Cookfs_Writerchannel_Output: set current"
            " size as [%ld]", instData->currentSize));
    }

done:
    CookfsLog(printf("Cookfs_Writerchannel_Output: ok"));
    return toWrite;
}

static Tcl_WideInt Cookfs_Writerchannel_WideSeek(ClientData instanceData,
    Tcl_WideInt offset, int seekMode, int *errorCodePtr)
{
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;

    CookfsLog(printf("Cookfs_Writerchannel_WideSeek: channel [%s] at [%p]"
        " seek to [%ld] mode %s(%d)", Tcl_GetChannelName(instData->channel),
        (void *)instData, offset, (seekMode == SEEK_SET ? "SEEK_SET" :
        (seekMode == SEEK_CUR ? "SEEK_CUR" :
        (seekMode == SEEK_END ? "SEEK_END" : "???"))), seekMode));

    *errorCodePtr = 0;

    switch (seekMode) {
    case SEEK_SET:
        break;
    case SEEK_CUR:
        offset += instData->currentOffset;
        break;
    case SEEK_END:
        offset += instData->currentSize;
        break;
    default:
        CookfsLog(printf("Cookfs_Writerchannel_WideSeek: unknown mode"));
        return -1;
    }

    if (offset < 0) {
        CookfsLog(printf("Cookfs_Writerchannel_WideSeek: incorrect"
            " offset [%ld]", offset));
        *errorCodePtr = EINVAL;
        return -1;
    }

    if (offset > instData->bufferSize) {
        if (!Cookfs_Writerchannel_Realloc(instData, offset, 1)) {
            *errorCodePtr = ENOSPC;
            return -1;
        }
    }

    if (instData->currentSize < offset) {
        instData->currentSize = offset;
        CookfsLog(printf("Cookfs_Writerchannel_WideSeek: set current"
            " size as [%ld]", instData->currentSize));
    }

    instData->currentOffset = offset;
    CookfsLog(printf("Cookfs_Writerchannel_WideSeek: set current"
        " offset as [%ld]", instData->currentOffset));
    return offset;
}

static int Cookfs_Writerchannel_Seek(ClientData instanceData, long offset,
    int seekMode, int *errorCodePtr)
{
    return Cookfs_Writerchannel_WideSeek(instanceData, offset, seekMode,
        errorCodePtr);
}

static void Cookfs_Writerchannel_ThreadAction(ClientData instanceData,
    int action)
{
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;

    if (instData->channel == NULL) {
        CookfsLog(printf("Cookfs_Writerchannel_ThreadAction: channel [NULL] at [%p]"
            " action [%d]", (void *)instData, action));
    } else {
        CookfsLog(printf("Cookfs_Writerchannel_ThreadAction: channel [%s] at [%p]"
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

static int Cookfs_Writerchannel_Ready(Tcl_Event* evPtr, int flags) {
    Cookfs_WriterChannelInstData *instData = ((ChannelEvent *)evPtr)->instData;

    if (instData == NULL) {
        CookfsLog(printf("Cookfs_Writerchannel_Ready: NULL data"));
        return 1;
    }

    CookfsLog(printf("Cookfs_Writerchannel_Ready: channel [%s] at [%p]"
        " flags [%d]", Tcl_GetChannelName(instData->channel),
        (void *)instData, flags));

    if (!(flags & TCL_FILE_EVENTS)) {
        CookfsLog(printf("Cookfs_Writerchannel_Ready: not TCL_FILE_EVENTS"));
        return 0;
    }

    instData->event = NULL;

    if (instData->interest) {
        CookfsLog(printf("Cookfs_Writerchannel_Ready: call Tcl_NotifyChannel"
            " with mask [%d]", instData->interest));
        Tcl_NotifyChannel(instData->channel, instData->interest);
    } else {
        CookfsLog(printf("Cookfs_Writerchannel_Ready: interest is zero"));
    }

    return 1;
}

static void Cookfs_Writerchannel_Watch(ClientData instanceData, int mask) {
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;

    CookfsLog(printf("Cookfs_Writerchannel_Watch: channel [%s] at [%p]"
        " mask [%d]", Tcl_GetChannelName(instData->channel),
        (void *)instData, mask));

    instData->interest = mask;

    if (!mask) {
        if (instData->event != NULL) {
            instData->event->instData = NULL;
            instData->event = NULL;
        }
        return;
    }

    if (instData->event == NULL) {
        instData->event = (ChannelEvent*)Tcl_Alloc(sizeof(ChannelEvent));
        if (instData->event == NULL) {
            return;
        }
        instData->event->header.proc = Cookfs_Writerchannel_Ready;
        instData->event->instData = instData;
        Tcl_QueueEvent(&instData->event->header, TCL_QUEUE_TAIL);
    }
    CookfsLog(printf("Cookfs_Writerchannel_Watch: ok"));
}

static int Cookfs_Writerchannel_Truncate(ClientData instanceData, Tcl_WideInt length) {
    Cookfs_WriterChannelInstData *instData =
        (Cookfs_WriterChannelInstData *) instanceData;

    CookfsLog(printf("Cookfs_Writerchannel_Truncate: channel [%s] at [%p]"
        " to [%ld]", Tcl_GetChannelName(instData->channel),
        (void *)instData, length));

    if (length < 0) {
        CookfsLog(printf("Cookfs_Writerchannel_Truncate: negative length"));
        return EINVAL;
    }

    if (length > instData->bufferSize) {
        if (!Cookfs_Writerchannel_Realloc(instData, length, 1)) {
            return ENOSPC;
        }
    } else {
        // Ignore realloc failure here because we are reducing the buffer size
        Cookfs_Writerchannel_Realloc(instData, length, 0);
    }

    if (instData->currentOffset > length) {
        instData->currentOffset = length;
    }

    instData->currentSize = length;
    CookfsLog(printf("Cookfs_Writerchannel_Truncate: ok"));
    return 0;
}