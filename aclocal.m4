#
# Include the TEA standard macro set
#

builtin(include,tclconfig/tcl.m4)

#
# Add here whatever m4 macros you want to define for your package
#

AC_DEFUN([COOKFS_SET_PLATFORM], [
    AC_MSG_CHECKING([for platform])
    AC_CANONICAL_BUILD
    if test -n "$build_alias"; then
        cookfs_platform="$build_alias"
    elif test -n "$build"; then
        cookfs_platform="$build"
    else
        AC_MSG_ERROR([could not detect current platform])
    fi
    AC_MSG_RESULT([$cookfs_platform])
    AC_DEFINE_UNQUOTED(COOKFS_PLATFORM, ["$cookfs_platform"])
])

AC_DEFUN([COOKFS_PROG_TCLSH], [
    AC_MSG_CHECKING([for correct tclsh])

    if ! test -f "${TCLSH_PROG}" && ! command -v "${TCLSH_PROG}" >/dev/null 2>&1; then
        if test "${TEA_PLATFORM}" = "windows"; then
            base_name="tclsh${TCL_MAJOR_VERSION}${TCL_MINOR_VERSION}"
            name_list="${base_name}${EXEEXT} \
                ${base_name}s${EXEEXT} \
                ${base_name}t${EXEEXT} \
                ${base_name}st${EXEEXT} \
                ${base_name}g${EXEEXT} \
                ${base_name}gs${EXEEXT} \
                ${base_name}gt${EXEEXT} \
                ${base_name}gst${EXEEXT}"
            for i in $name_list; do
                if test -f "${TCL_BIN_DIR}/../bin/$i"; then
                    TCLSH_PROG="`cd "${TCL_BIN_DIR}/../bin"; pwd`/$i"
                    break
                fi
            done
        fi
        if test -f "${TCLSH_PROG}"; then
            AC_MSG_RESULT([${TCLSH_PROG}])
            AC_SUBST(TCLSH_PROG)
        else
            AC_MSG_RESULT([fail])
            AC_MSG_ERROR([ERROR: could not find tclsh binary])
        fi
    else
        AC_MSG_RESULT([ok])
    fi
])

AC_DEFUN([COOKFS_SET_LDFLAGS], [

    _LDFLAGS="$LDFLAGS"
    _CFLAGS="$CFLAGS"
    LDFLAGS="-Wl,-Map=conftest.map"
    CFLAGS=
    AC_MSG_CHECKING([whether LD supports -Wl,-Map=])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([])],[
        AC_MSG_RESULT([yes])
        LDFLAGS="$_LDFLAGS -Wl,-Map=\[$]@.map"
    ],[
        AC_MSG_RESULT([no])
        LDFLAGS="$_LDFLAGS"
    ])
    CFLAGS="$_CFLAGS"

    _LDFLAGS="$LDFLAGS"
    _CFLAGS="$CFLAGS"
    LDFLAGS="-Wl,--gc-sections"
    CFLAGS=
    AC_MSG_CHECKING([whether LD supports -Wl,--gc-sections])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([])],[
        AC_MSG_RESULT([yes])
        LDFLAGS="$_LDFLAGS -Wl,--gc-sections -Wl,--as-needed"
    ],[
        AC_MSG_RESULT([no])
        LDFLAGS="$_LDFLAGS"
    ])
    CFLAGS="$_CFLAGS"

    if test "${CFLAGS_DEFAULT}" != "${CFLAGS_DEBUG}"; then

        # Test this only on MacOS as GNU ld interprets -dead_strip
        # as '-de'+'ad_strip'
        if test "$SHLIB_SUFFIX" = ".dylib"; then
            _LDFLAGS="$LDFLAGS"
            _CFLAGS="$CFLAGS"
            LDFLAGS="-Wl,-dead_strip"
            CFLAGS=
            AC_MSG_CHECKING([whether LD supports -Wl,-dead_strip])
            AC_LINK_IFELSE([AC_LANG_PROGRAM([])],[
                AC_MSG_RESULT([yes])
                LDFLAGS="$_LDFLAGS -Wl,-dead_strip"
            ],[
                AC_MSG_RESULT([no])
                LDFLAGS="$_LDFLAGS"
            ])
            CFLAGS="$_CFLAGS"
        fi

        _LDFLAGS="$LDFLAGS"
        _CFLAGS="$CFLAGS"
        LDFLAGS="-s"
        CFLAGS=
        AC_MSG_CHECKING([whether LD supports -s])
        AC_LINK_IFELSE([AC_LANG_PROGRAM([])],[
            AC_MSG_RESULT([yes])
            LDFLAGS="$_LDFLAGS -s"
        ],[
            AC_MSG_RESULT([no])
            LDFLAGS="$_LDFLAGS"
        ])
        CFLAGS="$_CFLAGS"
    fi

])
