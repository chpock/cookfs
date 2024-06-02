/*
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_WRITERCHANNEL_H
#define COOKFS_WRITERCHANNEL_H 1

typedef struct Cookfs_WriterChannelEvent {
  Tcl_Event header;
  struct Cookfs_WriterChannelInstData *instData;
} Cookfs_WriterChannelEvent;

typedef struct Cookfs_WriterChannelInstData {
    Tcl_Channel channel;
    Tcl_Interp *interp;
    Cookfs_WriterChannelEvent* event;
    int interest;
    Tcl_Obj *closeResult;

    Cookfs_Pages *pages;
    Cookfs_Fsindex *index;
    Cookfs_Writer *writer;

    Tcl_Obj *pathObj;
    int pathObjLen;

    void *buffer;
    Tcl_WideInt bufferSize;

    Tcl_WideInt currentOffset;
    Tcl_WideInt currentSize;
} Cookfs_WriterChannelInstData;

int Cookfs_InitWriterchannelCmd(Tcl_Interp *interp);

Tcl_Channel Cookfs_CreateWriterchannel(Cookfs_Pages *pages,
    Cookfs_Fsindex *index, Cookfs_Writer *writer, Tcl_Obj *pathObj,
    int pathObjLen, Cookfs_FsindexEntry *entry, Tcl_Interp *interp);

void Cookfs_CreateWriterchannelFree(Cookfs_WriterChannelInstData *instData);

#endif /* COOKFS_WRITERCHANNEL_H */

