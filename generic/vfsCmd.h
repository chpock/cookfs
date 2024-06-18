/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSCMD_H
#define COOKFS_VFSCMD_H 1

/* Tcl public API */

typedef struct {

#ifdef COOKFS_USETCLCMDS
    Tcl_Obj *pagesobject;
    Tcl_Obj *fsindexobject;
    int noregister;
    Tcl_Obj *bootstrap;
#endif

    int nocommand;
    int alwayscompress;

    Tcl_Obj *compression;

    Tcl_Obj *compresscommand;
    Tcl_Obj *decompresscommand;
    Tcl_Obj *asynccompresscommand;
    Tcl_Obj *asyncdecompresscommand;

    int asyncdecompressqueuesize;
    Tcl_WideInt endoffset;
    Tcl_Obj *setmetadata;
    int readonly;
    int writetomemory;
    int pagecachesize;
    int volume;
    Tcl_WideInt pagesize;
    Tcl_WideInt smallfilesize;
    Tcl_WideInt smallfilebuffer;
    int nodirectorymtime;
    Tcl_Obj *pagehash;
    int shared;

} Cookfs_VfsProps;

int Cookfs_InitVfsMountCmd(Tcl_Interp *interp);

int Cookfs_Mount(Tcl_Interp *interp, Tcl_Obj *archive, Tcl_Obj *local,
    Cookfs_VfsProps *aprops);

Cookfs_VfsProps *Cookfs_VfsPropsInit(Cookfs_VfsProps *p);
void Cookfs_VfsPropsFree(Cookfs_VfsProps *p);

void Cookfs_VfsPropSetVolume(Cookfs_VfsProps *p, int volume);
void Cookfs_VfsPropSetReadonly(Cookfs_VfsProps *p, int readonly);
void Cookfs_VfsPropSetWritetomemory(Cookfs_VfsProps *p, int writetomemory);
void Cookfs_VfsPropSetShared(Cookfs_VfsProps *p, int shared);

#endif /* COOKFS_VFSCMD_H */
