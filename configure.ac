#!/bin/bash -norc
dnl        This file is an input file used by the GNU "autoconf" program to
dnl        generate the file "configure", which is run during Tcl installation
dnl        to configure the system for the local environment.
#

#-----------------------------------------------------------------------
# This initializes the environment with PACKAGE_NAME and PACKAGE_VERSION
# set as provided.  These will also be added as -D defs in your Makefile
# so you can encode the package version directly into the source files.
#-----------------------------------------------------------------------

AC_INIT([cookfs],[1.5.0])

#--------------------------------------------------------------------
# Call TEA_INIT as the first TEA_ macro to set up initial vars.
# This will define a ${TEA_PLATFORM} variable == "unix" or "windows"
# as well as PKG_LIB_FILE and PKG_STUB_LIB_FILE.
#--------------------------------------------------------------------

TEA_INIT([3.7])

AC_CONFIG_AUX_DIR(tclconfig)

#--------------------------------------------------------------------
# Load the tclConfig.sh file
#--------------------------------------------------------------------

TEA_PATH_TCLCONFIG
TEA_LOAD_TCLCONFIG

#--------------------------------------------------------------------
# Load the tkConfig.sh file if necessary (Tk extension)
#--------------------------------------------------------------------

#TEA_PATH_TKCONFIG
#TEA_LOAD_TKCONFIG

#-----------------------------------------------------------------------
# Handle the --prefix=... option by defaulting to what Tcl gave.
# Must be called after TEA_LOAD_TCLCONFIG and before TEA_SETUP_COMPILER.
#-----------------------------------------------------------------------

TEA_PREFIX

#-----------------------------------------------------------------------
# Standard compiler checks.
# This sets up CC by using the CC env var, or looks for gcc otherwise.
# This also calls AC_PROG_CC, AC_PROG_INSTALL and a few others to create
# the basic setup necessary to compile executables.
#-----------------------------------------------------------------------

TEA_SETUP_COMPILER

TEA_ADD_SOURCES([cookfs.c common.c md5.h bindata.c])
TEA_ADD_HEADERS([])
TEA_ADD_INCLUDES([-I\"`${CYGPATH} ${srcdir}/generic`\"])
TEA_ADD_LIBS([])
TEA_ADD_CFLAGS([-Wall -Wextra -pedantic])
TEA_ADD_STUB_SOURCES([])
TEA_ADD_TCL_SOURCES([scripts/cookvfs.tcl scripts/memchan.tcl scripts/vfs.tcl scripts/optimize.tcl scripts/asyncworker_process.tcl scripts/asyncworker_thread.tcl])
TEA_ADD_TCL_SOURCES([scripts/readerchannel.tcl])
TEA_ADD_TCL_SOURCES([scripts/writer.tcl])

#--------------------------------------------------------------------
# Define a special symbol for Windows (BUILD_sample in this case) so
# that we create the export library with the dll.
#
# Windows creates a few extra files that need to be cleaned up.
# You can add more files to clean if your extension creates any extra
# files.
#
# TEA_ADD_* any platform specific compiler/build info here.
#--------------------------------------------------------------------

# Add pkgIndex.tcl if it is generated in the Makefile instead of ./configure
# and change Makefile.in to move it from CONFIG_CLEAN_FILES to BINARIES var.
#CLEANFILES="pkgIndex.tcl"
if test "${TEA_PLATFORM}" = "windows" ; then
    # Ensure no empty else clauses
    :
    #TEA_ADD_SOURCES([win/winFile.c])
    #TEA_ADD_INCLUDES([-I\"$(${CYGPATH} ${srcdir}/win)\"])
else
    # Ensure no empty else clauses
    :
    #TEA_ADD_SOURCES([unix/unixFile.c])
    #TEA_ADD_LIBS([-lsuperfly])
fi
AC_SUBST(CLEANFILES)

TEA_PUBLIC_TCL_HEADERS
#TEA_PRIVATE_TCL_HEADERS

#TEA_PUBLIC_TK_HEADERS
#TEA_PRIVATE_TK_HEADERS
#TEA_PATH_X

TEA_ENABLE_THREADS
TEA_ENABLE_SHARED

TEA_CONFIG_CFLAGS

TEA_ENABLE_SYMBOLS

if test "${SHARED_BUILD}" = "1"; then
AC_DEFINE(USE_TCL_STUBS, 1, [Use Tcl stubs])
fi

AC_ARG_ENABLE(internal-debug,  [  --enable-internal-debug   Enable internal debugging mechanism. Defaults to false],INTERNALDEBUG=${enableval},    INTERNALDEBUG=no)
AC_ARG_ENABLE(bz2,             [  --enable-bz2              Use bz2 compression. Defaults to false],                USEBZ2=${enableval},           USEBZ2=no)
AC_ARG_ENABLE(xz,              [  --enable-xz               Use xz compression. Defaults to false],                 USEXZ=${enableval},            USEXZ=no)
AC_ARG_ENABLE(tcl-fallback,    [  --enable-tcl-fallback     Install Tcl scripts if load fails. Defaults to false],  USEFALLBACK=${enableval},      USEFALLBACK=no)
AC_ARG_ENABLE(c-pages,         [  --disable-c-pages         Use pages handling written in C. Defaults to true],     USECPAGES=${enableval},        USECPAGES=yes)
AC_ARG_ENABLE(c-fsindex,       [  --disable-c-fsindex       Use index handling written in C. Defaults to true],     USECFSINDEX=${enableval},      USECFSINDEX=yes)
AC_ARG_ENABLE(c-readerchannel, [  --disable-c-readerchannel Use reader handler written in C. Defaults to true],     USECREADERCHAN=${enableval},   USECREADERCHAN=yes)

if test ${INTERNALDEBUG} = yes; then
    AC_DEFINE(COOKFS_INTERNAL_DEBUG)
fi

TEA_ADD_TCL_SOURCES([scripts/pages.tcl])
TEA_ADD_TCL_SOURCES([scripts/readerchannel.tcl])
TEA_ADD_TCL_SOURCES([scripts/fsindex.tcl])

if test ${USECPAGES} = yes; then
    AC_DEFINE(COOKFS_USECPAGES)
    COOKFS_PKGCONFIG_USECPAGES=1
    COOKFS_PKGCONFIG_FEATURE_ASIDE=1
    TEA_ADD_SOURCES([pages.c pagesCompr.c pagesCmd.c])

    # reader channel requires support for pages in C
    if test ${USECREADERCHAN} = yes; then
        AC_DEFINE(COOKFS_USECREADERCHAN)
        COOKFS_PKGCONFIG_USECREADERCHAN=1
        TEA_ADD_SOURCES([readerchannel.c readerchannelIO.c])
    else
        COOKFS_PKGCONFIG_USECREADERCHAN=0
    fi

    # enable bz2 files only if pages are handled using C
    if test ${USEBZ2} = yes; then
        BUILDCCODE=yes
        TEA_ADD_SOURCES([bzip2/blocksort.c bzip2/huffman.c bzip2/crctable.c bzip2/randtable.c bzip2/compress.c bzip2/decompress.c bzip2/bzlib.c])
        AC_DEFINE(COOKFS_USEBZ2)
        COOKFS_PKGCONFIG_USEBZ2=1
    else
        COOKFS_PKGCONFIG_USEBZ2=0
    fi

    if test ${USEXZ} = yes; then
        BUILDCCODE=yes
        # XZ requires these CPU features: sse4.1 avx avx2
        AC_MSG_CHECKING(for SSE4.1 instruction set)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
            #include <smmintrin.h>
        ]],[])],
            [AC_MSG_RESULT(yes)],
            [AC_MSG_RESULT([no, adding -msse4.1]); TEA_ADD_CFLAGS([-msse4.1])]
        )
        AC_MSG_CHECKING(for AVX/AVX2 instruction set)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
            #include <immintrin.h>
        ]],[[
            __m256i sub2;
        ]])],
          [AC_MSG_RESULT(yes)],
          [AC_MSG_RESULT([no, adding -mavx -mavx2]); TEA_ADD_CFLAGS([-mavx -mavx2])]
        )
        TEA_ADD_SOURCES([7zip/C/XzEnc.c 7zip/C/XzDec.c 7zip/C/LzmaEnc.c 7zip/C/Lzma2Enc.c 7zip/C/Lzma2Dec.c 7zip/C/Xz.c 7zip/C/XzIn.c 7zip/C/XzCrc64.c 7zip/C/XzCrc64Opt.c 7zip/C/LzFind.c 7zip/C/LzFindMt.c 7zip/C/LzFindOpt.c 7zip/C/MtCoder.c 7zip/C/MtDec.c 7zip/C/Sha256.c 7zip/C/Sha256Opt.c 7zip/C/7zCrc.c 7zip/C/7zCrcOpt.c 7zip/C/7zStream.c 7zip/C/Lzma86Enc.c 7zip/C/LzmaDec.c 7zip/C/CpuArch.c 7zip/C/Threads.c 7zip/C/Delta.c 7zip/C/Alloc.c 7zip/C/Bra.c 7zip/C/Bra86.c])
        TEA_ADD_INCLUDES([-I\"`${CYGPATH} ${srcdir}/7zip/C`\"])
        AC_DEFINE(COOKFS_USEXZ)
        COOKFS_PKGCONFIG_USEXZ=1
    else
        COOKFS_PKGCONFIG_USEXZ=0
    fi
else
    COOKFS_PKGCONFIG_USECPAGES=0
    COOKFS_PKGCONFIG_FEATURE_ASIDE=0

    # if not using c-pages, use Tcl only readerchannel
    COOKFS_PKGCONFIG_USECREADERCHAN=0

    # Tcl implementation offers bzip2 compression assuming Trf package is present
    COOKFS_PKGCONFIG_USEBZ2=1
fi

AC_SUBST(COOKFS_PKGCONFIG_FEATURE_ASIDE)
AC_SUBST(COOKFS_PKGCONFIG_USECPAGES)
AC_SUBST(COOKFS_PKGCONFIG_USECREADERCHAN)
AC_SUBST(COOKFS_PKGCONFIG_USEBZ2)
AC_SUBST(COOKFS_PKGCONFIG_USEXZ)

if test ${USECFSINDEX} = yes; then
    AC_DEFINE(COOKFS_USECFSINDEX)
    COOKFS_PKGCONFIG_USECFSINDEX=1
    COOKFS_PKGCONFIG_FEATURE_METADATA=1
    TEA_ADD_SOURCES([fsindex.c fsindexIO.c fsindexCmd.c])
else
    COOKFS_PKGCONFIG_USECFSINDEX=0

    # TODO: move this outside conditions since
    # metadata is supported in pure Tcl version as well
    COOKFS_PKGCONFIG_FEATURE_METADATA=1
fi
AC_SUBST(COOKFS_PKGCONFIG_USECFSINDEX)
AC_SUBST(COOKFS_PKGCONFIG_FEATURE_METADATA)


#--------------------------------------------------------------------
# This macro generates a line to use when building a library.  It
# depends on values set by the TEA_ENABLE_SHARED, TEA_ENABLE_SYMBOLS,
# and TEA_LOAD_TCLCONFIG macros above.
#--------------------------------------------------------------------

TEA_MAKE_LIB

#--------------------------------------------------------------------
# Determine the name of the tclsh and/or wish executables in the
# Tcl and Tk build directories or the location they were installed
# into. These paths are used to support running test cases only,
# the Makefile should not be making use of these paths to generate
# a pkgIndex.tcl file or anything else at extension build time.
#--------------------------------------------------------------------

TEA_PROG_TCLSH
#TEA_PROG_WISH

#--------------------------------------------------------------------
# Finally, substitute all of the various values into the Makefile.
# You may alternatively have a special pkgIndex.tcl.in or other files
# which require substituting th AC variables in.  Include these here.
#--------------------------------------------------------------------

AC_CONFIG_FILES([Makefile pkgIndex.tcl pkgconfig.tcl])
AC_OUTPUT
