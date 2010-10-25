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
    Cookfs_Readerchannel_SeekWide,
    NULL,
    NULL,
};

/*
int Cookfs_Readerchannel_Close(ClientData instanceData, Tcl_Interp *interp)
{
    
}
 */