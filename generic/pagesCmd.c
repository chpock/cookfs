/*
 * pagesCompre.c
 *
 * Provides Tcl commands for managing pages
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

/* definitions of static and/or internal functions */
static int CookfsPagesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsPagesCmdHash(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsPagesDeleteProc(ClientData clientData);
static int CookfsRegisterPagesObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsRegisterExistingPagesObjectCmd(Tcl_Interp *interp, Cookfs_Pages *p);

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_InitPagesCmd --
 *
 *	Initializes pages component for specified
 *	Tcl interpreter
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	Creates command for creating pages instances
 *
 *----------------------------------------------------------------------
 */

int Cookfs_InitPagesCmd(Tcl_Interp *interp) {
    Tcl_CreateNamespace(interp, "::cookfs::c::pages", NULL, NULL);
    Tcl_CreateObjCommand(interp, "::cookfs::c::pages", CookfsRegisterPagesObjectCmd,
        (ClientData) NULL, NULL);
    Tcl_CreateAlias(interp, "::cookfs::pages", interp, "::cookfs::c::pages", 0, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsGetPagesObjectCmd --
 *
 *      Returns a Tcl command for existing pages object
 *
 * Results:
 *      Tcl_Obj with zero refcount that contains Tcl command for
 *      the pages object or NULL on failure.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *CookfsGetPagesObjectCmd(Tcl_Interp *interp, Cookfs_Pages *p) {
    if (p == NULL) {
        return NULL;
    }
    CookfsRegisterExistingPagesObjectCmd(interp, p);
    Tcl_Obj *rc = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, p->commandToken, rc);
    CookfsLog(printf("CookfsGetPagesObjectCmd: return [%p]", (void *)rc));
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsRegisterExistingPagesObjectCmd --
 *
 *      Creates a Tcl command for existing pages object
 *
 * Results:
 *      None
 *
 * Side effects:
 *      New Tcl command is created on success. Set the interp result to
 *      the name of the created command.
 *
 *----------------------------------------------------------------------
 */

static void CookfsRegisterExistingPagesObjectCmd(Tcl_Interp *interp, Cookfs_Pages *p) {
    if (p->commandToken != NULL) {
        return;
    }
    char buf[128];
    /* create Tcl command and return its name */
    sprintf(buf, "::cookfs::c::pages::handle%p", (void *)p);
    p->commandToken = Tcl_CreateObjCommand(interp, buf, CookfsPagesCmd,
        (ClientData)p, CookfsPagesDeleteProc);
    p->interp = interp;
}

/* command for creating new objects that deal with pages */

/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsRegisterPagesObjectCmd --
 *
 *	Creates a new pages instance and Tcl command for managing
 *	this pages object
 *
 *	It accepts the following flags
 *	  -readonly		Open file as read-only
 *	  -readwrite		Open file for reading and writing
 *	  -cachesize		Number of pages to cache
 *	  -endoffset		If specified, use following offset as end of cookfs archive
 *	  			If not specified, end of file used as end of cookfs archive
 *	  -compression		Compression to use for new pages/index
 *	  			(allowed values: none, zlib, bzip2, custom)
 *	  -compresscommand	Command used for compressing archive, if -compression is set to custom
 *	  -decompresscommand	Command to use for decompressing pages with custom compression
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR on error; result for interp
 *	is set with proper error message in case of failure
 *
 * Side effects:
 *	New Tcl command is created on success
 *
 *----------------------------------------------------------------------
 */

static int CookfsRegisterPagesObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    static char *options[] = { "-readonly", "-readwrite", "-compression", "-cachesize", "-endoffset", "-compresscommand", "-decompresscommand", "-asynccompresscommand", "-asyncdecompresscommand", "-alwayscompress", "-asyncdecompressqueuesize", NULL };
    enum { optReadonly = 0, optReadwrite, optCompression, optCachesize, optEndoffset, optCompressCommand, optDecompressCommand, optAsyncCompressCommand, optAsyncDecompressCommand, optAlwaysCompress, optAsyncDecompressQueue };
    Cookfs_Pages *pages;
    int idx;
    int oReadOnly = 0;
    int oCompression;
    int tobjc = objc;
    int oCachesize = -1;
    int useFoffset = 0;
    int alwaysCompress = 0;
    int asyncDecompressQueueSize = 2;
    Tcl_WideInt foffset = 0;
    Tcl_Obj **tobjv = (Tcl_Obj **) objv;
    Tcl_Obj *compressCmd = NULL;
    Tcl_Obj *asyncCompressCmd = NULL;
    Tcl_Obj *asyncDecompressCmd = NULL;
    Tcl_Obj *decompressCmd = NULL;
    Tcl_Obj *compression = NULL;

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
                compression = tobjv[1];
                break;
            }
            case optCompressCommand: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                compressCmd = tobjv[1];
                break;
            }
            case optDecompressCommand: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                decompressCmd = tobjv[1];
                break;
            }
            case optAsyncCompressCommand: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                asyncCompressCmd = tobjv[1];
                break;
            }
            case optAsyncDecompressCommand: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                asyncDecompressCmd = tobjv[1];
                break;
            }
            case optEndoffset: {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                useFoffset = 1;
                if (Tcl_GetWideIntFromObj(interp, tobjv[1], &foffset) != TCL_OK) {
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
            case optAlwaysCompress:
                alwaysCompress = 1;
                break;
            case optAsyncDecompressQueue:
            {
                if (tobjc < 2) {
                    goto ERROR;
                }
                tobjc--;
                tobjv++;

                if (Tcl_GetIntFromObj(interp, tobjv[1], &asyncDecompressQueueSize) != TCL_OK) {
                    return TCL_ERROR;
                }
                break;
            }
            default:
                goto ERROR;
        }
        tobjc--;
        tobjv++;
    }

    if (Cookfs_CompressionFromObj(interp, compression, &oCompression) != TCL_OK) {
        return TCL_ERROR;
    }

    if (tobjc != 2) {
        goto ERROR;
    }

    /* Create cookfs instance */
    pages = Cookfs_PagesInit(interp, tobjv[1], oReadOnly, oCompression, NULL, useFoffset, foffset, 0, asyncDecompressQueueSize, compressCmd, decompressCmd, asyncCompressCmd, asyncDecompressCmd);

    if (pages == NULL) {
        return TCL_ERROR;
    }

    /* set whether compression should always be enabled */
    Cookfs_PagesSetAlwaysCompress(pages, alwaysCompress);

    /* set up cache size */
    Cookfs_PagesSetCacheSize(pages, oCachesize);
    CookfsLog(printf("Cookfs Page Cmd: %p -> %d\n", (void *)pages, pages->cacheSize))

    /* create Tcl command and return its name and set interp result to the command name */
    CookfsLog(printf("Create Tcl command for the pages object..."))
    Tcl_SetObjResult(interp, CookfsGetPagesObjectCmd(interp, pages));
    return TCL_OK;

ERROR:
    Tcl_WrongNumArgs(interp, 1, objv, "?-readonly|-readwrite? ?-compression mode? ?-cachesize numPages? ?-endoffset numBytes? ?-compresscommand tclCmd? ?-decompresscommand tclcmd? fileName");
    return TCL_ERROR;
}

/* command for creating new objects that deal with pages */

/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesCmd --
 *
 *	TODO: split into subcommands as functions
 *
 *	Handles subcommands for pages instance command
 *
 * Results:
 *	See subcommand comments for details
 *
 * Side effects:
 *	See subcommand comments for details
 *
 *----------------------------------------------------------------------
 */

static int CookfsPagesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static char *commands[] = {
        "add", "aside", "get", "gethead", "getheadmd5", "gettail",
        "gettailmd5", "hash", "index", "length", "dataoffset",
        "close", "delete", "cachesize", "filesize", "compression",
        "getcache", "ticktock",
        NULL
    };
    enum {
        cmdAdd = 0, cmdAside, cmdGet, cmdGetHead, cmdGetHeadMD5, cmdGetTail,
        cmdGetTailMD5, cmdHash, cmdIndex, cmdLength, cmdDataoffset,
        cmdClose, cmdDelete, cmdCachesize, cmdFilesize, cmdCompression,
        cmdGetCache, cmdTickTock
    };
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
            if (idx < 0) {
                Tcl_Obj *err = Cookfs_PagesGetLastError(p);
                if (err == NULL) {
                    err = Tcl_NewStringObj("Unable to add page", -1);
                }
                Tcl_SetObjResult(interp, err);
                return TCL_ERROR;
            }

            Tcl_SetObjResult(interp, Tcl_NewIntObj(idx));
            break;
        }
        case cmdGet:
        {
            int weight = 0;
            Tcl_Obj *rc;
            if (objc != 3 && objc != 5) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-weight weight? index");
                return TCL_ERROR;
            }
            if (objc > 3) {
                if (strcmp(Tcl_GetString(objv[2]), "-weight") != 0) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option"
                        " \"%s\" has been specified where -weight is expected",
                        Tcl_GetString(objv[2])));
                    return TCL_ERROR;
                }
                if (Tcl_GetIntFromObj(interp, objv[3], &weight) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            if (Tcl_GetIntFromObj(interp, objv[objc-1], &idx) != TCL_OK) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGet(p, idx, weight);
            CookfsLog(printf("cmdGet [%s]", rc == NULL ? "NULL" : "SET"))
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve chunk", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
                // Cookfs_PageGet always returns a page with refcount=1. We need
                // to decrease refcount now.
                Tcl_DecrRefCount(rc);
            }
            break;
        }
        case cmdGetHead:
        {
            Tcl_Obj *rc;
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }

            rc = Cookfs_PageGetHead(p);
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve head data", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
            }
            break;
        }
        case cmdGetHeadMD5:
        {
            Tcl_Obj *rc;
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }

            rc = Cookfs_PageGetHeadMD5(p);
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve head MD5", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
            }
            break;
        }
        case cmdGetTail:
        {
            Tcl_Obj *rc;
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }

            rc = Cookfs_PageGetTail(p);
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve tail data", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
            }
            break;
        }
        case cmdGetTailMD5:
        {
            Tcl_Obj *rc;
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }

            rc = Cookfs_PageGetTailMD5(p);
            if (rc == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to retrieve tail MD5", -1));
                return TCL_ERROR;
            }  else  {
                Tcl_SetObjResult(interp, rc);
            }
            break;
        }
        case cmdHash:
        {
            return CookfsPagesCmdHash(p, interp, objc, objv);
        }
        case cmdIndex:
        {
            if (objc == 3) {
                Cookfs_PagesSetIndex(p, objv[2]);
            }
            Tcl_SetObjResult(interp, Cookfs_PagesGetIndex(p));
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
        case cmdClose:
        {
            Tcl_WideInt offset;
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            offset = Cookfs_PagesClose(p);
            Tcl_SetObjResult(interp, Tcl_NewWideIntObj(offset));
            break;
        }
        case cmdDataoffset:
        {
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?index?");
                return TCL_ERROR;
            }
	    if (objc == 3) {
                if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK) {
                    return TCL_ERROR;
                }
		if (idx > p->dataNumPages) {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid page index", -1));
		    return TCL_ERROR;
		}
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(Cookfs_PagesGetPageOffset(p, idx)));
	    }  else  {
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(p->dataInitialOffset));
	    }
            break;
        }
        case cmdAside:
        {
            return CookfsPagesCmdAside(p, interp, objc, objv);
        }
        case cmdCachesize:
        {
            int csize = -1;
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?cachesize?");
                return TCL_ERROR;
            }
            if (objc == 3) {
                if (Tcl_GetIntFromObj(interp, objv[2], &csize) != TCL_OK) {
                    return TCL_ERROR;
                }
                Cookfs_PagesSetCacheSize(p, csize);
            }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(p->cacheSize));
            break;
        }
        case cmdFilesize:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, Tcl_NewWideIntObj(Cookfs_GetFilesize(p)));
            break;
        }
        case cmdCompression:
        {
            return CookfsPagesCmdCompression(p, interp, objc, objv);
        }
        case cmdGetCache:
        {
            if (objc < 2 || objc > 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "?index?");
                return TCL_ERROR;
            }
            Tcl_Obj *rc;
            if (objc == 3) {
                int index;
                if (Tcl_GetIntFromObj(interp, objv[2], &index) != TCL_OK) {
                    return TCL_ERROR;
                }
                int isCached = Cookfs_PagesIsCached(p, index);
                rc = Tcl_NewBooleanObj(isCached);
            } else {
                rc = Tcl_NewListObj(0, NULL);
                for (int i = 0; i < p->cacheSize; i++) {
                    if (p->cache[i].pageObj == NULL) {
                        continue;
                    }
                    Tcl_Obj *rec = Tcl_NewDictObj();
                    Tcl_DictObjPut(interp, rec, Tcl_NewStringObj("index", -1),
                        Tcl_NewIntObj(p->cache[i].pageIdx));
                    Tcl_DictObjPut(interp, rec, Tcl_NewStringObj("weight", -1),
                        Tcl_NewIntObj(p->cache[i].weight));
                    Tcl_DictObjPut(interp, rec, Tcl_NewStringObj("age", -1),
                        Tcl_NewIntObj(p->cache[i].age));
                    Tcl_ListObjAppendElement(interp, rc, rec);
                }
            }
            Tcl_SetObjResult(interp, rc);
            break;
        }
        case cmdTickTock:
        {
            int maxAge;
            if (objc < 2 || objc > 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "?maxAge?");
                return TCL_ERROR;
            }
            if (objc == 3) {
                if (Tcl_GetIntFromObj(interp, objv[2], &maxAge) != TCL_OK) {
                    return TCL_ERROR;
                }
                maxAge = Cookfs_PagesSetMaxAge(p, maxAge);
            } else {
                maxAge = Cookfs_PagesTickTock(p);
            }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(maxAge));
            break;
        }
    }
    return TCL_OK;
}

int CookfsPagesCmdAside(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "fileName");
        return TCL_ERROR;
    }

    Cookfs_Pages *asidePages;
    Tcl_Size fileNameSize;

    Tcl_GetStringFromObj(objv[2], &fileNameSize);

    if (fileNameSize > 0) {

        /* TODO: copy compression objects from original pages object in aside
           operations */
        asidePages = Cookfs_PagesInit(pages->interp, objv[2], 0,
            pages->fileCompression, NULL, 0, 0, 1, 0, NULL, NULL, NULL, NULL);

        if (asidePages == NULL) {
            CookfsLog(printf("Failed to create add-aside pages object"))
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create Cookfs object", -1));
            return TCL_ERROR;
        }  else  {
            CookfsLog(printf("Created add-aside pages object"))
        }

    }  else  {
        CookfsLog(printf("Removing aside page connection"))
        asidePages = NULL;
    }

    Cookfs_PagesSetAside(pages, asidePages);

    return TCL_OK;

}

int CookfsPagesCmdCompression(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

    if (objc > 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "?type?");
        return TCL_ERROR;
    }

    int oCompression;

    if (objc == 2) {
        oCompression = Cookfs_PagesGetCompression(pages);
    } else {
        if (Tcl_GetIndexFromObj(interp, objv[2], (const char **) cookfsCompressionOptions,
            "compression", 0, &oCompression) != TCL_OK)
        {
            return TCL_ERROR;
        }
        /* map compression from cookfsCompressionOptionMap */
        oCompression = cookfsCompressionOptionMap[oCompression];
        Cookfs_PagesSetCompression(pages, oCompression);
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(cookfsCompressionNames[oCompression], -1));

    return TCL_OK;

}

/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesCmdHash --
 *
 *	Set hash function used for storing cookfs pages
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	On success hash method is set
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsPagesCmdHash(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "?hash?");
        return TCL_ERROR;
    }
    if (objc == 3) {
        if (Cookfs_PagesSetHashByObj(pages, objv[2], interp) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    Tcl_SetObjResult(interp, Cookfs_PagesGetHashAsObj(pages));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesDeleteProc --
 *
 *	Deletes instance of pages when Tcl command is deleted
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsPagesDeleteProc(ClientData clientData) {
    Cookfs_Pages *pages = (Cookfs_Pages *) clientData;
    pages->commandToken = NULL;
    if (pages->isDead) {
        return;
    }
    CookfsLog(printf("DELETING PAGES COMMAND"))
    Cookfs_PagesFini(pages);
}
