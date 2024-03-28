/*
 * hashes.c
 *
 * Provides implementation for md5/sha1/sha256 hashes
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#ifdef COOKFS_USEXZ
#include "Sha256.h"
#include "Sha1.h"
#endif

static int CookfsMd5Cmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Tcl_Obj *obj;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "data");
        return TCL_ERROR;
    }

    obj = Cookfs_MD5FromObj(objv[1]);
    Tcl_SetObjResult(interp, obj);
    return TCL_OK;
}

static int CookfsSha256Cmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Tcl_Obj *obj;
    CSha256 ctx;
    unsigned char *bytes;
    unsigned char sha256[SHA256_DIGEST_SIZE];
    char hex[SHA256_DIGEST_SIZE*2+1];
    int i;
    int size;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "data");
        return TCL_ERROR;
    }

    bytes = Tcl_GetByteArrayFromObj(objv[1], &size);
    Sha256_Init(&ctx);
    Sha256_Update(&ctx, bytes, size);
    Sha256_Final(&ctx, sha256);

    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        sprintf(hex + i*2, "%02X", ((int) sha256[i]));
    }

    hex[SHA256_DIGEST_SIZE*2] = 0;
    obj = Tcl_NewStringObj(hex, SHA256_DIGEST_SIZE*2);
    Tcl_SetObjResult(interp, obj);

    return TCL_OK;
}

static int CookfsSha1Cmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Tcl_Obj *obj;
    CSha1 ctx;
    unsigned char *bytes;
    unsigned char sha1[SHA1_DIGEST_SIZE];
    char hex[SHA1_DIGEST_SIZE*2+1];
    int i;
    int size;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "data");
        return TCL_ERROR;
    }

    bytes = Tcl_GetByteArrayFromObj(objv[1], &size);
    Sha1_Init(&ctx);
    Sha1_Update(&ctx, bytes, size);
    Sha1_Final(&ctx, sha1);

    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        sprintf(hex + i*2, "%02X", ((int) sha1[i]));
    }

    hex[SHA1_DIGEST_SIZE*2] = 0;
    obj = Tcl_NewStringObj(hex, SHA1_DIGEST_SIZE*2);
    Tcl_SetObjResult(interp, obj);

    return TCL_OK;
}

int Cookfs_InitHashesCmd(Tcl_Interp *interp) {

    Tcl_CreateObjCommand(interp, "::cookfs::c::md5", (Tcl_ObjCmdProc *)CookfsMd5Cmd,
        (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

#ifdef COOKFS_USEXZ
    Tcl_CreateObjCommand(interp, "::cookfs::c::sha256", (Tcl_ObjCmdProc *)CookfsSha256Cmd,
        (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand(interp, "::cookfs::c::sha1", (Tcl_ObjCmdProc *)CookfsSha1Cmd,
        (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
#endif

    return TCL_OK;
}
