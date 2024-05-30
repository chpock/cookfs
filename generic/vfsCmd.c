/*
 * vfsCmd.c
 *
 * Provides Tcl commands for mounting cookfs
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

typedef int (Cookfs_MountHandleCommandProc)(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

// Forward declarations
static Tcl_ObjCmdProc CookfsMountCmd;
static Tcl_ObjCmdProc CookfsUnmountCmd;
static Tcl_ObjCmdProc CookfsMountHandleCmd;
static Tcl_CmdDeleteProc CookfsMountHandleCmdDeleteProc;

int Cookfs_InitVfsMountCmd(Tcl_Interp *interp) {

    Cookfs_CookfsRegister(interp);

    Tcl_CreateNamespace(interp, "::cookfs::c::vfs", NULL, NULL);

    Tcl_CreateObjCommand(interp, "::cookfs::c::Mount", CookfsMountCmd,
        (ClientData) NULL, NULL);

    Tcl_CreateAlias(interp, "::cookfs::Mount", interp,
        "::cookfs::c::Mount", 0, NULL);
    Tcl_CreateAlias(interp, "::vfs::cookfs::Mount", interp,
        "::cookfs::c::Mount", 0, NULL);

    Tcl_CreateObjCommand(interp, "::cookfs::c::Unmount", CookfsUnmountCmd,
        (ClientData) NULL, NULL);

    Tcl_CreateAlias(interp, "::cookfs::Unmount", interp,
        "::cookfs::c::Unmount", 0, NULL);

    return TCL_OK;

}

#define PROCESS_OPT_SWITCH(opt_name, var) \
    if (opt == opt_name) { \
        var = 1; \
        continue; \
    }

#define PROCESS_OPT_OBJ(opt_name, var) \
    if (opt == opt_name) { \
        var = objv[idx]; \
        continue; \
    }

#define PROCESS_OPT_INT(opt_name, var) \
    if (opt == opt_name) { \
        var = ival; \
        continue; \
    }

#define PROCESS_OPT_WIDEINT(opt_name, var) \
    if (opt == opt_name) { \
        var = wival; \
        continue; \
    }

static int CookfsMountCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    UNUSED(clientData);

    CookfsLog(printf("CookfsMountCmd: ENTER"));

#ifdef COOKFS_USETCLCMDS
    Tcl_Obj *pagesobject = NULL;
    Tcl_Obj *fsindexobject = NULL;
    int noregister = 0;
    Tcl_Obj *bootstrap = NULL;
#endif
    int nocommand = 0;
    Tcl_Obj *compression = NULL;
    int alwayscompress = 0;
    Tcl_Obj *compresscommand = NULL;
    Tcl_Obj *asynccompresscommand = NULL;
    Tcl_Obj *asyncdecompresscommand = NULL;
    int asyncdecompressqueuesize = 2;
    Tcl_Obj *decompresscommand = NULL;
    Tcl_WideInt endoffset = -1;
    Tcl_Obj *setmetadata = NULL;
    int readonly = 0;
    int writetomemory = 0;
    int pagecachesize = 8;
    int volume = 0;
    Tcl_WideInt pagesize = 262144;
    Tcl_WideInt smallfilesize = 32768;
    Tcl_WideInt smallfilebuffer = 4194304;
    int nodirectorymtime = 0;
    Tcl_Obj *pagehash = NULL;

    Tcl_Obj *archive = NULL;
    Tcl_Obj *local = NULL;

    static const char *const options[] = {
#ifdef COOKFS_USETCLCMDS
        "-pagesobject", "-fsindexobject", "-noregister", "-bootstrap",
#endif
        "-nocommand", "-compression", "-alwayscompress", "-compresscommand",
        "-asynccompresscommand", "-asyncdecompresscommand",
        "-asyncdecompressqueuesize", "-decompresscommand", "-endoffset",
        "-setmetadata", "-readonly", "-writetomemory", "-pagesize",
        "-pagecachesize", "-volume", "-smallfilesize", "-smallfilebuffer",
        "-nodirectorymtime", "-pagehash",
        NULL
    };

    enum options {
#ifdef COOKFS_USETCLCMDS
        OPT_PAGEOBJECT, OPT_FSINDEXOBJECT, OPT_NOREGISTER, OPT_BOOTSTRAP,
#endif
        OPT_NOCOMMAND, OPT_COMPRESSION, OPT_ALWAYSCOMPRESS, OPT_COMPRESSCOMMAND,
        OPT_ASYNCCOMPRESSCOMMAND, OPT_ASYNCDECOMPRESSCOMMAND,
        OPT_ASYNCDECOMPRESSQUEUESIZE, OPT_DECOMPRESSCOMMAND, OPT_ENDOFFSET,
        OPT_SETMETADATA, OPT_READONLY, OPT_WRITETOMEMORY, OPT_PAGESIZE,
        OPT_PAGECACHESIZE, OPT_VOLUME, OPT_SMALLFILESIZE, OPT_SMALLFILEBUFFER,
        OPT_NODIRECTORYMTIME, OPT_PAGEHASH
    };

    for (int idx = 1; idx < objc; idx++) {

        int opt;
        if (Tcl_GetIndexFromObj(interp, objv[idx], options,
            "option", TCL_EXACT, &opt) != TCL_OK) {

            // If the current argument is not an option but starts
            // with '-' - consider it a misspelled argument.
            char *arg = Tcl_GetString(objv[idx]);
            if (arg[0] == '-') {
                return TCL_ERROR;
            }

            // If the current argument is not an option, assume it is
            // archive or local argument.
            if (archive == NULL) {
                CookfsLog(printf("CookfsMountCmd: arg #%d is <archive>", idx));
                archive = objv[idx];
            } else if (local == NULL) {
                CookfsLog(printf("CookfsMountCmd: arg #%d is <local>", idx));
                local = objv[idx];
            } else {
                CookfsLog(printf("CookfsMountCmd: arg #%d is unknown", idx));
                Tcl_WrongNumArgs(interp, 1, objv, "?-option value ...? archive"
                    " local ?-option value ...?");
                return TCL_ERROR;
            }
            continue;
        }

        CookfsLog(printf("CookfsMountCmd: arg #%d is a known option", idx));
#ifdef COOKFS_USETCLCMDS
        PROCESS_OPT_SWITCH(OPT_NOREGISTER, noregister);
#endif
        PROCESS_OPT_SWITCH(OPT_NOCOMMAND, nocommand);
        PROCESS_OPT_SWITCH(OPT_ALWAYSCOMPRESS, alwayscompress);
        PROCESS_OPT_SWITCH(OPT_READONLY, readonly);
        PROCESS_OPT_SWITCH(OPT_WRITETOMEMORY, writetomemory);
        PROCESS_OPT_SWITCH(OPT_VOLUME, volume);
        PROCESS_OPT_SWITCH(OPT_NODIRECTORYMTIME, nodirectorymtime);

        // Other options require a single argument
        if (++idx == objc) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing argument to"
                " %s option", options[opt]));
            return TCL_ERROR;
        }

#ifdef COOKFS_USETCLCMDS
        PROCESS_OPT_OBJ(OPT_PAGEOBJECT, pagesobject);
        PROCESS_OPT_OBJ(OPT_FSINDEXOBJECT, fsindexobject);
        PROCESS_OPT_OBJ(OPT_BOOTSTRAP, bootstrap);
#endif
        PROCESS_OPT_OBJ(OPT_COMPRESSION, compression);
        PROCESS_OPT_OBJ(OPT_COMPRESSCOMMAND, compresscommand);
        PROCESS_OPT_OBJ(OPT_ASYNCCOMPRESSCOMMAND, asynccompresscommand);
        PROCESS_OPT_OBJ(OPT_ASYNCDECOMPRESSCOMMAND, asyncdecompresscommand);
        PROCESS_OPT_OBJ(OPT_DECOMPRESSCOMMAND, decompresscommand);
        PROCESS_OPT_OBJ(OPT_SETMETADATA, setmetadata);
        PROCESS_OPT_OBJ(OPT_PAGEHASH, pagehash);

        // OPT_ASYNCDECOMPRESSQUEUESIZE / OPT_PAGECACHESIZE - are unsigned int
        // values
        if (opt == OPT_ASYNCDECOMPRESSQUEUESIZE || opt == OPT_PAGECACHESIZE) {

            int ival;
            if (Tcl_GetIntFromObj(interp, objv[idx], &ival) != TCL_OK
                || ival < 0)
            {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unsigned integer"
                    " argument is expected for %s option, but got \"%s\"",
                    options[opt], Tcl_GetString(objv[idx])));
                return TCL_ERROR;
            }

            PROCESS_OPT_INT(OPT_ASYNCDECOMPRESSQUEUESIZE, asyncdecompressqueuesize);
            PROCESS_OPT_INT(OPT_PAGECACHESIZE, pagecachesize);

        }

        // Handle endoffset in a special way, since it is a signed Tcl_WideInt
        if (opt == OPT_ENDOFFSET) {
            if (Tcl_GetWideIntFromObj(interp, objv[idx], &endoffset) != TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("wide integer argument"
                    " is expected for %s option, but got \"%s\"", options[opt],
                    Tcl_GetString(objv[idx])));
                return TCL_ERROR;
            }
            // All ok. Let's go to the next argument.
            continue;
        }

        // Other options are an unsigned Tcl_WideInt
        Tcl_WideInt wival;
        if (Tcl_GetWideIntFromObj(interp, objv[idx], &wival) != TCL_OK || wival < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unsigned integer argument"
                " is expected for %s option, but got \"%s\"", options[opt],
                Tcl_GetString(objv[idx])));
            return TCL_ERROR;
        }

        PROCESS_OPT_WIDEINT(OPT_PAGESIZE, pagesize);
        PROCESS_OPT_WIDEINT(OPT_SMALLFILESIZE, smallfilesize);
        PROCESS_OPT_WIDEINT(OPT_SMALLFILEBUFFER, smallfilebuffer);

    }

    // Make sure that we have 2 mandatory arguments
    if (archive == NULL || local == NULL) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-option value ...? archive"
            " local ?-option value ...?");
        return TCL_ERROR;
    }

    if (smallfilesize > pagesize) {
        CookfsLog(printf("CookfsMountCmd: ERROR: smallfilesize [%ld]"
            " > pagesize [%ld]", smallfilesize, pagesize));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("smallfilesize cannot be"
            " larger than pagesize", -1));
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = NULL;
    Cookfs_Pages *pages = NULL;
    Cookfs_Fsindex *index = NULL;
    Cookfs_Writer *writer = NULL;

    Tcl_Obj *archiveActual = NULL;
    Tcl_Obj *localActual = NULL;

    Tcl_Obj *normalized;

    if (Tcl_GetCharLength(archive)) {
        CookfsLog(printf("CookfsMountCmd: normalize archive path [%s]",
            Tcl_GetString(archive)));
        // If the path is not empty, then normalize it
        normalized = Tcl_FSGetNormalizedPath(interp, archive);
        if (normalized == NULL) {
            CookfsLog(printf("CookfsMountCmd: got NULL"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not normalize"
                " archive path \"%s\"", Tcl_GetString(archive)));
            goto error;
        }
        CookfsLog(printf("CookfsMountCmd: got normalized path [%s]",
            Tcl_GetString(normalized)));
        archiveActual = normalized;
        Tcl_IncrRefCount(archiveActual);
    } else {
        CookfsLog(printf("CookfsMountCmd: use PWD as archive, since archive"
            " is an empty string"));
        // If the path is empty, then use PWD. Tcl_FSGetCwd() returns PWD
        // with already incremented refcount
        archiveActual = Tcl_FSGetCwd(interp);
        if (archiveActual == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to set archive"
                " to PWD", -1));
            goto error;
        }
    }

    if (!volume) {
        if (Tcl_GetCharLength(local)) {
            // If the path is not empty, then normalize it
            CookfsLog(printf("CookfsMountCmd: normalize local path [%s]",
                Tcl_GetString(local)));
            normalized = Tcl_FSGetNormalizedPath(interp, local);
            if (normalized == NULL) {
                CookfsLog(printf("CookfsMountCmd: got NULL"));
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not normalize"
                    " local path \"%s\"", Tcl_GetString(local)));
                goto error;
            }
            CookfsLog(printf("CookfsMountCmd: got normalized path [%s]",
                Tcl_GetString(normalized)));
            localActual = normalized;
            Tcl_IncrRefCount(localActual);
        } else {
            CookfsLog(printf("CookfsMountCmd: use PWD as archive, since"
                " archive is an empty string"));
            // If the path is empty, then use PWD. Tcl_FSGetCwd() returns PWD
            // with already incremented refcount
            localActual = Tcl_FSGetCwd(interp);
            if (localActual == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to set local"
                    " to PWD", -1));
                goto error;
            }
        }
    } else {
        CookfsLog(printf("CookfsMountCmd: use local as is, since it is"
            " a volume"));
        // Just use specified "local", but increase refcount
        localActual = local;
        Tcl_IncrRefCount(localActual);
    }

#ifdef COOKFS_USETCLCMDS
    if (pagesobject == NULL) {
#endif
        int oCompression;
        if (Cookfs_CompressionFromObj(interp, compression, &oCompression)
            != TCL_OK)
        {
            return TCL_ERROR;
        }

        CookfsLog(printf("CookfsMountCmd: creating the pages object"));
        pages = Cookfs_PagesInit(interp, archiveActual, readonly, oCompression,
            NULL, (endoffset == -1 ? 0 : 1), endoffset, 0,
            asyncdecompressqueuesize, compresscommand, decompresscommand,
            asynccompresscommand, asyncdecompresscommand);

        if (pages == NULL) {
            return TCL_ERROR;
        }

#ifdef COOKFS_USETCLCMDS
    } else {
        char *pagesCmd = Tcl_GetString(pagesobject);
        pages = Cookfs_PagesGetHandle(interp, pagesCmd);
        if (pages == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incorrect page object"
                " \"%s\" has been specified", pagesCmd));
            return TCL_ERROR;
        }
    }
#endif

    // set whether compression should always be enabled
    CookfsLog(printf("CookfsMountCmd: set pages always compress: %d",
        alwayscompress));
    Cookfs_PagesSetAlwaysCompress(pages, alwayscompress);
    // set up cache size
    CookfsLog(printf("CookfsMountCmd: set pages cache size: %d",
        pagecachesize));
    Cookfs_PagesSetCacheSize(pages, pagecachesize);

    // Archive path is not needed anymore
    Tcl_DecrRefCount(archiveActual);
    archiveActual = NULL;

#ifdef COOKFS_USETCLCMDS
    if (fsindexobject == NULL) {
#endif
        CookfsLog(printf("CookfsMountCmd: creating the index object"));
        index = Cookfs_FsindexFromPages(NULL, pages);
        if (index == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
                " index object", -1));
            goto error;
        }
#ifdef COOKFS_USETCLCMDS
    } else {
        char *indexCmd = Tcl_GetString(fsindexobject);
        index = Cookfs_FsindexGetHandle(interp, indexCmd);
        if (index == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incorrect fsindex object"
                " \"%s\" has been specified", indexCmd));
            goto error;
        }
    }
#endif

    const char *pagehashMetadataKey = "cookfs.pagehash";

    if (pages->dataNumPages) {
        CookfsLog(printf("CookfsMountCmd: pages contain data"));
        Tcl_Obj *pagehashActual = Cookfs_FsindexGetMetadata(index,
            pagehashMetadataKey);
        if (pagehashActual == NULL) {
            CookfsLog(printf("CookfsMountCmd: metadata doesn't contain"
                " pagehash, the default algo will be used"));
        } else {
            Tcl_IncrRefCount(pagehashActual);
            CookfsLog(printf("CookfsMountCmd: got pagehash from metadata [%s]",
                Tcl_GetString(pagehashActual)));
            CookfsLog(printf("CookfsMountCmd: set pagehash for pages"));
            // Don't set an error message in interp, we will set our own
            // to avoid confusion as this pagehash comes not from
            // specified parameters, but from metadata. This case is possible
            // when archive was created by some version of cookfs, and
            // the current version doesn't support this pagehash algorithm.
            int ret = Cookfs_PagesSetHashByObj(pages, pagehashActual, NULL);
            if (ret != TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to set pagehash"
                    " [%s] in pages object", Tcl_GetString(pagehashActual)));
            }
            Tcl_DecrRefCount(pagehashActual);
            if (ret != TCL_OK) {
                goto error;
            }
        }
    } else {

        CookfsLog(printf("CookfsMountCmd: pages don't contain data"));

#ifdef COOKFS_USETCLCMDS
        if (bootstrap != NULL) {
            CookfsLog(printf("CookfsMountCmd: bootstrap is specified"));
            int bootstrapLength;
            Tcl_GetByteArrayFromObj(bootstrap, &bootstrapLength);
            if (!bootstrapLength) {
                CookfsLog(printf("CookfsMountCmd: bootstrap is empty"));
            } else {
                CookfsLog(printf("CookfsMountCmd: add bootstrap"));
                int idx = Cookfs_PageAdd(pages, bootstrap);
                if (idx < 0) {
                    Tcl_Obj *err = Cookfs_PagesGetLastError(pages);
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to add"
                        " bootstrap: %s", (err == NULL ? "unknown error" :
                        Tcl_GetString(err))));
                    goto error;
                }
            }
        } else {
            CookfsLog(printf("CookfsMountCmd: bootstrap is not specified"));
        }
#endif

        // We will use this object as a flag whether we use the hash value
        // from the argument or the default value.
        Tcl_Obj *pagehashActual;
        if (pagehash == NULL) {
            pagehashActual = Tcl_NewStringObj("md5", -1);
            Tcl_IncrRefCount(pagehashActual);
            CookfsLog(printf("CookfsMountCmd: pagehash is not specified, use"
                " the default value [%s]", Tcl_GetString(pagehashActual)));
        } else {
            pagehashActual = pagehash;
            CookfsLog(printf("CookfsMountCmd: pagehash is specified [%s]",
                Tcl_GetString(pagehashActual)));
        }

        CookfsLog(printf("CookfsMountCmd: set pagehash for pages"));
        if (Cookfs_PagesSetHashByObj(pages, pagehashActual,
            interp) != TCL_OK)
        {
            // We have an error message in interp from
            // Cookfs_PagesSetHashByObj()
            // Release pagehashActual if it is not from arguments
            if (pagehash == NULL) {
                Tcl_DecrRefCount(pagehashActual);
            }
            goto error;
        }

        CookfsLog(printf("CookfsMountCmd: set pagehash in metadata"));
        Cookfs_FsindexSetMetadata(index, pagehashMetadataKey, pagehashActual);

        // If we use the default value, then this object should be released
        if (pagehash == NULL) {
            Tcl_DecrRefCount(pagehashActual);
        }

    }

    if (setmetadata != NULL) {
        CookfsLog(printf("CookfsMountCmd: setmetadata is specified"));
        Tcl_Obj **metadataKeyVal;
        int metadataCount;
        if (Tcl_ListObjGetElements(interp, setmetadata, &metadataCount,
            &metadataKeyVal) != TCL_OK)
        {
            CookfsLog(printf("CookfsMountCmd: could not convert setmetadata"
                " to a list"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not convert"
                " setmetadata option \"%s\" to list",
                Tcl_GetString(setmetadata)));
            goto error;
        }
        CookfsLog(printf("CookfsMountCmd: setmetadata was converted to list"
            " with %d length", metadataCount));
        if ((metadataCount % 2) != 0) {
            CookfsLog(printf("CookfsMountCmd: setmetadata list size is"
                " not even"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("setmetadata requires"
                " a list with an even number of elements, but got \"%s\"",
                Tcl_GetString(setmetadata)));
            goto error;
        }
        for (int i = 0; i < metadataCount; i++) {
            Tcl_Obj *key = metadataKeyVal[i++];
            Tcl_Obj *val = metadataKeyVal[i];
            CookfsLog(printf("CookfsMountCmd: setmetadata [%s] = [%s]",
                Tcl_GetString(key), Tcl_GetString(val)));
            Cookfs_FsindexSetMetadata(index, Tcl_GetString(key), val);
        }
    }

    CookfsLog(printf("CookfsMountCmd: creating the writer object"));
    writer = Cookfs_WriterInit(interp, pages, index, smallfilebuffer,
         smallfilesize, pagesize, writetomemory);
    if (writer == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
            " writer object", -1));
        goto error;
    }

    CookfsLog(printf("CookfsMountCmd: creating the vfs object"));
    vfs = Cookfs_VfsInit(interp, localActual, volume,
        (nodirectorymtime ? 0 : 1), readonly, pages, index, writer);
    if (vfs == NULL) {
        CookfsLog(printf("CookfsMountCmd: failed to create the vfs object"));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
            " vfs object", -1));
        goto error;
    }

    // We don't need localActual anymore
    Tcl_DecrRefCount(localActual);
    localActual = NULL;

    CookfsLog(printf("CookfsMountCmd: add mount point..."));
    if (!Cookfs_CookfsAddVfs(interp, vfs)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to add"
            " the mount point", -1));
        goto error;
    }

#ifdef COOKFS_USETCLCMDS
    if (!noregister) {
        CookfsLog(printf("CookfsMountCmd: registering the vfs in tclvfs..."));
        if (Cookfs_VfsRegisterInTclvfs(vfs) != TCL_OK) {
            Cookfs_CookfsRemoveVfs(interp, NULL, vfs);
            // We have an error message from Tclvfs in interp result
            CookfsLog(printf("CookfsMountCmd: failed to register vfs"
                " in tclvfs"));
            goto error;
        }
    } else {
        CookfsLog(printf("CookfsMountCmd: no need to register the vfs"
            " in tclvfs"));
    }
#endif

    Tcl_ResetResult(interp);

    if (!nocommand) {

        char cmd[128];
        sprintf(cmd, "::cookfs::c::vfs::mount%p", (void *)vfs);

        CookfsLog(printf("CookfsMountCmd: creating vfs command handler..."));
        vfs->commandToken = Tcl_CreateObjCommand(interp, cmd,
            CookfsMountHandleCmd, vfs, CookfsMountHandleCmdDeleteProc);

        Tcl_SetObjResult(interp, Tcl_NewStringObj(cmd, -1));

    } else {
        CookfsLog(printf("CookfsMountCmd: no need to create vfs"
            " command handler"));
    }

    CookfsLog(printf("CookfsMountCmd: ok [%s]", cmd));
    return TCL_OK;

error:

    if (archiveActual != NULL) {
        Tcl_DecrRefCount(archiveActual);
    }
    if (localActual != NULL) {
        Tcl_DecrRefCount(localActual);
    }

    // If VFS object exists, release only that object. Everything else
    // will be freed up because of it.
    if (vfs != NULL) {
        Cookfs_VfsFini(interp, vfs, NULL);
    } else {
        // Always release writer as it is creating within this function
        if (writer != NULL) {
            Cookfs_WriterFini(writer);
        }
        // If no fsindex object was specified and a fsindex object was created
        // by this procedure, release the fsindex object.
#ifdef COOKFS_USETCLCMDS
        if (fsindexobject == NULL && index != NULL) {
#else
        if (index != NULL) {
#endif
            Cookfs_FsindexFini(index);
        }
        // If no pages object was specified and a pages object was created by
        // this procedure, release the pages object.
#ifdef COOKFS_USETCLCMDS
        if (pagesobject == NULL && pages != NULL) {
#else
        if (pages != NULL) {
#endif
            Cookfs_PagesFini(pages);
        }
    }
    return TCL_ERROR;

}

static int CookfsUnmountCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    CookfsLog(printf("CookfsUnmountCmd: ENTER args count:%d", objc));

    Tcl_Obj *arg;
#ifdef COOKFS_USETCLCMDS
    if (objc < 2 || objc > 3 ||
        (objc == 3 && strcmp(Tcl_GetString(objv[1]), "-unregister") != 0))
    {
        CookfsLog(printf("CookfsUnmountCmd: wrong # args"));
        Tcl_WrongNumArgs(interp, 1, objv, "?-unregister? fsid|local");
        return TCL_ERROR;
    }

    if (objc == 2) {
        // args number is 2, i.e. we were called without -unregister switch
        // and the first argument is our real argument
        arg = objv[1];
    } else {
        // args number is 3, i.e. we were called with -unregister switch
        // and the second argument is our real argument
        arg = objv[2];
    }
#else
    if (objc != 2) {
        CookfsLog(printf("CookfsUnmountCmd: wrong # args"));
        Tcl_WrongNumArgs(interp, 1, objv, "fsid|local");
        return TCL_ERROR;
    }
    arg = objv[1];
#endif

    CookfsLog(printf("CookfsUnmountCmd: unmount [%s]",
        Tcl_GetString(arg)));

    Cookfs_Vfs *vfs = NULL;

    // First try checking if the argument is mountid
    if (sscanf(Tcl_GetString(arg), "::cookfs::c::vfs::mount%p",
        (void **)&vfs) == 1)
    {
        if (!Cookfs_CookfsIsVfsExist(vfs)) {
            CookfsLog(printf("CookfsUnmountCmd: given argument is"
                " invalid fsid"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("given argument \"%s\""
                " is invalid fsid", Tcl_GetString(arg)));
            return TCL_ERROR;
        } else {
            CookfsLog(printf("CookfsUnmountCmd: given argument is"
                " a fsid"));
        }
    } else {
        CookfsLog(printf("CookfsUnmountCmd: given argument is not"
            " a fsid"));
    }

    // If it was not found above, then check if argument is a mount point
    if (vfs == NULL) {
        vfs = Cookfs_CookfsFindVfs(arg, -1);
        if (vfs == NULL) {
            CookfsLog(printf("CookfsUnmountCmd: given argument is not"
                " a mount path"));
        } else {
            CookfsLog(printf("CookfsUnmountCmd: given argument is"
                " a mount path, mount struct [%p]", (void *)vfs));
        }
    }

    // If we could not find the mount again, try normalized path
    if (vfs == NULL) {
        Tcl_Obj *normalized = Tcl_FSGetNormalizedPath(interp, arg);
        if (normalized == NULL) {
            CookfsLog(printf("CookfsUnmountCmd: could not convert given argument"
                " to normalized path"));
        } else {
            CookfsLog(printf("CookfsUnmountCmd: check for normalized"
                " path [%s]", Tcl_GetString(normalized)));
            vfs = Cookfs_CookfsFindVfs(normalized, -1);
            if (vfs == NULL) {
                CookfsLog(printf("CookfsUnmountCmd: given argument is not"
                    " a normalized mount path"));
            } else {
                CookfsLog(printf("CookfsUnmountCmd: given argument is"
                    " a mount path, mount struct [%p]", (void *)vfs));
            }
        }
    }

    // Could not found the mount entry
    if (vfs == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("given argument \"%s\""
            " is invalid mount point or fsid", Tcl_GetString(arg)));
        return TCL_ERROR;
    }

    if (vfs->isDead) {
        CookfsLog(printf("CookfsUnmountCmd: the mount point is already"
            " in a terminating state"));
        return TCL_OK;
    }

#ifdef COOKFS_USETCLCMDS
    // If we were called with -unregister switch, then we are in unregister
    // callback from tclvfs. Cancel vfs registration status to avoid
    // double unregistration.
    if (objc == 3) {
        CookfsLog(printf("CookfsUnmountCmd: -unregister switch present,"
            " cancel tclvfs registration status"));
        vfs->isRegistered = 0;
    }
#endif

    // Try to remove the mount point from mount list
    CookfsLog(printf("CookfsUnmountCmd: remove the mount point"));
    vfs = Cookfs_CookfsRemoveVfs(interp, NULL, vfs);
    // If vfs is NULL, then Cookfs_CookfsRemoveVfs() could not find
    // the vfs in the vfs list. Return error.
    if (vfs == NULL) {
        CookfsLog(printf("CookfsUnmountCmd: got NULL"));
        return TCL_ERROR;
    }

    // Terminate the mount point
    CookfsLog(printf("CookfsUnmountCmd: terminate the mount point"));
    Tcl_WideInt pagesCloseOffset;
    if (Cookfs_VfsFini(interp, vfs, &pagesCloseOffset) != TCL_OK) {
        CookfsLog(printf("CookfsUnmountCmd: termination failed"));
        return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pagesCloseOffset));

    CookfsLog(printf("CookfsUnmountCmd: return ok and [%ld]",
        pagesCloseOffset));
    return TCL_OK;
}


static void CookfsMountHandleCmdDeleteProc(ClientData clientData) {
    Cookfs_Vfs *vfs = (Cookfs_Vfs *)clientData;
    vfs->commandToken = NULL;
}

#ifdef COOKFS_USETCLCMDS
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandGetpages;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandGetindex;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandGetwriter;
#endif
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandGetmetadata;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandSetmetadata;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandAside;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandWritetomemory;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandFilesize;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandSmallfilebuffersize;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandCompression;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandWritefiles;
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandOptimizelist;

static int CookfsMountHandleCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    Cookfs_Vfs *vfs = (Cookfs_Vfs *)clientData;

    static const char *const commands[] = {
#ifdef COOKFS_USETCLCMDS
        "getpages", "getindex", "getwriter",
#endif
        "getmetadata", "setmetadata", "aside", "writetomemory", "filesize",
        "smallfilebuffersize", "compression", "writeFiles", "optimizelist",
        NULL
    };
    enum commands {
#ifdef COOKFS_USETCLCMDS
        cmdGetpages, cmdGetindex, cmdGetwriter,
#endif
        cmdGetmetadata, cmdSetmetadata, cmdAside, cmdWritetomemory, cmdFilesize,
        cmdSmallfilebuffersize, cmdCompression, cmdWritefiles, cmdOptimizelist
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ?args?");
        return TCL_ERROR;
    }

    int command;
    if (Tcl_GetIndexFromObj(interp, objv[1], commands, "command", 0,
            &command) != TCL_OK)
    {
        return TCL_ERROR;
    }

    switch ((enum commands) command) {
#ifdef COOKFS_USETCLCMDS
    case cmdGetpages:
        return CookfsMountHandleCommandGetpages(vfs, interp, objc, objv);
    case cmdGetindex:
        return CookfsMountHandleCommandGetindex(vfs, interp, objc, objv);
    case cmdGetwriter:
        return CookfsMountHandleCommandGetwriter(vfs, interp, objc, objv);
#endif
    case cmdGetmetadata:
        return CookfsMountHandleCommandGetmetadata(vfs, interp, objc, objv);
    case cmdSetmetadata:
        return CookfsMountHandleCommandSetmetadata(vfs, interp, objc, objv);
    case cmdAside:
        return CookfsMountHandleCommandAside(vfs, interp, objc, objv);
    case cmdWritetomemory:
        return CookfsMountHandleCommandWritetomemory(vfs, interp, objc, objv);
    case cmdFilesize:
        return CookfsMountHandleCommandFilesize(vfs, interp, objc, objv);
    case cmdSmallfilebuffersize:
        return CookfsMountHandleCommandSmallfilebuffersize(vfs, interp, objc, objv);
    case cmdCompression:
        return CookfsMountHandleCommandCompression(vfs, interp, objc, objv);
    case cmdWritefiles:
        return CookfsMountHandleCommandWritefiles(vfs, interp, objc, objv);
    case cmdOptimizelist:
        return CookfsMountHandleCommandOptimizelist(vfs, interp, objc, objv);
    }

    return TCL_OK;
}

#ifdef COOKFS_USETCLCMDS

static int CookfsMountHandleCommandGetpages(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, CookfsGetPagesObjectCmd(interp, vfs->pages));
    return TCL_OK;
}

static int CookfsMountHandleCommandGetindex(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, CookfsGetFsindexObjectCmd(interp, vfs->index));
    return TCL_OK;
}

static int CookfsMountHandleCommandGetwriter(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, CookfsGetWriterObjectCmd(interp, vfs->writer));
    return TCL_OK;
}

#endif

static int CookfsMountHandleCommandGetmetadata(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    return CookfsFsindexCmdGetMetadata(vfs->index, interp, objc, objv);
}

static int CookfsMountHandleCommandSetmetadata(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (Cookfs_VfsIsReadonly(vfs)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Archive is read-only", -1));
        return TCL_ERROR;
    }
    return CookfsFsindexCmdSetMetadata(vfs->index, interp, objc, objv);
}

static int CookfsMountHandleCommandAside(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("CookfsMountHandleCommandAside: enter"));

    if (objc != 3) {
        CookfsLog(printf("CookfsMountHandleCommandAside: ERR: wrong # args"));
        Tcl_WrongNumArgs(interp, 2, objv, "filename");
        return TCL_ERROR;
    }

    if (Cookfs_WriterGetWritetomemory(vfs->writer)) {
        CookfsLog(printf("CookfsMountHandleCommandAside: ERROR: write to memory"
            " option enabled"));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Write to memory option"
            " enabled; not creating add-aside archive", -1));
        return TCL_ERROR;
    } else {
        CookfsLog(printf("CookfsMountHandleCommandAside: writer"
            " writetomemory: false"));
    }

    CookfsLog(printf("CookfsMountHandleCommandAside: purge writer..."));
    if (Cookfs_WriterPurge(vfs->writer) != TCL_OK) {
        return TCL_ERROR;
    }

    CookfsLog(printf("CookfsMountHandleCommandAside: run pages aside..."));
    if (CookfsPagesCmdAside(vfs->pages, interp, objc, objv) != TCL_OK) {
        return TCL_ERROR;
    }

    CookfsLog(printf("CookfsMountHandleCommandAside: refresh index..."));
    if (Cookfs_FsindexFromPages(vfs->index, vfs->pages) == NULL) {
        return TCL_ERROR;
    }

    CookfsLog(printf("CookfsMountHandleCommandAside: set writable mode"));
    Cookfs_VfsSetReadonly(vfs, 0);

    CookfsLog(printf("CookfsMountHandleCommandAside: ok"));
    return TCL_OK;
}

static int CookfsMountHandleCommandWritetomemory(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Cookfs_WriterSetWritetomemory(vfs->writer, 1);
    Cookfs_VfsSetReadonly(vfs, 0);
    return TCL_OK;
}

static int CookfsMountHandleCommandFilesize(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(Cookfs_GetFilesize(
        vfs->pages)));
    return TCL_OK;
}

static int CookfsMountHandleCommandSmallfilebuffersize(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    Tcl_WideInt size = Cookfs_WriterGetSmallfilebuffersize(vfs->writer);
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(size));
    return TCL_OK;
}

static int CookfsMountHandleCommandCompression(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc > 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "?type?");
        return TCL_ERROR;
    }

    if (objc == 3) {
        //always purge small files cache when compression changes
        int ret = Cookfs_WriterPurge(vfs->writer);
        if (ret != TCL_OK) {
            return ret;
        }
    }

    return CookfsPagesCmdCompression(vfs->pages, interp, objc, objv);
}

static int CookfsMountHandleCommandWritefiles(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    return CookfsWriterHandleCommandWrite(vfs->writer, interp, objc, objv);
}

static int CookfsMountHandleCommandOptimizelist(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: enter;"
        " objc: %d", objc));

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "base filelist");
        return TCL_ERROR;
    }

    int i;

    Tcl_Obj **fileTails;
    int fileCount;
    if (Tcl_ListObjGetElements(interp, objv[3], &fileCount, &fileTails)
        != TCL_OK)
    {
        return TCL_ERROR;
    }

    Cookfs_Pages *pages = vfs->pages;

    if (!pages->dataNumPages) {
        CookfsLog(printf("CookfsMountHandleCommandOptimizelist: there is"
            " no pages, return the list as is"));
        Tcl_SetObjResult(interp, objv[2]);
        return TCL_OK;
    }

    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: alloc pageFiles"));
    Tcl_Obj **pageFiles = ckalloc(sizeof(Tcl_Obj *) * pages->dataNumPages);
    if (pageFiles == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to alloc"
            " pageFiles", -1));
        return TCL_ERROR;
    }

    for (i = 0; i < pages->dataNumPages; i++) {
        pageFiles[i] = NULL;
    }

    Tcl_Obj *largeFiles = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(largeFiles);

    Tcl_Obj *baseTemplate = Tcl_NewListObj(1, &objv[2]);
    Tcl_IncrRefCount(baseTemplate);

    Cookfs_Fsindex *index = vfs->index;

    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: checking %d files",
        fileCount));
    for (i = 0; i < fileCount; i++) {

        Tcl_Obj *fileTail = fileTails[i];
        CookfsLog(printf("CookfsMountHandleCommandOptimizelist: checking"
            " file [%s]", Tcl_GetString(fileTail)));

        // Construct full path
        Tcl_Obj *fullName = Tcl_DuplicateObj(baseTemplate);
        Tcl_IncrRefCount(fullName);

        Tcl_ListObjAppendElement(NULL, fullName, fileTail);

        Tcl_Obj *fullNameJoined = Tcl_FSJoinPath(fullName, -1);
        Tcl_IncrRefCount(fullNameJoined);

        CookfsLog(printf("CookfsMountHandleCommandOptimizelist: full path:"
            " [%s]", Tcl_GetString(fullNameJoined)));

        Tcl_Obj *fullNameSplit = Tcl_FSSplitPath(fullNameJoined, NULL);
        Tcl_IncrRefCount(fullNameSplit);

        Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, fullNameSplit);

        Tcl_Obj *listToAdd = NULL;
        int pageNum = -1;

        if (entry == NULL) {
            CookfsLog(printf("CookfsMountHandleCommandOptimizelist: got NULL"
                " entry"));
            listToAdd = largeFiles;
        } else if (entry->fileBlocks != 1) {
            CookfsLog(printf("CookfsMountHandleCommandOptimizelist: fileBlocks"
                " [%d] is not 1", entry->fileBlocks));
            listToAdd = largeFiles;
        } else {
            // Check if the file has correct page number
            pageNum = entry->data.fileInfo.fileBlockOffsetSize[0];
            if (pageNum < 0 || pageNum >= pages->dataNumPages) {
                CookfsLog(printf("CookfsMountHandleCommandOptimizelist:"
                    " incorrect page number: %d", pageNum));
                listToAdd = largeFiles;
            }
        }

        if (listToAdd == NULL) {
            if (pageFiles[pageNum] == NULL) {
                pageFiles[pageNum] = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(pageFiles[pageNum]);
            }
            listToAdd = pageFiles[pageNum];
            CookfsLog(printf("CookfsMountHandleCommandOptimizelist: add to"
                " small file list, page: %d", pageNum));
        } else {
            CookfsLog(printf("CookfsMountHandleCommandOptimizelist: add to"
                " large file list"));
        }

        Tcl_ListObjAppendElement(NULL, listToAdd, fileTail);

        // Cleanup
        Tcl_DecrRefCount(fullNameSplit);
        Tcl_DecrRefCount(fullNameJoined);
        Tcl_DecrRefCount(fullName);

    }

    Tcl_DecrRefCount(baseTemplate);

    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: create a small"
        " file list"));
    Tcl_Obj *smallFiles = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(smallFiles);

    for (i = 0; i < pages->dataNumPages; i++) {
        if (pageFiles[i] != NULL) {
            CookfsLog(printf("CookfsMountHandleCommandOptimizelist: add files"
                " from page %d to small file list", i));
            Tcl_ListObjAppendList(interp, smallFiles, pageFiles[i]);
            Tcl_DecrRefCount(pageFiles[i]);
        }
    }

    ckfree(pageFiles);

    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: add the large"
        " files to the small files"));
    Tcl_ListObjAppendList(interp, smallFiles, largeFiles);
    Tcl_DecrRefCount(largeFiles);

    Tcl_SetObjResult(interp, smallFiles);
    CookfsLog(printf("CookfsMountHandleCommandOptimizelist: ok [%s]",
        Tcl_GetString(smallFiles)));
    Tcl_DecrRefCount(smallFiles);

    return TCL_OK;

}
