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
    Tcl_Obj *compression;
    int alwayscompress;
    Tcl_Obj *compresscommand;
    Tcl_Obj *asynccompresscommand;
    Tcl_Obj *asyncdecompresscommand;
    int asyncdecompressqueuesize;
    Tcl_Obj *decompresscommand;
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

} Cookfs_VfsProps;

int Cookfs_InitVfsMountCmd(Tcl_Interp *interp);

int Cookfs_Mount(Tcl_Interp *interp, Tcl_Obj *archive, Tcl_Obj *local,
    Cookfs_VfsProps *props);

Cookfs_VfsProps *Cookfs_VfsPropsInit(Cookfs_VfsProps *p);
void Cookfs_VfsPropsFree(Cookfs_VfsProps *p);

void Cookfs_VfsPropSetVolume(Cookfs_VfsProps *p, int volume);
void Cookfs_VfsPropSetReadonly(Cookfs_VfsProps *p, int readonly);

#endif /* COOKFS_VFSCMD_H */
