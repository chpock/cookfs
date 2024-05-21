/*
  (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_WRITERCHANNELIO_H
#define COOKFS_WRITERCHANNELIO_H 1

Tcl_CloseProc Cookfs_Writerchannel_CloseHandler;

const Tcl_ChannelType *CookfsWriterChannel(void);

#endif /* COOKFS_WRITERCHANNELIO_H */

