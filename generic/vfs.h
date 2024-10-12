/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFS_H
#define COOKFS_VFS_H 1

#include "pageObj.h"
#include "pages.h"
#include "fsindex.h"
#include "writer.h"

/* Tcl public API */

typedef struct Cookfs_Vfs {
    const char* mountStr;
    Tcl_Size mountLen;

    Tcl_Interp *interp;
    Tcl_Command commandToken;
#ifdef TCL_THREADS
    Tcl_ThreadId threadId;
#endif /* TCL_THREADS */

    int isDead;
#ifdef COOKFS_USETCLCMDS
    int isRegistered;
#endif

    int isCurrentDirTime;
    int isVolume;
    int isReadonly;
    int isShared;

    Cookfs_Pages *pages;
    Cookfs_Fsindex *index;
    Cookfs_Writer *writer;

} Cookfs_Vfs;

Cookfs_Vfs *Cookfs_VfsInit(Tcl_Interp* interp, Tcl_Obj* mountPoint,
    int isVolume, int isCurrentDirTime, int isReadonly, int isShared,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Cookfs_Writer *writer);

int Cookfs_VfsFini(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_WideInt *pagesCloseOffset);

const char *Cookfs_VfsFilesetGetActive(Cookfs_Vfs *vfs);
Tcl_Obj *Cookfs_VfsFilesetGet(Cookfs_Vfs *vfs);
int Cookfs_VfsFilesetSelect(Cookfs_Vfs *vfs, Tcl_Obj *fileset,
    Tcl_Obj **active, Tcl_Obj **err);
int Cookfs_VfsHasFileset(Cookfs_Vfs *vfs);
int Cookfs_VfsIsReadonly(Cookfs_Vfs *vfs);
void Cookfs_VfsSetReadonly(Cookfs_Vfs *vfs, int status);

#ifdef COOKFS_USETCLCMDS
void Cookfs_VfsUnregisterInTclvfs(Cookfs_Vfs *vfs);
int Cookfs_VfsRegisterInTclvfs(Cookfs_Vfs *vfs);
#endif

Tcl_Obj *CookfsGetVfsObjectCmd(Tcl_Interp* interp, Cookfs_Vfs *vfs);

int Cookfs_CookfsVfsLock(Cookfs_Vfs *vfsToLock);
int Cookfs_CookfsVfsUnlock(Cookfs_Vfs *vfsToUnlock);

#endif /* COOKFS_VFS_H */
