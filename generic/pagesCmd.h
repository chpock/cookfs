/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_PAGESCMD_H
#define COOKFS_PAGESCMD_H 1

/* Tcl public API */

/* only handle pages code if enabled in configure */
#ifdef COOKFS_USECPAGES

int Cookfs_InitPagesCmd(Tcl_Interp *interp);

#endif /* COOKFS_USECPAGES */

#endif /* COOKFS_PAGESCMD_H */
