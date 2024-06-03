/*
 * vfsVfs.c
 *
 * Provides methods for mounting/unmounting cookfs VFS in Tcl core
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

typedef struct ThreadSpecificData {
    Cookfs_Vfs *listOfMounts;
    Tcl_Obj *cookfsVolumes;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define COOKFS_ASSOC_KEY "::cookfs::c::inUse"

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

// Forward declarations
static Tcl_InterpDeleteProc Cookfs_CookfsUnregister;
static Tcl_ExitProc Cookfs_CookfsExitProc;
static Tcl_ExitProc Cookfs_CookfsThreadExitProc;

static void Cookfs_CookfsAddVolume(Tcl_Obj *volume);
static int Cookfs_CookfsRemoveVolume(Tcl_Obj *volume);

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
        Cookfs_Vfs *vfs = Cookfs_CookfsRemoveVfs(interp, NULL, NULL);
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
        Cookfs_CookfsAddVolume(vfs->mountObj);
    }
    Tcl_FSMountsChanged(CookfsFilesystem());

    return 1;

}

Cookfs_Vfs *Cookfs_CookfsRemoveVfs(Tcl_Interp *interp, Tcl_Obj* mountPoint,
    Cookfs_Vfs *vfsToRemove)
{

    CookfsLog(printf("Cookfs_CookfsRemoveVfs: want to remove vfs with mount"
        " point [%p] ptr [%p] interp [%p]", (void *)mountPoint,
        (void *)vfsToRemove, (void *)interp));

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

        if (mountPoint != NULL) {
            if (Tcl_FSEqualPaths(vfs->mountObj, mountPoint) != 1) {
                CookfsLog(printf("Cookfs_CookfsRemoveVfs: wrong path"));
                goto next;
            }
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
            Cookfs_CookfsRemoveVolume(vfs->mountObj);
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
        Tcl_ListObjAppendElement(NULL, returnObj, vfs->mountObj);
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
    return tsdPtr->cookfsVolumes;
}

static void Cookfs_CookfsAddVolume(Tcl_Obj *volume) {

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    if (tsdPtr->cookfsVolumes == NULL) {
        tsdPtr->cookfsVolumes = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(tsdPtr->cookfsVolumes);
    }

    Tcl_ListObjAppendElement(NULL, tsdPtr->cookfsVolumes, volume);

}

static int Cookfs_CookfsRemoveVolume(Tcl_Obj *volume) {

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    if (tsdPtr->cookfsVolumes == NULL || volume == NULL) {
        return TCL_OK;
    }

    Tcl_Size volumeLen;
    char *volumeStr = Tcl_GetStringFromObj(volume, &volumeLen);

    Tcl_Size volCount;
    Tcl_ListObjLength(NULL, tsdPtr->cookfsVolumes, &volCount);

    for (Tcl_Size i = 0; i < volCount; i++) {

        Tcl_Obj *obj;
        Tcl_ListObjIndex(NULL, tsdPtr->cookfsVolumes, i, &obj);

        Tcl_IncrRefCount(obj);

        Tcl_Size volumeCurLen;
        char *volumeCurStr = Tcl_GetStringFromObj(obj, &volumeCurLen);

        if (volumeCurLen != volumeLen ||
            strcmp(volumeStr, volumeCurStr) != 0)
        {
            Tcl_DecrRefCount(obj);
            continue;
        }

        // We have found the mount

        // If we have only one volume in the list, then just release
        // the volume list
        if (volCount == 1) {
            Tcl_DecrRefCount(tsdPtr->cookfsVolumes);
            tsdPtr->cookfsVolumes = NULL;
        } else {
            Tcl_ListObjReplace(NULL, tsdPtr->cookfsVolumes, i, 1, 0, NULL);
        }

        Tcl_DecrRefCount(obj);
        return TCL_OK;

    }

    return TCL_ERROR;

}
