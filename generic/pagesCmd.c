/* (c) 2010 Pawel Salawa */

#include "cookfs.h"

static int deprecatedCounter = 0;

static int CookfsPagesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsPagesDeleteProc(ClientData clientData);
static int CookfsRegisterPagesObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

int Cookfs_InitPagesCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::pages", CookfsRegisterPagesObjectCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
}

/* command for creating new objects that deal with pages */
static int CookfsRegisterPagesObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    char *options[] = { "-readonly", "-readwrite", "-compression", "-cachesize", NULL };
    enum { optReadonly = 0, optReadwrite, optCompression, optCachesize };
    char buf[128];
    Cookfs_Pages *pages;
    int idx = ++deprecatedCounter;
    int oReadOnly = 0;
    int oCompression = 0;
    int tobjc = objc;
    int oCachesize = -1;
    Tcl_Obj **tobjv = (Tcl_Obj **) objv;

    while (tobjc > 1) {
        if (Tcl_GetIndexFromObj(interp, tobjv[1], (const char **) options, "", 0, &idx) != TCL_OK) {
            break;
        }
        switch (idx) {
            case optReadonly:
                oReadOnly = 1;
                break;
            case optReadwrite:
                oReadOnly = 0;
                break;
            case optCompression: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                if (Tcl_GetIndexFromObj(interp, tobjv[1], (const char **) cookfsCompressionOptions, "compression", 0, &oCompression) != TCL_OK) {
                    return TCL_ERROR;
                }
                break;
            }
            case optCachesize: {
                int csize;
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                if (Tcl_GetIntFromObj(interp, tobjv[1], &csize) != TCL_OK) {
                    return TCL_ERROR;
                }

                /* disallow specifying negative values for cache */
                if (csize < 0) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("Negative cache size specified", -1));
                    return TCL_ERROR;
                }

                /* if number of pages to cache exceeds our capability, limit it quietly */
                if (csize > COOKFS_MAX_CACHE_PAGES) {
                    csize = COOKFS_MAX_CACHE_PAGES;
                }

                oCachesize = csize;
                break;
            }
        }
        tobjc--;
        tobjv++;
    }

    if (tobjc != 2) {
        goto ERROR;
    }

    /* TODO: parse arguments etc */    
    pages = Cookfs_PagesInit(tobjv[1], oReadOnly, oCompression, NULL);
    if (oCachesize >= 0) {
        pages->cacheSize = oCachesize;
    }
    if (pages == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create Cookfs object", -1));
        return TCL_ERROR;
    }
    sprintf(buf, "::cookfs::pageshandle%d", idx);

    Tcl_CreateObjCommand(interp, buf, CookfsPagesCmd, (ClientData) pages, CookfsPagesDeleteProc);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));   
    return TCL_OK;

ERROR:
    Tcl_WrongNumArgs(interp, 1, objv, "?-readonly|-readwrite? ?-compression mode? fileName");
    return TCL_ERROR;
}

static int CookfsPagesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    char *commands[] = { "add", "get", "index", "length", "dataoffset", "delete", NULL };
    enum { cmdAdd = 0, cmdGet, cmdIndex, cmdLength, cmdDataoffset, cmdDelete };
    int idx;
    Cookfs_Pages *p = (Cookfs_Pages *) clientData;
    
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], (const char **) commands, "command", 0, &idx) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (idx) {
        case cmdAdd:
        {
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "data");
                return TCL_ERROR;
            }
            idx = Cookfs_PageAdd(p, objv[2]);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(idx));
            break;
        }
        case cmdGet:
        {
            int idx;
            Tcl_Obj *rc;
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "index");
                return TCL_ERROR;
            }
            if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGet(p, idx);
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve part", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
            }
            break;
        }
        case cmdIndex:
        {
            if (objc == 3) {
                Tcl_DecrRefCount(p->dataIndex);
                p->dataIndex = objv[2];
                Tcl_IncrRefCount(p->dataIndex);
            }
            Tcl_SetObjResult(interp, p->dataIndex);
            break;
        }
        case cmdLength:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(p->dataNumPages));
            break;
        }
        case cmdDelete:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], NULL));
            break;
        }
        case cmdDataoffset:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, Tcl_NewWideIntObj(p->dataInitialOffset));
            break;
        }
    }
    return TCL_OK;
}

static void CookfsPagesDeleteProc(ClientData clientData) {
    CookfsLog(printf("DELETING PAGES COMMAND"))
    Cookfs_PagesFini((Cookfs_Pages *) clientData);
}
