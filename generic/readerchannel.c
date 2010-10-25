/* (c) 2010 Wojciech Kocjan */

#include "cookfs.h"

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
    NULL,
    NULL,
    NULL,
    NULL,
    Cookfs_Readerchannel_WideSeek,
    NULL,
    NULL,
};

int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp) {
    
}

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr) {
    
}

int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr) {
    
}

Tcl_WideInt Cookfs_Readerchannel_WideSeek(ClientData instanceData, Tcl_WideInt offset, int seekMode, int *errorCodePtr) {
    
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
