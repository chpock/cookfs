/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSDRIVER_H
#define COOKFS_VFSDRIVER_H 1

/* Tcl public API */

#define VFS_SEPARATOR '/'

typedef struct CookfsFSData {
    Tcl_Obj *attrListRoot;
    Tcl_Obj *attrList;
    Tcl_Obj *attrValVfs;
} CookfsFSData;

const Tcl_Filesystem *CookfsFilesystem(void);

#endif /* COOKFS_VFSDRIVER_H */
