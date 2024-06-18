/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEX_IO_H
#define COOKFS_FSINDEX_IO_H 1

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *fsindex);
Cookfs_Fsindex *Cookfs_FsindexFromBytes(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, unsigned char *bytes, Tcl_Size size);
Cookfs_Fsindex *Cookfs_FsindexFromTclObj(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Tcl_Obj *o);

#endif /* COOKFS_FSINDEX_IO_H */

