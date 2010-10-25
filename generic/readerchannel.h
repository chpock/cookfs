/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_READERCHANNEL_H
#define COOKFS_READERCHANNEL_H 1

#ifdef COOKFS_USECREADERCHANNEL

/*
typedef struct Cookfs_ReaderChannelInstData {
    Tcl_WideInt currentOffset;
    int currentBlock;
    int currentBlockOffset;
    
    int bufSize;
    int buf[1];
} Cookfs_ReaderChannelInstData;

int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp);

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr);
int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr);

int Cookfs_Readerchannel_Seek(ClientData instanceData, long offset, int seekMode, int *errorCodePtr);
Tcl_WideInt Cookfs_Readerchannel_WideSeek(ClientData instanceData, Tcl_WideInt offset, int seekMode, int *errorCodePtr);

void Cookfs_Readerchannel_Watch(ClientData instanceData, int mask);
 */

#endif /* COOKFS_USECREADERCHANNEL */

#endif /* COOKFS_READERCHANNEL_H */

