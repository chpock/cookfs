/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEXCMD_H
#define COOKFS_FSINDEXCMD_H 1

typedef enum {
    COOKFS_FSINDEX_FORWARD_COMMAND_GETMETADATA,
    COOKFS_FSINDEX_FORWARD_COMMAND_SETMETADATA
} Cookfs_FsindexForwardCmd;

int Cookfs_InitFsindexCmd(Tcl_Interp *interp);

Tcl_Obj *CookfsGetFsindexObjectCmd(Tcl_Interp *interp, void *i);

int Cookfs_FsindexCmdForward(Cookfs_FsindexForwardCmd cmd, void *i,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif /* COOKFS_FSINDEXCMD_H */
