#
# Include the TEA standard macro set
#

builtin(include,tclconfig/tcl.m4)

#
# Add here whatever m4 macros you want to define for your package
#

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
