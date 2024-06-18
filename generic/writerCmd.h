/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_WRITER_CMD_H
#define COOKFS_WRITER_CMD_H 1

/* Tcl public API */

typedef enum {
    COOKFS_WRITER_FORWARD_COMMAND_WRITE
} Cookfs_WriterForwardCmd;

int Cookfs_InitWriterCmd(Tcl_Interp *interp);

Tcl_Obj *CookfsGetWriterObjectCmd(Tcl_Interp *interp, void *w);

int Cookfs_WriterCmdForward(Cookfs_WriterForwardCmd cmd, void *w,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif /* COOKFS_WRITER_CMD_H */
