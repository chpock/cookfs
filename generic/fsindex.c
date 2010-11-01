/*
 * fsindex.c
 *
 * Provides functions for managing filesystem indexes
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"

#define COOKFSFSINDEX_FIND_FIND   0
#define COOKFSFSINDEX_FIND_CREATE 1
#define COOKFSFSINDEX_FIND_DELETE 2

/* declarations of static and/or internal functions */
static Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize);
static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int command, Cookfs_FsindexEntry *newFileNode);
static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_Fsindex *i, Cookfs_FsindexEntry *currentNode, char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode);
static void CookfsFsindexChildtableToHash(Cookfs_FsindexEntry *e);


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGetHandle --
 *
 *	Returns fsindex handle from provided Tcl command name
 *
 * Results:
 *	Pointer to Cookfs_Fsindex or NULL in case of failure
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;
    
    /* TODO: verify command suffix etc */

    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
	return NULL;
    }

    return (Cookfs_Fsindex *) (cmdInfo.objClientData);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexInit --
 *
 *	Create new fsindex instance.
 *
 * Results:
 *	Returns pointer to new instance
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexInit() {
    Cookfs_Fsindex *rc;
    rc = (Cookfs_Fsindex *) Tcl_Alloc(sizeof(Cookfs_Fsindex));
    rc->rootItem = Cookfs_FsindexEntryAlloc(0, COOKFS_NUMBLOCKS_DIRECTORY, COOKFS_USEHASH_DEFAULT);
    rc->rootItem->fileName = ".";
    return rc;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexFini --
 *
 *	Frees entire fsindex along with its child elements
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexFini(Cookfs_Fsindex *i) {
    Cookfs_FsindexEntryFree(i->rootItem);
    Tcl_Free((void *) i);
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGet --
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

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexGet - start"))

    fileNode = CookfsFsindexFind(i, NULL, pathList, COOKFSFSINDEX_FIND_FIND, NULL);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexGet - NULL"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexGet - success"))
    
    return fileNode;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSet --
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

Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Tcl_Obj *pathList, int numBlocks) {
    Cookfs_FsindexEntry *dirNode = NULL;
    Cookfs_FsindexEntry *fileNode;
    Cookfs_FsindexEntry *foundFileNode;
    Tcl_Obj *pathTail = NULL;
    char *pathTailStr;
    int pathTailLen;
    int listSize;

    CookfsLog(printf("Cookfs_FsindexSet - start"))

    if (Tcl_ListObjLength(NULL, pathList, &listSize) != TCL_OK) {
	CookfsLog(printf("Cookfs_FsindexSet - invalid list"))
        return NULL;
    }

    if (Tcl_ListObjIndex(NULL, pathList, listSize - 1, &pathTail) != TCL_OK) {
        CookfsLog(printf("Cookfs_FsindexSet - Unable to get element from a list"))
        return NULL;
    }
 
    CookfsLog(printf("Cookfs_FsindexSet - pathtail"))
    pathTailStr = Tcl_GetStringFromObj(pathTail, &pathTailLen);

    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks, COOKFS_USEHASH_DEFAULT);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexSet - unable to create entry"))
        return NULL;
    }
    CookfsLog(printf("Cookfs_FsindexSet - copy name - %s", pathTailStr))
    strcpy(fileNode->fileName, pathTailStr);

    foundFileNode = CookfsFsindexFind(i, &dirNode, pathList, COOKFSFSINDEX_FIND_CREATE, fileNode);
    if ((foundFileNode == NULL) || (dirNode == NULL)) {
        CookfsLog(printf("Cookfs_FsindexSet - NULL"))
        Cookfs_FsindexEntryFree(fileNode);
	return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexSet - creating node for \"%s\"; new count=%d", pathTailStr, dirNode->data.dirInfo.childCount))

    CookfsLog(printf("Cookfs_FsindexSet - success"))

    return fileNode;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSetInDirectory --
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

Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_Fsindex *i, Cookfs_FsindexEntry *currentNode, char *pathTailStr, int pathTailLen, int numBlocks) {
    Cookfs_FsindexEntry *fileNode;
    Cookfs_FsindexEntry *foundFileNode;
    CookfsLog(printf("Cookfs_FsindexSetInDirectory - begin (%s/%d)", pathTailStr, pathTailLen))
    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks, COOKFS_USEHASH_DEFAULT);
    strcpy(fileNode->fileName, pathTailStr);

    CookfsLog(printf("Cookfs_FsindexSetInDirectory - fileNode=%08x", fileNode))
    foundFileNode = CookfsFsindexFindInDirectory(i, currentNode, pathTailStr, COOKFSFSINDEX_FIND_CREATE, fileNode);
    if (foundFileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexSetInDirectory - NULL"))
        Cookfs_FsindexEntryFree(fileNode);
        CookfsLog(printf("Cookfs_FsindexSetInDirectory - cleanup done"))
	return NULL;
    }
    return fileNode;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexUnset --
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

int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexUnset - start"))

    fileNode = CookfsFsindexFind(i, NULL, pathList, COOKFSFSINDEX_FIND_DELETE, NULL);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexUnset - NULL"))
        return 0;
    }

    CookfsLog(printf("Cookfs_FsindexUnset - success"))
    
    return 1;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexList --
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

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Tcl_Obj *pathList, int *itemCountPtr) {
    Cookfs_FsindexEntry *dirNode = NULL;
    Cookfs_FsindexEntry *itemNode;
    Cookfs_FsindexEntry **result;
    int idx = 0;
    Tcl_HashSearch hashSearch;
    Tcl_HashEntry *hashEntry;

    CookfsLog(printf("Cookfs_FsindexList - start"))

    dirNode = CookfsFsindexFind(i, NULL, pathList, COOKFSFSINDEX_FIND_FIND, NULL);

    if (dirNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexList - not found"))
        return NULL;
    }
    if (dirNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("Cookfs_FsindexList - not directory"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexList - childCount = %d", dirNode->data.dirInfo.childCount))
    result = (Cookfs_FsindexEntry **) Tcl_Alloc((dirNode->data.dirInfo.childCount + 1) * sizeof(Cookfs_FsindexEntry *));
	
    CookfsLog(printf("Cookfs_FsindexList - isHash=%d", dirNode->data.dirInfo.isHash))
    if (dirNode->data.dirInfo.isHash) {
	hashEntry = Tcl_FirstHashEntry(&dirNode->data.dirInfo.dirData.children, &hashSearch);
	while (hashEntry != NULL) {
	    itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	    result[idx] = itemNode;
	    idx++;
	    hashEntry = Tcl_NextHashEntry(&hashSearch);
	}

	result[idx] = NULL;
    }  else  {
	int i;
	for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
	    if (dirNode->data.dirInfo.dirData.childTable[i] != NULL) {
		result[idx] = dirNode->data.dirInfo.dirData.childTable[i];
		idx++;
	    }
	}
	result[idx] = NULL;
    }
	
    if (itemCountPtr != NULL) {
	*itemCountPtr = dirNode->data.dirInfo.childCount;
    }
    
    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexListFree --
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

void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items) {
    Tcl_Free((void *) items);
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryAlloc --
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

Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(int fileNameLength, int numBlocks, int useHash) {
    int size0 = sizeof(Cookfs_FsindexEntry);
    int fileNameBytes;

    Cookfs_FsindexEntry *e;
    if (numBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        size0 += (numBlocks * 3 * sizeof(int));
    }
    if (fileNameLength > 255) {
        return NULL;
    }
    fileNameBytes = (fileNameLength + 8) & 0xf8;

    /* use single alloc for everything to limit number of memory allocations */
    e = (Cookfs_FsindexEntry *) Tcl_Alloc(size0 + fileNameBytes);
    e->fileName = ((char *) e) + size0;
    e->fileBlocks = numBlocks;
    e->fileNameLen = fileNameLength;
    if (numBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("Cookfs_FsindexEntryAlloc - directory, useHash=%d", useHash))
	if (useHash) {
	    Tcl_InitHashTable(&e->data.dirInfo.dirData.children, TCL_STRING_KEYS);
	    e->data.dirInfo.isHash = 1;
	}  else  {
	    int i;
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		e->data.dirInfo.dirData.childTable[i] = NULL;
	    }
	    e->data.dirInfo.isHash = 0;
	}
        e->data.dirInfo.childCount = 0;
    }  else  {
    }

    return e;   
}



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryFree --
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

void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e) {
    if (e->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
        Tcl_HashSearch hashSearch;
        Tcl_HashEntry *hashEntry;
        Cookfs_FsindexEntry *itemNode;

	if (e->data.dirInfo.isHash) {
	    hashEntry = Tcl_FirstHashEntry(&e->data.dirInfo.dirData.children, &hashSearch);
	    while (hashEntry != NULL) {
		itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
		hashEntry = Tcl_NextHashEntry(&hashSearch);
		Cookfs_FsindexEntryFree(itemNode);
	    }
	
	    Tcl_DeleteHashTable(&e->data.dirInfo.dirData.children);
	}  else  {
	    int i;
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		if (e->data.dirInfo.dirData.childTable[i] != NULL) {
		    Cookfs_FsindexEntryFree(e->data.dirInfo.dirData.childTable[i]);
		}
	    }
	}
    }
    Tcl_Free((void *) e);
}


/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFindElement --
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
static Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize) {
    int idx;
    Tcl_Obj *currentPath;
    Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *currentNode;
    Cookfs_FsindexEntry *nextNode;
    char *currentPathStr;

    currentNode = i->rootItem;
    
    CookfsLog(printf("Recursively finding %d path elemnets", listSize))

    for (idx = 0; idx < listSize; idx++) {
        CookfsLog(printf("Iterating at %s (%d); %d of %d", currentNode->fileName, currentNode->fileBlocks, idx, listSize))
        if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
            CookfsLog(printf("Parent is not a directory"))
            return NULL;
        }
        CookfsLog(printf("Getting element"))
        if (Tcl_ListObjIndex(NULL, pathList, idx, &currentPath) != TCL_OK) {
            CookfsLog(printf("Unable to get element from a list"))
            return NULL;
        }
        CookfsLog(printf("Got element"))
        currentPathStr = Tcl_GetStringFromObj(currentPath, NULL);
        CookfsLog(printf("Looking for %s", currentPathStr))

	if (currentNode->data.dirInfo.isHash) {
	    hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.dirData.children, currentPathStr);
	    if (hashEntry == NULL) {
		CookfsLog(printf("Unable to find item"))
		return NULL;
	    }
	    currentNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	}  else  {
	    int i;
	    nextNode = NULL;
	    CookfsLog(printf("Iterating over childTable to find %s", currentPathStr))
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		if ((currentNode->data.dirInfo.dirData.childTable[i] != NULL) && (strcmp(currentNode->data.dirInfo.dirData.childTable[i]->fileName, currentPathStr) == 0)) {
		    nextNode = currentNode->data.dirInfo.dirData.childTable[i];
		    CookfsLog(printf("Iterating over childTable to find %s - FOUND", currentPathStr))
		    break;
		}
	    }
	    currentNode = nextNode;
	    if (currentNode == NULL) {
		CookfsLog(printf("Unable to find item"))
		return NULL;
	    }
	}
    }
    return currentNode;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFind --
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

static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int command, Cookfs_FsindexEntry *newFileNode) {
    Cookfs_FsindexEntry *currentNode;
    int listSize;
    Tcl_Obj *pathTail;
    char *pathTailStr;
    
    if (Tcl_ListObjLength(NULL, pathList, &listSize) != TCL_OK) {
        return NULL;
    }
    if (listSize == 0) {
	if (command == COOKFSFSINDEX_FIND_FIND) {
            return i->rootItem;
	}  else  {
	    /* create or delete will not work with empty file list */
	    return NULL;
	}
    }

    CookfsLog(printf("CookfsFsindexCreateHashElement - LS=%d", listSize))
    
    currentNode = CookfsFsindexFindElement(i, pathList, listSize - 1);


    if (dirPtr != NULL) {
	*dirPtr = currentNode;
    }

    if (currentNode == NULL) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - node not found"))
        return NULL;
    }

    if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - not a directory"))
        return NULL;
    }

    if (Tcl_ListObjIndex(NULL, pathList, listSize - 1, &pathTail) != TCL_OK) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - Unable to get element from a list"))
        return NULL;
    }

    pathTailStr = Tcl_GetStringFromObj(pathTail, NULL);
    CookfsLog(printf("CookfsFsindexCreateHashElement - Path tail: %s", pathTailStr))

    return CookfsFsindexFindInDirectory(i, currentNode, pathTailStr, command, newFileNode);
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFindInDirectory --
 *
 *	TODO - describe what it does for all commands
 *
 *	TODO: comment why it made more sense to write as single function
 *
 * Results:
 *	TODO
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_Fsindex *i, Cookfs_FsindexEntry *currentNode, char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode) {
    /* iterate until a solution is found - break is placed at the end,
     * so unless a continue is invoked (to retry the step),
     * only one iteration occurs */
    while (1) {
	if (currentNode->data.dirInfo.isHash) {
	    Cookfs_FsindexEntry *fileNode = NULL;
	    Tcl_HashEntry *hashEntry;
	    int isNew;

	    if (command == COOKFSFSINDEX_FIND_CREATE) {
		hashEntry = Tcl_CreateHashEntry(&currentNode->data.dirInfo.dirData.children, pathTailStr, &isNew);
	    }  else  {
		hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.dirData.children, pathTailStr);
	    }

	    if (hashEntry != NULL) {
		CookfsLog(printf("CookfsFsindexCreateHashElement - found in hash table (isNew=%d; cmd=%d)", isNew, command))
		/* if entry exists or has just been created */
		fileNode = Tcl_GetHashValue(hashEntry);

		CookfsLog(printf("CookfsFsindexCreateHashElement - fileNode=%08x", fileNode))

		/* if we are to create a new entry, set new value */
		if (command == COOKFSFSINDEX_FIND_CREATE) {
		    if (!isNew) {
			/* if entry exists already, check if both are of same type */
			if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
			    || ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
			    /* TODO: how to treat overwrite of a directory with a directory? */
			    CookfsLog(printf("CookfsFsindexCreateHashElement - type mismatch"))
			    Cookfs_FsindexEntryFree(newFileNode);
			    return NULL;
			}

			Cookfs_FsindexEntryFree(fileNode);
		    }  else  {
			/* if it is a new entry, increment counter */
			currentNode->data.dirInfo.childCount++;
		    }

		    CookfsLog(printf("CookfsFsindexCreateHashElement - setting as newFileNode = %08x", newFileNode))
		    Tcl_SetHashValue(hashEntry, newFileNode);
		    CookfsLog(printf("CookfsFsindexCreateHashElement - setting as newFileNode done"))
		    return newFileNode;
		}  else if (command == COOKFSFSINDEX_FIND_DELETE) {
		    if ((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY)
			&& (fileNode->data.dirInfo.childCount > 0)) {
			/* if it is a non-empty directory, disallow unsetting it */
			return NULL;
		    }

		    currentNode->data.dirInfo.childCount--;
		    Tcl_DeleteHashEntry(hashEntry);
		    Cookfs_FsindexEntryFree(fileNode);
		}
		return fileNode;
	    }
	}  else  {
	    Cookfs_FsindexEntry *fileNode = NULL;
	    Cookfs_FsindexEntry **fileNodePtr = NULL;
	    int i;

	    CookfsLog(printf("CookfsFsindexCreateHashElement - looking in childTable"))
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		CookfsLog(printf("CookfsFsindexCreateHashElement - checking %d", i))
		if ((currentNode->data.dirInfo.dirData.childTable[i] != NULL) && (strcmp(currentNode->data.dirInfo.dirData.childTable[i]->fileName, pathTailStr) == 0)) {
		    fileNode = currentNode->data.dirInfo.dirData.childTable[i];
		    fileNodePtr = &currentNode->data.dirInfo.dirData.childTable[i];
		    CookfsLog(printf("CookfsFsindexCreateHashElement - found at %d", i))
		    break;
		}
		
	    }

	    if (fileNode != NULL) {
		CookfsLog(printf("CookfsFsindexCreateHashElement - found in table cmd=%d", command))
		/* if entry was found */
		if (command == COOKFSFSINDEX_FIND_DELETE) {
		    /* if it should be deleted, delete it */
		    currentNode->data.dirInfo.childCount--;
		    Cookfs_FsindexEntryFree(fileNode);
		    *fileNodePtr = NULL;
		    CookfsLog(printf("CookfsFsindexCreateHashElement - deleted"))
		}  else if (command == COOKFSFSINDEX_FIND_CREATE) {
		    /* if entry exists already, check if both are of same type */
		    CookfsLog(printf("CookfsFsindexCreateHashElement - updating..."))
		    if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
			|| ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
			/* TODO: how to treat overwrite of a directory with a directory? */
			CookfsLog(printf("CookfsFsindexCreateHashElement - update failed - type mismatch"))
			Cookfs_FsindexEntryFree(newFileNode);
			return NULL;
		    }

		    CookfsLog(printf("CookfsFsindexCreateHashElement - updated"))
		    Cookfs_FsindexEntryFree(fileNode);
		    *fileNodePtr = newFileNode;
		    return newFileNode;
		}
		return fileNode;
	    }  else  {
		if (command == COOKFSFSINDEX_FIND_CREATE) {
		    CookfsLog(printf("CookfsFsindexCreateHashElement - creating (%d)", currentNode->data.dirInfo.childCount))
		    /* if we ran out of entries, convert to hash table and try again */
		    if (currentNode->data.dirInfo.childCount >= (COOKFS_FSINDEX_TABLE_MAXENTRIES - 1)) {
			CookfsLog(printf("CookfsFsindexCreateHashElement - converting to hash"))
			CookfsFsindexChildtableToHash(currentNode);
			continue;
		    }  else  {
			/* if we have any spots available, update first free one */
			for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
			    CookfsLog(printf("CookfsFsindexCreateHashElement - create - checking %d", i))
			    if (currentNode->data.dirInfo.dirData.childTable[i] == NULL) {
				CookfsLog(printf("CookfsFsindexCreateHashElement - create - adding at %d", i))
				currentNode->data.dirInfo.dirData.childTable[i] = newFileNode;
				break;
			    }
			}
			currentNode->data.dirInfo.childCount++;
		    }
		    return newFileNode;
		}  else  {
		    return NULL;
		}
	    }

	    break;
	}
	break;
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexChildtableToHash --
 *
 *	Converts directory fsindex entry from using static array of
 *	children into hash table based.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsFsindexChildtableToHash(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntry *childTable[COOKFS_FSINDEX_TABLE_MAXENTRIES];
    int i;
    Tcl_HashEntry *hashEntry;
    int isNew;

    CookfsLog(printf("CookfsFsindexChildtableToHash: STARTING"))

    memcpy(childTable, e->data.dirInfo.dirData.childTable, sizeof(childTable));

    e->data.dirInfo.isHash = 1;
    Tcl_InitHashTable(&e->data.dirInfo.dirData.children, TCL_STRING_KEYS);

    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
	if (childTable[i] != NULL) {
	    CookfsLog(printf("CookfsFsindexChildtableToHash - copying %s", childTable[i]->fileName))
	    hashEntry = Tcl_CreateHashEntry(&e->data.dirInfo.dirData.children, childTable[i]->fileName, &isNew);

	    if (!isNew) {
		CookfsLog(printf("CookfsFsindexChildtableToHash - duplicate entry %s", childTable[i]->fileName))
	    }

	    Tcl_SetHashValue(hashEntry, childTable[i]);
	}
    }

    CookfsLog(printf("CookfsFsindexChildtableToHash: FINISHED"))
}
