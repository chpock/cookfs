/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_READERCHANNELIO_H
#define COOKFS_READERCHANNELIO_H 1

#ifdef COOKFS_USECREADERCHAN
int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp);

int Cookfs_Readerchannel_Input(ClientData instanceData, char *buf, int bufSize, int *errorCodePtr);
int Cookfs_Readerchannel_Output(ClientData instanceData, const char *buf, int toWrite, int *errorCodePtr);

int Cookfs_Readerchannel_Seek(ClientData instanceData, long offset, int seekMode, int *errorCodePtr);
Tcl_WideInt Cookfs_Readerchannel_WideSeek(ClientData instanceData, Tcl_WideInt offset, int seekMode, int *errorCodePtr);

void Cookfs_Readerchannel_Watch(ClientData instanceData, int mask);

#endif /* COOKFS_USECREADERCHAN */

#endif /* COOKFS_READERCHANNELIO_H */

