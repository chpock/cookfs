/*
 * hashes.c
 *
 * Provides implementation for md5 hash
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

static int CookfsMd5Cmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Tcl_Obj *obj;
    unsigned char *bytes;
    unsigned char md5[MD5_DIGEST_SIZE];
    Tcl_Size size;

    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-bin? data");
        return TCL_ERROR;
    }

    if (objc == 3) {
        const char *opt = Tcl_GetStringFromObj(objv[1], &size);
        if (size < 1 || strncmp(opt, "-bin", size) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "bad option \"%s\": must be -bin", opt));
            Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "option",
                opt, (const char *)NULL);
            return TCL_ERROR;
        }
        obj = objv[2];
    } else {
        obj = objv[1];
    }

    bytes = Tcl_GetByteArrayFromObj(obj, &size);
    Cookfs_MD5(bytes, size, md5);

    if (objc == 3) {
        obj = Tcl_NewByteArrayObj(md5, MD5_DIGEST_SIZE);
    } else {
        char hex[MD5_DIGEST_SIZE*2+1];
        for (int i = 0; i < MD5_DIGEST_SIZE; i++) {
            sprintf(hex + i*2, "%02X", ((int) md5[i]));
        }
        hex[MD5_DIGEST_SIZE*2] = 0;
        obj = Tcl_NewStringObj(hex, MD5_DIGEST_SIZE*2);
    }

    Tcl_SetObjResult(interp, obj);

    return TCL_OK;
}

int Cookfs_InitHashesCmd(Tcl_Interp *interp) {

    Tcl_CreateObjCommand(interp, "::cookfs::c::md5", (Tcl_ObjCmdProc *)CookfsMd5Cmd,
        (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

    Tcl_CreateAlias(interp, "::cookfs::md5", interp, "::cookfs::c::md5", 0, NULL);

    return TCL_OK;
}
