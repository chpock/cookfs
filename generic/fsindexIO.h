/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEX_IO_H
#define COOKFS_FSINDEX_IO_H 1

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *fsindex);
Cookfs_PageObj Cookfs_FsindexToPageObj(Cookfs_Fsindex *fsindex);
Cookfs_Fsindex *Cookfs_FsindexFromBytes(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, unsigned char *bytes, Tcl_Size size);
Cookfs_Fsindex *Cookfs_FsindexFromTclObj(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Tcl_Obj *o);
Cookfs_Fsindex *Cookfs_FsindexFromPageObj(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Cookfs_PageObj o);
#ifdef COOKFS_USECPAGES
Cookfs_Fsindex *Cookfs_FsindexFromPages(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Cookfs_Pages *pages);
#endif /* COOKFS_USECPAGES */

#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEX_IO_H */

