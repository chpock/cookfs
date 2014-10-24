/*
 * cookfs.c
 *
 * Provides Cookfs initialization code.
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_Init --
 *
 *	Initializes Cookfs C layer in a Tcl interpreter.
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR otherwise
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int
Cookfs_Init(Tcl_Interp *interp)
{
    char buf[256];
    if (Tcl_InitStubs(interp, "8.4", 0) == NULL) {
        return TCL_ERROR;
    }

    if (Tcl_PkgProvide(interp, "vfs::" PACKAGE_NAME "::c", PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }
    
    strcpy(buf, "namespace eval ::cookfs {} ; namespace eval ::cookfs::c {}");
    if (Tcl_EvalEx(interp, buf, -1, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) {
        return TCL_ERROR;
    }

#ifdef COOKFS_USECPAGES
    if (Cookfs_InitPagesCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#ifdef COOKFS_USECFSINDEX
    if (Cookfs_InitFsindexCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#ifdef COOKFS_USECREADERCHAN
    if (Cookfs_InitReaderchannelCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

    return TCL_OK;
}

