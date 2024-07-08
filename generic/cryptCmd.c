/*
 * cryptCmd.c
 *
 * Provides Tcl commands for cryptographic functions
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "crypt.h"
#include "cryptCmd.h"

static int CookfsAesEncryptObjectCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if ((objc != 3 && objc != 5) || (objc == 5 &&
        strcmp(Tcl_GetString(objv[1]), "-iv") != 0))
    {
        Tcl_WrongNumArgs(interp, 1, objv, "?-iv iv? data key");
        return TCL_ERROR;
    }

    Tcl_Size keyLen;
    unsigned char *keyStr = Tcl_GetByteArrayFromObj(objv[objc - 1], &keyLen);

    if (keyLen != COOKFS_ENCRYPT_KEY_SIZE) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("the key size must be exactly"
            " %d bytes, but a key of %" TCL_SIZE_MODIFIER "d bytes"
            " is specified", COOKFS_ENCRYPT_KEY_SIZE, keyLen));
        return TCL_ERROR;
    }

    Tcl_Size ivLen;
    unsigned char *ivStr;
    if (objc == 5) {
        ivStr = Tcl_GetByteArrayFromObj(objv[2], &ivLen);
        if (ivLen != COOKFS_PAGEOBJ_BLOCK_SIZE) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("the IV size must be exactly"
                " %d bytes, but an IV of %" TCL_SIZE_MODIFIER "d bytes"
                " is specified", COOKFS_PAGEOBJ_BLOCK_SIZE, ivLen));
            return TCL_ERROR;
        }
    } else {
        ivStr = NULL;
    }

    Cookfs_PageObj pg = Cookfs_PageObjNewFromByteArray(objv[objc - 2]);

    if (ivStr != NULL) {
        Cookfs_PageObjSetIV(pg, ivStr);
    }

    Cookfs_AesEncrypt(pg, keyStr);

    Tcl_SetObjResult(interp, Cookfs_PageObjCopyAsByteArrayIV(pg));

    Cookfs_PageObjBounceRefCount(pg);

    return TCL_OK;

}

static int CookfsAesDecryptObjectCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "data key");
        return TCL_ERROR;
    }

    Tcl_Size keyLen;
    unsigned char *keyStr = Tcl_GetByteArrayFromObj(objv[2], &keyLen);

    if (keyLen != COOKFS_ENCRYPT_KEY_SIZE) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("the key size must be exactly"
            " %d bytes, but a key of %" TCL_SIZE_MODIFIER "d bytes"
            " is specified", COOKFS_ENCRYPT_KEY_SIZE, keyLen));
        return TCL_ERROR;
    }

    Cookfs_PageObj pg = Cookfs_PageObjNewFromByteArrayIV(objv[1]);

    if (pg == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unencrypted data was"
            " specified", -1));
        return TCL_ERROR;
    }

    int rc = Cookfs_AesDecrypt(pg, keyStr);

    if (rc != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to decrypt"
            " the specified data", -1));
    } else {
        Tcl_SetObjResult(interp, Cookfs_PageObjCopyAsByteArray(pg));
    }

    Cookfs_PageObjBounceRefCount(pg);

    return rc;

}

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

    Tcl_CreateObjCommand(interp, "::cookfs::c::crypt::aes_encrypt",
        CookfsAesEncryptObjectCmd, (ClientData) NULL, NULL);

    Tcl_CreateObjCommand(interp, "::cookfs::c::crypt::aes_decrypt",
        CookfsAesDecryptObjectCmd, (ClientData) NULL, NULL);

    Tcl_CreateAlias(interp, "::cookfs::crypt::rng", interp,
        "::cookfs::c::crypt::rng", 0, NULL);
    Tcl_CreateAlias(interp, "::cookfs::crypt::pbkdf2_hmac", interp,
        "::cookfs::c::crypt::pbkdf2_hmac", 0, NULL);
    Tcl_CreateAlias(interp, "::cookfs::crypt::aes_encrypt", interp,
        "::cookfs::c::crypt::aes_encrypt", 0, NULL);
    Tcl_CreateAlias(interp, "::cookfs::crypt::aes_decrypt", interp,
        "::cookfs::c::crypt::aes_decrypt", 0, NULL);

    return TCL_OK;

}
