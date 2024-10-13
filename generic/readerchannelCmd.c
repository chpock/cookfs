/*
 * readerchannelCmd.c
 *
 * Provides implementation for reader channel
 *
 * (c) 2010-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "readerchannel.h"

/* command for creating new objects that deal with pages */
static int CookfsCreateReaderchannelCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    UNUSED(clientData);
    Cookfs_Pages *pages;
    Cookfs_Fsindex *fsindex;
    Cookfs_FsindexEntry *entry;
    Tcl_Channel channel;
    char *channelName;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "pagesObject fsindexObject relativepath");
        return TCL_ERROR;
    }

    pages = Cookfs_PagesGetHandle(interp, Tcl_GetStringFromObj(objv[1], NULL));

    if (pages == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find pages object", -1));
        return TCL_ERROR;
    }

    fsindex = Cookfs_FsindexGetHandle(interp, Tcl_GetStringFromObj(objv[2], NULL));

    CookfsLog(printf("fsindex [%p]", (void *)fsindex));
    if (fsindex == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find fsindex object", -1));
        return TCL_ERROR;
    }

    Tcl_Obj *err = NULL;
    if (!Cookfs_FsindexLockRead(fsindex, &err)) {
        if (err != NULL) {
            Tcl_SetObjResult(interp, err);
        }
        return TCL_ERROR;
    }

    int rc = TCL_OK;

    Cookfs_PathObj *pathObj = Cookfs_PathObjNewFromTclObj(objv[3]);
    Cookfs_PathObjIncrRefCount(pathObj);

    entry = Cookfs_FsindexGet(fsindex, pathObj);

    // If entry exists, make sure it is not a directory
    if (entry == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't open \"%s\":"
            " no such file or directory", Tcl_GetString(objv[3])));
        goto error;
    } else if (Cookfs_FsindexEntryIsDirectory(entry)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("file \"%s\" exists"
            " and it is a directory", Tcl_GetString(objv[3])));
        goto error;
    }

    channel = Cookfs_CreateReaderchannel(pages, fsindex, entry, interp, &channelName);

    if (channel == NULL) {
        goto error;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(channelName, -1));

    goto done;

error:
    rc = TCL_ERROR;

done:
    Cookfs_FsindexUnlock(fsindex);
    Cookfs_PathObjDecrRefCount(pathObj);
    return rc;
}

int Cookfs_InitReaderchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::c::readerchannel", CookfsCreateReaderchannelCmd,
        (ClientData) NULL, NULL);

    return TCL_OK;
}
