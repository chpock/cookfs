/*
 * vfsVfs.c
 *
 * Provides methods for mounting/unmounting cookfs VFS in Tcl core
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

typedef struct Cookfs_VolumeEntry {
    char *volumeStr;
    Tcl_Size volumeLen;
    struct Cookfs_VolumeEntry *next;
} Cookfs_VolumeEntry;

typedef struct ThreadSpecificData {
    Cookfs_Vfs *listOfMounts;
    Tcl_Obj *volumesObj;
    Cookfs_VolumeEntry *volumesList;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define COOKFS_ASSOC_KEY "::cookfs::c::inUse"

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

// Forward declarations
static Tcl_InterpDeleteProc Cookfs_CookfsUnregister;
static Tcl_ExitProc Cookfs_CookfsExitProc;
static Tcl_ExitProc Cookfs_CookfsThreadExitProc;

void Cookfs_CookfsRegister(Tcl_Interp *interp) {

    CookfsLog(printf("Cookfs_CookfsRegister: register in interp [%p]",
        (void *)interp));

    // Register Cookfs if it is not already registered
    CookfsFSData *fsdata = (CookfsFSData *)Tcl_FSData(CookfsFilesystem());
    if (fsdata == NULL) {

        fsdata = ckalloc(sizeof(CookfsFSData));
        if (fsdata == NULL) {
            CookfsLog(printf("Cookfs_CookfsRegister: failed to alloc fsdata"));
            return;
        }
        Tcl_Obj *objv[2];
        objv[0] = Tcl_NewStringObj("-vfs", -1);
        objv[1] = Tcl_NewStringObj("-handle", -1);
        fsdata->attrListRoot = Tcl_NewListObj(2, objv);
        fsdata->attrList = Tcl_NewListObj(1, objv);
        fsdata->attrValVfs = Tcl_NewIntObj(1);
        Tcl_IncrRefCount(fsdata->attrListRoot);
        Tcl_IncrRefCount(fsdata->attrList);
        Tcl_IncrRefCount(fsdata->attrValVfs);

        Tcl_FSRegister((ClientData)fsdata, CookfsFilesystem());
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
    CookfsFSData *fsdata = (CookfsFSData *)Tcl_FSData(CookfsFilesystem());
    if (fsdata != NULL) {
        Tcl_DecrRefCount(fsdata->attrListRoot);
        Tcl_DecrRefCount(fsdata->attrList);
        Tcl_DecrRefCount(fsdata->attrValVfs);
        ckfree(fsdata);
    }
    Tcl_FSUnregister(CookfsFilesystem());
}

static void Cookfs_CookfsThreadExitProc(ClientData clientData) {
    UNUSED(clientData);
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    // There is nothing here at the moment. Perhaps in the future it will be
    // used for thread-specific data cleansing.
    UNUSED(tsdPtr);
}

int Cookfs_CookfsAddVfs(Tcl_Interp *interp, Cookfs_Vfs *vfs) {

    CookfsLog(printf("Cookfs_CookfsAddVfs: add mount [%s] at [%p]",
        vfs->mountStr, (void *)vfs));

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    // Check if interp is properly configured to cleanup mount points
    if (Tcl_GetAssocData(interp, COOKFS_ASSOC_KEY, NULL) == NULL) {
        return 0;
    }

    vfs->nextVfs = tsdPtr->listOfMounts;
    tsdPtr->listOfMounts = vfs;

    if (vfs->isVolume) {

        Cookfs_VolumeEntry *v = ckalloc(sizeof(Cookfs_VolumeEntry) +
            vfs->mountLen + 1);
        if (v == NULL) {
            Tcl_Panic("failed to alloc Cookfs_VolumeEntry");
            return 0; // <- avoid cppcheck complaints
        }

        v->volumeStr = (char *)v + sizeof(Cookfs_VolumeEntry);
        v->volumeLen = vfs->mountLen;
        memcpy(v->volumeStr, vfs->mountStr, vfs->mountLen + 1);

        v->next = tsdPtr->volumesList;
        tsdPtr->volumesList = v;

        // Invalidate volume list cache
        if (tsdPtr->volumesObj != NULL) {
            Tcl_DecrRefCount(tsdPtr->volumesObj);
            tsdPtr->volumesObj = NULL;
        }

    }
    Tcl_FSMountsChanged(CookfsFilesystem());

    return 1;

}

Cookfs_Vfs *Cookfs_CookfsRemoveVfs(Tcl_Interp *interp,
    Cookfs_Vfs *vfsToRemove)
{

    CookfsLog(printf("Cookfs_CookfsRemoveVfs: want to remove vfs with mount"
        " ptr [%p] interp [%p]", (void *)vfsToRemove, (void *)interp));

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    Cookfs_Vfs *vfsPrev = NULL;
    Cookfs_Vfs *vfs = tsdPtr->listOfMounts;

    while (vfs != NULL) {

        CookfsLog(printf("Cookfs_CookfsRemoveVfs: check vfs [%p] at [%s] in"
            " interp [%p]", (void *)vfs, vfs->mountStr, (void *)vfs->interp));

        if (interp != vfs->interp) {
            CookfsLog(printf("Cookfs_CookfsRemoveVfs: wrong interp"));
            goto next;
        }

        if (vfsToRemove != NULL) {
            if (vfs != vfsToRemove) {
                CookfsLog(printf("Cookfs_CookfsRemoveVfs: wrong ptr"));
                goto next;
            }
        }

        // We have found the mount
        CookfsLog(printf("Cookfs_CookfsRemoveVfs: the vfs for deletion"
            " was found"));

#ifdef COOKFS_USETCLCMDS
        // Unregister the Cookfs from Tclvfs. Tclvfs will call our
        // Unmount command. It should be able find this mount point and
        // terminate without error. Thus, this mount point must be in
        // the mounted state during unregistration in Tclvfs.
        Cookfs_VfsUnregisterInTclvfs(vfs);
#endif

        // Remove the mount from the mount chain
        if (vfsPrev == NULL) {
            tsdPtr->listOfMounts = vfs->nextVfs;
        } else {
            vfsPrev->nextVfs = vfs->nextVfs;
        }

        if (vfs->isVolume) {

            Cookfs_VolumeEntry *volumePrev = NULL;
            Cookfs_VolumeEntry *volume = tsdPtr->volumesList;
            while (volume != NULL) {

                if (volume->volumeLen != vfs->mountLen ||
                    memcmp(volume->volumeStr, vfs->mountStr,
                    vfs->mountLen) != 0)
                {
                    volumePrev = volume;
                    volume = volume->next;
                    continue;
                }

                if (volumePrev == NULL) {
                    tsdPtr->volumesList = volume->next;
                } else {
                    volumePrev->next = volume->next;
                }

                ckfree(volume);

                // Invalidate volume list cache
                if (tsdPtr->volumesObj != NULL) {
                    Tcl_DecrRefCount(tsdPtr->volumesObj);
                    tsdPtr->volumesObj = NULL;
                }

                break;

            }

        }

        Tcl_FSMountsChanged(CookfsFilesystem());

        CookfsLog(printf("Cookfs_CookfsRemoveVfs: return [%p]",
            (void *)vfs));
        return vfs;

next:
        vfsPrev = vfs;
        vfs = vfs->nextVfs;
    }

    CookfsLog(printf("Cookfs_CookfsRemoveMount: return NULL"));
    return NULL;

}

void Cookfs_CookfsSearchVfsToListObj(Tcl_Obj *path, const char *pattern,
    Tcl_Obj *returnObj)
{

    CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: check mount points"));

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    Tcl_Size searchLen;
    const char *searchStr = Tcl_GetStringFromObj(Tcl_FSGetNormalizedPath(NULL,
        path), &searchLen);

    // Remove the trailing VFS path separator if it exists
    if (searchStr[searchLen-1] == VFS_SEPARATOR) {
        searchLen--;
    }

    Cookfs_Vfs *vfs = tsdPtr->listOfMounts;
    while (vfs != NULL) {
        if (vfs->mountLen <= (searchLen + 1)) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", vfs->mountStr, "mount path len is too small"));
            goto nextVfs;
        }
        if (strncmp(vfs->mountStr, searchStr, (size_t)searchLen) != 0) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", vfs->mountStr, "doesn't match"));
            goto nextVfs;
        }
        if (vfs->mountStr[searchLen] != '/') {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                    " [%s] - %s", vfs->mountStr, "no sep"));
            goto nextVfs;
        }
        if (strchr(vfs->mountStr + searchLen + 1, '/') != NULL) {
            CookfsLog(printf("Cookfs_CookfsSearchVfsToListObj: skip vfs"
                " [%s] - %s", vfs->mountStr, "found sep"));
            goto nextVfs;
        }
        if (!Tcl_StringCaseMatch(vfs->mountStr + searchLen + 1, pattern, 0)) {
            CookfsLog(printf("CookfsMatchInDirectory: skip vfs"
                " [%s] - %s", vfs->mountStr, "doesn't match pattern"));
            goto nextVfs;
        }
        CookfsLog(printf("CookfsMatchInDirectory: add vfs [%s]",
                vfs->mountStr));
        Tcl_ListObjAppendElement(NULL, returnObj,
            Tcl_NewStringObj(vfs->mountStr, vfs->mountLen));
nextVfs:
        vfs = vfs->nextVfs;
    }
}


int Cookfs_CookfsIsVfsExist(Cookfs_Vfs *vfsToSearch) {
    if (vfsToSearch == NULL) {
        return 0;
    }

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    Cookfs_Vfs *vfs = tsdPtr->listOfMounts;
    while (vfs != NULL) {
        if (vfs == vfsToSearch) {
            return 1;
        }
        vfs = vfs->nextVfs;
    }

    return 0;
}

Cookfs_Vfs *Cookfs_CookfsFindVfs(Tcl_Obj *path, Tcl_Size len) {
    if (path == NULL) {
        return NULL;
    }

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    char *searchStr;
    if (len == -1) {
        searchStr = Tcl_GetStringFromObj(path, &len);
    } else {
        searchStr = Tcl_GetString(path);
    }

    Cookfs_Vfs *vfs = tsdPtr->listOfMounts;
    while (vfs != NULL) {
        if (vfs->mountLen == len && strncmp(vfs->mountStr, searchStr,
            (size_t)len) == 0)
        {
            return vfs;
        }
        vfs = vfs->nextVfs;
    }

    return NULL;
}

Tcl_Obj *Cookfs_CookfsGetVolumesList(void) {
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    // CookfsLog(printf("Cookfs_CookfsGetVolumesList: ENTER"));

    if (tsdPtr->volumesObj == NULL && tsdPtr->volumesList != NULL) {

        // CookfsLog(printf("Cookfs_CookfsGetVolumesList: need to rebuild cache"));
        // Need to rebuild volume list cache
        tsdPtr->volumesObj = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(tsdPtr->volumesObj);

        Cookfs_VolumeEntry *v = tsdPtr->volumesList;
        while (v != NULL) {
            Tcl_Obj *obj = Tcl_NewStringObj(v->volumeStr, v->volumeLen);
            Tcl_ListObjAppendElement(NULL, tsdPtr->volumesObj, obj);
            v = v->next;
        }

    }

    // CookfsLog(printf("Cookfs_CookfsGetVolumesList: ok [%s]",
    //     tsdPtr->volumesObj == NULL ?
    //     "NULL" : Tcl_GetString(tsdPtr->volumesObj)));
    return tsdPtr->volumesObj;
}
