/*
 * writerchannelCmd.c
 *
 * Provides implementation for writer channel
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "writerchannel.h"

/* command for creating new objects that deal with pages */
static int CookfsCreateWriterchannelCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    if (objc != 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "pagesObject fsindexObject"
            " writerObject relativePath readflag");
        return TCL_ERROR;
    }

    int wantToRead;
    if (Tcl_GetBooleanFromObj(interp, objv[5], &wantToRead) != TCL_OK) {
        return TCL_ERROR;
    }

    Cookfs_Pages *pages = Cookfs_PagesGetHandle(interp,
        Tcl_GetStringFromObj(objv[1], NULL));
    CookfsLog(printf("pages [%p]", (void *)pages));

    if (pages == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " pages object", -1));
        return TCL_ERROR;
    }

    Cookfs_Fsindex *index = Cookfs_FsindexGetHandle(interp,
        Tcl_GetStringFromObj(objv[2], NULL));
    CookfsLog(printf("index [%p]", (void *)index));

    if (index == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " fsindex object", -1));
        return TCL_ERROR;
    }

    Cookfs_Writer *writer = Cookfs_WriterGetHandle(interp,
        Tcl_GetStringFromObj(objv[3], NULL));
    CookfsLog(printf("writer [%p]", (void *)writer));

    if (writer == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to find"
            " writer object", -1));
        return TCL_ERROR;
    }

    Cookfs_PathObj *pathObj = Cookfs_PathObjNewFromTclObj(objv[4]);
    Cookfs_PathObjIncrRefCount(pathObj);

    // If pathObjLen is 0, it is the root directory. We cannot open
    // the root directory as a writable file.
    if (pathObj->elementCount == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Could not open an empty"
            " file name for writing", -1));
        goto error;
    }

    // Verify that the entry exists and is not a directory, or the entry
    // does not exist but can be created (i.e. it has a parent directory).
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, pathObj);

    // If entry exists, make sure it is not a directory
    if (entry != NULL) {
        if (Cookfs_FsindexEntryIsDirectory(entry)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("file \"%s\" exists"
                " and it is a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    } else {
        // Make sure that parent directory exists
        Cookfs_FsindexEntry *entryParent = CookfsFsindexFindElement(index,
            pathObj, pathObj->elementCount - 1);
        if (entryParent == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since the parent directory"
                " does not exist", Tcl_GetString(objv[4])));
            goto error;
        }

        // Check if parent is a directory
        if (!Cookfs_FsindexEntryIsDirectory(entryParent)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to open"
                " file \"%s\" for writing, since its parent"
                " is not a directory", Tcl_GetString(objv[4])));
            goto error;
        }
    }

    // Everything looks good. Let's create a channel. If we don't want
    // to read previous data from the file and it should be created
    // from scratch, then pass NULL as entry.

    Tcl_Channel channel = Cookfs_CreateWriterchannel(pages, index, writer,
        pathObj, (wantToRead ? entry : NULL), interp);

    if (channel == NULL) {
        goto error;
    }

    // We don't need the pathObj anymore
    Cookfs_PathObjDecrRefCount(pathObj);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        Tcl_GetChannelName(channel), -1));
    return TCL_OK;

error:

    Cookfs_PathObjDecrRefCount(pathObj);
    return TCL_ERROR;

}

int Cookfs_InitWriterchannelCmd(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "::cookfs::c::writerchannel",
        CookfsCreateWriterchannelCmd, (ClientData) NULL, NULL);
    return TCL_OK;
}
