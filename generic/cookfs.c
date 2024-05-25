/*
 * cookfs.c
 *
 * Provides Cookfs initialization code.
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
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

DLLEXPORT int
Cookfs_Init(Tcl_Interp *interp)
{
    char buf[256];
    if (Tcl_InitStubs(interp, "8.5", 0) == NULL) {
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

#ifdef COOKFS_USECWRITER
    if (Cookfs_InitWriterCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#ifdef COOKFS_USECWRITERCHAN
    if (Cookfs_InitWriterchannelCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

    if (Cookfs_InitBinaryDataCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Cookfs_InitHashesCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }

#ifdef COOKFS_USECVFS
    if (Cookfs_InitVfsMountCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#ifdef COOKFS_USECPKGCONFIG
    Tcl_RegisterConfig(interp, PACKAGE_NAME, cookfs_pkgconfig, "iso8859-1");

    if (Tcl_PkgProvide(interp, PACKAGE_NAME "::pkgconfig", PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

    if (Tcl_PkgProvide(interp, PACKAGE_NAME "::c", PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

