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

#ifndef Tcl_BounceRefCount
#define Tcl_BounceRefCount(x) Tcl_IncrRefCount((x));Tcl_DecrRefCount((x))
#endif

#ifdef COOKFS_INTERNAL_DEBUG

#ifndef __FUNCTION_NAME__
    #ifdef _WIN32   // WINDOWS
        #define __FUNCTION_NAME__   __FUNCTION__
    #else          // GCC
        #define __FUNCTION_NAME__   __func__
    #endif
#endif

static inline void __cookfs_debug_dump(unsigned char *data, Tcl_Size size) {
    printf("Dump: 00 01 02 03  04 05 06 07  08 09 0A 0B  0C 0D 0E 0F\n");
    printf("--------------------------------------------------------\n");
    int row = 0;
    int col = 0;
    for (Tcl_Size i = 0; i < size; i++) {
        if (!col) {
            printf(" %02X |", row);
        } else if (!(col % 4)) {
            printf(" ");
        }
        printf(" %02X", data[i]);
        if (col == 15) {
            printf("\n");
            col = 0;
            row++;
        } else {
            col++;
        }
    }
    if (col) {
        printf("\n");
    }
    printf("------------------------------[ Total: %8" TCL_SIZE_MODIFIER "d"
        " bytes ]-\n", size);
}

// This is an experiment to print debug messages indented according to
// the current stack depth. The -funwind-tables compiler key must be used
// for the backtrace() function to work.
//
// This feature is not currently used, but may be used in the future.
//
// #include <execinfo.h>
// static inline int ___get_stack_depth() {
//     void *buffer[200];
//     return backtrace(buffer, 200);
// }
// #define CookfsLog(a) {printf("%d ", ___get_stack_depth()); a; printf("\n"); fflush(stdout);}

// #define CookfsLog(a) {printf("[%p] ", (void *)Tcl_GetCurrentThread()); a; printf("\n"); fflush(stdout);}
// #define CookfsLog2(a) {printf("[%p] ", (void *)Tcl_GetCurrentThread()); printf("%s: ", __FUNCTION_NAME__); a; printf("\n"); fflush(stdout);}
#define CookfsLog(a) {a; printf("\n"); fflush(stdout);}
#define CookfsLog2(a) {printf("%s: ", __FUNCTION_NAME__); a; printf("\n"); fflush(stdout);}
#define CookfsDump(d,s) __cookfs_debug_dump((unsigned char *)d,s)
#else
#define CookfsLog(a) {}
#define CookfsLog2(a) {}
#define CookfsDump(d,s) {}
#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

#ifndef STRINGIFY
#  define STRINGIFY(x) STRINGIFY1(x)
#  define STRINGIFY1(x) #x
#endif

#define SET_ERROR(e) \
    if (err != NULL) { \
        if (*err != NULL) { Tcl_BounceRefCount(*err); }; \
        *err = (e); \
    }

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

#define PRINTF_MD5_FORMAT "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
#define PRINTF_MD5_VAR(x) (x)[0] ,(x)[1], (x)[2], (x)[3],  \
                          (x)[4] ,(x)[5], (x)[6], (x)[7],  \
                          (x)[8] ,(x)[9], (x)[10],(x)[11], \
                          (x)[12],(x)[13],(x)[14],(x)[15]

#include "common.h"
#include "bindata.h"
#include "hashes.h"
#include "pathObj.h"
#include "tclcookfs.h"

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
    {"c-crypto",         STRINGIFY(COOKFS_PKGCONFIG_USECCRYPTO)},
    {"feature-crypto",   STRINGIFY(COOKFS_PKGCONFIG_FEATURE_CRYPTO)},
    {"feature-aside",    STRINGIFY(COOKFS_PKGCONFIG_FEATURE_ASIDE)},
    {"feature-bzip2",    STRINGIFY(COOKFS_PKGCONFIG_USEBZ2)},
    {"feature-lzma",     STRINGIFY(COOKFS_PKGCONFIG_USELZMA)},
    {"feature-zstd",     STRINGIFY(COOKFS_PKGCONFIG_USEZSTD)},
    {"feature-brotli",   STRINGIFY(COOKFS_PKGCONFIG_USEBROTLI)},
    {"feature-metadata", STRINGIFY(COOKFS_PKGCONFIG_FEATURE_METADATA)},
    {"tcl-commands",     STRINGIFY(COOKFS_PKGCONFIG_USETCLCMDS)},
    {NULL, NULL}
};
#endif /* COOKFS_USECPKGCONFIG */

#endif /* COOKFS_H */
