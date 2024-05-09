/*
 * cookfs.h
 *
 * Main cookfs include file:
 * - includes standard headers
 * - defines (temporary) debug/logging macro definitions
 * - includes additional header files, if enabled in configure
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#ifndef COOKFS_H
#define COOKFS_H 1

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "common.h"
#include "bindata.h"
#include "hashes.h"

#ifdef COOKFS_USECPAGES
#include "pages.h"
#include "pagesCmd.h"
#include "pagesCompr.h"
#endif /* COOKFS_USECPAGES */

#ifdef COOKFS_USECFSINDEX
#include "fsindex.h"
#include "fsindexIO.h"
#include "fsindexCmd.h"
#endif /* COOKFS_USECFSINDEX */

#ifdef COOKFS_USECREADERCHAN
#include "readerchannel.h"
#include "readerchannelIO.h"
#endif /* COOKFS_USECREADERCHAN */

#ifdef COOKFS_USEPKGCONFIG
static Tcl_Config const cookfs_pkgconfig[] = {
    {"package-version",  PACKAGE_VERSION},
    {"c-pages",          STRINGIFY(COOKFS_PKGCONFIG_USECPAGES)},
    {"c-fsindex",        STRINGIFY(COOKFS_PKGCONFIG_USECFSINDEX)},
    {"c-readerchannel",  STRINGIFY(COOKFS_PKGCONFIG_USECREADERCHAN)},
    {"feature-aside",    STRINGIFY(COOKFS_PKGCONFIG_FEATURE_ASIDE)},
    {"feature-bzip2",    STRINGIFY(COOKFS_PKGCONFIG_USEBZ2)},
    {"feature-xz",       STRINGIFY(COOKFS_PKGCONFIG_USEXZ)},
    {"feature-metadata", STRINGIFY(COOKFS_PKGCONFIG_FEATURE_METADATA)},
    {NULL, NULL}
};
#endif /* COOKFS_USEPKGCONFIG */

#endif /* COOKFS_H */
