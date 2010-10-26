/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_READERCHANNEL_H
#define COOKFS_READERCHANNEL_H 1

#ifdef COOKFS_USECREADERCHAN
typedef struct Cookfs_ReaderChannelInstData {
    char channelName[20];
    Tcl_Channel channel;
    Tcl_TimerToken watchTimer;

    Cookfs_Pages *pages;

    Tcl_WideInt currentOffset;
    Tcl_WideInt fileSize;
    int currentBlock;
    int currentBlockOffset;

    int bufSize;
    int buf[1];
} Cookfs_ReaderChannelInstData;

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Tcl_Obj *pagesList, Tcl_Interp *interp);

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages, int bufSize);
int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData, Tcl_Interp *interp);
void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData);

int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp);

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr);
int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr);

int Cookfs_Readerchannel_Seek(ClientData instanceData, long offset, int seekMode, int *errorCodePtr);
Tcl_WideInt Cookfs_Readerchannel_WideSeek(ClientData instanceData, Tcl_WideInt offset, int seekMode, int *errorCodePtr);

void Cookfs_Readerchannel_Watch(ClientData instanceData, int mask);

int Cookfs_InitReaderchannelCmd(Tcl_Interp *interp);

#endif /* COOKFS_USECREADERCHAN */

#endif /* COOKFS_READERCHANNEL_H */

