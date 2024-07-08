/*
 * cookfs.c
 *
 * Provides Cookfs initialization code.
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "crypto.h"

#ifdef COOKFS_USECPAGES
#include "pagesCmd.h"
#endif /* COOKFS_USECPAGES */

#ifdef COOKFS_USECFSINDEX
#include "fsindexCmd.h"
#endif /* COOKFS_USECFSINDEX */

#ifdef COOKFS_USECREADERCHAN
#include "readerchannelCmd.h"
#endif /* COOKFS_USECREADERCHAN */

#ifdef COOKFS_USECWRITER
#include "writerCmd.h"
#endif /* COOKFS_USECWRITER */

#ifdef COOKFS_USECVFS
#include "vfsCmd.h"
#endif /* COOKFS_USECVFS */

#ifdef COOKFS_USECWRITERCHAN
#include "writerchannelCmd.h"
#endif /* COOKFS_USECWRITERCHAN */

#ifdef COOKFS_USECCRYPTO
#include "cryptoCmd.h"
#endif /* COOKFS_USECCRYPTO */

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

#if TCL_MAJOR_VERSION > 8
#define MIN_TCL_VERSION "9.0"
#else
#define MIN_TCL_VERSION "8.5"
#endif

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/common_interface_defs.h>
#include <signal.h>
void abort_handler(int sig) {
    UNUSED(sig);
    fprintf(stderr, "\nAborted. Backtrace:\n\n");
    __sanitizer_print_stack_trace();
}
#endif /* __SANITIZE_ADDRESS__ */

DLLEXPORT int
Cookfs_Init(Tcl_Interp *interp) // cppcheck-suppress unusedFunction
{

#ifdef __SANITIZE_ADDRESS__
    signal(SIGABRT, abort_handler);
#endif /* __SANITIZE_ADDRESS__ */

    if (Tcl_InitStubs(interp, MIN_TCL_VERSION, 0) == NULL) {
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

#if defined(COOKFS_USECCRYPTO)
    Cookfs_CryptoInit();
#if defined(COOKFS_USETCLCMDS)
    if (Cookfs_InitCryptoCmd(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif
#endif

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

    if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

