/*
 * vfsVfs.c
 *
 * Provides methods for mounting/unmounting cookfs VFS in Tcl core
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "vfs.h"
#include "vfsVfs.h"
#include "vfsDriver.h"

typedef struct Cookfs_VfsEntry {
    Cookfs_Vfs *vfs;
    int isShared;
    Tcl_Interp *interp;
    char *mountStr;
    Tcl_Size mountLen;
    struct Cookfs_VfsEntry *next;
} Cookfs_VfsEntry;

typedef struct ThreadSpecificData {

    Cookfs_VfsEntry *vfsList;

    int globalChangeId;

    Cookfs_VfsEntry *vfsListCached;
    Tcl_Obj *volumeListObjCached;

} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define COOKFS_ASSOC_KEY "::cookfs::c::inUse"

/*
#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))
*/

#define TCL_TSD_GET(var, keyPtr) \
    ThreadSpecificData *var = (ThreadSpecificData *) \
        Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

#ifdef TCL_THREADS

#define TCL_TSD_UPDATE(var, keyPtr) \
    TCL_TSD_GET(var, keyPtr); \
    if (var->globalChangeId != global.changeId) { \
        Cookfs_CookfsUpdateThreadSpecificData(var); \
    }

#else

#define TCL_TSD_UPDATE(var, keyPtr) TCL_TSD_GET(var, keyPtr)

#endif /* TCL_THREADS */


#ifdef TCL_THREADS
static struct {
    Tcl_Mutex mx;
    Cookfs_VfsEntry *vfsList;
    int changeId;
} global = {
    NULL, NULL, 0
};
#endif /* TCL_THREADS */

// Forward declarations
static Tcl_InterpDeleteProc Cookfs_CookfsUnregister;
static Tcl_ExitProc Cookfs_CookfsExitProc;
static Tcl_ExitProc Cookfs_CookfsThreadExitProc;

static void Cookfs_CookfsRemoveVfsEntry(Cookfs_VfsEntry **vfsListToPtr,
    Cookfs_Vfs *vfs)
{
    Cookfs_VfsEntry *eprev = NULL;
    Cookfs_VfsEntry *ecur = *vfsListToPtr;
    while (ecur != NULL) {
        if (ecur->vfs == vfs) {
            // We found vfs that needs to be removed
            if (eprev == NULL) {
                *vfsListToPtr = ecur->next;
            } else {
                eprev->next = ecur->next;
            }
            ckfree(ecur);
            break;
        }
        eprev = ecur;
        ecur = ecur->next;
    }
}

static void Cookfs_CookfsFillCache(Cookfs_VfsEntry **vfsListToPtr,
    Tcl_Obj **volumeListToPtr, Cookfs_VfsEntry *vfsListFrom)
{
    CookfsLog(printf("Cookfs_CookfsFillCache: enter"));

    while (vfsListFrom != NULL) {

        Cookfs_VfsEntry *newEntryVfs = ckalloc(sizeof(Cookfs_VfsEntry) +
            vfsListFrom->vfs->mountLen + 1);
        if (newEntryVfs == NULL) {
            Tcl_Panic("failed to alloc Cookfs_VfsEntry");
            return; // <- to avoid cppcheck complaints
        }
        CookfsLog(printf("Cookfs_CookfsFillCache: new entry %p",
            (void *)newEntryVfs));
        newEntryVfs->isShared = vfsListFrom->vfs->isShared;
        newEntryVfs->interp = vfsListFrom->vfs->interp;
        newEntryVfs->mountLen = vfsListFrom->vfs->mountLen;
        newEntryVfs->mountStr = (char *)newEntryVfs + sizeof(Cookfs_VfsEntry);
        memcpy(newEntryVfs->mountStr, vfsListFrom->vfs->mountStr,
            newEntryVfs->mountLen + 1);
        newEntryVfs->vfs = vfsListFrom->vfs;
        newEntryVfs->next = *vfsListToPtr;
        *vfsListToPtr = newEntryVfs;

        // CookfsLog(printf("Cookfs_CookfsFillCache: added vfs [%s]",
        //     newEntryVfs->mountStr));

        if (vfsListFrom->vfs->isVolume) {
            if (*volumeListToPtr == NULL) {
                *volumeListToPtr = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(*volumeListToPtr);
                CookfsLog(printf("Cookfs_CookfsFillCache: volume list %p",
                    (void *)(*volumeListToPtr)));
            }
            Tcl_Obj *obj = Tcl_NewStringObj(vfsListFrom->vfs->mountStr,
                vfsListFrom->vfs->mountLen);
            Tcl_ListObjAppendElement(NULL, *volumeListToPtr, obj);
            // CookfsLog(printf("Cookfs_CookfsFillCache: added volume"));
        }

        vfsListFrom = vfsListFrom->next;

    }

    CookfsLog(printf("Cookfs_CookfsFillCache: ok"));
}

static void Cookfs_CookfsUpdateThreadSpecificData(ThreadSpecificData *tsdPtr) {

    CookfsLog(printf("Cookfs_CookfsUpdateThreadSpecificData: enter"));

    // Free anything we have now.
    while (tsdPtr->vfsListCached != NULL) {
        Cookfs_VfsEntry *entryVfs = tsdPtr->vfsListCached;
        tsdPtr->vfsListCached = entryVfs->next;
        CookfsLog(printf("Cookfs_CookfsUpdateThreadSpecificData: free entry %p",
            (void *)entryVfs));
        ckfree(entryVfs);
    }

    if (tsdPtr->volumeListObjCached != NULL) {
        CookfsLog(printf("Cookfs_CookfsUpdateThreadSpecificData: free list obj %p",
            (void *)tsdPtr->volumeListObjCached));
        Tcl_DecrRefCount(tsdPtr->volumeListObjCached);
        tsdPtr->volumeListObjCached = NULL;
    }

    // Construct a cache of avaiable VFS.
    // The entries won't be in exact order, but we don't care about that.
    Cookfs_CookfsFillCache(&tsdPtr->vfsListCached,
        &tsdPtr->volumeListObjCached, tsdPtr->vfsList);

#ifdef TCL_THREADS
    // Lock global data now
    Tcl_MutexLock(&global.mx);
    // Add global vfs
    Cookfs_CookfsFillCache(&tsdPtr->vfsListCached,
        &tsdPtr->volumeListObjCached, global.vfsList);
    // Commit current global state
    tsdPtr->globalChangeId = global.changeId;
    Tcl_MutexUnlock(&global.mx);
#endif /* TCL_THREADS */

    CookfsLog(printf("Cookfs_CookfsUpdateThreadSpecificData: ok"));
    return;

}

void Cookfs_CookfsRegister(Tcl_Interp *interp) {

    CookfsLog(printf("Cookfs_CookfsRegister: register in interp [%p]",
        (void *)interp));

    // Register Cookfs if it is not already registered
    void *fsdata = (void *)Tcl_FSData(CookfsFilesystem());
    if (fsdata == NULL) {
        Tcl_FSRegister((ClientData)1, CookfsFilesystem());
        Tcl_CreateExitHandler(Cookfs_CookfsExitProc, (ClientData) NULL);
        Tcl_CreateThreadExitHandler(Cookfs_CookfsThreadExitProc, NULL);
    } else {
        CookfsLog(printf("Cookfs_CookfsRegister: already registered"));
    }

    // Set callback to cleanup mount points for deleted interp
    Tcl_SetAssocData(interp, COOKFS_ASSOC_KEY, Cookfs_CookfsUnregister,
        (ClientData) 1);
}

static void Cookfs_CookfsUnregister(ClientData clientData,
    Tcl_Interp *interp)
{
    UNUSED(clientData);

    CookfsLog(printf("Cookfs_CookfsUnregister: unregister in interp [%p]",
        (void *)interp));

    // Remove all mount points
    while (1) {
        CookfsLog(printf("Cookfs_CookfsUnregister: remove the next vfs..."));
        Cookfs_Vfs *vfs = Cookfs_CookfsRemoveVfs(interp, NULL);
        if (vfs == NULL) {
            CookfsLog(printf("Cookfs_CookfsUnregister: no more vfs"));
            break;
        }
        CookfsLog(printf("Cookfs_CookfsUnregister: free vfs..."));
        Cookfs_VfsFini(interp, vfs, NULL);
    }

    // Don't call Tcl_DeleteAssocData() here. We are in the callback
    // of removing assocdata, so it is being removed now. It also causes
    // a crash in Tcl9: https://core.tcl-lang.org/tcl/tktview/34870ab575
    //
    // Tcl_DeleteAssocData(interp, COOKFS_ASSOC_KEY);
}

static void Cookfs_CookfsExitProc(ClientData clientData) {
    UNUSED(clientData);
    CookfsLog(printf("Cookfs_CookfsExitProc: ENTER"));
    Tcl_FSUnregister(CookfsFilesystem());
    CookfsLog(printf("Cookfs_CookfsExitProc: ok"));
}

static void Cookfs_CookfsThreadExitProc(ClientData clientData) {
    UNUSED(clientData);
    CookfsLog(printf("Cookfs_CookfsThreadExitProc: ENTER"));
    // ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    // There is nothing here at the moment. Perhaps in the future it will be
    // used for thread-specific data cleansing.
    // UNUSED(tsdPtr);
    CookfsLog(printf("Cookfs_CookfsThreadExitProc: ok"));
}

int Cookfs_CookfsAddVfs(Tcl_Interp *interp, Cookfs_Vfs *vfs) {

    CookfsLog(printf("Cookfs_CookfsAddVfs: add mount [%s] at [%p] isShared %d",
        vfs->mountStr, (void *)vfs, vfs->isShared));

    // Check if interp is properly configured to cleanup mount points
    if (Tcl_GetAssocData(interp, COOKFS_ASSOC_KEY, NULL) == NULL) {
        return 0;
    }

    TCL_TSD_GET(tsdPtr, &dataKeyCookfs);
    // ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    //ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(0);

    Cookfs_VfsEntry *newEntryVfs = ckalloc(sizeof(Cookfs_VfsEntry));
    if (newEntryVfs == NULL) {
        Tcl_Panic("failed to alloc Cookfs_VfsEntry");
        return 0; // <- to avoid cppcheck complaints
    }
    newEntryVfs->vfs = vfs;
    newEntryVfs->isShared = vfs->isShared;

#ifdef TCL_THREADS
    if (vfs->isShared) {
        Tcl_MutexLock(&global.mx);
        newEntryVfs->next = global.vfsList;
        global.vfsList = newEntryVfs;
        global.changeId++;
        Tcl_MutexUnlock(&global.mx);
    } else {
#endif /* TCL_THREADS */
        newEntryVfs->next = tsdPtr->vfsList;
        tsdPtr->vfsList = newEntryVfs;
#ifdef TCL_THREADS
    }
#endif /* TCL_THREADS */

    // Force update cache
    Cookfs_CookfsUpdateThreadSpecificData(tsdPtr);

    Tcl_FSMountsChanged(CookfsFilesystem());

    return 1;

}

Cookfs_Vfs *Cookfs_CookfsRemoveVfs(Tcl_Interp *interp,
    Cookfs_Vfs *vfsToRemove)
{

    CookfsLog(printf("Cookfs_CookfsRemoveVfs: want to remove vfs with mount"
        " ptr [%p] interp [%p]", (void *)vfsToRemove, (void *)interp));

    //ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    //ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    Cookfs_VfsEntry *e = tsdPtr->vfsListCached;
    while (e != NULL) {

        CookfsLog(printf("Cookfs_CookfsRemoveVfs: check vfs [%p] at [%s] in"
            " interp [%p]", (void *)e->vfs, e->mountStr, (void *)e->interp));

        if (interp != e->interp) {
            CookfsLog(printf("Cookfs_CookfsRemoveVfs: wrong interp"));
            goto next;
        }

        if (vfsToRemove != NULL) {
            if (e->vfs != vfsToRemove) {
                CookfsLog(printf("Cookfs_CookfsRemoveVfs: wrong ptr"));
                goto next;
            }
        }

        // We have found the mount
        CookfsLog(printf("Cookfs_CookfsRemoveVfs: the vfs for deletion"
            " was found"));
        break;

next:
        e = e->next;

    }

    // If we have e == NULL here, then there is no suitable vfs to delete.
    if (e == NULL) {
        CookfsLog(printf("Cookfs_CookfsRemoveMount: return NULL"));
        return NULL;
    }

    // If we found the global vfs, then theoretically it could be deleted
    // since the last call to Cookfs_CookfsGetThreadSpecificData().
    // However, this is not possible since we only allow vfs to be deleted
    // from the interpreter in which it was created. And if we found this vfs,
    // then it was created by the current thread. Thus we can be sure that
    // this vfs is still alive.

#ifdef COOKFS_USETCLCMDS
    // Unregister the Cookfs from Tclvfs. Tclvfs will call our
    // Unmount command. It should be able find this mount point and
    // terminate without error. Thus, this mount point must be in
    // the mounted state during unregistration in Tclvfs.
    Cookfs_VfsUnregisterInTclvfs(e->vfs);
#endif

    // Remove the mount from the mount chain
#ifdef TCL_THREADS
    if (e->isShared) {
        Tcl_MutexLock(&global.mx);
        Cookfs_CookfsRemoveVfsEntry(&global.vfsList, e->vfs);
        global.changeId++;
        Tcl_MutexUnlock(&global.mx);
    } else {
#endif /* TCL_THREADS */
        Cookfs_CookfsRemoveVfsEntry(&tsdPtr->vfsList, e->vfs);
#ifdef TCL_THREADS
    }
#endif /* TCL_THREADS */

    // Save vfs because we want to return it. UpdateThreadSpecificData will
    // release Cookfs_VfsEntry e.
    vfsToRemove = e->vfs;

    // Force update cache
    Cookfs_CookfsUpdateThreadSpecificData(tsdPtr);

    Tcl_FSMountsChanged(CookfsFilesystem());

    CookfsLog(printf("Cookfs_CookfsRemoveVfs: return [%p]",
        (void *)vfsToRemove));
    return vfsToRemove;

}

void Cookfs_CookfsSearchVfsToListObj(Tcl_Obj *path, const char *pattern,
    Tcl_Obj *returnObj)
{

    CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: check mount points"));

    // ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    // ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    Tcl_Size searchLen;
    const char *searchStr = Tcl_GetStringFromObj(Tcl_FSGetNormalizedPath(NULL,
        path), &searchLen);

    // Remove the trailing VFS path separator if it exists
    // if (searchStr[searchLen-1] == VFS_SEPARATOR) {
    //     searchLen--;
    // }

    Cookfs_VfsEntry *e = tsdPtr->vfsListCached;
    while (e != NULL) {
        if (e->mountLen <= (searchLen + 1)) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", e->mountStr, "mount path len is too small"));
            goto nextVfs;
        }
        if (strncmp(e->mountStr, searchStr, (size_t)searchLen) != 0) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", e->mountStr, "doesn't match"));
            goto nextVfs;
        }
        if (e->mountStr[searchLen] != '/') {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                    " [%s] - %s", e->mountStr, "no sep"));
            goto nextVfs;
        }
        if (strchr(e->mountStr + searchLen + 1, '/') != NULL) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", e->mountStr, "found sep"));
            goto nextVfs;
        }
        if (!Tcl_StringCaseMatch(e->mountStr + searchLen + 1, pattern, 0)) {
            CookfsLog(printf("CookfsMatchInDirectory: skip vfs"
                " [%s] - %s", e->mountStr, "doesn't match pattern"));
            goto nextVfs;
        }
        CookfsLog(printf("CookfsMatchInDirectory: add vfs [%s]",
                e->mountStr));
        Tcl_ListObjAppendElement(NULL, returnObj,
            Tcl_NewStringObj(e->mountStr, e->mountLen));
nextVfs:
        e = e->next;
    }
}


int Cookfs_CookfsIsVfsExist(Cookfs_Vfs *vfsToSearch) {
    if (vfsToSearch == NULL) {
        return 0;
    }

    CookfsLog(printf("Cookfs_CookfsIsVfsExist: ENTER"));
    //ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    //ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    Cookfs_VfsEntry *e = tsdPtr->vfsListCached;
    while (e != NULL) {
        if (e->vfs == vfsToSearch) {
            return 1;
        }
        e = e->next;
    }

    return 0;
}

int Cookfs_CookfsVfsLock(Cookfs_Vfs *vfsToLock) {

    // This is some smart locking function. It works as follows:
    //
    // 1. Get updated list of VFS in ThreadSpecificData
    // 2. Lock VFS changes (global.mx mutex)
    // 3. Compare globalChangeId to make sure VFS was not unmounted
    //    between items #1 and #2.
    // 4. If the globalChangeId is not match, then jump the start and
    //    try again.
    // 5. Ensure that VFS is in list
    //

    // 1. Get updated list of VFS in ThreadSpecificData
    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

#ifdef TCL_THREADS
tryagain:

    // 2. Lock VFS changes (global.mx mutex)
    Tcl_MutexLock(&global.mx);

    // 3. Compare globalChangeId to make sure VFS was not unmounted
    //    between items #1 and #2.
    if (tsdPtr->globalChangeId != global.changeId) {
        // 4. If the globalChangeId is not match, then jump the start and
        //    try again.
        Tcl_MutexUnlock(&global.mx);
        Cookfs_CookfsUpdateThreadSpecificData(tsdPtr);
        goto tryagain;
    }
#endif /* TCL_THREADS */

    // 4. Ensure that VFS is in list
    Cookfs_VfsEntry *e = tsdPtr->vfsListCached;
    while (e != NULL) {
        if (e->vfs == vfsToLock && !e->vfs->isDead) {
            break;
        }
        e = e->next;
    }
    // If e == NULL, the VFS is not found. In this case, false is returned.
    if (e == NULL) {
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&global.mx);
#endif /* TCL_THREADS */
        return 0;
    }

    return 1;

}

int Cookfs_CookfsVfsUnlock(Cookfs_Vfs *vfsToUnlock) {
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&global.mx);
#endif /* TCL_THREADS */
    UNUSED(vfsToUnlock);
    return 1;
}

Cookfs_Vfs *Cookfs_CookfsFindVfs(Tcl_Obj *path, Tcl_Size len) {
    if (path == NULL) {
        return NULL;
    }

    // ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    // ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    char *searchStr;
    if (len == -1) {
        searchStr = Tcl_GetStringFromObj(path, &len);
    } else {
        searchStr = Tcl_GetString(path);
    }
    // CookfsLog(printf("Cookfs_CookfsFindVfs: ENTER [%s]", searchStr));

    Cookfs_VfsEntry *e = tsdPtr->vfsListCached;
    while (e != NULL) {
        if (e->mountLen == len && strncmp(e->mountStr, searchStr,
            (size_t)len) == 0)
        {
            return e->vfs;
        }
        e = e->next;
    }

    return NULL;
}

Cookfs_Vfs *Cookfs_CookfsSplitWithVfs(Tcl_Obj *path,
    Cookfs_PathObj **pathObjPtr)
{
    // CookfsLog(printf("Cookfs_CookfsSplitWithVfs: ENTER [%s]",
    //     Tcl_GetString(path)));

    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    Tcl_Size searchLen;
    char *searchStr = Tcl_GetStringFromObj(path, &searchLen);

    // Here we want to find the longest mount path that matches a given path.
    // This is necessary because we can have vfs mount points inside
    // another vfs.

    Tcl_Size foundSize = 0;
    Cookfs_VfsEntry *foundEntry = NULL;

    Cookfs_VfsEntry *e;
    for (e = tsdPtr->vfsListCached; e != NULL; e = e->next) {
        Tcl_Size mountLen = e->mountLen;
        // If we already found a mount point longer then the current one, or
        // if the current mount point is longer than the search path, or
        // if the current mount point doesn't match the search path,
        // then continue
        if (foundSize > mountLen ||
            mountLen > searchLen ||
            memcmp(e->mountStr, searchStr, mountLen) != 0)
        {
            continue;
        }
        // Also, we check that we have filesystem separator in the search
        // string at the point after the end of the VFS mount or the search
        // string is the VFS mount point (they are the same length).
        // This will prevent files like '/foo/mount.bar' from being considered
        // as belonging to the mount point '/foo/mount'. Here we expect
        // a normalized input path with the internal Tcl file system
        // separator '/'. Thus, we don't check for '\' on Windows and '/'
        // on Unix.
        //
        // However, we also should consider a case where mount point contains
        // a filesystem separator at the end. For example, if we have a mount
        // point like 'mount://'. In this case, we don't need to perform
        // the above check because we have already verified that the start
        // of the search string matches the VFS mount point.
        if (e->mountStr[mountLen - 1] != VFS_SEPARATOR &&
            mountLen != searchLen &&
            searchStr[mountLen] != VFS_SEPARATOR)
        {
            continue;
        }

        // We found matching the mount point.
        foundSize = mountLen;
        foundEntry = e;
    }

    // Return NULL if we didn't find anything.
    if (foundEntry == NULL) {
        // CookfsLog(printf("Cookfs_CookfsSplitWithVfs: return NULL"));
        return NULL;
    }

    // Remove the mount path from the search path
    searchStr += foundSize;
    searchLen -= foundSize;
    // Remove any possible file system separator that may remain after
    // removing the mount path.
    if (searchStr[0] == '/') {
        searchStr++;
        searchLen--;
    }

    // Create a new PathObj now
    *pathObjPtr = Cookfs_PathObjNewFromStr(searchStr, searchLen);

    // Return found vfs
    return foundEntry->vfs;

}

Tcl_Obj *Cookfs_CookfsGetVolumesList(void) {
    //ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    //return Tcl_NewObj();
    //ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    //UNUSED(tsdPtr);

    TCL_TSD_UPDATE(tsdPtr, &dataKeyCookfs);

    //ThreadSpecificData *tsdPtr = Cookfs_CookfsGetThreadSpecificData(1);
    //CookfsLog(printf("Cookfs_CookfsGetVolumesList: ENTER"));
    //
    // if (tsdPtr->volumesObj == NULL && tsdPtr->volumesList != NULL) {

    //     // CookfsLog(printf("Cookfs_CookfsGetVolumesList: need to rebuild cache"));
    //     // Need to rebuild volume list cache
    //     tsdPtr->volumesObj = Tcl_NewListObj(0, NULL);
    //     Tcl_IncrRefCount(tsdPtr->volumesObj);

    //     Cookfs_VolumeEntry *v = tsdPtr->volumesList;
    //     while (v != NULL) {
    //         Tcl_Obj *obj = Tcl_NewStringObj(v->volumeStr, v->volumeLen);
    //         Tcl_ListObjAppendElement(NULL, tsdPtr->volumesObj, obj);
    //         v = v->next;
    //     }

    // }

    // // CookfsLog(printf("Cookfs_CookfsGetVolumesList: ok [%s]",
    // //     tsdPtr->volumesObj == NULL ?
    // //     "NULL" : Tcl_GetString(tsdPtr->volumesObj)));

    return tsdPtr->volumeListObjCached;

}
