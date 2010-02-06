/* (c) 2010 Pawel Salawa */

#include "cookfs.h"

int
Cookfs_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, "8.6", 0) == NULL) {
        return TCL_ERROR;
    }

/*
    if (Tcl_PkgRequire(interp, PACKAGE_NAME "::tcl", "1.0", 0) == NULL) {
        return TCL_ERROR;
    }
*/
    if (Tcl_PkgProvide(interp, PACKAGE_NAME "::c", PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }
    
    Tcl_CreateNamespace(interp, "::cookfs", NULL, NULL);
    if (Cookfs_InitPagesCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Cookfs_InitFsindexCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

