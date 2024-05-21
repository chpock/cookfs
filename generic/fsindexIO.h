/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEX_IO_H
#define COOKFS_FSINDEX_IO_H 1

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *i);
Cookfs_Fsindex *Cookfs_FsindexFromObject(Cookfs_Fsindex *i, Tcl_Obj *o);
Cookfs_Fsindex *Cookfs_FsindexFromPages(Cookfs_Fsindex *fsindex, Cookfs_Pages *pages);

#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEX_IO_H */

