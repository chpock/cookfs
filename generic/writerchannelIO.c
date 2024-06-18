/*
 * writerchannelIO.c
 *
 * Provides API implementation for writer channel
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "writerchannel.h"
#include "writerchannelIO.h"
#include <errno.h>

#if TCL_MAJOR_VERSION < 9
static Tcl_DriverCloseProc Cookfs_Writerchannel_Close;
static Tcl_DriverSeekProc Cookfs_Writerchannel_Seek;
#endif
static Tcl_DriverInputProc Cookfs_Writerchannel_Input;
static Tcl_DriverOutputProc Cookfs_Writerchannel_Output;
// Tcl_DriverSetOptionProc *Cookfs_Writerchannel_SetOptionProc;
// Tcl_DriverGetOptionProc *Cookfs_Writerchannel_GetOptionProc;
static Tcl_DriverWatchProc Cookfs_Writerchannel_Watch;
// Tcl_DriverGetHandleProc *Cookfs_Writerchannel_GetHandleProc;
static Tcl_DriverClose2Proc Cookfs_Writerchannel_Close2;
static Tcl_DriverBlockModeProc Cookfs_Writerchannel_BlockMode;
// Tcl_DriverFlushProc *Cookfs_Writerchannel_FlushProc;
// Tcl_DriverHandlerProc *Cookfs_Writerchannel_HandlerProc;
static Tcl_DriverWideSeekProc Cookfs_Writerchannel_WideSeek;
static Tcl_DriverThreadActionProc Cookfs_Writerchannel_ThreadAction;
static Tcl_DriverTruncateProc Cookfs_Writerchannel_Truncate;

static Tcl_ChannelType cookfsWriterChannel = {
    "cookfswriter",
    TCL_CHANNEL_VERSION_5,
#if TCL_MAJOR_VERSION < 9
    Cookfs_Writerchannel_Close,
#else
    NULL,
#endif
    Cookfs_Writerchannel_Input,
    Cookfs_Writerchannel_Output,
#if TCL_MAJOR_VERSION < 9
    Cookfs_Writerchannel_Seek,
#else
    NULL,
#endif
    NULL,
    NULL,
    Cookfs_Writerchannel_Watch,
    NULL,
    Cookfs_Writerchannel_Close2,
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
        " resize buffer from [%" TCL_LL_MODIFIER "d] to [%" TCL_LL_MODIFIER
        "d] clear?%d", Tcl_GetChannelName(instData->channel), (void *)instData,
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

    CookfsLog(printf("Cookfs_Writerchannel_Realloc: try to realloc to [%"
        TCL_LL_MODIFIER "d]", newBufferSize));

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
            " [%" TCL_LL_MODIFIER "d] count bytes [%" TCL_LL_MODIFIER "d]",
            instData->bufferSize + diff, diffActual - diff));
        memset((void *)((char *)newBuffer + instData->bufferSize + diff),
            0, diffActual - diff);
    } else if (diffActual != diff) {
        CookfsLog(printf("Cookfs_Writerchannel_Realloc: cleanup from offset"
            " [%" TCL_LL_MODIFIER "d] count bytes [%" TCL_LL_MODIFIER "d]",
            instData->bufferSize, diffActual));
        memset((void *)((char *)newBuffer + instData->bufferSize),
            0, diffActual);
    }

done:
    instData->buffer = newBuffer;
    instData->bufferSize = newBufferSize;
    return 1;
}

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

    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: flush channel"));
    Tcl_Flush(instData->channel);

    // Release the closeResult if it exists
    if (instData->closeResult != NULL) {
        Tcl_DecrRefCount(instData->closeResult);
        instData->closeResult = NULL;
    }

    CookfsLog(printf("Cookfs_Writerchannel_CloseHandler: write file..."));
    Tcl_Obj *err = NULL;
    if (!Cookfs_WriterLockWrite(instData->writer, &err)) {
        goto done;
    }
    if (Cookfs_WriterAddFile(instData->writer, instData->pathObj, instData->entry,
        COOKFS_WRITER_SOURCE_BUFFER, instData->buffer, instData->currentSize,
        &err) == TCL_OK)
    {
        // buffer is owned by writer now. Set to null to avoid releasing it.
        instData->buffer = NULL;
        instData->bufferSize = 0;
    }
    Cookfs_WriterUnlock(instData->writer);

done:

    instData->closeResult = err;
    if (instData->closeResult != NULL) {
        Tcl_IncrRefCount(instData->closeResult);
    }

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

static int Cookfs_Writerchannel_Close2(ClientData instanceData,
    Tcl_Interp *interp, int flags)
{
    if ((flags & (TCL_CLOSE_READ|TCL_CLOSE_WRITE)) == 0) {
        return Cookfs_Writerchannel_Close(instanceData, interp);
    }
    return EINVAL;
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
    CookfsLog(printf("Cookfs_Writerchannel_Input: have [%" TCL_LL_MODIFIER "d]"
        " data available", available));

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

// cppcheck-suppress-begin constParameterCallback
static int Cookfs_Writerchannel_Output(ClientData instanceData,
    const char *buf, int toWrite, int *errorCodePtr)
{
// cppcheck-suppress-end constParameterCallback
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
            " size as [%" TCL_LL_MODIFIER "d]", instData->currentSize));
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
        " seek to [%" TCL_LL_MODIFIER "d] mode %s(%d)",
        Tcl_GetChannelName(instData->channel),
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
            " offset [%" TCL_LL_MODIFIER "d]", offset));
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
            " size as [%" TCL_LL_MODIFIER "d]", instData->currentSize));
    }

    instData->currentOffset = offset;
    CookfsLog(printf("Cookfs_Writerchannel_WideSeek: set current"
        " offset as [%" TCL_LL_MODIFIER "d]", instData->currentOffset));
    return offset;
}

#if TCL_MAJOR_VERSION < 9
static int Cookfs_Writerchannel_Seek(ClientData instanceData, long offset,
    int seekMode, int *errorCodePtr)
{
    return Cookfs_Writerchannel_WideSeek(instanceData, offset, seekMode,
        errorCodePtr);
}
#endif

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
    Cookfs_WriterChannelInstData *instData =
        ((Cookfs_WriterChannelEvent *)evPtr)->instData;

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
        instData->event = (Cookfs_WriterChannelEvent*)ckalloc(
            sizeof(Cookfs_WriterChannelEvent));
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
        " to [%" TCL_LL_MODIFIER "d]", Tcl_GetChannelName(instData->channel),
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
