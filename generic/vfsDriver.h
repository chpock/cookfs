/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_VFSDRIVER_H
#define COOKFS_VFSDRIVER_H 1

/* Tcl public API */

#define VFS_SEPARATOR '/'

const Tcl_Filesystem *CookfsFilesystem(void);
void CookfsThreadExitProc(ClientData clientData);

#endif /* COOKFS_VFSDRIVER_H */
