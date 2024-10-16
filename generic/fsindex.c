/*
 * fsindex.c
 *
 * Provides functions for managing filesystem indexes
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "fsindex.h"
#include "fsindexInt.h"

#define COOKFSFSINDEX_FIND_FIND             0
#define COOKFSFSINDEX_FIND_CREATE           1
#define COOKFSFSINDEX_FIND_DELETE           2
#define COOKFSFSINDEX_FIND_DELETE_RECURSIVE 3

const char *filesetMetadataKey = "cookfs.fileset";
const char *fileset_platform = COOKFS_PLATFORM;
const char *fileset_tcl_version = "tcl" STRINGIFY(TCL_MAJOR_VERSION)
    STRINGIFY(TCL_MINOR_VERSION);
const char *fileset_auto = COOKFS_PLATFORM ".tcl" STRINGIFY(TCL_MAJOR_VERSION)
    STRINGIFY(TCL_MINOR_VERSION);


// The 'custom' option should be the latest in the list. It will be used in
// Cookfs_FsindexFileSetSet() if nothing else is found.
static const struct {
    const char *option;
    Cookfs_FsindexFileSetType type;
} cookfs_fileset_options[] = {
    { "auto",        COOKFS_FSINDEX_FILESET_AUTO        },
    { "tcl_version", COOKFS_FSINDEX_FILESET_TCL_VERSION },
    { "platform",    COOKFS_FSINDEX_FILESET_PLATFORM    },
    { "custom",      COOKFS_FSINDEX_FILESET_CUSTOM      },
    { .option = NULL }
};

/* declarations of static and/or internal functions */
static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Cookfs_PathObj *pathObj, int command, Cookfs_FsindexEntry *newFileNode);
static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_FsindexEntry *currentNode, const char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode);
static void CookfsFsindexChildtableToHash(Cookfs_FsindexEntry *e);
static void Cookfs_FsindexFree(Cookfs_Fsindex *i);
static Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(Cookfs_Fsindex *fsindex, int fileNameLength, int numBlocks, int useHash);
static void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e);

int Cookfs_FsindexLockRW(int isWrite, Cookfs_Fsindex *i, Tcl_Obj **err) {
    int ret = 1;
#ifdef TCL_THREADS
    CookfsLog(printf("try to %s lock...", isWrite ? "WRITE" : "READ"));
    if (isWrite) {
        ret = Cookfs_RWMutexLockWrite(i->mx);
    } else {
        ret = Cookfs_RWMutexLockRead(i->mx);
    }
    if (ret && i->isDead == 1) {
        // If object is terminated, don't allow everything.
        ret = 0;
        Cookfs_RWMutexUnlock(i->mx);
    }
    if (!ret) {
        CookfsLog(printf("FAILED to %s lock", isWrite ? "WRITE" : "READ"));
        if (err != NULL) {
            *err = Tcl_NewStringObj("stalled fsindex object detected", -1);
        }
    } else {
        CookfsLog(printf("ok - %s lock (%d)", isWrite ? "WRITE" : "READ",
            Cookfs_RWMutexGetLocks(i->mx)));
    }
#else
    UNUSED(isWrite);
    UNUSED(i);
    UNUSED(err);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_FsindexUnlock(Cookfs_Fsindex *i) {
#ifdef TCL_THREADS
    Cookfs_RWMutexUnlock(i->mx);
    CookfsLog(printf("ok (%d)", Cookfs_RWMutexGetLocks(i->mx)));
#else
    UNUSED(i);
#endif /* TCL_THREADS */
    return 1;
}

int Cookfs_FsindexLockHard(Cookfs_Fsindex *i) {
    i->lockHard = 1;
    return 1;
}

int Cookfs_FsindexUnlockHard(Cookfs_Fsindex *i) {
    i->lockHard = 0;
    return 1;
}

int Cookfs_FsindexLockSoft(Cookfs_Fsindex *i) {
    int ret = 1;
#ifdef TCL_THREADS
    Tcl_MutexLock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    if (i->isDead) {
        ret = 0;
    } else {
        i->lockSoft++;
    }
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_FsindexUnlockSoft(Cookfs_Fsindex *i) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    assert(i->lockSoft > 0);
    i->lockSoft--;
    if (i->isDead == 1) {
        Cookfs_FsindexFree(i);
    } else {
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    }
    return 1;
}

void Cookfs_FsindexLockExclusive(Cookfs_Fsindex *i) {
#ifdef TCL_THREADS
    CookfsLog(printf("try to lock exclusive..."));
    Cookfs_RWMutexLockExclusive(i->mx);
    CookfsLog(printf("ok"));
#else
    UNUSED(i);
#endif /* TCL_THREADS */
}

Tcl_WideInt Cookfs_FsindexEntryGetFilesize(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntryWantRead(e);
    return e->data.fileInfo.fileSize;
}

int Cookfs_FsindexEntryGetBlockCount(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntryWantRead(e);
    return e->fileBlocks;
}

int Cookfs_FsindexEntryLock(Cookfs_FsindexEntry *e) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&e->mxRefCount);
#endif /* TCL_THREADS */
    e->refcount++;
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&e->mxRefCount);
#endif /* TCL_THREADS */
    return 1;
}

int Cookfs_FsindexEntryUnlock(Cookfs_FsindexEntry *e) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&e->mxRefCount);
#endif /* TCL_THREADS */
    e->refcount--;
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&e->mxRefCount);
#endif /* TCL_THREADS */
    // If refcount < 0, there is an Unlock() without a corresponding Lock().
    // Treat this as an error.
    assert(e->refcount >= 0);
    return 1;
}

Tcl_Obj *Cookfs_FsindexFilesetListObj(Cookfs_Fsindex *fsIndex) {

    Tcl_Obj *result = NULL;

    CookfsLog(printf("enter..."));

    if (!Cookfs_FsindexLockRead(fsIndex, NULL)) {
        result = Tcl_NewObj();
        goto done;
    }

    const char *fileset_active = Cookfs_FsindexFileSetGetActive(fsIndex);

    if (fileset_active == NULL) {
        result = Tcl_NewObj();
        goto unlock;
    }

    result = Tcl_NewListObj(0, NULL);

    Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(fileset_active,
        -1));

    int count;
    Cookfs_FsindexEntry **entry_list = Cookfs_FsindexListEntry(
        fsIndex->rootItem, &count);

    for (int idx = 0; idx < count; idx++) {

        unsigned char length;
        const char *filename = Cookfs_FsindexEntryGetFileName(entry_list[idx],
            &length);

        if (strcmp(filename, fileset_active) == 0) {
            continue;
        }

        Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(filename,
            length));

    }

    Cookfs_FsindexListFree(entry_list);

unlock:

    Cookfs_FsindexUnlock(fsIndex);

done:

    CookfsLog(printf("return: [%s]", Tcl_GetString(result)));
    return result;

}

int Cookfs_FsindexHasFileset(Cookfs_Fsindex *i) {
    return (i->rootItem != i->rootItemVirtual);
}

const char *Cookfs_FsindexFileSetGetActive(Cookfs_Fsindex *i) {
    Cookfs_FsindexWantRead(i);
    if (i->rootItem == i->rootItemVirtual) {
        return NULL;
    }
    return i->rootItemVirtual->fileName;
}

static Cookfs_FsindexFileSetType Cookfs_FsindexFileSetGetType(Cookfs_Fsindex *i) {

    Cookfs_FsindexWantRead(i);

    Tcl_Obj *typeObj = Cookfs_FsindexGetMetadata(i, filesetMetadataKey);

    if (typeObj == NULL) {
        CookfsLog(printf("return: [none]"));
        return COOKFS_FSINDEX_FILESET_NONE;
    }

    Cookfs_FsindexFileSetType type;
    const char *typeStr = Tcl_GetString(typeObj);

    int idx = 0;
    for (const char **option = (const char **)cookfs_fileset_options;;
        option = (const char **)((char *)option +
        sizeof(cookfs_fileset_options[0])),
            idx++)
    {
        if (*option == NULL) {
            type = COOKFS_FSINDEX_FILESET_CUSTOM;
            assert(0 && "Cookfs_FsindexFileSetGetType(): unknown fileset type in metadata");
        } else if (strcmp(*option, typeStr) == 0) {
            type = cookfs_fileset_options[idx].type;
        } else {
            continue;
        }
        break;
    }

    Tcl_BounceRefCount(typeObj);

    CookfsLog(printf("return: [%s]",
        type == COOKFS_FSINDEX_FILESET_NONE ? "none" :
        type == COOKFS_FSINDEX_FILESET_AUTO ? "auto" :
        type == COOKFS_FSINDEX_FILESET_TCL_VERSION ? "tcl_version" :
        type == COOKFS_FSINDEX_FILESET_PLATFORM ? "platform" :
        type == COOKFS_FSINDEX_FILESET_CUSTOM ? "custom" :
        "UNKNOWN"));

    return type;
}

static Cookfs_FsindexFileSetType Cookfs_FsindexFileSetLookupType(const char *typeStr,
    const char **option_str_ptr)
{

    CookfsLog(printf("type: [%s]", typeStr));

    Cookfs_FsindexFileSetType type;
    const char *option_str = NULL;
    int idx = 0;
    for (const char **option = (const char **)cookfs_fileset_options;;
        option = (const char **)((char *)option +
        sizeof(cookfs_fileset_options[0])),
            idx++)
    {
        CookfsLog(printf("check option: #%d [%s]", idx, *option));
        if (*option == NULL) {
            type = COOKFS_FSINDEX_FILESET_CUSTOM;
        } else {
            option_str = *option;
            if (strcmp(*option, typeStr) == 0) {
                type = cookfs_fileset_options[idx].type;
            } else {
                continue;
            }
        }
        break;
    }

    CookfsLog(printf("normalized type: [%s]",
        type == COOKFS_FSINDEX_FILESET_AUTO ? "auto" :
        type == COOKFS_FSINDEX_FILESET_TCL_VERSION ? "tcl_version" :
        type == COOKFS_FSINDEX_FILESET_PLATFORM ? "platform" :
        type == COOKFS_FSINDEX_FILESET_CUSTOM ? "custom" :
        "UNKNOWN"));

    *option_str_ptr = option_str;
    return type;
}

static int Cookfs_FsindexFileSetCreateAndSet(Cookfs_Fsindex *i,
    const char *fileset_node_name, Tcl_Obj **err)
{

    CookfsLog(printf("create new entry: [%s]", fileset_node_name));

    int length = strlen(fileset_node_name);

    Cookfs_FsindexEntry *fileset_node = Cookfs_FsindexSetInDirectory(i->rootItem,
        fileset_node_name, length, COOKFS_NUMBLOCKS_DIRECTORY);

    if (fileset_node == NULL) {
        CookfsLog(printf("return: ERROR (unable to create entry)"));
        SET_ERROR(Tcl_ObjPrintf("unable to create fileset with"
            " the specified name '%s'", fileset_node_name));
        return TCL_ERROR;
    }

    i->rootItemVirtual = fileset_node;

    return TCL_OK;

}

static Cookfs_FsindexEntry *Cookfs_FsindexFileSetLookup(Cookfs_Fsindex *i,
    const char *fileset_node_name)
{

    Cookfs_FsindexEntry *e = NULL;

    if (fileset_node_name == NULL) {
        CookfsLog(printf("lookup for fileset: <first available>"));
    } else {
        CookfsLog(printf("lookup for fileset: [%s]", fileset_node_name));
    }

    int count;
    Cookfs_FsindexEntry **entry_list = Cookfs_FsindexListEntry(i->rootItem,
        &count);
    assert(entry_list != NULL && "Cookfs_FsindexFileSetLookup():"
        " entry_list is NULL");

    if (fileset_node_name == NULL) {

        assert(count > 0 && "Cookfs_FsindexFileSetSelect(): count == 0,"
            " no fileset found");

        e = entry_list[0];

    } else {

        CookfsLog(printf("have %d entries in the root", count));

        for (int idx = 0; idx < count; idx++) {
            CookfsLog(printf("check entry: [%s]", entry_list[idx]->fileName));
            if (strcmp(entry_list[idx]->fileName, fileset_node_name) == 0) {
                CookfsLog(printf("required fileset has been found,"
                    " set it and return"));
                e = entry_list[idx];
                break;
            }
        }

    }

    Cookfs_FsindexListFree(entry_list);
    CookfsLog(printf("return fileset: [%s]", e == NULL ? "<NULL>" : e->fileName));
    return e;

}

int Cookfs_FsindexFileSetSelect(Cookfs_Fsindex *i, const char *typeStr,
    int readonly, Tcl_Obj **err)
{

    Cookfs_FsindexWantWrite(i);
    CookfsLog(printf("type: [%s]", typeStr == NULL ? "<NULL>" : typeStr));

    Cookfs_FsindexFileSetType type;
    const char *option_str = NULL;

    // Now let's check if fsindex is a newly created index or if it already
    // contains files. The result of setting fileset will depend heavily
    // on this fact.
    if (i->rootItem->data.dirInfo.childCount == 0) {

        // If we are here, then we have a newly created index
        CookfsLog(printf("fsindex is a newly created"));

        // If we don't want to set any fileset type, then we have nothing
        // to do.
        if (typeStr == NULL) {
            return TCL_OK;
        }

        if (readonly) {
            CookfsLog(printf("return: ERROR (readonly on empty fsindex)"));
            SET_ERROR(Tcl_NewStringObj("unable to create fileset in readonly"
                " fsindex", -1));
            return TCL_ERROR;
        }

        type = Cookfs_FsindexFileSetLookupType(typeStr, &option_str);

        const char *fileset_node_name = NULL;
        switch (type) {
        case COOKFS_FSINDEX_FILESET_NONE:
            // This case should be impossible as we don't have 'none' in
            // cookfs_fileset_options struct;
            assert(0 && "Cookfs_FsindexFileSetSet(): got 'none' fileset type");
            break;
        case COOKFS_FSINDEX_FILESET_AUTO:
            fileset_node_name = fileset_auto;
            break;
        case COOKFS_FSINDEX_FILESET_TCL_VERSION:
            fileset_node_name = fileset_tcl_version;
            break;
        case COOKFS_FSINDEX_FILESET_PLATFORM:
            fileset_node_name = fileset_platform;
            break;
        case COOKFS_FSINDEX_FILESET_CUSTOM:
            fileset_node_name = typeStr;
            break;
        }

        CookfsLog(printf("create new entry: [%s]", fileset_node_name));

        if (Cookfs_FsindexFileSetCreateAndSet(i, fileset_node_name, err)
            != TCL_OK)
        {
            return TCL_ERROR;
        }

        CookfsLog(printf("set metadata: [%s] = [%s]", filesetMetadataKey,
            option_str));

        Cookfs_FsindexSetMetadataRaw(i, filesetMetadataKey,
            (const unsigned char *)option_str, strlen(option_str));

        CookfsLog(printf("return: ok"));

        return TCL_OK;

    }

    // If we are here, then we have not a newly created index. It already
    // contains something.
    CookfsLog(printf("fsindex is NOT a newly created"));

    const char *fileset_node_name = NULL;
    // Check current fsindex fileset type
    type = Cookfs_FsindexFileSetGetType(i);

    // If typeStr is not specified, we want to select a set of files
    // automatically.
    if (typeStr == NULL) {

        CookfsLog(printf("typeStr is NULL, we have to select fileset"
            " automatically"));

        switch (type) {

        case COOKFS_FSINDEX_FILESET_NONE:

            CookfsLog(printf("fsindex has no fileset, there is nothing"
                " to do"));

            return TCL_OK;

        case COOKFS_FSINDEX_FILESET_CUSTOM:

            CookfsLog(printf("fsindex has custom fileset type, select"
                " the first available fileset"));

            i->rootItemVirtual = Cookfs_FsindexFileSetLookup(i, NULL);

            CookfsLog(printf("activate custom fileset: [%s]",
                i->rootItemVirtual->fileName));

            return TCL_OK;

        case COOKFS_FSINDEX_FILESET_AUTO:
            fileset_node_name = fileset_auto;
            break;
        case COOKFS_FSINDEX_FILESET_TCL_VERSION:
            fileset_node_name = fileset_tcl_version;
            break;
        case COOKFS_FSINDEX_FILESET_PLATFORM:
            fileset_node_name = fileset_platform;
            break;
        }

        // If we are here, then we have automatic fileset based on Tcl version,
        // platform, or both. If we are in readonly mode, then try to
        // select fileset based on current conditions. If we are in writable
        // mode, then we will try to select existing fileset based on current
        // conditions, and we don't have such fileset yet, then we will create
        // a new one.

        CookfsLog(printf("want to set auto-fileset: [%s]", fileset_node_name));

    } else {

        if (type == COOKFS_FSINDEX_FILESET_NONE) {
            CookfsLog(printf("return: ERROR (unable to set fileset on"
                " non-empty fsindex)"));
            SET_ERROR(Tcl_NewStringObj("cannot set a fileset to a non-empty"
                " fsindex without an initialized fileset", -1));
            return TCL_ERROR;
        }

        CookfsLog(printf("typeStr is defined, we have to select fileset:"
            " [%s]", typeStr));

        fileset_node_name = typeStr;

    }

    Cookfs_FsindexEntry *e = Cookfs_FsindexFileSetLookup(i, fileset_node_name);

    if (e != NULL) {
        CookfsLog(printf("required fileset has been found, set it and"
            " return"));
        i->rootItemVirtual = e;
        return TCL_OK;
    }

    // If we are here, then we don't have the required fileset in fsindex.
    // If we are in readonly mode, consider this as an error.
    if (readonly) {
        CookfsLog(printf("return: ERROR (unable to find the fileset and"
            " unable to create it in RO mode)"));
        SET_ERROR(Tcl_ObjPrintf("VFS does not have the required fileset"
            " \"%s\", it cannot be created due to read-only mode",
            fileset_node_name));
        return TCL_ERROR;
    }

    if (Cookfs_FsindexFileSetCreateAndSet(i, fileset_node_name, err)
        != TCL_OK)
    {
        CookfsLog(printf("return: ERROR"));
        return TCL_ERROR;
    }

    CookfsLog(printf("return: ok"));
    return TCL_OK;

}

int Cookfs_FsindexEntryGetBlock(Cookfs_FsindexEntry *e, int blockNumber,
    int *pageNum, int *pageOffset,int *pageSize)
{
    Cookfs_FsindexEntryWantRead(e);
    int blockIndexOffset = blockNumber * 3;
    if (pageNum != NULL) {
        *pageNum = e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 0];
    }
    if (pageOffset != NULL) {
        *pageOffset = e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 1];
    }
    if (pageSize != NULL) {
        *pageSize = e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 2];
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntrySetFileSize --
 *
 *      Updates the filesize field for specified entry
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexEntrySetFileSize(Cookfs_FsindexEntry *e,
    Tcl_WideInt fileSize)
{
    Cookfs_FsindexEntryWantWrite(e);
    e->data.fileInfo.fileSize = fileSize;
    return;
}

void Cookfs_FsindexEntrySetFileTime(Cookfs_FsindexEntry *e,
    Tcl_WideInt fileTime)
{
    Cookfs_FsindexEntryWantWrite(e);
    e->fileTime = fileTime;
    return;
}

Tcl_WideInt Cookfs_FsindexEntryGetFileTime(Cookfs_FsindexEntry *e)
{
    Cookfs_FsindexEntryWantRead(e);
    return e->fileTime;
}

const char *Cookfs_FsindexEntryGetFileName(Cookfs_FsindexEntry *e,
    unsigned char *fileNameLen)
{
    Cookfs_FsindexEntryWantRead(e);
    if (fileNameLen != NULL) {
        *fileNameLen = e->fileNameLen;
    }
    return e->fileName;
}

void Cookfs_FsindexEntryIncrBlockPageIndex(Cookfs_FsindexEntry *e,
    int blockNumber, int change)
{
    Cookfs_FsindexEntryWantWrite(e);
    int blockIndexOffset = blockNumber * 3 + 0;
    e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 0] += change;
    return;
}

int Cookfs_FsindexEntryIsInactive(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntryWantRead(e);
    return e->isInactive;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntrySetBlock --
 *
 *      Updates the block data for specified entry
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexEntrySetBlock(Cookfs_FsindexEntry *e, int blockNumber,
    int pageIndex, int pageOffset, int pageSize)
{
    Cookfs_Fsindex *i = e->fsindex;
    Cookfs_FsindexWantWrite(i);
    int blockIndexOffset = blockNumber * 3 + 0;

    // Reducing block utilization for an already set block index
    Cookfs_FsindexModifyBlockUsage(i,
        e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 0], -1);

    e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 0] = pageIndex;
    e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 1] = pageOffset;
    if (pageSize >= 0) {
        e->data.fileInfo.fileBlockOffsetSize[blockIndexOffset + 2] = pageSize;
    }
    // Increase block utilization for the new block index
    Cookfs_FsindexModifyBlockUsage(i, pageIndex, 1);
    // Register new change in the change counter
    Cookfs_FsindexIncrChangeCount(i, 1);
    return ;
}

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
    Cookfs_FsindexEntryWantRead(e);
    if (Cookfs_FsindexEntryIsDirectory(e)) {
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
    Cookfs_FsindexEntryWantRead(e);
    return ((e != NULL && e->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) ? 1 : 0);
}

int Cookfs_FsindexEntryIsEmptyDirectory(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntryWantRead(e);
    return (e->data.dirInfo.childCount ? 1 : 0);
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
    Cookfs_FsindexWantWrite(i);
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
    Cookfs_FsindexWantWrite(i);
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
    CookfsLog(printf("from [%p] index [%d]", (void *)i, idx));
    Cookfs_FsindexWantRead(i);
    if (idx < 0 || i->blockIndexSize <= idx) {
        return 0;
    }
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

    Cookfs_FsindexWantWrite(i);

    // if blockIndexSize is not zero and blockIndex is NULL then we are
    // in terminate mode so we just return
    if (i->blockIndexSize && !i->blockIndex) {
        return;
    }

    // if the index < 0, then the file has not used any blocks yet so
    // we just return
    if (idx < 0) {
        return;
    }

    // Ignore aside page numbers
    if (COOKFS_PAGES_ISASIDE(idx)) {
        CookfsLog(printf("WARNING: called with aside page number"));
        return;
    }

    CookfsLog(printf("increase block index [%d] by [%d]", idx, count));
    // check if we have enough space in the block index
    if (i->blockIndexSize <= idx) {
        // Increase blockIndexSize by idx+100 to reduce memory reallocations
        // when filling the index and modifying block statistics
        // block by block.
        int maxBlockIndexSize = idx + 100;
        CookfsLog(printf("expand block index buffer from [%d] to [%d]",
            i->blockIndexSize, maxBlockIndexSize));
        // expand our block index
        i->blockIndex = (int *)ckrealloc(i->blockIndex,
            sizeof(int) * maxBlockIndexSize);
        // initialize newly allocated block indexes with zeros
        for (; i->blockIndexSize < maxBlockIndexSize; i->blockIndexSize++) {
            i->blockIndex[i->blockIndexSize] = 0;
        }
    } else {
        CookfsLog(printf("current value is [%d]", i->blockIndex[idx]));
    }

    i->blockIndex[idx] += count;
    CookfsLog(printf("new value is [%d]", i->blockIndex[idx]));

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGetHandle --
 *
 *      Returns fsindex handle from provided Tcl command name
 *
 * Results:
 *      Pointer to Cookfs_Fsindex or NULL in case of failure
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;

    /* TODO: verify command suffix etc */

    CookfsLog(printf("get handle from cmd [%s]", cmdName));
    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
        return NULL;
    }

    CookfsLog(printf("return [%p]", cmdInfo.objClientData));
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

Cookfs_Fsindex *Cookfs_FsindexInit(Tcl_Interp *interp, Cookfs_Fsindex *i) {
    Cookfs_Fsindex *rc;
    if (i == NULL) {
        rc = (Cookfs_Fsindex *) ckalloc(sizeof(Cookfs_Fsindex));
        rc->commandToken = NULL;
        rc->interp = interp;
        rc->isDead = 0;
        rc->lockSoft = 0;
        rc->lockHard = 0;
        rc->inactiveItems = NULL;
#ifdef TCL_THREADS
        /* initialize thread locks */
        rc->mx = Cookfs_RWMutexInit();
        rc->threadId = Tcl_GetCurrentThread();
        rc->mxLockSoft = NULL;
#endif /* TCL_THREADS */
    } else {
        rc = i;
    }
    rc->rootItem = Cookfs_FsindexEntryAlloc(rc, 0, COOKFS_NUMBLOCKS_DIRECTORY, COOKFS_USEHASH_DEFAULT);
    rc->rootItem->fileName = ".";
    rc->rootItemVirtual = rc->rootItem;
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
 *      Clears entire fsindex along with its child elements
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
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
        ckfree(Tcl_GetHashValue(hashEntry));
        Tcl_DeleteHashEntry(hashEntry);
    }
    Tcl_DeleteHashTable(&i->metadataHash);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexFini --
 *
 *      Frees entire fsindex along with its child elements
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static void Cookfs_FsindexFree(Cookfs_Fsindex *i) {
    CookfsLog(printf("Cleaning up fsindex"));
    CookfsLog(printf("Cleaning up inactive items"));
    while (i->inactiveItems != NULL) {
        Cookfs_FsindexEntry *e = i->inactiveItems;
        i->inactiveItems = e->next;
        CookfsLog(printf("release inactive entry %p",
            (void *)e));
#ifdef TCL_THREADS
        Tcl_MutexFinalize(&e->mxRefCount);
#endif /* TCL_THREADS */
        ckfree((void *) e);
    }
#ifdef TCL_THREADS
    CookfsLog(printf("Cleaning up thread locks"));
    Cookfs_RWMutexFini(i->mx);
    Tcl_MutexUnlock(&i->mxLockSoft);
    Tcl_MutexFinalize(&i->mxLockSoft);
#endif /* TCL_THREADS */
    /* clean up storage */
    CookfsLog(printf("Releasing fsindex"));
    ckfree((void *) i);
}

void Cookfs_FsindexFini(Cookfs_Fsindex *i) {
    if (i->isDead == 1) {
        return;
    }
    if (i->lockHard) {
        CookfsLog(printf("could not remove locked object"));
        return;
    }

    Cookfs_FsindexLockExclusive(i);

    CookfsLog(printf("aquire mutex"));
    // By acquisition the lockSoft mutex, we will be sure that no other
    // thread calls Cookfs_FsindexUnlockSoft() that can release this object
    // while this function is running.
#ifdef TCL_THREADS
    Tcl_MutexLock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    i->isDead = 1;

    Cookfs_FsindexCleanup(i);

    CookfsLog(printf("release"));
    if (i->commandToken != NULL) {
        CookfsLog(printf("Cleaning tcl command"));
        Tcl_DeleteCommandFromToken(i->interp, i->commandToken);
    } else {
        CookfsLog(printf("No tcl command"));
    }

    // Unlock fsindex now. It is possible that some threads are waiting for
    // read/write events. Let them go on and fail because of a dead object.
    Cookfs_FsindexUnlock(i);

    if (i->lockSoft) {
        CookfsLog(printf("The fsindex object is soft-locked"))
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&i->mxLockSoft);
#endif /* TCL_THREADS */
    } else {
        Cookfs_FsindexFree(i);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGet --
 *
 *      Retrieves information about specified element in specified
 *      fsindex instance
 *
 *      pathList provides a path as list, separated by fs separator
 *      for example for path/to/file, the list would be {path to file}
 *      and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *      Entry if successful, NULL otherwise
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj) {
    Cookfs_FsindexWantRead(i);

    Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("start"))

    /* run FIND command to get existing entry */
    fileNode = CookfsFsindexFind(i, NULL, pathObj, COOKFSFSINDEX_FIND_FIND, NULL);

    /* return NULL if not found */
    if (fileNode == NULL) {
        CookfsLog(printf("return: NULL"))
        return NULL;
    }

    CookfsLog(printf("return: success"))

    return fileNode;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSet --
 *
 *      Sets entry in specified path to file or directory
 *      If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, it is a directory
 *      otherwise it is a file with specified number of blocks
 *
 *      pathList provides a path as list, separated by fs separator
 *      for example for path/to/file, the list would be {path to file}
 *      and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *      Newly created/updated entry if successful, NULL otherwise
 *
 * Side effects:
 *      Can create pointer to this entry in parent element
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry *Cookfs_FsindexSetDirectory(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj) {
    return Cookfs_FsindexSet(i, pathObj, COOKFS_NUMBLOCKS_DIRECTORY);
}

Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int numBlocks) {
    Cookfs_FsindexWantWrite(i);

    Cookfs_FsindexEntry *dirNode = NULL;
    Cookfs_FsindexEntry *fileNode;
    const Cookfs_FsindexEntry *foundFileNode;

    CookfsLog(printf("start, listSize: %d", pathObj->elementCount));

    if (pathObj->elementCount == 0) {
        return NULL;
    }

    /* create new entry for object - used by CookfsFsindexFind() if
     * existing entry was not found */
    fileNode = Cookfs_FsindexEntryAlloc(i, pathObj->tailNameLength, numBlocks, COOKFS_USEHASH_DEFAULT);
    if (fileNode == NULL) {
        CookfsLog(printf("unable to create entry"));
        return NULL;
    }
    CookfsLog(printf("copy name - %s", pathObj->tailName));
    memcpy(fileNode->fileName, pathObj->tailName, pathObj->tailNameLength + 1);

    /* run CREATE command - if entry exists, currently passed fileNode will be freed */
    foundFileNode = CookfsFsindexFind(i, &dirNode, pathObj, COOKFSFSINDEX_FIND_CREATE, fileNode);

    /* if finding failed (i.e. parent did not exist), return NULL */
    if ((foundFileNode == NULL) || (dirNode == NULL)) {
        CookfsLog(printf("return: NULL"));
        /* the current node is already released by CookfsFsindexFind() */
        return NULL;
    }

    CookfsLog(printf("creating node for \"%s\"; new count=%d", pathObj->tailName, dirNode->data.dirInfo.childCount));

    return fileNode;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSetInDirectory --
 *
 *      Sets entry in specified parent entry
 *      If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, it is a directory
 *      otherwise it is a file with specified number of blocks
 *
 *      pathTailStr and pathTailLen are file tail and its length in bytes
 *
 * Results:
 *      Newly created/updated entry if successful, NULL otherwise
 *
 * Side effects:
 *      Can create pointer to this entry in parent element
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_FsindexEntry *currentNode, const char *pathTailStr, int pathTailLen, int numBlocks) {
    Cookfs_FsindexEntryWantWrite(currentNode);
    Cookfs_FsindexEntry *fileNode;
    const Cookfs_FsindexEntry *foundFileNode;
    CookfsLog(printf("begin (%s/%d)", pathTailStr, pathTailLen))
    fileNode = Cookfs_FsindexEntryAlloc(currentNode->fsindex, pathTailLen, numBlocks, COOKFS_USEHASH_DEFAULT);
    memcpy(fileNode->fileName, pathTailStr, pathTailLen + 1);

    CookfsLog(printf("fileNode=%p", (void *)fileNode))
    foundFileNode = CookfsFsindexFindInDirectory(currentNode, pathTailStr, COOKFSFSINDEX_FIND_CREATE, fileNode);
    if (foundFileNode == NULL) {
        CookfsLog(printf("NULL"))
        Cookfs_FsindexEntryFree(fileNode);
        CookfsLog(printf("cleanup done"))
        return NULL;
    }
    return fileNode;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexUnset --
 *
 *      Removes specified entry from filesystem index
 *
 *      Only empty directories and files can be removed
 *
 * Results:
 *      1 on success; 0 on error
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj) {
    Cookfs_FsindexWantWrite(i);
    const Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("start"))

    /* invoke DELETE command */
    fileNode = CookfsFsindexFind(i, NULL, pathObj, COOKFSFSINDEX_FIND_DELETE, NULL);
    if (fileNode == NULL) {
        CookfsLog(printf("return: NULL"))
        return 0;
    }

    CookfsLog(printf("return: success"))

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexUnsetRecursive --
 *
 *      Removes specified entry and all child entries from filesystem index
 *
 * Results:
 *      1 on success; 0 on error
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnsetRecursive(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj) {
    Cookfs_FsindexWantWrite(i);
    const Cookfs_FsindexEntry *fileNode;

    CookfsLog(printf("start"))

    /* invoke DELETE command */
    fileNode = CookfsFsindexFind(i, NULL, pathObj, COOKFSFSINDEX_FIND_DELETE_RECURSIVE, NULL);
    if (fileNode == NULL) {
        CookfsLog(printf("return: NULL"))
        return 0;
    }

    CookfsLog(printf("return: success"))

    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexListEntry --
 *
 *      Lists entries in specified path entry of filesystem index
 *
 *      Number of entries is put in itemCountPtr, if it is non-NULL
 *
 * Results:
 *      Array of Cookfs_FsindexEntry items; NULL is placed after
 *      all of the entries; call to Cookfs_FsindexListFree()
 *      should be made to free results from this function
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_FsindexEntry **Cookfs_FsindexListEntry(Cookfs_FsindexEntry *dirNode, int *itemCountPtr) {
    Cookfs_FsindexEntry *itemNode;
    Cookfs_FsindexEntry **result;
    int idx = 0;

    CookfsLog(printf("start"))

    if (dirNode == NULL) {
        CookfsLog(printf("return: not found"))
        return NULL;
    }

    Cookfs_FsindexEntryWantRead(dirNode);

    if (dirNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("return: not directory"))
        return NULL;
    }

    CookfsLog(printf("childCount = %d", dirNode->data.dirInfo.childCount))
    result = (Cookfs_FsindexEntry **) ckalloc((dirNode->data.dirInfo.childCount + 1) * sizeof(Cookfs_FsindexEntry *));

    CookfsLog(printf("isHash=%d", dirNode->data.dirInfo.isHash))
    if (dirNode->data.dirInfo.isHash) {
        Tcl_HashSearch hashSearch;
        Tcl_HashEntry *hashEntry = Tcl_FirstHashEntry(&dirNode->data.dirInfo.dirData.children, &hashSearch);
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

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int *itemCountPtr) {
    Cookfs_FsindexWantRead(i);

    CookfsLog(printf("start"))

    Cookfs_FsindexEntry *dirNode = CookfsFsindexFind(i, NULL, pathObj,
        COOKFSFSINDEX_FIND_FIND, NULL);

    return Cookfs_FsindexListEntry(dirNode, itemCountPtr);

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexListFree --
 *
 *      Frees results from calls to Cookfs_FsindexList()
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items) {
    ckfree((void *) items);
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryAlloc --
 *
 *      Creates a new entry; specified space is allocated for filename
 *      and storing numBlocks blocks
 *
 *      If numBlocks == COOKFS_NUMBLOCKS_DIRECTORY, directory entry is
 *      created - it is used to store child elements, not block information
 *
 *      Otherwise a file entry is created - it allows storing up to 3*numBlocks
 *      integers for block-offset-size triplets that specify where file data
 *      is stored
 *
 * Results:
 *      Pointer to newly created entry
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(Cookfs_Fsindex *fsindex, int fileNameLength, int numBlocks, int useHash) {
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
    e = (Cookfs_FsindexEntry *) ckalloc(size0 + fileNameBytes);
    e->fileName = ((char *) e) + size0;
    e->fileBlocks = numBlocks;
    e->fileNameLen = fileNameLength;
    e->isFileBlocksInitialized = NULL;
    e->fsindex = fsindex;
    e->refcount = 0;
    e->isInactive = 0;
#ifdef TCL_THREADS
    e->mxRefCount = NULL;
#endif /* TCL_THREADS */
    if (numBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
        /* create directory structure - either a hash table or static child array */
        CookfsLog(printf("directory, useHash=%d", useHash))
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

        // Set block index to dummy value -1 so that its block usage counter is
        // not touched when the entry is updated.
        for (int i = 0; i < numBlocks; i++) {
            e->data.fileInfo.fileBlockOffsetSize[i*3 + 0] = -1;
        }
    }

    //CookfsLog(printf("allocated entry %p", e));
    return e;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexEntryFree --
 *
 *      Frees entry along with all child entries
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e) {
    //CookfsLog(printf("%p with fileBlocks [%d]", e, e->fileBlocks));
    if (e->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
        /* for directory, recursively free all children */
        if (e->data.dirInfo.isHash) {
            Tcl_HashSearch hashSearch;
            Tcl_HashEntry *hashEntry;
            /* iterate over hash table for all children */
            hashEntry = Tcl_FirstHashEntry(&e->data.dirInfo.dirData.children, &hashSearch);
            while (hashEntry != NULL) {
                Cookfs_FsindexEntry *itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
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
            //CookfsLog(printf("modify block#%d", e->fileBlocks));
            Cookfs_FsindexModifyBlockUsage(e->isFileBlocksInitialized, e->data.fileInfo.fileBlockOffsetSize[e->fileBlocks * 3 + 0], -1);
        }
    }

    /* free entry structure itself */
    if (e->refcount) {
        CookfsLog(printf("move entry %p to inactive list", (void *)e));
        e->isInactive = 1;
        e->next = e->fsindex->inactiveItems;
        e->fsindex->inactiveItems = e;
    } else {
        // CookfsLog(printf("release entry %p", (void *)e));
#ifdef TCL_THREADS
        Tcl_MutexFinalize(&e->mxRefCount);
#endif /* TCL_THREADS */
        ckfree((void *) e);
    }
}

static void Cookfs_FsindexEntryForeach(Cookfs_FsindexEntry *e, Cookfs_FsindexForeachProc *proc, ClientData clientData) {

    proc(e, clientData);

    if (e->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        // Do nothing else if the current entry is not a directory
        return;
    }

    // for directory, recursively free all children
    if (e->data.dirInfo.isHash) {
        Tcl_HashSearch hashSearch;
        Tcl_HashEntry *hashEntry;
        // iterate over hash table for all children
        hashEntry = Tcl_FirstHashEntry(&e->data.dirInfo.dirData.children, &hashSearch);
        while (hashEntry != NULL) {
            Cookfs_FsindexEntry *itemNode = (Cookfs_FsindexEntry *)Tcl_GetHashValue(hashEntry);
            Cookfs_FsindexEntryForeach(itemNode, proc, clientData);
            hashEntry = Tcl_NextHashEntry(&hashSearch);
        }
    } else {
        // iterate through children and free them */
        for (int i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
            if (e->data.dirInfo.dirData.childTable[i] != NULL) {
                Cookfs_FsindexEntryForeach(e->data.dirInfo.dirData.childTable[i], proc, clientData);
            }
        }
    }

}

void Cookfs_FsindexForeach(Cookfs_Fsindex *i, Cookfs_FsindexForeachProc *proc, ClientData clientData) {
    Cookfs_FsindexWantRead(i);
    Cookfs_FsindexEntryForeach(i->rootItem, proc, clientData);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexGetMetadata --
 *
 *      Gets object at current metadata; this should be binary data and
 *      is stored as series of bytes when (de)serializing
 *
 * Results:
 *      Tcl_Obj containing currently set data
 *      Tcl_IncrRefCount and Tcl_DecrRefCount should be used to handle
 *      reference counter for returned object
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_FsindexGetMetadata(Cookfs_Fsindex *i, const char *paramName) {
    CookfsLog(printf("name [%s]", paramName));
    Cookfs_FsindexWantRead(i);
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_FindHashEntry(&i->metadataHash, paramName);
    if (hashEntry == NULL) {
        CookfsLog(printf("return: NULL"));
        return NULL;
    }
    unsigned char *value = Tcl_GetHashValue(hashEntry);
    Tcl_Obj *result = Tcl_NewByteArrayObj(value + sizeof(Tcl_Size),
        *((Tcl_Size *)value));
    CookfsLog(printf("return: [%s] (obj: %p)", Tcl_GetString(result),
        (void *)result));
    return result;
}

/*
Tcl_Obj *Cookfs_FsindexGetMetadataAll(Cookfs_Fsindex *i) {

    Cookfs_FsindexWantRead(i);

    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_HashSearch hSearch;

    for (Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(&i->metadataHash, &hSearch);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&hSearch))
    {

        Tcl_Obj *keyObj = Tcl_NewStringObj((const char *)Tcl_GetHashKey(
            &i->metadataHash, hPtr), -1);

        unsigned char *valueStr = Tcl_GetHashValue(hPtr);
        Tcl_Obj *valueObj = Tcl_NewByteArrayObj(valueStr + sizeof(Tcl_Size),
            *((Tcl_Size *)valueStr));

        Tcl_DictObjPut(NULL, result, keyObj, valueObj);

    }

    return result;

}
*/

Tcl_Obj *Cookfs_FsindexGetMetadataAllKeys(Cookfs_Fsindex *i) {

    Cookfs_FsindexWantRead(i);

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    Tcl_HashSearch hSearch;

    for (Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(&i->metadataHash, &hSearch);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&hSearch))
    {

        Tcl_Obj *keyObj = Tcl_NewStringObj((const char *)Tcl_GetHashKey(
            &i->metadataHash, hPtr), -1); // cppcheck-suppress knownArgument

        Tcl_ListObjAppendElement(NULL, result, keyObj);

    }

    return result;

}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexSetMetadata --
 *
 *      TODO
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Previously set value is removed if present; its reference counter
 *      is decremented, which might free it
 *
 *----------------------------------------------------------------------
 */

void Cookfs_FsindexSetMetadataRaw(Cookfs_Fsindex *i, const char *paramName,
    const unsigned char *dataPtr, Tcl_Size dataSize)
{
    Cookfs_FsindexWantWrite(i);
    int isNew;
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_CreateHashEntry(&i->metadataHash, paramName, &isNew);

    /* decrement reference count for old value, if set */
    if (!isNew) {
        ckfree(Tcl_GetHashValue(hashEntry));
    }

    unsigned char *value = ckalloc(sizeof(Tcl_Size) + dataSize);
    CookfsLog(printf("key [%s] size %" TCL_SIZE_MODIFIER "d value ptr %p",
        paramName, dataSize, (void *)value));
    if (value == NULL) {
        Tcl_Panic("failed to alloc metadata value");
        return;
    }
    *((Tcl_Size *)value) = dataSize;
    memcpy(value + sizeof(Tcl_Size), dataPtr, dataSize);

    Tcl_SetHashValue(hashEntry, (ClientData) value);
    Cookfs_FsindexIncrChangeCount(i, 1);
}

void Cookfs_FsindexSetMetadata(Cookfs_Fsindex *i, const char *paramName, Tcl_Obj *data) {
    Tcl_Size dataSize;
    unsigned char *dataPtr = Tcl_GetByteArrayFromObj(data, &dataSize);
    Cookfs_FsindexSetMetadataRaw(i, paramName, dataPtr, dataSize);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexUnsetMetadata --
 *
 *      TODO
 *
 * Results:
 *      Non-zero value if entry existed
 *
 * Side effects:
 *      Previously set value is removed if present; its reference counter
 *      is decremented, which might free it
 *
 *----------------------------------------------------------------------
 */

int Cookfs_FsindexUnsetMetadata(Cookfs_Fsindex *i, const char *paramName) {
    Cookfs_FsindexWantWrite(i);
    Tcl_HashEntry *hashEntry;
    hashEntry = Tcl_FindHashEntry(&i->metadataHash, paramName);
    if (hashEntry != NULL) {
        ckfree(Tcl_GetHashValue(hashEntry));
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
 *      Finds entry using specified path list
 *
 *      pathList provides a path as list, separated by fs separator
 *      for example for path/to/file, the list would be {path to file}
 *      and can be created using Tcl_FSSplitPath() Tcl API
 *
 * Results:
 *      Pointrer to entry if found; otherwise NULL
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
Cookfs_FsindexEntry *CookfsFsindexFindElement(const Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int listSize) {
    Cookfs_FsindexWantRead(i);
    int idx;
    Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *currentNode;
    Cookfs_FsindexEntry *nextNode;

    /* start off with root item */
    currentNode = i->rootItemVirtual;

    CookfsLog(printf("Recursively finding %d path elemnets", listSize));

    /* iterate over each element in the list */
    for (idx = 0; idx < listSize; idx++) {
        CookfsLog(printf("Iterating at %s (%d); %d of %d", currentNode->fileName,
            currentNode->fileBlocks, idx, listSize));

        /* if current node is not a directory, return NULL */
        if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
            CookfsLog(printf("Parent is not a directory"));
            return NULL;
        }

        char *currentPathStr = pathObj->element[idx].name0;

        if (currentNode->data.dirInfo.isHash) {
            /* if current entry is a hash table, locate child using Tcl_FindHashEntry () */
            hashEntry = Tcl_FindHashEntry(&currentNode->data.dirInfo.dirData.children, currentPathStr);
            if (hashEntry == NULL) {
                CookfsLog(printf("Unable to find item"));
                return NULL;
            }
            currentNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
        }  else  {
            /* if it is an array of children, iterate and find matching child */
            nextNode = NULL;
            CookfsLog(printf("Iterating over childTable to find %s", currentPathStr));
            for (int j = 0 ; j < COOKFS_FSINDEX_TABLE_MAXENTRIES; j++) {
                if ((currentNode->data.dirInfo.dirData.childTable[j] != NULL) && (strcmp(currentNode->data.dirInfo.dirData.childTable[j]->fileName, currentPathStr) == 0)) {
                    nextNode = currentNode->data.dirInfo.dirData.childTable[j];
                    CookfsLog(printf("Iterating over childTable to find %s - FOUND", currentPathStr));
                    break;
                }
            }

            /* check if entry was found - if not, return NULL */
            currentNode = nextNode;
            if (currentNode == NULL) {
                CookfsLog(printf("Unable to find item"));
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
 *      Multipurpose function for creating, finding and deleting entry
 *
 *      If dirPtr is not NULL, parent entry is stored in specified location
 *
 *      See CookfsFsindexFindInDirectory() for details
 *
 * Results:
 *      Entry or NULL; see CookfsFsindexFindInDirectory() for details
 *
 * Side effects:
 *      See CookfsFsindexFindInDirectory() for details
 *
 *----------------------------------------------------------------------
 */

static Cookfs_FsindexEntry *CookfsFsindexFind(Cookfs_Fsindex *i, Cookfs_FsindexEntry **dirPtr, Cookfs_PathObj *pathObj, int command, Cookfs_FsindexEntry *newFileNode) {
    Cookfs_FsindexWantRead(i);
    Cookfs_FsindexEntry *currentNode;

    if (pathObj->elementCount == 0) {
        if (command == COOKFSFSINDEX_FIND_FIND) {
            return i->rootItemVirtual;
        }  else  {
            /* create or delete will not work with empty file list */
            goto error;
        }
    }

    CookfsLog(printf("path elements: %d", pathObj->elementCount));

    /* find parent element */
    currentNode = CookfsFsindexFindElement(i, pathObj, pathObj->elementCount - 1);

    /* if dirPtr was specified, store parent entry */
    if (dirPtr != NULL) {
        *dirPtr = currentNode;
    }

    /* if parent was not found or is not a directory, return NULL */
    if (currentNode == NULL) {
        CookfsLog(printf("return NULL (node not found)"));
        goto error;
    }

    if (currentNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) {
        CookfsLog(printf("return NULL (not a directory)"));
        goto error;
    }

    /* get information about fail of the file name
     * and invoke CookfsFsindexFindInDirectory() */
    CookfsLog(printf("path tail: %s", pathObj->tailName));

    Cookfs_FsindexEntry *rc = CookfsFsindexFindInDirectory(currentNode, pathObj->tailName, command, newFileNode);
    if (command != COOKFSFSINDEX_FIND_FIND && rc != NULL) {
        Cookfs_FsindexIncrChangeCount(i, 1);
    }
    return rc;

error:

    if (newFileNode != NULL) {
        Cookfs_FsindexEntryFree(newFileNode);
    }
    return NULL;

}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexFindInDirectory --
 *
 *      Multipurpose function for creating, finding and deleting entry
 *      in specified directory entry
 *
 *      NOTE: It is created as single function since most operations and
 *      handling of hash vs static array are the same for all 3 commands
 *
 *      Commands:
 *      - COOKFSFSINDEX_FIND_FIND - finds specified entry,
 *        return NULL if not found; newFileNode is ignored
 *
 *      - COOKFSFSINDEX_FIND_CREATE - finds specified entry, creates if
 *        it does not exist; if entry did not exist, it is set to newFileNode;
 *        if it existed, newFileNode is put in place of old entry and previous
 *        entry is freed; if it is not possible to set entry, newFileNode is freed
 *        and NULL is returned
 *
 *      - COOKFSFSINDEX_FIND_DELETE - deletes specified entry; if entry previously
 *        existed, it is returned (after being freed - it is only returned for )
 *
 *      - COOKFSFSINDEX_FIND_DELETE_RECURSIVE - deletes specified entry and all
 *        child entries; if entry previously existed, it is returned (after being
 *        freed - it is only returned for )
 *
 * Results:
 *      Dependant on command:
 *      - COOKFSFSINDEX_FIND_FIND - found entry or NULL
 *
 *      - COOKFSFSINDEX_FIND_CREATE - if successful, returns newFileNode;
 *         otherwise returns NULL
 *
 *      - COOKFSFSINDEX_FIND_DELETE - entry if existed, NULL otherwise
 *
 *      - COOKFSFSINDEX_FIND_DELETE_RECURSIVE - entry if existed, NULL otherwise
 *
 * Side effects:
 *      Can convert parent entry (currentNode) from array of children into
 *      a hash table - it is not permitted to depend on previous value of
 *      currentNode->isHash after calling this function
 *
 *----------------------------------------------------------------------
 */

static Cookfs_FsindexEntry *CookfsFsindexFindInDirectory(Cookfs_FsindexEntry *currentNode, const char *pathTailStr, int command, Cookfs_FsindexEntry *newFileNode) {
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
                isNew = 0;
            }

            /* if entry has been found/created */
            if (hashEntry != NULL) {
                CookfsLog(printf("found in hash table (isNew=%d; cmd=%d)", isNew, command))
                /* if entry exists or has just been created */
                fileNode = Tcl_GetHashValue(hashEntry);

                CookfsLog(printf("fileNode=%p", (void *)fileNode))

                /* if we are to create a new entry, set new value */
                if (command == COOKFSFSINDEX_FIND_CREATE) {
                    if (!isNew) {
                        /* if entry exists already, check if both are of same type */
                        if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
                            || ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
                            /* if entry type does not match, free newFileNode and return NULL */
                            CookfsLog(printf("type mismatch"))
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
                    CookfsLog(printf("setting as newFileNode = %p", (void *)newFileNode))
                    Tcl_SetHashValue(hashEntry, newFileNode);
                    CookfsLog(printf("setting as newFileNode done"))
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
            CookfsLog(printf("looking in childTable"))
            for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
                if ((currentNode->data.dirInfo.dirData.childTable[i] != NULL) && (strcmp(currentNode->data.dirInfo.dirData.childTable[i]->fileName, pathTailStr) == 0)) {
                    fileNode = currentNode->data.dirInfo.dirData.childTable[i];
                    fileNodePtr = &currentNode->data.dirInfo.dirData.childTable[i];
                    CookfsLog(printf("found at %d", i))
                    break;
                }

            }

            /* if found, perform specified command */
            if (fileNode != NULL) {
                CookfsLog(printf("found in table cmd=%d", command))
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
                    CookfsLog(printf("deleted"))
                }  else if (command == COOKFSFSINDEX_FIND_CREATE) {
                    /* if entry exists already, check if both are of same type */
                    CookfsLog(printf("updating..."))
                    if (((fileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY))
                        || ((fileNode->fileBlocks != COOKFS_NUMBLOCKS_DIRECTORY) && (newFileNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY))) {
                        /* if entry type does not match, free newFileNode and return NULL */
                        CookfsLog(printf("update failed - type mismatch"))
                        Cookfs_FsindexEntryFree(newFileNode);
                        return NULL;
                    }

                    /* if types match, overwrite entry with new value */
                    CookfsLog(printf("updated"))
                    Cookfs_FsindexEntryFree(fileNode);
                    *fileNodePtr = newFileNode;
                    return newFileNode;
                }
                return fileNode;
            }  else  {
                CookfsLog(printf("not found"));
                /* if entry was not found */
                if (command == COOKFSFSINDEX_FIND_CREATE) {
                    /* for create operation, add new child to static array */
                    CookfsLog(printf("creating (%d)", currentNode->data.dirInfo.childCount))
                    if (currentNode->data.dirInfo.childCount >= (COOKFS_FSINDEX_TABLE_MAXENTRIES - 1)) {
                        /* if we ran out of entries in the array, convert to hash table and try again */
                        CookfsLog(printf("converting to hash"))
                        CookfsFsindexChildtableToHash(currentNode);
                        continue;
                    }  else  {
                        /* if we have any spots available, update first free one */
                        for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
                            if (currentNode->data.dirInfo.dirData.childTable[i] == NULL) {
                                CookfsLog(printf("create - adding at %d", i))
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
 *      Converts directory fsindex entry from using static array of
 *      children into hash table based.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static void CookfsFsindexChildtableToHash(Cookfs_FsindexEntry *e) {
    Cookfs_FsindexEntry *childTable[COOKFS_FSINDEX_TABLE_MAXENTRIES];
    int i;
    Tcl_HashEntry *hashEntry;
    int isNew;

    CookfsLog(printf("enter"))

    /* copy previous values to temporary table and initialize hash table for storage */
    memcpy(childTable, e->data.dirInfo.dirData.childTable, sizeof(childTable));

    e->data.dirInfo.isHash = 1;
    Tcl_InitHashTable(&e->data.dirInfo.dirData.children, TCL_STRING_KEYS);

    /* copy old entries to hash table */
    for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
        if (childTable[i] != NULL) {
            CookfsLog(printf("copying %s", childTable[i]->fileName))
            hashEntry = Tcl_CreateHashEntry(&e->data.dirInfo.dirData.children, childTable[i]->fileName, &isNew);

            Tcl_SetHashValue(hashEntry, childTable[i]);
        }
    }

    CookfsLog(printf("return: ok"))
}


