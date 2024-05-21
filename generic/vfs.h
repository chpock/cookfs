/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFS_H
#define COOKFS_VFS_H 1

/* Tcl public API */

typedef struct Cookfs_Vfs {
    CONST char* mountStr;
    Tcl_Obj *mountObj;
    int mountLen;

    Tcl_Interp *interp;
    Tcl_Command commandToken;

    int isDead;
    int isRegistered;

    int isCurrentDirTime;
    int isVolume;
    int isReadonly;

    Cookfs_Pages *pages;
    Cookfs_Fsindex *index;
    Cookfs_Writer *writer;

    struct Cookfs_Vfs* nextVfs;
} Cookfs_Vfs;

Cookfs_Vfs *Cookfs_VfsInit(Tcl_Interp* interp, Tcl_Obj* mountPoint,
    int isVolume, int isCurrentDirTime, int isReadonly,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Cookfs_Writer *writer);

int Cookfs_VfsFini(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_WideInt *pagesCloseOffset);

int Cookfs_VfsIsReadonly(Cookfs_Vfs *vfs);
void Cookfs_VfsSetReadonly(Cookfs_Vfs *vfs, int status);

void Cookfs_VfsUnregisterInTclvfs(Cookfs_Vfs *vfs);
int Cookfs_VfsRegisterInTclvfs(Cookfs_Vfs *vfs);

#endif /* COOKFS_VFS_H */
