/*
 * pagesCompre.c
 *
 * Provides Tcl commands for managing pages
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesCmd.h"

/* definitions of static and/or internal functions */
static int CookfsPagesCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsPagesCmdHash(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsPagesDeleteProc(ClientData clientData);
static int CookfsRegisterPagesObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsRegisterExistingPagesObjectCmd(Tcl_Interp *interp, void *p);

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

Tcl_Obj *CookfsGetPagesObjectCmd(Tcl_Interp *interp, void *p) {
    if (p == NULL) {
        CookfsLog(printf("CookfsGetPagesObjectCmd: return NULL"));
        return NULL;
    }
    Cookfs_Pages *pages = (Cookfs_Pages *)p;
    CookfsLog(printf("CookfsGetPagesObjectCmd: enter interp:%p my interp:%p",
        (void *)interp, (void *)pages->interp));
    CookfsRegisterExistingPagesObjectCmd(pages->interp, pages);
    Tcl_Obj *rc = Tcl_NewObj();
    Tcl_GetCommandFullName(pages->interp, pages->commandToken, rc);
    if (interp == pages->interp) {
        goto done;
    }
    const char *cmd = Tcl_GetString(rc);
    if (Tcl_GetAliasObj(interp, cmd, NULL, NULL, NULL, NULL) == TCL_OK) {
        CookfsLog(printf("CookfsGetPagesObjectCmd: alias already exists"));
        goto done;
    }
    CookfsLog(printf("CookfsGetPagesObjectCmd: create interp alias"));
    Tcl_CreateAlias(interp, cmd, pages->interp, cmd, 0, NULL);
done:
    CookfsLog(printf("CookfsGetPagesObjectCmd: return [%s]",
        Tcl_GetString(rc)));
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

static void CookfsRegisterExistingPagesObjectCmd(Tcl_Interp *interp, void *p) {
    Cookfs_Pages *pages = (Cookfs_Pages *)p;
    if (pages->commandToken != NULL) {
        return;
    }
    char buf[128];
    /* create Tcl command and return its name */
    sprintf(buf, "::cookfs::c::pages::handle%p", (void *)p);
    pages->commandToken = Tcl_CreateObjCommand(interp, buf, CookfsPagesCmd,
        (ClientData)pages, CookfsPagesDeleteProc);
    pages->interp = interp;
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
    Tcl_Obj *err = NULL;
    pages = Cookfs_PagesInit(interp, tobjv[1], oReadOnly, oCompression, NULL,
        useFoffset, foffset, 0, asyncDecompressQueueSize, compressCmd,
        decompressCmd, asyncCompressCmd, asyncDecompressCmd, &err);
    if (err != NULL) {
        Tcl_SetObjResult(interp, err);
    }
    if (pages == NULL) {
        return TCL_ERROR;
    }

    Cookfs_PagesLockWrite(pages, NULL);

    /* set whether compression should always be enabled */
    Cookfs_PagesSetAlwaysCompress(pages, alwaysCompress);

    /* set up cache size */
    Cookfs_PagesSetCacheSize(pages, oCachesize);
    CookfsLog(printf("Cookfs Page Cmd: %p -> %d\n", (void *)pages, pages->cacheSize))

    Cookfs_PagesUnlock(pages);

    /* create Tcl command and return its name and set interp result to the command name */
    CookfsLog(printf("Create Tcl command for the pages object..."))
    Tcl_SetObjResult(interp, CookfsGetPagesObjectCmd(interp, pages));
    return TCL_OK;

ERROR:
    Tcl_WrongNumArgs(interp, 1, objv, "?-readonly|-readwrite? ?-compression mode? ?-cachesize numPages? ?-endoffset numBytes? ?-compresscommand tclCmd? ?-decompresscommand tclcmd? fileName");
    return TCL_ERROR;
}

static int CookfsPagesCmdAside(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsPagesCmdCompression(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);


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
    Tcl_Obj *err = NULL;
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
            Tcl_Size size;
            unsigned char *bytes = Tcl_GetByteArrayFromObj(objv[2], &size);
            if (!Cookfs_PagesLockWrite(p, NULL)) {
                return TCL_ERROR;
            }
            idx = Cookfs_PageAddRaw(p, bytes, size, &err);
            Cookfs_PagesUnlock(p);
            if (idx < 0) {
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
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            Cookfs_PageObj data = Cookfs_PageGet(p, idx, weight, &err);
            Cookfs_PagesUnlock(p);
            // Do not increate refcount for data as Cookfs_PageGet() returns
            // pages with refcount=1.
            CookfsLog(printf("cmdGet data [%s]", data == NULL ? "NULL" : "SET"))
            if (data != NULL) {
                rc = Cookfs_PageObjCopyAsByteArray(data);
                Cookfs_PageObjDecrRefCount(data);
            } else {
                rc = NULL;
            }
            // Check rc here again as conversion PageObj->ByteArray may fail
            // due to OOM.
            CookfsLog(printf("cmdGet [%s]", rc == NULL ? "NULL" : "SET"))
            if (rc == NULL) {
                if (err == NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to"
                        " retrieve chunk", -1));
                } else {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to"
                        " retrieve chunk: %s", Tcl_GetString(err)));
                    Tcl_IncrRefCount(err);
                    Tcl_DecrRefCount(err);
                }
                return TCL_ERROR;
            } else {
                Tcl_SetObjResult(interp, rc);
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

            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGetHead(p);
            Cookfs_PagesUnlock(p);
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

            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGetHeadMD5(p);
            Cookfs_PagesUnlock(p);
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

            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGetTail(p);
            Cookfs_PagesUnlock(p);
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

            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            rc = Cookfs_PageGetTailMD5(p);
            Cookfs_PagesUnlock(p);
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
            Cookfs_PageObj data;
            if (objc == 3) {
                data = Cookfs_PageObjNewFromByteArray(objv[2]);
                Cookfs_PageObjIncrRefCount(data);
                if (!Cookfs_PagesLockWrite(p, NULL)) {
                    return TCL_ERROR;
                }
                Cookfs_PagesSetIndex(p, data);
                Cookfs_PagesUnlock(p);
                Cookfs_PageObjDecrRefCount(data);
            }
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            data = Cookfs_PagesGetIndex(p);
            Cookfs_PagesUnlock(p);
            if (data == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewObj());
            } else {
                Cookfs_PageObjIncrRefCount(data);
                Tcl_Obj *rc = Cookfs_PageObjCopyAsByteArray(data);
                Cookfs_PageObjDecrRefCount(data);
                if (rc == NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to"
                        " convert from PageObj", -1));
                } else {
                    Tcl_SetObjResult(interp, rc);
                }
            }
            break;
        }
        case cmdLength:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            int len = Cookfs_PagesGetLength(p);
            Cookfs_PagesUnlock(p);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(len));
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
            if (!Cookfs_PagesLockWrite(p, NULL)) {
                return TCL_ERROR;
            }
            offset = Cookfs_PagesClose(p);
            Cookfs_PagesUnlock(p);
            Tcl_SetObjResult(interp, Tcl_NewWideIntObj(offset));
            break;
        }
        case cmdDataoffset:
        {
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?index?");
                return TCL_ERROR;
            }
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
	    if (objc == 3) {
                if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK) {
                    Cookfs_PagesUnlock(p);
                    return TCL_ERROR;
                }
		if (idx > Cookfs_PagesGetLength(p)) {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid page index", -1));
		    Cookfs_PagesUnlock(p);
		    return TCL_ERROR;
		}
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(Cookfs_PagesGetPageOffset(p, idx)));
	    }  else  {
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(p->dataInitialOffset));
	    }
	    Cookfs_PagesUnlock(p);
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
                if (!Cookfs_PagesLockWrite(p, NULL)) {
                    return TCL_ERROR;
                }
                Cookfs_PagesSetCacheSize(p, csize);
                Cookfs_PagesUnlock(p);
            }
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            csize = p->cacheSize;
            Cookfs_PagesUnlock(p);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(csize));
            break;
        }
        case cmdFilesize:
        {
            if (objc != 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            Tcl_WideInt rc = Cookfs_GetFilesize(p);
            Cookfs_PagesUnlock(p);
            Tcl_SetObjResult(interp, Tcl_NewWideIntObj(rc));
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
            if (!Cookfs_PagesLockRead(p, NULL)) {
                return TCL_ERROR;
            }
            if (objc == 3) {
                int index;
                if (Tcl_GetIntFromObj(interp, objv[2], &index) != TCL_OK) {
                    Cookfs_PagesUnlock(p);
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
            Cookfs_PagesUnlock(p);
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
            if (!Cookfs_PagesLockWrite(p, NULL)) {
                return TCL_ERROR;
            }
            if (objc == 3) {
                if (Tcl_GetIntFromObj(interp, objv[2], &maxAge) != TCL_OK) {
                    Cookfs_PagesUnlock(p);
                    return TCL_ERROR;
                }
                maxAge = Cookfs_PagesSetMaxAge(p, maxAge);
            } else {
                maxAge = Cookfs_PagesTickTock(p);
            }
            Cookfs_PagesUnlock(p);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(maxAge));
            break;
        }
    }
    return TCL_OK;
}

static int CookfsPagesCmdAside(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

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
        /* TODO: pass pointer to err object instead of NULL and produce
           the corresponding error message below if Cookfs_PagesInit()
           failed. */
        asidePages = Cookfs_PagesInit(pages->interp, objv[2], 0,
            pages->fileCompression, NULL, 0, 0, 1, 0, NULL, NULL, NULL, NULL,
            NULL);

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

    if (!Cookfs_PagesLockWrite(pages, NULL)) {
        return TCL_ERROR;
    }
    Cookfs_PagesSetAside(pages, asidePages);
    Cookfs_PagesUnlock(pages);

    return TCL_OK;

}

static int CookfsPagesCmdCompression(Cookfs_Pages *pages, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

    if (objc > 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "?type?");
        return TCL_ERROR;
    }

    int oCompression;

    if (objc == 2) {
        if (!Cookfs_PagesLockRead(pages, NULL)) {
            return TCL_ERROR;
        }
        oCompression = Cookfs_PagesGetCompression(pages);
    } else {
        if (Tcl_GetIndexFromObj(interp, objv[2], (const char **) cookfsCompressionOptions,
            "compression", 0, &oCompression) != TCL_OK)
        {
            return TCL_ERROR;
        }
        /* map compression from cookfsCompressionOptionMap */
        oCompression = cookfsCompressionOptionMap[oCompression];
        if (!Cookfs_PagesLockWrite(pages, NULL)) {
            return TCL_ERROR;
        }
        Cookfs_PagesSetCompression(pages, oCompression);
    }
    Cookfs_PagesUnlock(pages);

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
        if (!Cookfs_PagesLockWrite(pages, NULL)) {
            return TCL_ERROR;
        }
        int ret = Cookfs_PagesSetHashByObj(pages, objv[2], interp);
        Cookfs_PagesUnlock(pages);
        if (ret != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (!Cookfs_PagesLockRead(pages, NULL)) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Cookfs_PagesGetHashAsObj(pages));
    Cookfs_PagesUnlock(pages);
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

int Cookfs_PagesCmdForward(Cookfs_PagesForwardCmd cmd, void *p,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    switch (cmd) {
    case COOKFS_PAGES_FORWARD_COMMAND_ASIDE:
        return CookfsPagesCmdAside(p, interp, objc, objv);
    case COOKFS_PAGES_FORWARD_COMMAND_COMPRESSION:
        return CookfsPagesCmdCompression(p, interp, objc, objv);
    }
    return TCL_ERROR;
}

