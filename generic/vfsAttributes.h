/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSATTRIBUTES_H
#define COOKFS_VFSATTRIBUTES_H 1

#include "vfs.h"

TYPEDEF_ENUM_COUNT(Cookfs_VfsAttributeSetType, COOKFS_VFS_ATTRIBUTE_SET_COUNT,
    COOKFS_VFS_ATTRIBUTE_SET_VFS,
    COOKFS_VFS_ATTRIBUTE_SET_FILE,
    COOKFS_VFS_ATTRIBUTE_SET_DIRECTORY
);

TYPEDEF_ENUM_COUNT(Cookfs_VfsAttribute, COOKFS_VFS_ATTRIBUTE_COUNT,
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_HANDLE,
    COOKFS_VFS_ATTRIBUTE_FILESET,
    COOKFS_VFS_ATTRIBUTE_METADATA,
    COOKFS_VFS_ATTRIBUTE_PAGES
);

Cookfs_VfsAttribute Cookfs_VfsAttributeGetFromSet(
    Cookfs_VfsAttributeSetType attr_set, int index);

Tcl_Obj *Cookfs_VfsAttributeList(Cookfs_VfsAttributeSetType attr_set);

int Cookfs_VfsAttributeSet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Tcl_Obj *value);

int Cookfs_VfsAttributeGet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Tcl_Obj **result_ptr);

void CookfsVfsAttributesThreadExit(ClientData clientData);

#endif /* COOKFS_VFSATTRIBUTES_H */
