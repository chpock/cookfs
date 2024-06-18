/*
 * cookfs.h
 *
 * Main cookfs include file:
 * - includes standard headers
 * - defines (temporary) debug/logging macro definitions
 * - includes additional header files, if enabled in configure
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#ifndef COOKFS_H
#define COOKFS_H 1

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef COOKFS_INTERNAL_DEBUG
#define CookfsLog(a) {a; printf("\n"); fflush(stdout);}
#else
#define CookfsLog(a) {}
#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

#ifndef STRINGIFY
#  define STRINGIFY(x) STRINGIFY1(x)
#  define STRINGIFY1(x) #x
#endif

/*
 * Backwards compatibility for size type change
 */

#ifndef TCL_SIZE_MAX
#include <limits.h>
#ifndef Tcl_Size
    typedef int Tcl_Size;
#endif /* Tcl_Size */
#define TCL_SIZE_MODIFIER ""
#define Tcl_GetSizeIntFromObj Tcl_GetIntFromObj
#define Tcl_NewSizeIntFromObj Tcl_NewIntObj
#define TCL_SIZE_MAX INT_MAX
#else
#define Tcl_NewSizeIntFromObj Tcl_NewWideIntObj
#endif /* TCL_SIZE_MAX */

#define SET_ERROR(e) \
    if (err != NULL) { *err = (e); }

#define SET_ERROR_STR(e) SET_ERROR(Tcl_NewStringObj((e), -1));

#if 10 * TCL_MAJOR_VERSION + TCL_MINOR_VERSION < 86
#define USE_VFS_COMMANDS_FOR_ZIP 1
#else
#define USE_ZLIB_TCL86 1
#endif

#if 10 * TCL_MAJOR_VERSION + TCL_MINOR_VERSION >= 85
#define USE_TCL_TRUNCATE 1
#endif

// Define __SANITIZE_ADDRESS__ if asan is enabled in clang. In GCC, this
// is already defined.
#if !defined(__SANITIZE_ADDRESS__) && defined(__has_feature)
#if __has_feature(address_sanitizer) // for clang
#define __SANITIZE_ADDRESS__ // GCC already sets this
#endif
#endif

#include "common.h"
#include "bindata.h"
#include "hashes.h"
#include "pathObj.h"

#ifdef TCL_THREADS
#include "threads.h"
#endif /* TCL_THREADS */

#ifdef COOKFS_USECPKGCONFIG
static Tcl_Config const cookfs_pkgconfig[] = {
    {"package-version",  PACKAGE_VERSION},
    {"c-pages",          STRINGIFY(COOKFS_PKGCONFIG_USECPAGES)},
    {"c-fsindex",        STRINGIFY(COOKFS_PKGCONFIG_USECFSINDEX)},
    {"c-readerchannel",  STRINGIFY(COOKFS_PKGCONFIG_USECREADERCHAN)},
    {"c-writerchannel",  STRINGIFY(COOKFS_PKGCONFIG_USECWRITERCHAN)},
    {"c-vfs",            STRINGIFY(COOKFS_PKGCONFIG_USECVFS)},
    {"c-writer",         STRINGIFY(COOKFS_PKGCONFIG_USECWRITER)},
    {"feature-aside",    STRINGIFY(COOKFS_PKGCONFIG_FEATURE_ASIDE)},
    {"feature-bzip2",    STRINGIFY(COOKFS_PKGCONFIG_USEBZ2)},
    {"feature-lzma",     STRINGIFY(COOKFS_PKGCONFIG_USELZMA)},
    {"feature-metadata", STRINGIFY(COOKFS_PKGCONFIG_FEATURE_METADATA)},
    {"tcl-commands",     STRINGIFY(COOKFS_PKGCONFIG_USETCLCMDS)},
    {NULL, NULL}
};
#endif /* COOKFS_USECPKGCONFIG */

#endif /* COOKFS_H */
