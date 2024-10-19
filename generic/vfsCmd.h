/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSCMD_H
#define COOKFS_VFSCMD_H 1

#include "vfs.h"

/* Tcl public API */

int Cookfs_InitVfsMountCmd(Tcl_Interp *interp);

int CookfsMountHandleCommandPasswordImpl(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    Tcl_Obj *password);

#endif /* COOKFS_VFSCMD_H */
