/* (c) 2010 Pawel Salawa */

#ifndef COOKFS_FSINEDX_IO_H
#define COOKFS_FSINEDX_IO_H 1

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *i);
Cookfs_Fsindex *Cookfs_FsindexFromObject(Tcl_Obj *o);

#endif
