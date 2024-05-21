/*
 * vfs.c
 *
 * Provides implementation for writer
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

Cookfs_Writer *Cookfs_WriterInit(Tcl_Interp* interp,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Tcl_WideInt smallfilebuffer, Tcl_WideInt smallfilesize,
    Tcl_WideInt pagesize, int writetomemory)
{

    CookfsLog(printf("Cookfs_WriterInit: init mount in interp [%p];"
        " pages:%p index:%p smbuf:%ld sms:%ld pagesize:%ld writetomem:%d",
        (void *)interp, (void *)pages, (void *)index,
        smallfilebuffer, smallfilesize, pagesize, writetomemory));

    Cookfs_Writer *w = NULL;

    if (interp == NULL || pages == NULL || index == NULL) {
        CookfsLog(printf("Cookfs_WriterInit: failed, something is NULL"));
        return NULL;
    }

    // Initialize writer

    Tcl_Obj *objv[13];
    objv[0] = Tcl_NewStringObj("::cookfs::tcl::writer", -1);
    objv[1] = Tcl_NewStringObj("-pages", -1);
    objv[2] = CookfsGetPagesObjectCmd(interp, pages);
    objv[3] = Tcl_NewStringObj("-index", -1);
    objv[4] = CookfsGetFsindexObjectCmd(interp, index);
    objv[5] = Tcl_NewStringObj("-smallfilebuffersize", -1);
    objv[6] = Tcl_NewWideIntObj(smallfilebuffer);
    objv[7] = Tcl_NewStringObj("-smallfilesize", -1);
    objv[8] = Tcl_NewWideIntObj(smallfilesize);
    objv[9] = Tcl_NewStringObj("-pagesize", -1);
    objv[10] = Tcl_NewWideIntObj(pagesize);
    objv[11] = Tcl_NewStringObj("-writetomemory", -1);
    objv[12] = Tcl_NewIntObj(writetomemory);

    Tcl_Obj *cmd = Tcl_NewListObj(13, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterInit: init Tcl writer..."));
    int ret = Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterInit: Tcl writer: %d", ret));
    if (ret != TCL_OK) {
        return NULL;
    }

    w = (Cookfs_Writer *)ckalloc(sizeof(Cookfs_Writer));
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterInit: failed, OOM"));
        return NULL;
    }

    w->interp = interp;

    // Use interp result from above initialization as a writer command
    w->commandObj = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(w->commandObj);
    CookfsLog(printf("Cookfs_WriterInit: created writer [%s]",
        Tcl_GetString(w->commandObj)));

    return w;

}

void Cookfs_WriterFini(Cookfs_Writer *w) {
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterFini: ERROR: writer is NULL"));
        return;
    }
    Tcl_Obj *objv[2];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("delete", -1);
    Tcl_Obj *cmd = Tcl_NewListObj(2, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterFini: free Tcl writer..."));
    // Ignore result here
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    if (ret != TCL_OK) {
        Tcl_ResetResult(w->interp);
    }
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterFini: Tcl writer: %d", ret));
    return;
}

int Cookfs_WriterPurge(Cookfs_Writer *w) {
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterPurge: ERROR: writer is NULL"));
        return 0;
    }
    Tcl_Obj *objv[2];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("purge", -1);
    Tcl_Obj *cmd = Tcl_NewListObj(2, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterPurge: call Tcl writer..."));
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterPurge: Tcl writer: %d", ret));
    return ret;
}

int Cookfs_WriterGetWritetomemory(Cookfs_Writer *w, int *status) {
    // Set status before anything else to return known value
    // in case of error and avoid undefined behaviour
    *status = 0;
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterGetWritetomemory: ERROR: writer is"
            " NULL"));
        return TCL_ERROR;
    }
    Tcl_Obj *objv[2];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("writetomemory", -1);
    Tcl_Obj *cmd = Tcl_NewListObj(2, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterGetWritetomemory: call Tcl writer..."));
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterGetWritetomemory: Tcl writer: %d", ret));
    if (ret == TCL_OK) {
        if (Tcl_GetIntFromObj(w->interp, Tcl_GetObjResult(w->interp),
            status) != TCL_OK)
        {
            CookfsLog(printf("Cookfs_WriterGetWritetomemory: unable to get"
                " an integer from writer result"));
            ret = TCL_ERROR;
        } else {
            CookfsLog(printf("Cookfs_WriterGetWritetomemory: got value from"
                " writer: %d", *status));
        }
    }
    return ret;
}

int Cookfs_WriterSetWritetomemory(Cookfs_Writer *w, int status) {
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterSetWritetomemory: ERROR: writer is"
            " NULL"));
        return TCL_ERROR;
    }
    Tcl_Obj *objv[3];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("writetomemory", -1);
    objv[2] = Tcl_NewIntObj(status);
    Tcl_Obj *cmd = Tcl_NewListObj(3, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterSetWritetomemory: call Tcl writer with"
        " arg [%d]...", status));
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterSetWritetomemory: Tcl writer: %d", ret));
    return ret;
}

int Cookfs_WriterGetSmallfilebuffersize(Cookfs_Writer *w, Tcl_WideInt *size) {
    // Set size before anything else to return known value
    // in case of error and avoid undefined behaviour
    *size = 0;
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterGetSmallfilebuffersize: ERROR:"
            " writer is NULL"));
        return TCL_ERROR;
    }
    Tcl_Obj *objv[2];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("smallfilebuffersize", -1);
    Tcl_Obj *cmd = Tcl_NewListObj(2, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterGetSmallfilebuffersize: call Tcl writer..."));
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterGetSmallfilebuffersize: Tcl writer: %d", ret));
    if (ret == TCL_OK) {
        if (Tcl_GetWideIntFromObj(w->interp, Tcl_GetObjResult(w->interp),
            size) != TCL_OK)
        {
            CookfsLog(printf("Cookfs_WriterGetSmallfilebuffersize: unable to"
                " get a wide integer from writer result"));
            ret = TCL_ERROR;
        } else {
            CookfsLog(printf("Cookfs_WriterGetSmallfilebuffersize: got value"
                " from writer: %ld", *size));
        }
    }
    return ret;
}

int Cookfs_WriterSetIndex(Cookfs_Writer *w, Cookfs_Fsindex *index) {
    if (w == NULL) {
        CookfsLog(printf("Cookfs_WriterSetIndex: ERROR: writer is"
            " NULL"));
        return TCL_ERROR;
    }
    Tcl_Obj *objv[3];
    objv[0] = w->commandObj;
    objv[1] = Tcl_NewStringObj("index", -1);
    objv[2] = CookfsGetFsindexObjectCmd(w->interp, index);;
    Tcl_Obj *cmd = Tcl_NewListObj(3, objv);
    Tcl_IncrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterSetIndex: call Tcl writer..."));
    int ret = Tcl_EvalObjEx(w->interp, cmd, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    CookfsLog(printf("Cookfs_WriterSetIndex: Tcl writer: %d", ret));
    return ret;
}
