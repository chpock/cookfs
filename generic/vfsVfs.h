/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSVFS_H
#define COOKFS_VFSVFS_H 1

/* Tcl public API */

void Cookfs_CookfsRegister(Tcl_Interp *interp);

Cookfs_Vfs *Cookfs_CookfsFindVfs(Tcl_Obj *path, Tcl_Size len);
Cookfs_Vfs *Cookfs_CookfsSplitWithVfs(Tcl_Obj *path,
    Cookfs_PathObj **pathObjPtr);

Tcl_Obj *Cookfs_CookfsGetVolumesList(void);

void Cookfs_CookfsSearchVfsToListObj(Tcl_Obj *path, const char *pattern,
    Tcl_Obj *returnObj);

int Cookfs_CookfsAddVfs(Tcl_Interp *interp, Cookfs_Vfs *vfs);
Cookfs_Vfs *Cookfs_CookfsRemoveVfs(Tcl_Interp *interp,
    Cookfs_Vfs *vfsToRemove);
int Cookfs_CookfsIsVfsExist(Cookfs_Vfs *vfsToSearch);

#endif /* COOKFS_VFSVFS_H */
