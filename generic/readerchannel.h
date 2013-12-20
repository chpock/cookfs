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

int Cookfs_InitReaderchannelCmd(Tcl_Interp *interp);

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages, Tcl_Obj *listObj, Tcl_Interp *interp, char **channelNamePtr);

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages, int bufSize);
int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData, Tcl_Interp *interp);
void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData);

#endif /* COOKFS_USECREADERCHAN */

#endif /* COOKFS_READERCHANNEL_H */

