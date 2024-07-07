/*
 * cryptCmd.c
 *
 * Provides Tcl commands for cryptographic functions
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "crypt.h"

static int CookfsRandomGenerateObjectCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "size");
        return TCL_ERROR;
    }

    int size;
    if (Tcl_GetIntFromObj(interp, objv[1], &size) != TCL_OK) {
        return TCL_ERROR;
    }

    Tcl_Obj *rc = Tcl_NewByteArrayObj(NULL, 0);
    unsigned char *str = Tcl_SetByteArrayLength(rc, size);

    Cookfs_RandomGenerate(interp, str, size);
    Tcl_SetObjResult(interp, rc);

    return TCL_OK;
}

static int CookfsPbkdf2HmacObjectCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-iterations iterations? ?-dklen"
            " dklen? password salt");
        return TCL_ERROR;
    }

    static const char *const options[] = {
        "-iterations", "-dklen", NULL
    };

    // Default values for iterations and dklen
    int opt_vals[2] = { 1, 32 };

    int idx = 1;
    for (; idx < objc - 2; idx++) {

        int opt;
        if (Tcl_GetIndexFromObj(interp, objv[idx], options, "option", 0, &opt)
            != TCL_OK)
        {
            return TCL_ERROR;
        }

        if (++idx >= objc - 2) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing argument to"
                " %s option", options[opt]));
            return TCL_ERROR;
        }

        if (Tcl_GetIntFromObj(NULL, objv[idx], &opt_vals[opt]) != TCL_OK
            || opt_vals[opt] < 1)
        {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("option %s requires"
                " an unsigned integer >= 1 as value, but got \"%s\"",
                options[opt], Tcl_GetString(objv[idx])));
            return TCL_ERROR;
        }

    }

    Tcl_Size passLen;
    unsigned char *passStr = Tcl_GetByteArrayFromObj(objv[idx++], &passLen);

    Tcl_Size saltLen;
    unsigned char *saltStr = Tcl_GetByteArrayFromObj(objv[idx++], &saltLen);

    Tcl_Obj *rc = Tcl_NewByteArrayObj(NULL, 0);
    unsigned char *str = Tcl_SetByteArrayLength(rc, opt_vals[1]);

    Cookfs_Pbkdf2Hmac(passStr, passLen, saltStr, saltLen, opt_vals[0],
        opt_vals[1], str);

    Tcl_SetObjResult(interp, rc);

    return TCL_OK;
}

int Cookfs_InitCryptCmd(Tcl_Interp *interp) {

    Tcl_CreateNamespace(interp, "::cookfs::c::crypt", NULL, NULL);

    Tcl_CreateObjCommand(interp, "::cookfs::c::crypt::rng",
        CookfsRandomGenerateObjectCmd, (ClientData) NULL, NULL);

    Tcl_CreateObjCommand(interp, "::cookfs::c::crypt::pbkdf2_hmac",
        CookfsPbkdf2HmacObjectCmd, (ClientData) NULL, NULL);

    Tcl_CreateAlias(interp, "::cookfs::crypt::rng", interp,
        "::cookfs::c::crypt::rng", 0, NULL);
    Tcl_CreateAlias(interp, "::cookfs::crypt::pbkdf2_hmac", interp,
        "::cookfs::c::crypt::pbkdf2_hmac", 0, NULL);

    return TCL_OK;

}
