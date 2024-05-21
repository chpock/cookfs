/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEXCMD_H
#define COOKFS_FSINDEXCMD_H 1

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

int Cookfs_InitFsindexCmd(Tcl_Interp *interp);
void CookfsRegisterExistingFsindexObjectCmd(Tcl_Interp *interp, Cookfs_Fsindex *i);
Tcl_Obj *CookfsGetFsindexObjectCmd(Tcl_Interp *interp, Cookfs_Fsindex *i);


int CookfsFsindexCmdGetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int CookfsFsindexCmdSetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);


#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEXCMD_H */
