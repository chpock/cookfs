/*
 * fsindex.c
 *
 * Provides functions for managing filesystem indexes
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

#define COOKFSFSINDEX_FIND_FIND             0
#define COOKFSFSINDEX_FIND_CREATE           1
#define COOKFSFSINDEX_FIND_DELETE           2
#define COOKFSFSINDEX_FIND_DELETE_RECURSIVE 3

/* declarations of static and/or internal functions */
Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize);
static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Tcl_Obj *pathList, int command, Cookfs_FsindexEntry *newFileNode);
static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_FsindexEntry *currentNode, char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode);
static void CookfsFsindexChildtableToHash(Cookfs_FsindexEntry *e);

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryIsPending --
 *
 *      Checks if the specified entry contains data that is not yet in
 *      the pages, but is in the small file buffer
 *
 * Results:
 *      Returns 1 if the specified entry is a pending state and 0 otherwise
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexEntryIsPending(Cookfs_FsindexEntry *e) {
    if (e == NULL || Cookfs_FsindexEntryIsDirectory(e)) {
        return 0;
    }
    for (int i = 0; i < (e->fileBlocks * 3); i += 3) {
        if (e->data.fileInfo.fileBlockOffsetSize[i] < 0) {
            return 1;
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryIsDirectory --
 *
 *      Checks if the specified entry is a directory
 *
 * Results:
 *      Returns 1 if the specified entry is a directory and 0 if it is a file
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexEntryIsDirectory(Cookfs_FsindexEntry *e) {
    return ((e != NULL && e->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) ? 1 : 0);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexIncrChangeCount --
 *
 *      Increases the change counter
 *
 * Results:
 *      Current value of the change counter
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_WideInt Cookfs_FsindexIncrChangeCount(Cookfs_Fsindex *i, int count) {
    i->changeCount += count;
    return i->changeCount;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexResetChangeCount --
 *
 *      Resets the change counter to zero
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexResetChangeCount(Cookfs_Fsindex *i) {
    i->changeCount = 0;
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGetBlockUsage --
 *
 *      Returns the number of files in the specified block.
 *
 * Results:
 *      The number of files in the specified block.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexGetBlockUsage(Cookfs_Fsindex *i, int idx) {
    CookfsLog(printf("Cookfs_FsindexGetBlockUsage: from [%p] index [%d]", (void *)i, idx));
    if (idx < 0 || i->blockIndexSize <= idx)
        return 0;
    return i->blockIndex[idx];
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexModifyBlockUsage --
 *
 *      Add or substract the number of files in the specified block.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Expands the buffer for block indexes if the specified block index
 *      extends beyond the currently known blocks.
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexModifyBlockUsage(Cookfs_Fsindex *i, int idx, int count) {

    // if blockIndexSize is not zero and blockIndex is NULL then we are
    // in terminate mode so we just return
    if (i->blockIndexSize && !i->blockIndex)
        return;

    // if the index < 0, then the file has not used any blocks yet so
    // we just return
    if (idx < 0)
        return;

    CookfsLog(printf("Cookfs_FsindexModifyBlockUsage: increase block index [%d] by [%d]", idx, count));
    // check if we have enough space in the block index
    if (i->blockIndexSize <= idx) {
        CookfsLog(printf("Cookfs_FsindexModifyBlockUsage: expand block index buffer from [%d] to [%d]", i->blockIndexSize, idx + 1));
        // expand our block index
        i->blockIndex = (int *)ckrealloc(i->blockIndex, sizeof(int) * (idx + 1));
        // initialize newly allocated block indexes with zeros
        for (; i->blockIndexSize <= idx; i->blockIndexSize++) {
            i->blockIndex[i->blockIndexSize] = 0;
        }
    } else {
        CookfsLog(printf("Cookfs_FsindexModifyBlockUsage: current value is [%d]", i->blockIndex[idx]));
    }

    i->blockIndex[idx] += count;
    CookfsLog(printf("Cookfs_FsindexModifyBlockUsage: new value is [%d]", i->blockIndex[idx]));

}

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

    CookfsLog(printf("Cookfs_FsindexGetHandle: get handle from cmd [%s]", cmdName));
    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
	return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexGetHandle: return [%p]", cmdInfo.objClientData));
    /* if we found proper Tcl command, its objClientData is Cookfs_Fsindex */
    return (Cookfs_Fsindex *) (cmdInfo.objClientData);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexInit --
 *
 *      Initializes the given fsindex instance. If the given fsindex is NULL,
 *      then allocates it.
 *
 * Results:
 *      Returns pointer to new instance
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexInit(Cookfs_Fsindex *i) {
    Cookfs_Fsindex *rc;
    if (i == NULL) {
        rc = (Cookfs_Fsindex *) Tcl_Alloc(sizeof(Cookfs_Fsindex));
        rc->commandToken = NULL;
        rc->interp = NULL;
        rc->isDead = 0;
    } else {
        rc = i;
    }
    rc->rootItem = Cookfs_FsindexEntryAlloc(0, COOKFS_NUMBLOCKS_DIRECTORY, COOKFS_USEHASH_DEFAULT);
    rc->rootItem->fileName = ".";
    rc->blockIndexSize = 0;
    rc->blockIndex = NULL;
    rc->changeCount = 0;
    Tcl_InitHashTable(&rc->metadataHash, TCL_STRING_KEYS);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexCleanup --
 *
 *	Clears entire fsindex along with its child elements
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexCleanup(Cookfs_Fsindex *i) {
    Tcl_HashEntry *hashEntry;
    Tcl_HashSearch hashSearch;

    /* free up block index, set it to NULL to avoid modifications when
       unsetting individual entries */
    if (i->blockIndex != NULL) {
        ckfree(i->blockIndex);
        i->blockIndex = NULL;
    }

    /* free up root entry and memory for storing fsindex */
    Cookfs_FsindexEntryFree(i->rootItem);

    /* free all entries in hash table */
    for (hashEntry = Tcl_FirstHashEntry(&i->metadataHash, &hashSearch); hashEntry != NULL; hashEntry = Tcl_NextHashEntry(&hashSearch)) {
        Tcl_DecrRefCount((Tcl_Obj *) Tcl_GetHashValue(hashEntry));
        Tcl_DeleteHashEntry(hashEntry);
    }
    Tcl_DeleteHashTable(&i->metadataHash);
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
    if (i->isDead) {
        return;
    }
    i->isDead = 1;
    CookfsLog(printf("Cookfs_FsindexFini: release"));
    Cookfs_FsindexCleanup(i);
    if (i->commandToken != NULL) {
        CookfsLog(printf("Cleaning tcl command"));
        Tcl_DeleteCommandFromToken(i->interp, i->commandToken);
    } else {
        CookfsLog(printf("No tcl command"));
    }
    Tcl_Free((void *) i);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGet --
 *
 *	Retrieves information about specified element in specified
 *	fsindex instance
 *
 *	pathList provides a path as list, separated by fs separator
 *	for example for path/to/file, the list would be {path to file}
 *	and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *	Entry if successful, NULL otherwise
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexGet - start"))

    /* run FIND command to get existing entry */
    fileNode = CookfsFsindexFind(i, NULL, pathList, COOKFSFSINDEX_FIND_FIND, NULL);

    /* return NULL if not found */
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
 *	Sets entry in specified path to file or directory
 *	If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, it is a directory
 *	otherwise it is a file with specified number of blocks
 *
 *	pathList provides a path as list, separated by fs separator
 *	for example for path/to/file, the list would be {path to file}
 *	and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *	Newly created/updated entry if successful, NULL otherwise
 *
 * Side effects:
 *	Can create pointer to this entry in parent element
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

    /* get list length and last element (tail) */
    if (Tcl_ListObjLength(NULL, pathList, &listSize) != TCL_OK) {
	CookfsLog(printf("Cookfs_FsindexSet - invalid list"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexSet - listSize=%d", listSize))

    if (listSize == 0) {
        return NULL;
    }

    if (Tcl_ListObjIndex(NULL, pathList, listSize - 1, &pathTail) != TCL_OK) {
        CookfsLog(printf("Cookfs_FsindexSet - Unable to get element from a list"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexSet - pathtail"))
    pathTailStr = Tcl_GetStringFromObj(pathTail, &pathTailLen);

    /* create new entry for object - used by CookfsFsindexFind() if
     * existing entry was not found */
    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks, COOKFS_USEHASH_DEFAULT);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexSet - unable to create entry"))
        return NULL;
    }
    CookfsLog(printf("Cookfs_FsindexSet - copy name - %s", pathTailStr))
    strcpy(fileNode->fileName, pathTailStr);

    /* run CREATE command - if entry exists, currently passed fileNode will be freed */
    foundFileNode = CookfsFsindexFind(i, &dirNode, pathList, COOKFSFSINDEX_FIND_CREATE, fileNode);

    /* if finding failed (i.e. parent did not exist), return NULL */
    if ((foundFileNode == NULL) || (dirNode == NULL)) {
        CookfsLog(printf("Cookfs_FsindexSet - NULL"))
        /* the current node is already released by CookfsFsindexFind() */
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexSet - creating node for \"%s\"; new count=%d", pathTailStr, dirNode->data.dirInfo.childCount))

    return fileNode;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSetInDirectory --
 *
 *	Sets entry in specified parent entry
 *	If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, it is a directory
 *	otherwise it is a file with specified number of blocks
 *
 *	pathTailStr and pathTailLen are file tail and its length in bytes
 *
 * Results:
 *	Newly created/updated entry if successful, NULL otherwise
 *
 * Side effects:
 *	Can create pointer to this entry in parent element
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_FsindexEntry *currentNode, char *pathTailStr, int pathTailLen, int numBlocks) {
    Cookfs_FsindexEntry *fileNode;
    Cookfs_FsindexEntry *foundFileNode;
    CookfsLog(printf("Cookfs_FsindexSetInDirectory - begin (%s/%d)", pathTailStr, pathTailLen))
    fileNode = Cookfs_FsindexEntryAlloc(pathTailLen, numBlocks, COOKFS_USEHASH_DEFAULT);
    strcpy(fileNode->fileName, pathTailStr);

    CookfsLog(printf("Cookfs_FsindexSetInDirectory - fileNode=%p", (void *)fileNode))
    foundFileNode = CookfsFsindexFindInDirectory(currentNode, pathTailStr, COOKFSFSINDEX_FIND_CREATE, fileNode);
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
 *	Removes specified entry from filesystem index
 *
 *	Only empty directories and files can be removed
 *
 * Results:
 *	1 on success; 0 on error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexUnset - start"))

    /* invoke DELETE command */
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
 * Cookfs_FsindexUnsetRecursive --
 *
 *	Removes specified entry and all child entries from filesystem index
 *
 * Results:
 *	1 on success; 0 on error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnsetRecursive(Cookfs_Fsindex *i, Tcl_Obj *pathList) {
    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("Cookfs_FsindexUnsetRecursive - start"))

    /* invoke DELETE command */
    fileNode = CookfsFsindexFind(i, NULL, pathList, COOKFSFSINDEX_FIND_DELETE_RECURSIVE, NULL);
    if (fileNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexUnsetRecursive - NULL"))
        return 0;
    }

    CookfsLog(printf("Cookfs_FsindexUnsetRecursive - success"))

    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexListEntry --
 *
 *	Lists entries in specified path entry of filesystem index
 *
 *	Number of entries is put in itemCountPtr, if it is non-NULL
 *
 * Results:
 *	Array of Cookfs_FsindexEntry items; NULL is placed after
 *	all of the entries; call to Cookfs_FsindexListFree()
 *	should be made to free results from this function
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry **Cookfs_FsindexListEntry(Cookfs_FsindexEntry *dirNode, int *itemCountPtr) {
    Cookfs_FsindexEntry *itemNode;
    Cookfs_FsindexEntry **result;
    int idx = 0;
    Tcl_HashSearch hashSearch;
    Tcl_HashEntry *hashEntry;

    CookfsLog(printf("Cookfs_FsindexListEntry - start"))

    if (dirNode == NULL) {
        CookfsLog(printf("Cookfs_FsindexListEntry - not found"))
        return NULL;
    }
    if (dirNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("Cookfs_FsindexListEntry - not directory"))
        return NULL;
    }

    CookfsLog(printf("Cookfs_FsindexListEntry - childCount = %d", dirNode->data.dirInfo.childCount))
    result = (Cookfs_FsindexEntry **) Tcl_Alloc((dirNode->data.dirInfo.childCount + 1) * sizeof(Cookfs_FsindexEntry *));

    CookfsLog(printf("Cookfs_FsindexListEntry - isHash=%d", dirNode->data.dirInfo.isHash))
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
 * Cookfs_FsindexList --
 *
 *      Lists entries in specified path of filesystem index.
 *
 *      Number of entries is put in itemCountPtr, if it is non-NULL
 *
 * Results:
 *      See Cookfs_FsindexListEntry.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Tcl_Obj *pathList, int *itemCountPtr) {

    CookfsLog(printf("Cookfs_FsindexList - start"))

    Cookfs_FsindexEntry *dirNode = CookfsFsindexFind(i, NULL, pathList,
        COOKFSFSINDEX_FIND_FIND, NULL);

    return Cookfs_FsindexListEntry(dirNode, itemCountPtr);

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexListFree --
 *
 *	Frees results from calls to Cookfs_FsindexList()
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
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
 *	Creates a new entry; specified space is allocated for filename
 *	and storing numBlocks blocks
 *
 *	If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, directory entry is
 *	created - it is used to store child elements, not block information
 *
 *	Otherwise a file entry is created - it allows storing up to 3*numBlocks
 *	integers for block-offset-size triplets that specify where file data
 *	is stored
 *
 * Results:
 *	Pointer to newly created entry
 *
 * Side effects:
 *	None
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
    e->isFileBlocksInitialized = NULL;
    if (numBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
	/* create directory structure - either a hash table or static child array */
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
	/* for files, block information is filled by caller */
    }

    //CookfsLog(printf("Cookfs_FsindexEntryAlloc: allocated entry %p", e));
    return e;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryFree --
 *
 *	Frees entry along with all child entries
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e) {
    //CookfsLog(printf("Cookfs_FsindexEntryFree: %p with fileBlocks [%d]", e, e->fileBlocks));
    if (e->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
	/* for directory, recursively free all children */
        Cookfs_FsindexEntry *itemNode;

	if (e->data.dirInfo.isHash) {
	    Tcl_HashSearch hashSearch;
	    Tcl_HashEntry *hashEntry;
	    /* iterate over hash table for all children */
	    hashEntry = Tcl_FirstHashEntry(&e->data.dirInfo.dirData.children, &hashSearch);
	    while (hashEntry != NULL) {
		itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
		hashEntry = Tcl_NextHashEntry(&hashSearch);
		Cookfs_FsindexEntryFree(itemNode);
	    }

	    /* clean hash table */
	    Tcl_DeleteHashTable(&e->data.dirInfo.dirData.children);
	}  else  {
	    int i;
	    /* iterate through children and free them */
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		if (e->data.dirInfo.dirData.childTable[i] != NULL) {
		    Cookfs_FsindexEntryFree(e->data.dirInfo.dirData.childTable[i]);
		}
	    }
	}
    } else if (e->isFileBlocksInitialized != NULL && e->fileBlocks > 0) {
	for (e->fileBlocks--; e->fileBlocks >= 0; e->fileBlocks--) {
	    //CookfsLog(printf("Cookfs_FsindexEntryFree: modify block#%d", e->fileBlocks));
	    Cookfs_FsindexModifyBlockUsage(e->isFileBlocksInitialized, e->data.fileInfo.fileBlockOffsetSize[e->fileBlocks * 3 + 0], -1);
	}
    }

    /* free entry structure itself */
    Tcl_Free((void *) e);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGetMetadata --
 *
 *	Gets object at current metadata; this should be binary data and
 *	is stored as series of bytes when (de)serializing
 *
 * Results:
 *	Tcl_Obj containing currently set data
 *	Tcl_IncrRefCount and Tcl_DecrRefCount should be used to handle
 *	reference counter for returned object
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_FsindexGetMetadata(Cookfs_Fsindex *i, const char *paramName) {
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_FindHashEntry(&i->metadataHash, paramName);
    if (hashEntry != NULL) {
	return (Tcl_Obj *) Tcl_GetHashValue(hashEntry);
    }  else  {
	return NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSetMetadata --
 *
 *	TODO
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Previously set value is removed if present; its reference counter
 *	is decremented, which might free it
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexSetMetadata(Cookfs_Fsindex *i, const char *paramName, Tcl_Obj *data) {
    int isNew;
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_CreateHashEntry(&i->metadataHash, paramName, &isNew);

    /* decrement reference count for old value, if set */
    if (!isNew) {
	Tcl_DecrRefCount((Tcl_Obj *) Tcl_GetHashValue(hashEntry));
    }
    Tcl_IncrRefCount(data);
    Tcl_SetHashValue(hashEntry, (ClientData) data);
    Cookfs_FsindexIncrChangeCount(i, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexUnsetMetadata --
 *
 *	TODO
 *
 * Results:
 *	Non-zero value if entry existed
 *
 * Side effects:
 *	Previously set value is removed if present; its reference counter
 *	is decremented, which might free it
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnsetMetadata(Cookfs_Fsindex *i, const char *paramName) {
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_FindHashEntry(&i->metadataHash, paramName);
    if (hashEntry != NULL) {
	Tcl_DecrRefCount((Tcl_Obj *) Tcl_GetHashValue(hashEntry));
	Tcl_DeleteHashEntry(hashEntry);
	Cookfs_FsindexIncrChangeCount(i, 1);
	return 1;
    }  else  {
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFindElement --
 *
 *	Finds entry using specified path list
 *
 *	pathList provides a path as list, separated by fs separator
 *	for example for path/to/file, the list would be {path to file}
 *	and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *	Pointrer to entry if found; otherwise NULL
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
Cookfs_FsindexEntry *CookfsFsindexFindElement(Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize) {
    int idx;
    Tcl_Obj *currentPath;
    Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *currentNode;
    Cookfs_FsindexEntry *nextNode;
    char *currentPathStr;

    /* start off with root item */
    currentNode = i->rootItem;

    CookfsLog(printf("Recursively finding %d path elemnets", listSize))

    /* iterate over each element in the list */
    for (idx = 0; idx < listSize; idx++) {
        CookfsLog(printf("Iterating at %s (%d); %d of %d", currentNode->fileName, currentNode->fileBlocks, idx, listSize))

	/* if current node is not a directory, return NULL */
        if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
            CookfsLog(printf("Parent is not a directory"))
            return NULL;
        }

	/* get current element in the list */
        CookfsLog(printf("Getting element"))
        if (Tcl_ListObjIndex(NULL, pathList, idx, &currentPath) != TCL_OK) {
            CookfsLog(printf("Unable to get element from a list"))
            return NULL;
        }
        CookfsLog(printf("Got element"))
        currentPathStr = Tcl_GetStringFromObj(currentPath, NULL);
        CookfsLog(printf("Looking for %s", currentPathStr))


	if (currentNode->data.dirInfo.isHash) {
	    /* if current entry is a hash table, locate child using Tcl_FindHashEntry () */
	    hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.dirData.children, currentPathStr);
	    if (hashEntry == NULL) {
		CookfsLog(printf("Unable to find item"))
		return NULL;
	    }
	    currentNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	}  else  {
	    /* if it is an array of children, iterate and find matching child */
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

	    /* check if entry was found - if not, return NULL */
	    currentNode = nextNode;
	    if (currentNode == NULL) {
		CookfsLog(printf("Unable to find item"))
		return NULL;
	    }
	}
    }

    return currentNode;
}

/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFind --
 *
 *	Multipurpose function for creating, finding and deleting entry
 *
 *	If dirPtr is not NULL, parent entry is stored in specified location
 *
 *	See CookfsFsindexFindInDirectory() for details
 *
 * Results:
 *	Entry or NULL; see CookfsFsindexFindInDirectory() for details
 *
 * Side effects:
 *	See CookfsFsindexFindInDirectory() for details
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

    /* find parent element */
    currentNode = CookfsFsindexFindElement(i, pathList, listSize - 1);

    /* if dirPtr was specified, store parent entry */
    if (dirPtr != NULL) {
	*dirPtr = currentNode;
    }

    /* if parent was not found or is not a directory, return NULL */
    if (currentNode == NULL) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - node not found"))
        return NULL;
    }

    if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - not a directory"))
        return NULL;
    }

    /* get information about fail of the file name
     * and invoke CookfsFsindexFindInDirectory() */
    if (Tcl_ListObjIndex(NULL, pathList, listSize - 1, &pathTail) != TCL_OK) {
        CookfsLog(printf("CookfsFsindexCreateHashElement - Unable to get element"))
        return NULL;
    }

    pathTailStr = Tcl_GetStringFromObj(pathTail, NULL);
    CookfsLog(printf("CookfsFsindexCreateHashElement - Path tail: %s", pathTailStr))

    Cookfs_FsindexEntry *rc = CookfsFsindexFindInDirectory(currentNode, pathTailStr, command, newFileNode);
    if (command != COOKFSFSINDEX_FIND_FIND && rc != NULL) {
        Cookfs_FsindexIncrChangeCount(i, 1);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFindInDirectory --
 *
 *	Multipurpose function for creating, finding and deleting entry
 *	in specified directory entry
 *
 *	NOTE: It is created as single function since most operations and
 *	handling of hash vs static array are the same for all 3 commands
 *
 *	Commands:
 *	- COOKFSFSINDEX_FIND_FIND - finds specified entry,
 *	  return NULL if not found; newFileNode is ignored
 *
 *	- COOKFSFSINDEX_FIND_CREATE - finds specified entry, creates if
 *	  it does not exist; if entry did not exist, it is set to newFileNode;
 *	  if it existed, newFileNode is put in place of old entry and previous
 *	  entry is freed; if it is not possible to set entry, newFileNode is freed
 *	  and NULL is returned
 *
 *	- COOKFSFSINDEX_FIND_DELETE - deletes specified entry; if entry previously
 *	  existed, it is returned (after being freed - it is only returned for )
 *
 *	- COOKFSFSINDEX_FIND_DELETE_RECURSIVE - deletes specified entry and all
 *	  child entries; if entry previously existed, it is returned (after being
 *	  freed - it is only returned for )
 *
 * Results:
 *	Dependant on command:
 *	- COOKFSFSINDEX_FIND_FIND - found entry or NULL
 *
 *	- COOKFSFSINDEX_FIND_CREATE - if successful, returns newFileNode;
 *	   otherwise returns NULL
 *
 *	- COOKFSFSINDEX_FIND_DELETE - entry if existed, NULL otherwise
 *
 *	- COOKFSFSINDEX_FIND_DELETE_RECURSIVE - entry if existed, NULL otherwise
 *
 * Side effects:
 *	Can convert parent entry (currentNode) from array of children into
 *	a hash table - it is not permitted to depend on previous value of
 *	currentNode->isHash after calling this function
 *
 *----------------------------------------------------------------------
 */

static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_FsindexEntry *currentNode, char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode) {
    /* the main iteration occurs until the process has completed
     * usually either entry or NULL is returned in first iteration,
     * however if static array is converted into a hash table,
     * second iteration occurs in order to actually perform operation */
    while (1) {
	if (currentNode->data.dirInfo.isHash) {
	    /* currentNode is using hash table */
	    Cookfs_FsindexEntry *fileNode = NULL;
	    Tcl_HashEntry *hashEntry;
	    int isNew;

	    /* depending on whether we are willing to create new entry, call proper hash function */
	    if (command == COOKFSFSINDEX_FIND_CREATE) {
		hashEntry = Tcl_CreateHashEntry(&currentNode->data.dirInfo.dirData.children, pathTailStr, &isNew);
	    }  else  {
		hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.dirData.children, pathTailStr);
	    }

	    /* if entry has been found/created */
	    if (hashEntry != NULL) {
		CookfsLog(printf("CookfsFsindexFindInDirectory - found in hash table (isNew=%d; cmd=%d)", isNew, command))
		/* if entry exists or has just been created */
		fileNode = Tcl_GetHashValue(hashEntry);

		CookfsLog(printf("CookfsFsindexFindInDirectory - fileNode=%p", (void *)fileNode))

		/* if we are to create a new entry, set new value */
		if (command == COOKFSFSINDEX_FIND_CREATE) {
		    if (!isNew) {
			/* if entry exists already, check if both are of same type */
			if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
			    || ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
			    /* if entry type does not match, free newFileNode and return NULL */
			    CookfsLog(printf("CookfsFsindexFindInDirectory - type mismatch"))
			    Cookfs_FsindexEntryFree(newFileNode);
			    return NULL;
			}

			/* if type matches, free previous node */
			Cookfs_FsindexEntryFree(fileNode);
		    }  else  {
			/* if it is a new entry, increment counter */
			currentNode->data.dirInfo.childCount++;
		    }

		    /* set new value for specified hash table entry */
		    CookfsLog(printf("CookfsFsindexFindInDirectory - setting as newFileNode = %p", (void *)newFileNode))
		    Tcl_SetHashValue(hashEntry, newFileNode);
		    CookfsLog(printf("CookfsFsindexFindInDirectory - setting as newFileNode done"))
		    return newFileNode;
		}  else if ((command == COOKFSFSINDEX_FIND_DELETE) || (command == COOKFSFSINDEX_FIND_DELETE_RECURSIVE)) {
		    /* if deleting current node, check if it can be deleted */
		    if ((command == COOKFSFSINDEX_FIND_DELETE)
		        && (fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY)
			&& (fileNode->data.dirInfo.childCount > 0)) {
			/* if it is a non-empty directory, disallow unsetting it */
			return NULL;
		    }

		    /* decrement childCount, free the entry */
		    currentNode->data.dirInfo.childCount--;
		    Tcl_DeleteHashEntry(hashEntry);
		    Cookfs_FsindexEntryFree(fileNode);
		}

		/* return node, if found */
		return fileNode;
	    }
	}  else  {
	    /* if currentNode is not using hash table */
	    Cookfs_FsindexEntry *fileNode = NULL;
	    Cookfs_FsindexEntry **fileNodePtr = NULL;
	    int i;

	    /* iterate over children array and find matching node */
	    CookfsLog(printf("CookfsFsindexFindInDirectory - looking in childTable"))
	    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
		CookfsLog(printf("CookfsFsindexFindInDirectory - checking %d", i))
		if ((currentNode->data.dirInfo.dirData.childTable[i] != NULL) && (strcmp(currentNode->data.dirInfo.dirData.childTable[i]->fileName, pathTailStr) == 0)) {
		    fileNode = currentNode->data.dirInfo.dirData.childTable[i];
		    fileNodePtr = &currentNode->data.dirInfo.dirData.childTable[i];
		    CookfsLog(printf("CookfsFsindexFindInDirectory - found at %d", i))
		    break;
		}

	    }

	    /* if found, perform specified command */
	    if (fileNode != NULL) {
		CookfsLog(printf("CookfsFsindexFindInDirectory - found in table cmd=%d", command))
		if ((command == COOKFSFSINDEX_FIND_DELETE) || (command == COOKFSFSINDEX_FIND_DELETE_RECURSIVE)) {
		    /* if deleting current node, check if it can be deleted */
		    if ((command == COOKFSFSINDEX_FIND_DELETE)
		        && (fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY)
			&& (fileNode->data.dirInfo.childCount > 0)) {
			/* if it is a non-empty directory, disallow unsetting it */
			return NULL;
		    }

		    /* free current node, set this element of the array to NULL */
		    currentNode->data.dirInfo.childCount--;
		    Cookfs_FsindexEntryFree(fileNode);
		    *fileNodePtr = NULL;
		    CookfsLog(printf("CookfsFsindexFindInDirectory - deleted"))
		}  else if (command == COOKFSFSINDEX_FIND_CREATE) {
		    /* if entry exists already, check if both are of same type */
		    CookfsLog(printf("CookfsFsindexFindInDirectory - updating..."))
		    if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
			|| ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
			/* if entry type does not match, free newFileNode and return NULL */
			CookfsLog(printf("CookfsFsindexFindInDirectory - update failed - type mismatch"))
			Cookfs_FsindexEntryFree(newFileNode);
			return NULL;
		    }

		    /* if types match, overwrite entry with new value */
		    CookfsLog(printf("CookfsFsindexFindInDirectory - updated"))
		    Cookfs_FsindexEntryFree(fileNode);
		    *fileNodePtr = newFileNode;
		    return newFileNode;
		}
		return fileNode;
	    }  else  {
		/* if entry was not found */
		if (command == COOKFSFSINDEX_FIND_CREATE) {
		    /* for create operation, add new child to static array */
		    CookfsLog(printf("CookfsFsindexFindInDirectory - creating (%d)", currentNode->data.dirInfo.childCount))
		    if (currentNode->data.dirInfo.childCount >= (COOKFS_FSINDEX_TABLE_MAXENTRIES - 1)) {
			/* if we ran out of entries in the array, convert to hash table and try again */
			CookfsLog(printf("CookfsFsindexFindInDirectory - converting to hash"))
			CookfsFsindexChildtableToHash(currentNode);
			continue;
		    }  else  {
			/* if we have any spots available, update first free one */
			for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
			    CookfsLog(printf("CookfsFsindexFindInDirectory - create - checking %d", i))
			    if (currentNode->data.dirInfo.dirData.childTable[i] == NULL) {
				CookfsLog(printf("CookfsFsindexFindInDirectory - create - adding at %d", i))
				currentNode->data.dirInfo.dirData.childTable[i] = newFileNode;
				break;
			    }
			}
			currentNode->data.dirInfo.childCount++;
		    }
		    /* return newFileNode */
		    return newFileNode;
		}  else  {
		    /* entry was not found, return NULL */
		    return NULL;
		}
	    }
	}

	/* unless we did continue somewhere above, return NULL */
	return NULL;
    }
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

    /* copy previous values to temporary table and initialize hash table for storage */
    memcpy(childTable, e->data.dirInfo.dirData.childTable, sizeof(childTable));

    e->data.dirInfo.isHash = 1;
    Tcl_InitHashTable(&e->data.dirInfo.dirData.children, TCL_STRING_KEYS);

    /* copy old entries to hash table */
    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
	if (childTable[i] != NULL) {
	    CookfsLog(printf("CookfsFsindexChildtableToHash - copying %s", childTable[i]->fileName))
	    hashEntry = Tcl_CreateHashEntry(&e->data.dirInfo.dirData.children, childTable[i]->fileName, &isNew);

	    Tcl_SetHashValue(hashEntry, childTable[i]);
	}
    }

    CookfsLog(printf("CookfsFsindexChildtableToHash: FINISHED"))
}


