/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEXCMD_H
#define COOKFS_FSINDEXCMD_H 1

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

int Cookfs_InitFsindexCmd(Tcl_Interp *interp);

#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEXCMD_H */
