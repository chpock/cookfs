/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_PAGESCMD_H
#define COOKFS_PAGESCMD_H 1

/* Tcl public API */

/* only handle pages code if enabled in configure */
#ifdef COOKFS_USECPAGES

int Cookfs_InitPagesCmd(Tcl_Interp *interp);
Tcl_Obj *CookfsGetPagesObjectCmd(Tcl_Interp *interp, Cookfs_Pages *p);

int CookfsPagesCmdCompression(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int CookfsPagesCmdAside(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif /* COOKFS_USECPAGES */

#endif /* COOKFS_PAGESCMD_H */
