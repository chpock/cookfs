/*
 * vfs.c
 *
 * Provides internal methods for cookfs VFS
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

Cookfs_Vfs *Cookfs_VfsInit(Tcl_Interp* interp, Tcl_Obj* mountPoint,
    int isVolume, int isCurrentDirTime, int isReadonly,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Cookfs_Writer *writer)
{

    CookfsLog(printf("Cookfs_VfsInit: init mount in interp [%p];"
        " pages:%p index:%p writer:%p volume?%d", (void *)interp,
        (void *)pages, (void *)index, (void *)writer, isVolume));

    Cookfs_Vfs *vfs = NULL;

    if (interp == NULL || mountPoint == NULL || index == NULL
        || writer == NULL)
    {
        goto error;
    }

    vfs = (Cookfs_Vfs *)ckalloc(sizeof(Cookfs_Vfs));
    if (vfs == NULL) {
        goto error;
    }

    char *mountPointString = Tcl_GetStringFromObj(mountPoint,
        &vfs->mountLen);

    vfs->mountStr = (char *)ckalloc(1 + (unsigned)vfs->mountLen);
    if (vfs->mountStr == NULL) {
        goto error;
    }
    strcpy((char *)vfs->mountStr, mountPointString);

    vfs->mountObj = mountPoint;
    Tcl_IncrRefCount(mountPoint);

    vfs->nextVfs = NULL;
    vfs->commandToken = NULL;
    vfs->interp = interp;
    vfs->isDead = 0;
#ifdef COOKFS_USETCLCMDS
    vfs->isRegistered = 0;
#endif
    vfs->isVolume = isVolume;
    vfs->isCurrentDirTime = isCurrentDirTime;
    vfs->isReadonly = isReadonly;

    vfs->pages = pages;
    vfs->index = index;
    vfs->writer = writer;

    CookfsLog(printf("Cookfs_VfsInit: ok [%p]", (void *)vfs));
    return vfs;

error:
    if (vfs != NULL) {
        if (vfs->mountStr != NULL) {
            ckfree((void *)vfs->mountStr);
        }
        ckfree(vfs);
    }
    CookfsLog(printf("Cookfs_VfsInit: fail [NULL]"));
    return NULL;

}

int Cookfs_VfsFini(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_WideInt *pagesCloseOffset)
{

    CookfsLog(printf("Cookfs_VfsFini: terminate mount [%s] at [%p]",
        vfs->mountStr, (void *)vfs));

    // If something goes wrong, return pagesCloseOffset to zero to
    // avoid undefined behavior
    if (pagesCloseOffset != NULL) {
        *pagesCloseOffset = 0;
    }

    if (vfs->isDead) {
        CookfsLog(printf("Cookfs_VfsFini: the mount point is already in"
            " a terminating state"));
        return TCL_OK;
    }

    // Let's purge writer first
    CookfsLog(printf("Cookfs_VfsFini: purge writer..."));
    if (Cookfs_WriterPurge(vfs->writer) != TCL_OK) {
        CookfsLog(printf("Cookfs_VfsFini: return an error, writer failed"));
        return TCL_ERROR;
    }

    Tcl_WideInt changecount = Cookfs_FsindexIncrChangeCount(vfs->index, 0);
    CookfsLog(printf("Cookfs_VfsFini: changecount from index: %" TCL_LL_MODIFIER
        "d", changecount));
    if (!changecount) {
        goto skipSavingIndex;
    }

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("Cookfs_VfsFini: pages readonly: true"));
        goto skipSavingIndex;
    } else {
        CookfsLog(printf("Cookfs_VfsFini: pages readonly: false"));
    }

    if (Cookfs_WriterGetWritetomemory(vfs->writer)) {
        CookfsLog(printf("Cookfs_VfsFini: writer writetomemory: true"));
        goto skipSavingIndex;
    } else {
        CookfsLog(printf("Cookfs_VfsFini: writer writetomemory: false"));
    }

    // If we are here, then we need to store index
    CookfsLog(printf("Cookfs_VfsFini: dump index..."));
    Tcl_Obj *exportObj = Cookfs_FsindexToObject(vfs->index);
    if (exportObj == NULL) {
        CookfsLog(printf("Cookfs_VfsFini: failed to get index dump"));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unable to get index"
            " dump", -1));
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(exportObj);

    CookfsLog(printf("Cookfs_VfsFini: store index..."));
    Cookfs_PagesSetIndex(vfs->pages, exportObj);
    Tcl_DecrRefCount(exportObj);

skipSavingIndex:

    // Cleanup everything

    vfs->isDead = 1;

    // Cleanup writer
    CookfsLog(printf("Cookfs_VfsFini: delete writer..."));
    Cookfs_WriterFini(vfs->writer);

    if (vfs->pages == NULL) {
        goto skipPages;
    }

    // Cleanup pages
    CookfsLog(printf("Cookfs_VfsFini: close pages..."));
    Tcl_WideInt offset = Cookfs_PagesClose(vfs->pages);
    if (pagesCloseOffset != NULL) {
        *pagesCloseOffset = offset;
    }
    CookfsLog(printf("Cookfs_VfsFini: delete pages..."));
    Cookfs_PagesFini(vfs->pages);

skipPages:

    // Cleanup index
    CookfsLog(printf("Cookfs_VfsFini: delete index..."));
    Cookfs_FsindexFini(vfs->index);

    // Remove mount Tcl command is exists
    if (vfs->commandToken != NULL) {
        CookfsLog(printf("Cookfs_VfsFini: delete comand..."));
        Tcl_DeleteCommandFromToken(vfs->interp, vfs->commandToken);
    } else {
        CookfsLog(printf("Cookfs_VfsFini: command is not registered"));
    }

    // Cleanup own fields
    CookfsLog(printf("Cookfs_VfsFini: cleanup own fields"));
    Tcl_DecrRefCount(vfs->mountObj);
    ckfree((char *)vfs->mountStr);
    ckfree((char *)vfs);

    CookfsLog(printf("Cookfs_VfsFini: ok"));
    return TCL_OK;
}

#ifdef COOKFS_USETCLCMDS

void Cookfs_VfsUnregisterInTclvfs(Cookfs_Vfs *vfs) {

    if (!vfs->isRegistered) {
        return;
    }

    CookfsLog(printf("Cookfs_VfsUnregisterInTclvfs: vfs [%p]", (void *)vfs));

    // Mark VFS as dead now. Tclfvs will call our Unmount command, and
    // we don't want to run it since we're probably already in
    // the unmounting process
    int savedDeadState = vfs->isDead;
    vfs->isDead = 1;

    Tcl_Obj *objv[2];
    objv[0] = Tcl_NewStringObj("::vfs::unmount", -1);
    objv[1] = vfs->mountObj;
    Tcl_Obj *cmd = Tcl_NewListObj(2, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_VfsUnregisterInTclvfs: call tclvfs: [%s]",
        Tcl_GetString(cmd)));
    int ret = Tcl_EvalObjEx(vfs->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_VfsUnregisterInTclvfs: tclvfs returned: %d",
        ret));
    if (ret != TCL_OK) {
        // Let's ignore failure from tclvfs and cleanup interp from
        // its error
        Tcl_ResetResult(vfs->interp);
        CookfsLog(printf("Cookfs_VfsUnregisterInTclvfs: cleanup interp from"
            " tclvfs failure"));
    } else {
        CookfsLog(printf("Cookfs_VfsUnregisterInTclvfs: ok"));
    }

    // The VFS is not registered in tclvfs anymore
    vfs->isRegistered = 0;
    // Restore isDead state
    vfs->isDead = savedDeadState;

    return;
}

int Cookfs_VfsRegisterInTclvfs(Cookfs_Vfs *vfs) {

    if (vfs->isRegistered) {
        return TCL_OK;
    }

    if (Tcl_PkgPresent(vfs->interp, "vfs", NULL, 0) == NULL) {
        CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: want to register cookfs,"
            " but vfs package is not loaded"));
        return TCL_OK;
    }

    CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: vfs [%p] in [%s]",
        (void *)vfs, Tcl_GetString(vfs->mountObj)));

    Tcl_Obj *objv[3];

    // Create an unregister callback
    objv[0] = Tcl_NewStringObj("::cookfs::c::Unmount", -1);
    objv[1] = Tcl_NewStringObj("-unregister", -1);

    // Set the unregister callback as the third parameter
    // for ::vfs::RegisterMount
    objv[2] = Tcl_NewListObj(2, objv);

    // Construct the first two words of the tclvfs registration command
    objv[0] = Tcl_NewStringObj("::vfs::RegisterMount", -1);
    objv[1] = vfs->mountObj;

    // Construct the registration command
    Tcl_Obj *cmd = Tcl_NewListObj(3, objv);

    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: call tclvfs: [%s]",
        Tcl_GetString(cmd)));
    int ret = Tcl_EvalObjEx(vfs->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: tclvfs returned: %d",
        ret));
    if (ret == TCL_OK) {
        // If registration fails, leave the error message in interp result
        // and do not set the registered flag
        vfs->isRegistered = 1;
        CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: ok"));
    } else {
        CookfsLog(printf("Cookfs_VfsRegisterInTclvfs: ERROR"));
    }

    return ret;
}

#endif

int Cookfs_VfsIsReadonly(Cookfs_Vfs *vfs) {
    return vfs->isReadonly;
}

void Cookfs_VfsSetReadonly(Cookfs_Vfs *vfs, int status) {
    vfs->isReadonly = status;
    return;
}

Tcl_Obj *CookfsGetVfsObjectCmd(Cookfs_Vfs *vfs) {
    Tcl_Obj *rc = Tcl_NewObj();
    if (vfs->commandToken != NULL) {
        Tcl_GetCommandFullName(vfs->interp, vfs->commandToken, rc);
    }
    return rc;
}
