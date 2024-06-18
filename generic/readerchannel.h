/*
  (c) 2010 Wojciech Kocjan, Pawel Salawa
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_READERCHANNEL_H
#define COOKFS_READERCHANNEL_H 1

#include "pageObj.h"
#include "pages.h"
#include "fsindex.h"

typedef struct Cookfs_ReaderChannelEvent {
  Tcl_Event header;
  struct Cookfs_ReaderChannelInstData *instData;
} Cookfs_ReaderChannelEvent;

typedef struct Cookfs_ReaderChannelInstData {
    Tcl_Channel channel;
    Cookfs_ReaderChannelEvent *event;
    int interest;

    Cookfs_Pages *pages;
    Cookfs_Fsindex *fsindex;
    Cookfs_FsindexEntry *entry;

    Tcl_WideInt currentOffset;
    int currentBlock;
    int currentBlockOffset;
    int firstTimeRead;

    Cookfs_PageObj cachedPageObj;
    int cachedPageNum;

} Cookfs_ReaderChannelInstData;

Tcl_Channel Cookfs_CreateReaderchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *fsindex, Cookfs_FsindexEntry *entry, Tcl_Interp *interp,
    char **channelNamePtr);

Cookfs_ReaderChannelInstData *Cookfs_CreateReaderchannelAlloc(Cookfs_Pages *pages,
    Cookfs_Fsindex *fsindex, Cookfs_FsindexEntry *entry);

int Cookfs_CreateReaderchannelCreate(Cookfs_ReaderChannelInstData *instData,
    Tcl_Interp *interp);

void Cookfs_CreateReaderchannelFree(Cookfs_ReaderChannelInstData *instData);

#endif /* COOKFS_READERCHANNEL_H */

