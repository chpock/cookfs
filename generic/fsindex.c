/* (c) 2010 Pawel Salawa */

#include "cookfs.h"

static Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize);
static Tcl_HashEntry *CookfsFsindexFindHashElement(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int create, Tcl_Obj **filePathObj, int *isNewPtr);
static Cookfs_FsindexEntry *CookfsFsindexGetElement(Cookfs_Fsindex *i, Tcl_Obj *pathList);

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
    static Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexGet - start"))

    hashEntry = CookfsFsindexFindHashElement(i, NULL, pathList, 0, NULL, NULL);
    if (hashEntry == NULL) {
        CookfsLog(printf("Cookfs_FsindexGet - NULL"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexGet - gethashvalue"))

    fileNode = Tcl_GetHashValue(hashEntry);

    CookfsLog(printf("Cookfs_FsindexGet - success"))
    
    return fileNode;
}

Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Tcl_Obj *pathList, int numBlocks) {
    static Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *dirNode = NULL;
    Cookfs_FsindexEntry *fileNode;
    Tcl_Obj *pathTail;
    int isNew;
    char *pathTailStr;
    int pathTailLen;

    CookfsLog(printf("Cookfs_FsindexSet - start"))

    hashEntry = CookfsFsindexFindHashElement(i, &dirNode, pathList, 1, &pathTail, &isNew);
    if ((hashEntry == NULL) || (dirNode == NULL)) {
        CookfsLog(printf("Cookfs_FsindexSet - NULL"))
        return NULL;
    }

    if (!isNew) {
        /* TODO: clean up old entry & check if changing file type */
        CookfsLog(printf("Cookfs_FsindexSet - clean up old!"))
        fileNode = Tcl_GetHashValue(hashEntry);
        if (fileNode != NULL) {
            if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (numBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
                || ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (numBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
                CookfsLog(printf("Cookfs_FsindexSet - convert between file/directory"))
                return NULL;
            }  else  {
                Cookfs_FsindexEntryFree(fileNode);
            }
            Tcl_SetHashValue(hashEntry, NULL);
        }
    }  else  {
        dirNode->data.dirInfo.childCount++;
    }

    CookfsLog(printf("Cookfs_FsindexSet - creating node for \"%s\"; new count=%d", dirNode->fileName, dirNode->data.dirInfo.childCount))

    pathTailStr = Tcl_GetStringFromObj(pathTail, &pathTailLen);

    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexSet - unable to create entry"))
        return NULL;
    }
    CookfsLog(printf("Cookfs_FsindexSet - copy name - %s", pathTailStr))
    strcpy(fileNode->fileName, pathTailStr);
    Tcl_SetHashValue(hashEntry, fileNode);

    CookfsLog(printf("Cookfs_FsindexSet - success"))

    return fileNode;
}

int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    static Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *itemNode;
    Cookfs_FsindexEntry *dirNode;

    CookfsLog(printf("Cookfs_FsindexUnset - start"))

    hashEntry = CookfsFsindexFindHashElement(i, &dirNode, pathList, 0, NULL, NULL);
    if (hashEntry == NULL) {
        CookfsLog(printf("Cookfs_FsindexUnset - not found"))
        return 0;
    }
    
    itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
    if (itemNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
        if (itemNode->data.dirInfo.childCount > 0) {
            CookfsLog(printf("Cookfs_FsindexUnset - directory not empty"))
            return 0;
        }
    }

    CookfsLog(printf("Cookfs_FsindexUnset - freeing itemNode"))
    Cookfs_FsindexEntryFree(itemNode);

    CookfsLog(printf("Cookfs_FsindexUnset - removing"))
    dirNode->data.dirInfo.childCount--;
    Tcl_DeleteHashEntry(hashEntry);

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

    dirNode = CookfsFsindexGetElement(i, pathList);
    if (dirNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexList - not found"))
        return NULL;
    }
    if (dirNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("Cookfs_FsindexList - not directory"))
        return NULL;
    }

    result = (Cookfs_FsindexEntry **) Tcl_Alloc((dirNode->data.dirInfo.childCount + 1) * sizeof(Cookfs_FsindexEntry *));
    
    hashEntry = Tcl_FirstHashEntry(&dirNode->data.dirInfo.children, &hashSearch);
    while (hashEntry != NULL) {
        itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
        result[idx] = itemNode;
        idx++;
        hashEntry = Tcl_NextHashEntry(&hashSearch);
    }

    result[idx] = NULL;
    
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
        Tcl_InitHashTable(&e->data.dirInfo.children, TCL_STRING_KEYS);
        e->data.dirInfo.childCount = 0;
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

        hashEntry = Tcl_FirstHashEntry(&e->data.dirInfo.children, &hashSearch);
        while (hashEntry != NULL) {
            itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
            hashEntry = Tcl_NextHashEntry(&hashSearch);
            Cookfs_FsindexEntryFree(itemNode);
        }
    
        Tcl_DeleteHashTable(&e->data.dirInfo.children);
        /* TODO: delete all children */
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
        hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.children, currentPathStr);
        if (hashEntry == NULL) {
            CookfsLog(printf("Unable to find item"))
            return NULL;
        }
        currentNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
    }
    return currentNode;
}

static Tcl_HashEntry *CookfsFsindexFindHashElement(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int create, Tcl_Obj **filePathObj, int *isNewPtr) {
    Cookfs_FsindexEntry *currentNode;
    int listSize;
    Tcl_HashEntry *hashEntry;
    Tcl_Obj *pathTail;
    char *pathTailStr;
    
    if (Tcl_ListObjLength(NULL, pathList, &listSize) != TCL_OK) {
        return NULL;
    }
    if (listSize == 0) {
        return NULL;
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

    if (create) {
        hashEntry = Tcl_CreateHashEntry(&currentNode->data.dirInfo.children, pathTailStr, isNewPtr);
    }  else  {
        hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.children, pathTailStr);
    }

    if (dirPtr != NULL) {
        *dirPtr = currentNode;
    }

    if (filePathObj != NULL) {
        *filePathObj = pathTail;
    }

    return hashEntry;
}

static Cookfs_FsindexEntry *CookfsFsindexGetElement(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *currentNode;
    int listSize;
    
    if (Tcl_ListObjLength(NULL, pathList, &listSize) != TCL_OK) {
        return NULL;
    }

    CookfsLog(printf("CookfsFsindexGetElement - LS=%d", listSize))
    
    currentNode = CookfsFsindexFindElement(i, pathList, listSize);
    
    return currentNode;
}

