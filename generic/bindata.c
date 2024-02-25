/*
 * bindata.c
 *
 * Provides Tcl commands for passing binary data across threads without copying memory
 *
 * (c) 2014 Wojciech Kocjan
 */

#include "cookfs.h"

/* definitions of static and/or internal functions */
static int CookfsBinaryDataCommand(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsBinaryDataParseAddress(Tcl_Interp *interp, Tcl_Obj *address, void **addressPtr);
static Tcl_Obj *CookfsBinaryDataCreateAddress(void *address);


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_InitBinaryDataCmd --
 *
 *	Initializes binary data command in specific
 *	Tcl interpreter
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_InitBinaryDataCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::binarydata", CookfsBinaryDataCommand,
        (ClientData) NULL, NULL);

    return TCL_OK;
}


/* definitions of static and/or internal functions */

static int CookfsBinaryDataCommand(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static char *commands[] = { "create", "retrieve", NULL };
    enum { cmdCreate, cmdRetrieve };
    int cmd;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "create|retrieve data");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], (const char **) commands, "command", 0, &cmd) != TCL_OK) {
        return TCL_ERROR;
    }
    
    switch (cmd) {
        case cmdCreate:
        {
            Tcl_IncrRefCount(objv[2]);
            Tcl_SetObjResult(interp, CookfsBinaryDataCreateAddress((void *) objv[2]));
            return TCL_OK;
        }
        case cmdRetrieve:
        {
            Tcl_Obj *address;
            if (CookfsBinaryDataParseAddress(interp, objv[2], (void **) &address) != TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve binary data", -1));
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, address);
            Tcl_DecrRefCount(address);
            return TCL_OK;
        }
    }
    
    // fallback
    return TCL_ERROR;
}

  
static int CookfsBinaryDataParseAddress(Tcl_Interp *interp, Tcl_Obj *address, void **addressPtr) {
    if (sscanf(Tcl_GetStringFromObj(address, NULL), "%p", addressPtr) == 1) {
        return TCL_OK;
    } else {
        *addressPtr = NULL;
        return TCL_ERROR;
    }
}


static Tcl_Obj *CookfsBinaryDataCreateAddress(void *address) {
    char buf[128];
    sprintf(buf, "%p", address);
    return Tcl_NewStringObj(buf, -1);
}
