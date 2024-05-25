/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_WRITER_CMD_H
#define COOKFS_WRITER_CMD_H 1

/* Tcl public API */

typedef int (Cookfs_WriterHandleCommandProc)(Cookfs_Writer *w,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

int Cookfs_InitWriterCmd(Tcl_Interp *interp);

Tcl_Obj *CookfsGetWriterObjectCmd(Tcl_Interp *interp, Cookfs_Writer *w);
Cookfs_WriterHandleCommandProc CookfsWriterHandleCommandWrite;

#endif /* COOKFS_WRITER_CMD_H */
