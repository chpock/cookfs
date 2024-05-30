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
Cookfs_Init(Tcl_Interp *interp) // cppcheck-suppress unusedFunction
{

    if (Tcl_InitStubs(interp, "8.5", 0) == NULL) {
        return TCL_ERROR;
    }

    Tcl_CreateNamespace(interp, "::cookfs", NULL, NULL);
    Tcl_CreateNamespace(interp, "::cookfs::c", NULL, NULL);

#if defined(COOKFS_USECPAGES) && defined(COOKFS_USETCLCMDS)
    if (Cookfs_InitPagesCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#if defined(COOKFS_USECFSINDEX) && defined(COOKFS_USETCLCMDS)
    if (Cookfs_InitFsindexCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#if defined(COOKFS_USECREADERCHAN) && defined(COOKFS_USETCLCMDS)
    if (Cookfs_InitReaderchannelCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#if defined(COOKFS_USECWRITER) && defined(COOKFS_USETCLCMDS)
    if (Cookfs_InitWriterCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#if defined(COOKFS_USECWRITERCHAN) && defined(COOKFS_USETCLCMDS)
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

#if defined(COOKFS_USECVFS)
    if (Cookfs_InitVfsMountCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif

#if defined(COOKFS_USECPKGCONFIG)
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

