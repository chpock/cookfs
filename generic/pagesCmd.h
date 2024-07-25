/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_PAGESCMD_H
#define COOKFS_PAGESCMD_H 1

/* Tcl public API */

typedef enum {
#ifdef COOKFS_USECCRYPTO
    COOKFS_PAGES_FORWARD_COMMAND_PASSWORD,
#endif /* COOKFS_USECCRYPTO */
    COOKFS_PAGES_FORWARD_COMMAND_ASIDE,
    COOKFS_PAGES_FORWARD_COMMAND_COMPRESSION
} Cookfs_PagesForwardCmd;

int Cookfs_InitPagesCmd(Tcl_Interp *interp);

Tcl_Obj *CookfsGetPagesObjectCmd(Tcl_Interp *interp, void *p);

int Cookfs_PagesCmdForward(Cookfs_PagesForwardCmd cmd, void *p,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif /* COOKFS_PAGESCMD_H */
