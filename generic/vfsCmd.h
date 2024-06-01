/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSCMD_H
#define COOKFS_VFSCMD_H 1

/* Tcl public API */

int Cookfs_InitVfsMountCmd(Tcl_Interp *interp);

int Cookfs_Mount(Tcl_Interp *interp, Tcl_Obj *archive, Tcl_Obj *local);

#endif /* COOKFS_VFSCMD_H */
