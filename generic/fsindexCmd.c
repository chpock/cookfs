/*
 * fsindexCmd.c
 *
 * Provides Tcl commands for managing filesystem indexes
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

/* declarations of static and/or internal functions */
static int CookfsRegisterFsindexObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsFsindexDeleteProc(ClientData clientData);
static int CookfsFsindexCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdExport(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdGetmtime(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdSetmtime(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdSet(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdUnset(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdGet(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdList(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdDelete(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdUnsetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdGetBlockUsage(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdChangeCount(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int CookfsFsindexCmdImport(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

// These functions are in header
//static int CookfsFsindexCmdSetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
//static int CookfsFsindexCmdGetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_InitFsindexCmd --
 *
 *	Initializes filesystem index component for specified
 *	Tcl interpreter
 *
 * Results:
 *	TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	Creates command for creating fsindex instances
 *
 *----------------------------------------------------------------------
 */

int Cookfs_InitFsindexCmd(Tcl_Interp *interp) {
    Tcl_CreateNamespace(interp, "::cookfs::c::fsindex", NULL, NULL);
    Tcl_CreateObjCommand(interp, "::cookfs::c::fsindex", CookfsRegisterFsindexObjectCmd,
        (ClientData) NULL, NULL);
    Tcl_CreateAlias(interp, "::cookfs::fsindex", interp, "::cookfs::c::fsindex", 0, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsGetFsindexObjectCmd --
 *
 *      Returns a Tcl command for existing fsindex object
 *
 * Results:
 *      Tcl_Obj with zero refcount that contains Tcl command for
 *      the fsindex object or NULL on failure.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *CookfsGetFsindexObjectCmd(Tcl_Interp *interp, Cookfs_Fsindex *i) {
    if (i == NULL) {
        return NULL;
    }
    CookfsRegisterExistingFsindexObjectCmd(interp, i);
    Tcl_Obj *rc = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, i->commandToken, rc);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsRegisterExistingFsindexObjectCmd --
 *
 *      Creates a Tcl command for existing fsindex object
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

void CookfsRegisterExistingFsindexObjectCmd(Tcl_Interp *interp, Cookfs_Fsindex *i) {
    if (i->commandToken != NULL) {
        return;
    }
    char buf[128];
    /* create Tcl command and return its name */
    sprintf(buf, "::cookfs::c::fsindex::handle%p", (void *)i);
    i->commandToken = Tcl_CreateObjCommand(interp, buf, CookfsFsindexCmd,
        (ClientData) i, CookfsFsindexDeleteProc);
    i->interp = interp;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
}


/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsRegisterFsindexObjectCmd --
 *
 *	Creates a new fsindex instance and Tcl command for managing
 *	this filesystem index object
 *
 *	New fsindex is created if no additional arguments are specified
 *
 *	Importing existing fsindex is done by running the command
 *	with binary data from [$fsindex export] as first argument
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

/* command for creating new objects that deal with pages */
static int CookfsRegisterFsindexObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Cookfs_Fsindex *i;

    /* we only accept no or 1 argument - if more specified, return usage information */
    if (objc > 2) {
        goto ERROR;
    }

    /* import fsindex from specified data if specified, otherwise create new fsindex */
    if (objc == 2) {
        i = Cookfs_FsindexFromObject(NULL, objv[1]);
        CookfsLog(printf("CookfsRegisterFsindexObjectCmd: created fsindex from obj [%p]", i));
    } else {
        i = Cookfs_FsindexInit(NULL);
        CookfsLog(printf("CookfsRegisterFsindexObjectCmd: created fsindex from scratch [%p]", i));
    }

    /* throw error if import or creation failed */
    if (i == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create index object", -1));
        return TCL_ERROR;
    }

    /* create Tcl command and return its name and set interp result to the command name */
    CookfsLog(printf("Create Tcl command for the fsindex object..."))
    CookfsRegisterExistingFsindexObjectCmd(interp, i);
    return TCL_OK;

ERROR:
    Tcl_WrongNumArgs(interp, 1, objv, "?binaryData?");
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexDeleteProc --
 *
 *	Deletes instance of fsindex when Tcl command is deleted
 *
 * Results:
 *	None
 *
 * Side effects:
 *	All fsindex information is freed
 *
 *----------------------------------------------------------------------
 */

static void CookfsFsindexDeleteProc(ClientData clientData) {
    Cookfs_Fsindex *fsindex = (Cookfs_Fsindex *) clientData;
    fsindex->commandToken = NULL;
    if (fsindex->isDead) {
        return;
    }
    CookfsLog(printf("DELETING FSINDEX COMMAND"))
    Cookfs_FsindexFini(fsindex);
    CookfsLog(printf("DELETED FSINDEX COMMAND"))
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmd --
 *
 *	Handles subcommands for fsindex instance command
 *
 * Results:
 *	See subcommand comments for details
 *
 * Side effects:
 *	See subcommand comments for details
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    static char *commands[] = { "export", "list", "get", "getmtime", "set", "setmtime", "unset", "delete", "setmetadata", "getmetadata", "unsetmetadata", "getblockusage", "changecount", "import", NULL };
    enum { cmdExport, cmdList, cmdGet, cmdGetmtime, cmdSet, cmdSetmtime, cmdUnset, cmdDelete, cmdSetMetadata, cmdGetMetadata, cmdUnsetMetadata, cmdGetBlockUsage, cmdChangeCount, cmdImport };
    int idx;

    Cookfs_Fsindex *fsIndex = (Cookfs_Fsindex *) clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], (const char **) commands, "command", 0, &idx) != TCL_OK) {
        return TCL_ERROR;
    }

    /* run proper subcommand */
    switch (idx) {
        case cmdExport:
	    return CookfsFsindexCmdExport(fsIndex, interp, objc, objv);
        case cmdGetmtime:
	    return CookfsFsindexCmdGetmtime(fsIndex, interp, objc, objv);
        case cmdSetmtime:
	    return CookfsFsindexCmdSetmtime(fsIndex, interp, objc, objv);
        case cmdSet:
	    return CookfsFsindexCmdSet(fsIndex, interp, objc, objv);
        case cmdUnset:
	    return CookfsFsindexCmdUnset(fsIndex, interp, objc, objv);
        case cmdGet:
	     return CookfsFsindexCmdGet(fsIndex, interp, objc, objv);
        case cmdList:
	     return CookfsFsindexCmdList(fsIndex, interp, objc, objv);
        case cmdDelete:
	    return CookfsFsindexCmdDelete(fsIndex, interp, objc, objv);
        case cmdSetMetadata:
	    return CookfsFsindexCmdSetMetadata(fsIndex, interp, objc, objv);
        case cmdUnsetMetadata:
	    return CookfsFsindexCmdUnsetMetadata(fsIndex, interp, objc, objv);
        case cmdGetMetadata:
	     return CookfsFsindexCmdGetMetadata(fsIndex, interp, objc, objv);
        case cmdGetBlockUsage:
	     return CookfsFsindexCmdGetBlockUsage(fsIndex, interp, objc, objv);
	case cmdChangeCount:
	     return CookfsFsindexCmdChangeCount(fsIndex, interp, objc, objv);
	case cmdImport:
	     return CookfsFsindexCmdImport(fsIndex, interp, objc, objv);
        default:
            return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdChangeCount --
 *
 *      Returns the current value for the change counter.
 *
 * Results:
 *      Returns TCL_OK
 *      Interp result is set to the change counter.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdChangeCount(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    /* check arguments */
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }

    Tcl_WideInt rc = Cookfs_FsindexIncrChangeCount(fsIndex, 0);
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(rc));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdGetBlockUsage --
 *
 *      Returns the number of files in the specified block.
 *
 * Results:
 *      Returns TCL_OK on success; TCL_ERROR on error
 *      On success interp result is set to the number of files.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdGetBlockUsage(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int num;
    int idx;

    /* check arguments */
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "block");
        return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("could not get integer from block arg", -1));
        return TCL_ERROR;
    }

    num = Cookfs_FsindexGetBlockUsage(fsIndex, idx);

    Tcl_SetObjResult(interp, Tcl_NewIntObj(num));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdExport --
 *
 *	Exports fsindex internal storage to binary, platform-independant
 *	format.
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	On success interp result is set to binary object containing
 *	exported data
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdExport(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *exportObj;

    /* check arguments */
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }

    /* export to Tcl_Obj */
    exportObj = Cookfs_FsindexToObject(fsIndex);

    if (exportObj == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to export fsIndex", -1));
	return TCL_OK;
    }  else  {
	Tcl_SetObjResult(interp, exportObj);
	return TCL_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdImport --
 *
 *      Import fsindex from binary platform-independant format to
 *      internal storage.
 *
 * Results:
 *      Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdImport(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    /* check arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "data");
	return TCL_ERROR;
    }

    Cookfs_Fsindex *result = Cookfs_FsindexFromObject(fsIndex, objv[2]);

    return (result == NULL ? TCL_ERROR : TCL_OK);
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdGetmtime --
 *
 *	Gets modification time for specified file
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	On success interp result contains mtime for specified file
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdGetmtime(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Cookfs_FsindexEntry *entry;

    /* check arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    /* get entry and free splitPath */
    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);

    /* check if entry was returned */
    if (entry == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    /* return mtime as wide integer */
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(entry->fileTime));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdSetmtime --
 *
 *	Sets modification time for specified file
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdSetmtime(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Cookfs_FsindexEntry *entry;
    Tcl_WideInt fileTime;

    /* check arguments */
    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "path mtime");
	return TCL_ERROR;
    }

    /* get new fileTime, return error if not a valid wide integer */
    if (Tcl_GetWideIntFromObj(interp, objv[3], &fileTime) != TCL_OK) {
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    /* get entry and free splitPath */
    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);

    /* check if entry was returned */
    if (entry == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    /* update mtime */
    entry->fileTime = fileTime;
    Cookfs_FsindexIncrChangeCount(fsIndex, 1);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdSet --
 *
 *	Creates or modifies entry information in an fsindex.
 *
 *	For new entries, they are created; parent item must exist;
 *	for existing ones, they are modified if allowed
 *	(only files can be re-created for now)
 *
 *	If run with 2 arguments, new item is created as file and
 *	second argument specifies list of blocks as
 *	block-offset-size triplets
 *
 *	If run with 1 argument, new item is created as directory
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	Modifies fsindex hierarchy
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdSet(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int i;
    int numBlocks;
    int fileBlockData;
    Tcl_Obj *splitPath;
    Tcl_Obj **listElements;
    Tcl_WideInt fileTime;
    Cookfs_FsindexEntry *entry;

    /* check number of arguments */
    if ((objc < 4) || (objc > 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "path mtime ?filedata?");
	return TCL_ERROR;
    }

    /* first get mtime for file */
    if (Tcl_GetWideIntFromObj(interp, objv[3], &fileTime) != TCL_OK) {
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    if (objc == 4) {
	/* create new directory if no list of blocks has been specified */
	entry = Cookfs_FsindexSet(fsIndex, splitPath, COOKFS_NUMBLOCKS_DIRECTORY);
	if (entry == NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create entry", -1));
	    Tcl_DecrRefCount(splitPath);
	    return TCL_ERROR;
	}
    }  else  {
	/* otherwise try to create a file - first get list of blocks */
	if (Tcl_ListObjGetElements(interp, objv[4], &numBlocks, &listElements) != TCL_OK) {
	    Tcl_DecrRefCount(splitPath);
	    return TCL_ERROR;
	}

	numBlocks /= 3;

	/* create new fsindex entry, return error if it fails (i.e. due to failed constraints) */
	entry = Cookfs_FsindexSet(fsIndex, splitPath, numBlocks);
	if (entry == NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create entry", -1));
	    Tcl_DecrRefCount(splitPath);
	    return TCL_ERROR;
	}
	entry->data.fileInfo.fileSize = 0;

	/* copy all integers from filedata into newly created entry */
	for (i = 0; i < numBlocks * 3; i++) {
	    if (Tcl_GetIntFromObj(interp, listElements[i], &fileBlockData) != TCL_OK) {
		/* if getting integer failed, remove partial item */
		Cookfs_FsindexUnset(fsIndex, splitPath);
		Tcl_DecrRefCount(splitPath);
		CookfsLog(printf("Getting from list failed"))
		return TCL_ERROR;
	    }
	    entry->data.fileInfo.fileBlockOffsetSize[i] = fileBlockData;
	    CookfsLog(printf("Dump %d -> %d", i, fileBlockData))
	}

	/* calculate file size by iterating over each block and adding its size */
	for (i = 0; i < numBlocks; i++) {
	    entry->data.fileInfo.fileSize += entry->data.fileInfo.fileBlockOffsetSize[i * 3 + 2];
	    Cookfs_FsindexModifyBlockUsage(fsIndex, entry->data.fileInfo.fileBlockOffsetSize[i * 3 + 0], 1);
	}
	entry->isFileBlocksInitialized = fsIndex;
	CookfsLog(printf("Size: %d", (int) entry->data.fileInfo.fileSize))
    }

    /* set mtime */
    entry->fileTime = fileTime;

    /* free splitPath and return */

    Tcl_DecrRefCount(splitPath);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdUnset --
 *
 *	Unsets specified entry in fsindex
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	Corresponding entry is removed
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdUnset(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    int result;

    /* check number of arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    /* unset using fsindex API */
    result = Cookfs_FsindexUnset(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);

    /* provide error message if operation failed */
    if (!result) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to unset item", -1));
	return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdGet --
 *
 *	Gets fsindex entry and returns it as a Tcl list
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	In case of success result is set with a list describing entry
 *
 *	For directories, result is a 1 element list containing file
 *	modification time
 *
 *	For files, it is a list consisting of file modification time,
 *	file size and block-offset-size triplet list
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdGet(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Tcl_Obj *resultObjects[3];
    Tcl_Obj **resultList;
    Cookfs_FsindexEntry *entry;

    /* check arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    /* get entry and free splitPath */
    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);

    /* check if entry was returned */
    if (entry == NULL) {
	CookfsLog(printf("cmdGet - entry==NULL"))
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    /* create file time in resulting list */
    resultObjects[0] = Tcl_NewWideIntObj(entry->fileTime);
    if (entry->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
	/* for directories, return a 1 element list only containing file modification time */
	Tcl_SetObjResult(interp, Tcl_NewListObj(1, resultObjects));
    }  else  {
	int i;

	/* for files, store file size and create a sublist with block-offset-size triplets */
	resultObjects[1] = Tcl_NewWideIntObj(entry->data.fileInfo.fileSize);
	resultList = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * (entry->fileBlocks * 3));
	for (i = 0; i < (entry->fileBlocks * 3); i++) {
	    resultList[i] = Tcl_NewIntObj(entry->data.fileInfo.fileBlockOffsetSize[i]);
	}
	/* create new list from newly created array, free temporary memory and return 3 element list */
	resultObjects[2] = Tcl_NewListObj(entry->fileBlocks * 3, resultList);
	Tcl_Free((void *) resultList);
	Tcl_SetObjResult(interp, Tcl_NewListObj(3, resultObjects));
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdList --
 *
 *	List entries in specified path
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	In case of success result is set with a list of names of
 *	all children
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdList(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Tcl_Obj **resultList;
    Cookfs_FsindexEntry **results;
    int itemCount, idx;

    /* check arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    /* get split path to entry */
    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    /* list specified path; return error if path not found */
    results = Cookfs_FsindexList(fsIndex, splitPath, &itemCount);
    Tcl_DecrRefCount(splitPath);

    if (results == NULL) {
	CookfsLog(printf("cmdList - results==NULL"))
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    /* create a file list from result of Cookfs_FsindexList() */
    resultList = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * itemCount);
    for (idx = 0; idx < itemCount; idx++) {
	resultList[idx] = Tcl_NewStringObj(results[idx]->fileName, -1);
    }

    /* the list is now copied, we can free the original memory */
    Cookfs_FsindexListFree(results);

    Tcl_SetObjResult(interp, Tcl_NewListObj(itemCount, resultList));
    Tcl_Free((void *) resultList);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdDelete --
 *
 *	Deletes fsindex object and all associated elements
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdDelete(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(fsIndex);
    /* check arguments */
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }
    Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], NULL));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdSetMetadata --
 *
 *	TODO
 *
 * Results:
 *	Returns TCL_OK
 *
 * Side effects:
 *	Modifies fsindex metadata
 *
 *----------------------------------------------------------------------
 */

int CookfsFsindexCmdSetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    const char *paramName;

    /* check number of arguments */
    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "parameter value");
	return TCL_ERROR;
    }

    paramName = Tcl_GetStringFromObj(objv[2], NULL);

    Cookfs_FsindexSetMetadata(fsIndex, paramName, objv[3]);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdUnsetMetadata --
 *
 *	TODO
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *
 * Side effects:
 *	Corresponding entry is removed
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdUnsetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    const char *paramName;

    /* check number of arguments */
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "parameter");
	return TCL_ERROR;
    }

    paramName = Tcl_GetStringFromObj(objv[2], NULL);

    if (!Cookfs_FsindexUnsetMetadata(fsIndex, paramName)) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Parameter not defined", -1));
	return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdGetMetadata --
 *
 *	TODO
 *
 * Results:
 *	Returns TCL_OK on success; TCL_ERROR on error
 *	TODO
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int CookfsFsindexCmdGetMetadata(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *value;
    const char *paramName;

    /* check arguments */
    if ((objc < 3) || (objc > 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "paramater ?defaultValue?");
	return TCL_ERROR;
    }

    paramName = Tcl_GetStringFromObj(objv[2], NULL);
    value = Cookfs_FsindexGetMetadata(fsIndex, paramName);

    if (value == NULL) {
	if (objc >= 4) {
	    value = objv[3];
	}  else  {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("Parameter not defined", -1));
	    return TCL_ERROR;
	}
    }

    Tcl_SetObjResult(interp, value);

    return TCL_OK;
}


