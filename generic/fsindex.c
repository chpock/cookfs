/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#include "cookfs.h"

#define COOKFSFSINDEX_FIND_FIND   0
#define COOKFSFSINDEX_FIND_CREATE 1
#define COOKFSFSINDEX_FIND_DELETE 2

/* TODO: merge those three functions */
static Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize);
static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int command, Cookfs_FsindexEntry *newFileNode);

Cookfs_Fsindex *Cookfs_FsindexInit() {
    Cookfs_Fsindex *rc;
    rc = (Cookfs_Fsindex *) Tcl_Alloc(sizeof(Cookfs_Fsindex));
    rc->rootItem = Cookfs_FsindexEntryAlloc(0, COOKFS_NUMBLOCKS_DIRECTORY);
    return rc;
}

void Cookfs_FsindexFini(Cookfs_Fsindex *i) {
    Cookfs_FsindexEntryFree(i->rootItem);
    Tcl_Free((void *) i);
}

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

    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks);
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

    CookfsLog(printf("Cookfs_FsindexSet - creating node for \"%s\"; new count=%d", dirNode->fileName, dirNode->data.dirInfo.childCount))

    CookfsLog(printf("Cookfs_FsindexSet - success"))

    return fileNode;
}

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

    result = (Cookfs_FsindexEntry **) Tcl_Alloc((dirNode->data.dirInfo.childCount + 1) * sizeof(Cookfs_FsindexEntry *));
	
    if (dirNode->data.dirInfo.isHash) {
	hashEntry = Tcl_FirstHashEntry(&dirNode->data.dirInfo.dirData.children, &hashSearch);
	while (hashEntry != NULL) {
	    itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	    result[idx] = itemNode;
	    idx++;
	    hashEntry = Tcl_NextHashEntry(&hashSearch);
	}

	result[idx] = NULL;
    }
	
    if (itemCountPtr != NULL) {
	*itemCountPtr = dirNode->data.dirInfo.childCount;
    }
    
    return result;
}

void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items) {
    Tcl_Free((void *) items);
}

Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(int fileNameLength, int numBlocks) {
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
	if (1) {
	    Tcl_InitHashTable(&e->data.dirInfo.dirData.children, TCL_STRING_KEYS);
	}  else  {
	    /* TODO: handle dirTable */
	}
        e->data.dirInfo.childCount = 0;
        e->data.dirInfo.isHash = 1;
    }  else  {
        e->data.fileInfo.memoryBlock = NULL;
    }

    return e;   
}

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
	    /* TODO: handle dirTable */
	}
    }
    Tcl_Free((void *) e);
}

static Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize) {
    int idx;
    Tcl_Obj *currentPath;
    Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *currentNode;
    char *currentPathStr;

    CookfsLog(printf("INIT"))

    currentNode = i->rootItem;
    
    CookfsLog(printf("INIT2"))

    CookfsLog(printf("Iterating for %d elemnets", listSize))

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
	    /* TODO: handle dirTable */
	}
    }
    return currentNode;
}

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

    if (dirPtr != NULL) {
	*dirPtr = currentNode;
    }

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
	    fileNode = Tcl_GetHashValue(hashEntry);

	    /* if we are to create a new entry, set new value */
	    if (command == COOKFSFSINDEX_FIND_CREATE) {
		if (!isNew) {
		    /* if entry exists already, check if both are of same type */
		    if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
			|| ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
			/* TODO: how to treat overwrite of a directory with a directory? */
			Cookfs_FsindexEntryFree(newFileNode);
			return NULL;
		    }

		    Cookfs_FsindexEntryFree(fileNode);
		}  else  {
		    currentNode->data.dirInfo.childCount++;
		}

		Tcl_SetHashValue(hashEntry, newFileNode);
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
	/* TODO: handle dirTable */
    }
    return NULL;
}

