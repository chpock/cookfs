/*
 * vfs.c
 *
 * Provides implementation for writer
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

// Creating a writer object from Tcl is not supported as for now
// static Tcl_ObjCmdProc CookfsWriterCmd;

static Tcl_ObjCmdProc CookfsWriterHandlerCmd;
static Tcl_CmdDeleteProc CookfsWriterHandlerCmdDeleteProc;

int Cookfs_InitWriterCmd(Tcl_Interp *interp) {

    Tcl_CreateNamespace(interp, "::cookfs::c::writer", NULL, NULL);

    // Creating a writer object from Tcl is not supported as for now

    /*
    Tcl_CreateObjCommand(interp, "::cookfs::c::writer", CookfsWriterCmd,
        (ClientData) NULL, NULL);

    Tcl_CreateAlias(interp, "::cookfs::writer", interp, "::cookfs::c::writer",
        0, NULL);

    */

    return TCL_OK;

}

static void CookfsRegisterExistingWriterObjectCmd(Tcl_Interp *interp, Cookfs_Writer *w) {
    if (w->commandToken != NULL) {
        return;
    }
    char buf[128];
    /* create Tcl command and return its name */
    sprintf(buf, "::cookfs::c::writer::handle%p", (void *)w);
    w->commandToken = Tcl_CreateObjCommand(interp, buf, CookfsWriterHandlerCmd,
        (ClientData) w, CookfsWriterHandlerCmdDeleteProc);
    w->interp = interp;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
}


Tcl_Obj *CookfsGetWriterObjectCmd(Tcl_Interp *interp, Cookfs_Writer *w) {
    CookfsRegisterExistingWriterObjectCmd(interp, w);
    Tcl_Obj *rc = Tcl_NewObj();
    Tcl_GetCommandFullName(interp, w->commandToken, rc);
    return rc;
}

// Creating a writer object from Tcl is not supported as for now
/*
static int CookfsWriterCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    UNUSED(clientData);
    UNUSED(objc);
    UNUSED(objv);

    CookfsLog(printf("CookfsWriterCmd: ENTER"));

    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s is not implemented."
        " A new writer object should be created by cookfs::Mount command.",
        Tcl_GetString(objv[0])));

    CookfsLog(printf("CookfsWriterCmd: return ERROR"));

    return TCL_ERROR;
}
*/

static void CookfsWriterHandlerCmdDeleteProc(ClientData clientData) {
    Cookfs_Writer *w = (Cookfs_Writer *)clientData;
    w->commandToken = NULL;
}

static Cookfs_WriterHandleCommandProc CookfsWriterHandleCommandGetbuf;
// In headers:
// Cookfs_WriterHandleCommandProc CookfsWriterHandleCommandWrite;

static int CookfsWriterHandlerCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    Cookfs_Writer *w = (Cookfs_Writer *)clientData;

    static const char *const commands[] = {
        "getbuf", "write",
        NULL
    };
    enum commands {
        cmdGetbuf, cmdWrite
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ?args?");
        return TCL_ERROR;
    }

    int command;
    if (Tcl_GetIndexFromObj(interp, objv[1], commands, "command", 0,
            &command) != TCL_OK)
    {
        CookfsLog(printf("CookfsWriterHandlerCmd: ERROR: unknown command [%s]",
            Tcl_GetString(objv[1])));
        return TCL_ERROR;
    }

    switch ((enum commands) command) {
    case cmdGetbuf:
        return CookfsWriterHandleCommandGetbuf(w, interp, objc, objv);
    case cmdWrite:
        return CookfsWriterHandleCommandWrite(w, interp, objc, objv);
    }

    return TCL_OK;

}

static int CookfsWriterHandleCommandGetbuf(Cookfs_Writer *w,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("CookfsWriterHandleCommandGetbuf: enter"));

    if (objc != 3) {
        CookfsLog(printf("CookfsWriterHandleCommandGetbuf: ERR: wrong # args"));
        Tcl_WrongNumArgs(interp, 2, objv, "index");
        return TCL_ERROR;
    }

    int bufNumber;
    if (Tcl_GetIntFromObj(interp, objv[2], &bufNumber) != TCL_OK) {
        CookfsLog(printf("CookfsWriterHandleCommandGetbuf: ERROR: wrong buf #"
            " [%s]", Tcl_GetString(objv[2])));
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("integer index is expected,"
            " but got \"%s\"", Tcl_GetString(objv[2])));
        return TCL_ERROR;
    }

    Tcl_Obj *rc = Cookfs_WriterGetBufferObj(w, bufNumber);
    if (rc == NULL) {
        CookfsLog(printf("CookfsWriterHandleCommandGetbuf: ERROR: got NULL"));
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to get buf index %d",
            bufNumber));
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, rc);
    CookfsLog(printf("CookfsWriterHandleCommandGetbuf: ok"));
    return TCL_OK;

}

int CookfsWriterHandleCommandWrite(Cookfs_Writer *w,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("CookfsWriterHandleCommandWrite: enter"));

    if (objc < 3 || ((objc - 2) % 4) != 0) {
        CookfsLog(printf("CookfsWriterHandleCommandWrite: ERR: wrong # args"
            " (%d)", objc));
        Tcl_WrongNumArgs(interp, 2, objv, "path datatype data size ?path"
            " datatype data size...?");
        return TCL_ERROR;
    }

    static const char *const dataTypes[] = {
        "file", "channel", "data", NULL
    };

    for (int i = 2; i < objc;) {

        Tcl_Obj *path = objv[i++];
        Cookfs_PathObj *pathObj = Cookfs_PathObjNewFromTclObj(path);
        Cookfs_PathObjIncrRefCount(pathObj);

        int dataTypeInt;
        if (Tcl_GetIndexFromObj(interp, objv[i++], dataTypes, "datatype",
            TCL_EXACT, &dataTypeInt) != TCL_OK)
        {
            CookfsLog(printf("CookfsWriterHandleCommandWrite: ERR: unknown"
                " datatype [%s]", Tcl_GetString(objv[i - 1])));
            goto error;
        }

        Tcl_Channel channel = NULL;
        Tcl_Obj *data = objv[i++];
        if ((Cookfs_WriterDataSource)dataTypeInt == COOKFS_WRITER_SOURCE_CHANNEL) {
            int mode;
            channel = Tcl_GetChannel(interp, Tcl_GetString(data), &mode);
            if (channel == NULL) {
                CookfsLog(printf("CookfsWriterHandleCommandWrite: ERR: is not a"
                    " channel [%s]", Tcl_GetString(data)));
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("channel name expected,"
                    " but got \"%s\"", Tcl_GetString(data)));
                goto error;
            }
            if (!(mode & TCL_READABLE)) {
                CookfsLog(printf("CookfsWriterHandleCommandWrite: ERR: channel"
                    " [%s] is not readable", Tcl_GetString(data)));
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("channel \"%s\" is not"
                    " readable", Tcl_GetString(data)));
                goto error;
            }
        }

        Tcl_Obj *dataSizeObj = objv[i++];
        Tcl_WideInt dataSize = -1;
        if (Tcl_GetCharLength(dataSizeObj) != 0) {
            if (Tcl_GetWideIntFromObj(interp, dataSizeObj, &dataSize)
                != TCL_OK)
            {
                CookfsLog(printf("CookfsWriterHandleCommandWrite: ERR: datasize"
                    " [%s] is not a wide int", Tcl_GetString(dataSizeObj)));
                goto error;
            }
        }

        Tcl_Obj *err = NULL;
        int ret = Cookfs_WriterAddFile(w, pathObj,
            (Cookfs_WriterDataSource)dataTypeInt, (
            (Cookfs_WriterDataSource)dataTypeInt == COOKFS_WRITER_SOURCE_CHANNEL
            ? (void *)channel : (void *)data), dataSize, &err);
        if (ret != TCL_OK) {
            if (err == NULL) {
                CookfsLog(printf("CookfsWriterHandleCommandWrite: got error"
                    " and unknown message from Cookfs_WriterAddFile()"));
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown error"));
            } else {
                CookfsLog(printf("CookfsWriterHandleCommandWrite: got error"
                    " from Cookfs_WriterAddFile(): %s", Tcl_GetString(err)));
                Tcl_SetObjResult(interp, err);
            }
            goto error;
        }

        Cookfs_PathObjDecrRefCount(pathObj);

        continue;

error:

        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unable to add \"%s\": %s",
            Tcl_GetString(path), Tcl_GetString(Tcl_GetObjResult(interp))));
        CookfsLog(printf("CookfsWriterHandleCommandWrite: ERROR while adding"
            " [%s]", Tcl_GetString(path)));
        Cookfs_PathObjDecrRefCount(pathObj);
        return TCL_ERROR;

    }

    return TCL_OK;

}
