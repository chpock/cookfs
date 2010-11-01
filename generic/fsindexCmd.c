/*
 * fsindexCmd.c
 *
 * Provides Tcl commands for managing filesystem indexes
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"

/* counter for all fsindex objects */
static int deprecatedCounter = 0;

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



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_InitFsindexCmd --
 *
 *	Initializes filesystem index component for specified
 *	Tcl interpreter
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Creates command for creating fsindex instances
 *
 *----------------------------------------------------------------------
 */

int Cookfs_InitFsindexCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::fsindex", CookfsRegisterFsindexObjectCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
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
 *	TCL_OK on success; TCL_ERROR otherwise; result for interp
 *	is set with proper error message in case of failure
 *
 * Side effects:
 *	New Tcl command is created on success
 *
 *----------------------------------------------------------------------
 */

/* command for creating new objects that deal with pages */
static int CookfsRegisterFsindexObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    char buf[64];
    int idx;
    Cookfs_Fsindex *i;

    idx = ++deprecatedCounter;
    sprintf(buf, "::cookfs::fsindexhandle%d", idx);

    /* we only accept no or 1 argument - if more specified, return usage information */
    if (objc > 2) {
        goto ERROR;
    }

    /* import fsindex from specified data if specified, otherwise create new fsindex */
    if (objc == 2) {
        i = Cookfs_FsindexFromObject(objv[1]);
    }  else  {
        i = Cookfs_FsindexInit();
    }

    /* throw error if import or creation failed */
    if (i == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create index object", -1));
        return TCL_ERROR;
    }

    /* create new Tcl command to manage newly created fsindex and return its name */
    Tcl_CreateObjCommand(interp, buf, CookfsFsindexCmd, (ClientData) i, CookfsFsindexDeleteProc);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));   
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
    CookfsLog(printf("DELETING FSINDEX COMMAND"))
    Cookfs_FsindexFini((Cookfs_Fsindex *) clientData);
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
    static char *commands[] = { "export", "list", "get", "getmtime", "set", "setmtime", "unset", "delete", NULL };
    enum { cmdExport, cmdList, cmdGet, cmdGetmtime, cmdSet, cmdSetmtime, cmdUnset, cmdDelete };
    int idx;
    
    Cookfs_Fsindex *fsIndex = (Cookfs_Fsindex *) clientData;
    
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], (const char **) commands, "command", 0, &idx) != TCL_OK) {
        return TCL_ERROR;
    }

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
        default:
            return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexCmdExport --
 *
 *	TODO
 *
 * Results:
 *	Returns TCL_OK on error; TCL_ERROR on failure
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
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }

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
 * CookfsFsindexCmdGetmtime --
 *
 *	Gets modification time for specified file
 *
 * Results:
 *	Returns TCL_OK on error; TCL_ERROR on failure
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
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);
    
    if (entry == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

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
 *	Returns TCL_OK on error; TCL_ERROR on failure
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

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "path mtime");
	return TCL_ERROR;
    }

    if (Tcl_GetWideIntFromObj(interp, objv[3], &fileTime) != TCL_OK) {
	return TCL_ERROR;
    }

    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);
    
    if (entry == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    entry->fileTime = fileTime;

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
 *	Returns TCL_OK on error; TCL_ERROR on failure
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
    if ((objc < 4) || (objc > 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "path mtime ?filedata?");
	return TCL_ERROR;
    }

    /* first get mtime for file */
    if (Tcl_GetWideIntFromObj(interp, objv[3], &fileTime) != TCL_OK) {
	return TCL_ERROR;
    }

    /* try to get split path from specified path */
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
	}
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
 *	Returns TCL_OK on error; TCL_ERROR on failure
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
 *	TODO
 *
 * Results:
 *	TODO
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdGet(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Tcl_Obj *resultObjects[3];
    Tcl_Obj **resultList;
    Cookfs_FsindexEntry *entry;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    entry = Cookfs_FsindexGet(fsIndex, splitPath);
    Tcl_DecrRefCount(splitPath);
    
    if (entry == NULL) {
	CookfsLog(printf("cmdGet - entry==NULL"))
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    resultObjects[0] = Tcl_NewWideIntObj(entry->fileTime);
    if (entry->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
	Tcl_SetObjResult(interp, Tcl_NewListObj(1, resultObjects));
    }  else  {
	int i;
	resultObjects[1] = Tcl_NewWideIntObj(entry->data.fileInfo.fileSize);
	resultList = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * (entry->fileBlocks * 3));
	for (i = 0; i < (entry->fileBlocks * 3); i++) {
	    resultList[i] = Tcl_NewIntObj(entry->data.fileInfo.fileBlockOffsetSize[i]);
	}
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
 *	TODO
 *
 * Results:
 *	TODO
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdList(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *splitPath;
    Tcl_Obj **resultList;
    Cookfs_FsindexEntry **results;
    int itemCount, idx;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "path");
	return TCL_ERROR;
    }

    splitPath = Tcl_FSSplitPath(objv[2], NULL);
    Tcl_IncrRefCount(splitPath);
    if (splitPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
	return TCL_ERROR;
    }

    results = Cookfs_FsindexList(fsIndex, splitPath, &itemCount);
    Tcl_DecrRefCount(splitPath);
    
    if (results == NULL) {
	CookfsLog(printf("cmdList - results==NULL"))
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Entry not found", -1));
	return TCL_ERROR;
    }

    resultList = (Tcl_Obj **) Tcl_Alloc(sizeof(Tcl_Obj *) * itemCount);
    for (idx = 0; idx < itemCount; idx++) {
	resultList[idx] = Tcl_NewStringObj(results[idx]->fileName, -1);
    }
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
 *	TODO
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexCmdDelete(Cookfs_Fsindex *fsIndex, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }
    Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], NULL));
    return TCL_OK;
}

