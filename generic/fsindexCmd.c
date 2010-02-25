/* (c) 2010 Pawel Salawa */

#include "cookfs.h"

static int deprecatedCounter = 0;

static int CookfsRegisterFsindexObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void CookfsFsindexDeleteProc(ClientData clientData);
static int CookfsFsindexCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

int Cookfs_InitFsindexCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::fsindex", CookfsRegisterFsindexObjectCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
}

/* command for creating new objects that deal with pages */
static int CookfsRegisterFsindexObjectCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    char buf[64];
    int idx;
    Cookfs_Fsindex *i;

    idx = ++deprecatedCounter;
    sprintf(buf, "::cookfs::fsindexhandle%d", idx);

    if (objc > 2) {
        goto ERROR;
    }

    if (objc == 1) {
        i = Cookfs_FsindexInit();
    }  else  {
        i = Cookfs_FsindexFromObject(objv[1]);
    }
    if (i == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create index object", -1));
        return TCL_ERROR;
    }

    Tcl_CreateObjCommand(interp, buf, CookfsFsindexCmd, (ClientData) i, CookfsFsindexDeleteProc);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));   
    return TCL_OK;

ERROR:
    Tcl_WrongNumArgs(interp, 1, objv, "?binaryData?");
    return TCL_ERROR;
}

static int CookfsFsindexCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    char *commands[] = { "export", "list", "get", "getmtime", "set", "setmtime", "unset", "delete", NULL };
    enum { cmdExport, cmdList, cmdGet, cmdGetmtime, cmdSet, cmdSetmtime, cmdUnset, cmdDelete };
    int idx;
    int i;
    
    Cookfs_Fsindex *fsIndex = (Cookfs_Fsindex *) clientData;
    
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], (const char **) commands, "command", 0, &idx) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (idx) {
        case cmdExport: {
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
        case cmdGetmtime: {
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
        case cmdSetmtime: {
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
        case cmdSet: {
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

            if (Tcl_GetWideIntFromObj(interp, objv[3], &fileTime) != TCL_OK) {
                return TCL_ERROR;
            }

            splitPath = Tcl_FSSplitPath(objv[2], NULL);
            Tcl_IncrRefCount(splitPath);
            if (splitPath == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to split path", -1));
                return TCL_ERROR;
            }

            if (objc == 4) {
                entry = Cookfs_FsindexSet(fsIndex, splitPath, COOKFS_NUMBLOCKS_DIRECTORY);
                if (entry == NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create entry", -1));
                    Tcl_DecrRefCount(splitPath);
                    return TCL_ERROR;
                }
            }  else  {
                if (Tcl_ListObjGetElements(interp, objv[4], &numBlocks, &listElements) != TCL_OK) {
                    Tcl_DecrRefCount(splitPath);
                    return TCL_ERROR;
                }

                numBlocks /= 3;

                entry = Cookfs_FsindexSet(fsIndex, splitPath, numBlocks);
                if (entry == NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create entry", -1));
                    Tcl_DecrRefCount(splitPath);
                    return TCL_ERROR;
                }
                entry->data.fileInfo.fileSize = 0;
                
                for (i = 0; i < numBlocks * 3; i++) {
                    if (Tcl_GetIntFromObj(interp, listElements[i], &fileBlockData) != TCL_OK) {
                        Tcl_DecrRefCount(splitPath);
                        CookfsLog(printf("WTF! - list crashed!"))
                        return TCL_ERROR;
                    }
                    entry->data.fileInfo.fileBlockOffsetSize[i] = fileBlockData;
                    CookfsLog(printf("Dump %d -> %d", i, fileBlockData))
                }
                for (i = 0; i < numBlocks; i++) {
                    entry->data.fileInfo.fileSize += entry->data.fileInfo.fileBlockOffsetSize[i * 3 + 2];
                }
                CookfsLog(printf("Size: %d", (int) entry->data.fileInfo.fileSize))
            }
            entry->fileTime = fileTime;
            
            Tcl_DecrRefCount(splitPath);
            break;
        }
        case cmdUnset: {
            Tcl_Obj *splitPath;
            int result;
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

            result = Cookfs_FsindexUnset(fsIndex, splitPath);
            Tcl_DecrRefCount(splitPath);
            
            if (!result) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to unset item", -1));
                return TCL_ERROR;
            }
            break;
        }
        case cmdGet: {
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
            break;
        }
        case cmdList: {
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
        default:
            return TCL_ERROR;
    }

    return TCL_OK;
}

static void CookfsFsindexDeleteProc(ClientData clientData) {
    CookfsLog(printf("DELETING FSINDEX COMMAND"))
    Cookfs_FsindexFini((Cookfs_Fsindex *) clientData);
    CookfsLog(printf("DELETED FSINDEX COMMAND"))
}
