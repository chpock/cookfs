/*
 * vfsCmd.c
 *
 * Provides Tcl commands for mounting cookfs
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "vfs.h"
#include "fsindexIO.h"
#include "vfsCmd.h"
#include "vfsVfs.h"
#include "vfsDriver.h"
#include "pagesCompr.h"
#include "pagesCmd.h"
#include "fsindexCmd.h"
#include "writerCmd.h"

#define COOKFS_PROP_DEFAULT_PAGESIZE        262144
#define COOKFS_PROP_DEFAULT_SMALLFILESIZE   32768
#define COOKFS_PROP_DEFAULT_SMALLFILEBUFFER 4194304

struct _Cookfs_VfsProps {

//#ifdef COOKFS_USETCLCMDS
    Tcl_Obj *pagesobject;
    Tcl_Obj *fsindexobject;
    int noregister;
    Tcl_Obj *bootstrap;
//#endif

    int nocommand;
    int alwayscompress;

    Cookfs_CompressionType compression;
    int compressionlevel;

    Tcl_Obj *compresscommand;
    Tcl_Obj *decompresscommand;
    Tcl_Obj *asynccompresscommand;
    Tcl_Obj *asyncdecompresscommand;

    int asyncdecompressqueuesize;
    Tcl_WideInt endoffset;
    Tcl_Obj *setmetadata;
    int readonly;
    int writetomemory;
    int pagecachesize;
    int volume;
    Tcl_WideInt pagesize;
    Tcl_WideInt smallfilesize;
    Tcl_WideInt smallfilebuffer;
    int nodirectorymtime;
    Cookfs_HashType pagehash;

    int shared;

//#ifdef COOKFS_USECCRYPTO
    Tcl_Obj *password;
    int encryptkey;
    int encryptlevel;
//#endif /* COOKFS_USECCRYPTO */

    Tcl_Obj *fileset;

};

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

Cookfs_VfsProps *Cookfs_VfsPropsInit(void) {
    Cookfs_VfsProps *p = ckalloc(sizeof(Cookfs_VfsProps));
    if (p == NULL) {
        Tcl_Panic("Cookfs_VfsPropsInit(): failed to alloc");
        // Just to avoid complaints from cppcheck
        return NULL;
    }
    memset(p, 0, sizeof(Cookfs_VfsProps));

    // Since we have cleared p, we don't need to set NULL / null values.
    // However, let's leave these values here as comments so that we have
    // a clear idea of the default values.

    // p->pagesobject = NULL;
    // p->fsindexobject = NULL;
    // p->bootstrap = NULL;

#ifdef COOKFS_USETCLCMDS
    // p->noregister = 0;
#else
    p->noregister = -1;
#endif /* COOKFS_USETCLCMDS */

    // p->nocommand = 0;
    p->compression = COOKFS_COMPRESSION_DEFAULT;
    // p->compressionlevel = 0;
    // p->alwayscompress = 0;
    // p->compresscommand = NULL;
    // p->asynccompresscommand = NULL;
    // p->asyncdecompresscommand = NULL;
    p->asyncdecompressqueuesize = 2;
    // p->decompresscommand = NULL;
    p->endoffset = -1;
    // p->setmetadata = NULL;
    // p->readonly = 0;
    // p->writetomemory = 0;
    p->pagecachesize = 8;
    // p->volume = 0;
    p->pagesize = -1;
    p->smallfilesize = -1;
    p->smallfilebuffer = -1;
    // p->nodirectorymtime = 0;
    p->pagehash = COOKFS_HASH_DEFAULT;
    // p->shared = 0;
    // p->fileset = NULL;

    // p->password = NULL;
    // p->encryptkey = 0;
    p->encryptlevel = -1;

#ifdef COOKFS_USECCRYPTO
    // p->encryptkey = 0;
#else
    p->encryptkey = -1;
#endif /* COOKFS_USECCRYPTO */

    CookfsLog(printf("return: %p", (void *)p));
    return p;
}

void Cookfs_VfsPropsFree(Cookfs_VfsProps *p) {
    ckfree(p);
}

void Cookfs_VfsPropSet(Cookfs_VfsProps *p, Cookfs_VfsPropertiesType type,
    intptr_t value)
{
    switch (type) {
    case COOKFS_PROP_PAGESOBJ:
        p->pagesobject = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_FSINDEXOBJ:
        p->fsindexobject = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_NOREGISTER:
        p->noregister = value;
        break;
    case COOKFS_PROP_BOOTSTRAP:
        p->bootstrap = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_NOCOMMAND:
        p->nocommand = value;
        break;
    case COOKFS_PROP_COMPRESSION:
        p->compression = (Cookfs_CompressionType)value;
        break;
    case COOKFS_PROP_COMPRESSIONLEVEL:
        p->compressionlevel = value;
        break;
    case COOKFS_PROP_ALWAYSCOMPRESS:
        p->alwayscompress = value;
        break;
    case COOKFS_PROP_COMPRESSCOMMAND:
        p->compresscommand = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_DECOMPRESSCOMMAND:
        p->decompresscommand = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_ASYNCCOMPRESSCOMMAND:
        p->asynccompresscommand = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_ASYNCDECOMPRESSCOMMAND:
        p->asyncdecompresscommand = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_ASYNCDECOMPRESSQUEUESIZE:
        p->asyncdecompressqueuesize = value;
        break;
    case COOKFS_PROP_ENDOFFSET:
        p->endoffset = value;
        break;
    case COOKFS_PROP_SETMETADATA:
        p->setmetadata = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_READONLY:
        p->readonly = value;
        break;
    case COOKFS_PROP_WRITETOMEMORY:
        p->writetomemory = value;
        break;
    case COOKFS_PROP_PAGECACHESIZE:
        p->pagecachesize = value;
        break;
    case COOKFS_PROP_VOLUME:
        p->volume = value;
        break;
    case COOKFS_PROP_PAGESIZE:
        p->pagesize = value;
        break;
    case COOKFS_PROP_SMALLFILESIZE:
        p->smallfilesize = value;
        break;
    case COOKFS_PROP_SMALLFILEBUFFER:
        p->smallfilebuffer = value;
        break;
    case COOKFS_PROP_NODIRECTORYMTIME:
        p->nodirectorymtime = value;
        break;
    case COOKFS_PROP_PAGEHASH:
        p->pagehash = (Cookfs_HashType)value;
        break;
    case COOKFS_PROP_SHARED:
        p->shared = value;
        break;
    case COOKFS_PROP_PASSWORD:
        p->password = (Tcl_Obj *)value;
        break;
    case COOKFS_PROP_ENCRYPTKEY:
        p->encryptkey = value;
        break;
    case COOKFS_PROP_ENCRYPTLEVEL:
        p->encryptlevel = value;
        break;
    case COOKFS_PROP_FILESET:
        p->fileset = (Tcl_Obj *)value;
        break;
    }
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

    int rc;

    CookfsLog(printf("ENTER"));

    static const char *const options[] = {
#ifdef COOKFS_USETCLCMDS
        "-pagesobject", "-fsindexobject", "-noregister", "-bootstrap",
#endif /* COOKFS_USETCLCMDS */
#ifdef COOKFS_USECCRYPTO
        "-password", "-encryptkey", "-encryptlevel",
#endif /* COOKFS_USECCRYPTO */
#if defined(COOKFS_USECALLBACKS)
        "-compresscommand",
        "-asynccompresscommand", "-asyncdecompresscommand",
        "-asyncdecompressqueuesize", "-decompresscommand",
#endif /* COOKFS_USECALLBACKS */
        "-nocommand", "-compression", "-alwayscompress", "-endoffset",
        "-setmetadata", "-readonly", "-writetomemory", "-pagesize",
        "-pagecachesize", "-volume", "-smallfilesize", "-smallfilebuffer",
        "-nodirectorymtime", "-pagehash", "-shared", "-fileset",
        NULL
    };

    enum options {
#ifdef COOKFS_USETCLCMDS
        OPT_PAGEOBJECT, OPT_FSINDEXOBJECT, OPT_NOREGISTER, OPT_BOOTSTRAP,
#endif
#ifdef COOKFS_USECCRYPTO
        OPT_PASSWORD, OPT_ENCRYPTKEY, OPT_ENCRYPTLEVEL,
#endif /* COOKFS_USECCRYPTO */
#if defined(COOKFS_USECALLBACKS)
        OPT_COMPRESSCOMMAND,
        OPT_ASYNCCOMPRESSCOMMAND, OPT_ASYNCDECOMPRESSCOMMAND,
        OPT_ASYNCDECOMPRESSQUEUESIZE, OPT_DECOMPRESSCOMMAND,
#endif /* COOKFS_USECALLBACKS */
        OPT_NOCOMMAND, OPT_COMPRESSION, OPT_ALWAYSCOMPRESS, OPT_ENDOFFSET,
        OPT_SETMETADATA, OPT_READONLY, OPT_WRITETOMEMORY, OPT_PAGESIZE,
        OPT_PAGECACHESIZE, OPT_VOLUME, OPT_SMALLFILESIZE, OPT_SMALLFILEBUFFER,
        OPT_NODIRECTORYMTIME, OPT_PAGEHASH, OPT_SHARED, OPT_FILESET
    };

    Cookfs_VfsProps *props = Cookfs_VfsPropsInit();

    Tcl_Obj *archive = NULL;
    Tcl_Obj *local = NULL;

    Tcl_Obj *compression = NULL;
    Tcl_Obj *pagehash = NULL;

    for (int idx = 1; idx < objc; idx++) {

        int opt;
        if (Tcl_GetIndexFromObj(interp, objv[idx], options,
            "option", TCL_EXACT, &opt) != TCL_OK) {

            // If the current argument is not an option but starts
            // with '-' - consider it a misspelled argument.
            char *arg = Tcl_GetString(objv[idx]);
            if (arg[0] == '-') {
                goto error;
            }

            // If the current argument is not an option, assume it is
            // archive or local argument.
            if (archive == NULL) {
                CookfsLog(printf("arg #%d is <archive>", idx));
                archive = objv[idx];
            } else if (local == NULL) {
                CookfsLog(printf("arg #%d is <local>", idx));
                local = objv[idx];
            } else {
                CookfsLog(printf("arg #%d is unknown", idx));
                goto wrongArgNum;
            }
            continue;
        }

        CookfsLog(printf("arg #%d is a known option", idx));
#ifdef COOKFS_USETCLCMDS
        PROCESS_OPT_SWITCH(OPT_NOREGISTER, props->noregister);
#endif
#ifdef COOKFS_USECCRYPTO
        PROCESS_OPT_SWITCH(OPT_ENCRYPTKEY, props->encryptkey);
#endif /* COOKFS_USECCRYPTO */
        PROCESS_OPT_SWITCH(OPT_NOCOMMAND, props->nocommand);
        PROCESS_OPT_SWITCH(OPT_ALWAYSCOMPRESS, props->alwayscompress);
        PROCESS_OPT_SWITCH(OPT_READONLY, props->readonly);
        PROCESS_OPT_SWITCH(OPT_WRITETOMEMORY, props->writetomemory);
        PROCESS_OPT_SWITCH(OPT_VOLUME, props->volume);
        PROCESS_OPT_SWITCH(OPT_NODIRECTORYMTIME, props->nodirectorymtime);
        PROCESS_OPT_SWITCH(OPT_SHARED, props->shared);

        // Other options require a single argument
        if (++idx == objc) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing argument to"
                " %s option", options[opt]));
            goto error;
        }

#ifdef COOKFS_USETCLCMDS
        PROCESS_OPT_OBJ(OPT_PAGEOBJECT, props->pagesobject);
        PROCESS_OPT_OBJ(OPT_FSINDEXOBJECT, props->fsindexobject);
        PROCESS_OPT_OBJ(OPT_BOOTSTRAP, props->bootstrap);
#endif
#ifdef COOKFS_USECCRYPTO
        PROCESS_OPT_OBJ(OPT_PASSWORD, props->password);
#endif /* COOKFS_USECCRYPTO */
        PROCESS_OPT_OBJ(OPT_COMPRESSION, compression);
#if defined(COOKFS_USECALLBACKS)
        PROCESS_OPT_OBJ(OPT_COMPRESSCOMMAND, props->compresscommand);
        PROCESS_OPT_OBJ(OPT_ASYNCCOMPRESSCOMMAND, props->asynccompresscommand);
        PROCESS_OPT_OBJ(OPT_ASYNCDECOMPRESSCOMMAND, props->asyncdecompresscommand);
        PROCESS_OPT_OBJ(OPT_DECOMPRESSCOMMAND, props->decompresscommand);
#endif /* COOKFS_USECALLBACKS */
        PROCESS_OPT_OBJ(OPT_SETMETADATA, props->setmetadata);
        PROCESS_OPT_OBJ(OPT_PAGEHASH, pagehash);
        PROCESS_OPT_OBJ(OPT_FILESET, props->fileset);

        // OPT_ASYNCDECOMPRESSQUEUESIZE / OPT_PAGECACHESIZE - are unsigned int
        // values
        if (opt == OPT_PAGECACHESIZE
#if defined(COOKFS_USECALLBACKS)
            || opt == OPT_ASYNCDECOMPRESSQUEUESIZE
#endif /* COOKFS_USECALLBACKS */
        ) {

            int ival;
            if (Tcl_GetIntFromObj(interp, objv[idx], &ival) != TCL_OK
                || ival < 0)
            {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unsigned integer"
                    " argument is expected for %s option, but got \"%s\"",
                    options[opt], Tcl_GetString(objv[idx])));
                goto error;
            }

#if defined(COOKFS_USECALLBACKS)
            PROCESS_OPT_INT(OPT_ASYNCDECOMPRESSQUEUESIZE, props->asyncdecompressqueuesize);
#endif /* COOKFS_USECALLBACKS */
            PROCESS_OPT_INT(OPT_PAGECACHESIZE, props->pagecachesize);

        }

        // OPT_ENCRYPTLEVEL - is int
#ifdef COOKFS_USECCRYPTO
        if (opt == OPT_ENCRYPTLEVEL) {

            int ival;
            if (Tcl_GetIntFromObj(interp, objv[idx], &ival) != TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("integer"
                    " argument is expected for %s option, but got \"%s\"",
                    options[opt], Tcl_GetString(objv[idx])));
                goto error;
            }

            PROCESS_OPT_INT(OPT_ENCRYPTLEVEL, props->encryptlevel);
        }
#endif /* COOKFS_USECCRYPTO */

        // Handle endoffset in a special way, since it is a signed Tcl_WideInt
        if (opt == OPT_ENDOFFSET) {
            if (Tcl_GetWideIntFromObj(interp, objv[idx], &props->endoffset)
                != TCL_OK)
            {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("wide integer argument"
                    " is expected for %s option, but got \"%s\"", options[opt],
                    Tcl_GetString(objv[idx])));
                goto error;
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
            goto error;
        }

        PROCESS_OPT_WIDEINT(OPT_PAGESIZE, props->pagesize);
        PROCESS_OPT_WIDEINT(OPT_SMALLFILESIZE, props->smallfilesize);
        PROCESS_OPT_WIDEINT(OPT_SMALLFILEBUFFER, props->smallfilebuffer);

    }

    // Validate the compression argument
    if (compression != NULL) {
        if (Cookfs_CompressionFromObj(interp, compression, &props->compression,
            &props->compressionlevel) != TCL_OK)
        {
            goto error;
        }
    }

    // Validate the pagehash argument
    if (pagehash != NULL) {
        if (Cookfs_HashFromObj(interp, pagehash, &props->pagehash) != TCL_OK) {
            rc = TCL_ERROR;
            goto error;
        }
    }

    // Make sure that we have 2 mandatory arguments
    if (archive == NULL || local == NULL) {
        // However, when 'writetomemory' is true, we can accept only
        // one argument.
        if (props->writetomemory && archive != NULL) {
            local = archive;
            archive = NULL;
        } else {
            goto wrongArgNum;
        }
    }

    rc = Cookfs_Mount(interp, archive, local, props);

    goto done;

wrongArgNum:

    Tcl_WrongNumArgs(interp, 1, objv, "?-option value ...? archive"
        " local ?-option value ...?");

error:
    rc = TCL_ERROR;

done:
    Cookfs_VfsPropsFree(props);
    return rc;
}

int Cookfs_Mount(Tcl_Interp *interp, Tcl_Obj *archive, Tcl_Obj *local,
    Cookfs_VfsProps *props)
{

    CookfsLog(printf("ENTER"));

    int freeProps = 0;
    if (props == NULL) {
        props = Cookfs_VfsPropsInit();
        freeProps = 1;
    }

    Cookfs_Vfs *vfs = NULL;
    Cookfs_Pages *pages = NULL;
    Cookfs_Fsindex *index = NULL;
    Cookfs_Writer *writer = NULL;

    Tcl_Obj *archiveActual = NULL;
    Tcl_Obj *localActual = NULL;
    Tcl_Obj *err = NULL;

    Tcl_Obj *normalized;

    // Validate the properties

#ifndef TCL_THREADS
    if (props->shared) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("shared VFS between threads"
            " is not supported by this package build", -1));
        goto error;
    }
#endif /* TCL_THREADS */

#ifndef COOKFS_USETCLCMDS
    if (props->pagesobject != NULL || props->fsindexobject != NULL ||
        props->bootstrap != NULL || props->noregister != -1)
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("this package was built"
            " without Tcl commands support. Options pagesobject,"
            " fsindexobject, bootstrap and noregister are not available",
            -1));
        goto error;
    }
#endif /* COOKFS_USETCLCMDS */

#ifndef COOKFS_USECCRYPTO
    if (props->password != NULL || props->encryptkey != -1 ||
        props->encryptlevel != -1)
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("this package was built"
            " without encryption support. Options password, encryptkey,"
            " encryptlevel are not available", -1));
        goto error;
    }
#endif /* COOKFS_USECCRYPTO */

#if defined(COOKFS_USECALLBACKS)
    if (props->shared && (
        props->compresscommand != NULL ||
        props->decompresscommand != NULL ||
        props->asynccompresscommand != NULL ||
        props->asyncdecompresscommand != NULL))
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot use tcl commands"
            " in thread-shared mode", -1));
        goto error;
    }
#endif /* COOKFS_USECALLBACKS */

    // if write to memory option was selected, open archive as read only anyway
    if (props->writetomemory) {
        props->readonly = 1;
    }

    if (archive == NULL) {
        goto skipArchive;
    }

    if (Tcl_GetCharLength(archive)) {
        CookfsLog(printf("normalize archive path [%s]",
            Tcl_GetString(archive)));
        // If the path is not empty, then normalize it
        normalized = Tcl_FSGetNormalizedPath(interp, archive);
        if (normalized == NULL) {
            CookfsLog(printf("got NULL"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not normalize"
                " archive path \"%s\"", Tcl_GetString(archive)));
            goto error;
        }
        CookfsLog(printf("got normalized path [%s]",
            Tcl_GetString(normalized)));
        archiveActual = normalized;
        Tcl_IncrRefCount(archiveActual);
    } else {
        CookfsLog(printf("use PWD as archive, since archive is an empty"
            " string"));
        // If the path is empty, then use PWD. Tcl_FSGetCwd() returns PWD
        // with already incremented refcount
        archiveActual = Tcl_FSGetCwd(interp);
        if (archiveActual == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to set archive"
                " to PWD", -1));
            goto error;
        }
    }

skipArchive:

    if (!props->volume) {
        if (Tcl_GetCharLength(local)) {
            // If the path is not empty, then normalize it
            CookfsLog(printf("normalize local path [%s]",
                Tcl_GetString(local)));
            normalized = Tcl_FSGetNormalizedPath(interp, local);
            if (normalized == NULL) {
                CookfsLog(printf("got NULL"));
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not normalize"
                    " local path \"%s\"", Tcl_GetString(local)));
                goto error;
            }
            CookfsLog(printf("got normalized path [%s]",
                Tcl_GetString(normalized)));
            localActual = normalized;
            Tcl_IncrRefCount(localActual);
        } else {
            CookfsLog(printf("use PWD as archive, since archive is an empty"
                " string"));
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
        CookfsLog(printf("use local as is, since it is a volume"));
        // Just use specified "local", but increase refcount
        localActual = local;
        Tcl_IncrRefCount(localActual);
    }

    if (archive == NULL) {
        goto skipPages;
    }

#ifdef COOKFS_USETCLCMDS
    if (props->pagesobject == NULL) {
#endif
        CookfsLog(printf("creating the pages object"));
        // TODO: pass a pointer to err variable instead of NULL and
        // handle the corresponding error message
        pages = Cookfs_PagesInit(interp, archiveActual, props->readonly,
            props->compression, props->compressionlevel, props->compression,
            props->compressionlevel,
#ifdef COOKFS_USECCRYPTO
            props->password, props->encryptkey, props->encryptlevel,
#else
            NULL, 0, -1,
#endif /* COOKFS_USECCRYPTO */
            NULL, (props->endoffset == -1 ? 0 : 1), props->endoffset, 0,
#if defined(COOKFS_USECALLBACKS)
            props->asyncdecompressqueuesize,
            props->compresscommand, props->decompresscommand,
            props->asynccompresscommand, props->asyncdecompresscommand,
#endif /* COOKFS_USECALLBACKS */
            NULL);

        if (pages == NULL) {
            // Ignore page creation failure for writetomemory VFS
            if (props->writetomemory) {
                goto skipPagesConfiguration;
            }
            goto error;
        }

#ifdef COOKFS_USETCLCMDS
    } else {
        char *pagesCmd = Tcl_GetString(props->pagesobject);
        pages = Cookfs_PagesGetHandle(interp, pagesCmd);
        if (pages == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incorrect page object"
                " \"%s\" has been specified", pagesCmd));
            goto error;
        }
    }
#endif

    Cookfs_PagesLockHard(pages);
    Cookfs_PagesLockWrite(pages, NULL);

    // set whether compression should always be enabled
    CookfsLog(printf("set pages always compress: %d", props->alwayscompress));
    Cookfs_PagesSetAlwaysCompress(pages, props->alwayscompress);
    // set up cache size
    CookfsLog(printf("set pages cache size: %d", props->pagecachesize));
    Cookfs_PagesSetCacheSize(pages, props->pagecachesize);

skipPagesConfiguration:

    // Archive path is not needed anymore
    Tcl_DecrRefCount(archiveActual);
    archiveActual = NULL;

skipPages:

#ifdef COOKFS_USETCLCMDS
    if (props->fsindexobject == NULL) {
#endif
        CookfsLog(printf("creating the index object"));
        Cookfs_PageObj indexDataObj = NULL;
        if (pages != NULL) {
            indexDataObj = Cookfs_PagesGetIndex(pages);
            if (indexDataObj == NULL) {
                CookfsLog(printf("got NULL as index data"));
            } else {
                Cookfs_PageObjIncrRefCount(indexDataObj);
                CookfsLog(printf("got index data %" TCL_SIZE_MODIFIER "d"
                    " bytes", Cookfs_PageObjSize(indexDataObj)));
                if (!Cookfs_PageObjSize(indexDataObj)) {
                    Cookfs_PageObjDecrRefCount(indexDataObj);
                    indexDataObj = NULL;
                }
            }
        }
        if (indexDataObj == NULL) {
            index = Cookfs_FsindexInit(interp, NULL);
        } else {
            index = Cookfs_FsindexFromBytes(interp, NULL, indexDataObj->buf,
                Cookfs_PageObjSize(indexDataObj));
            Cookfs_PageObjDecrRefCount(indexDataObj);
        }
        if (index == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
                " index object", -1));
            goto error;
        }
#ifdef COOKFS_USETCLCMDS
    } else {
        char *indexCmd = Tcl_GetString(props->fsindexobject);
        index = Cookfs_FsindexGetHandle(interp, indexCmd);
        if (index == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incorrect fsindex object"
                " \"%s\" has been specified", indexCmd));
            goto error;
        }
    }
#endif

    Cookfs_FsindexLockHard(index);
    Cookfs_FsindexLockWrite(index, NULL);

    if (Cookfs_FsindexFileSetSelect(index,
        (props->fileset == NULL ? NULL : Tcl_GetString(props->fileset)),
        props->readonly, &err) != TCL_OK)
    {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("error when selecting"
            " a fileset: ", -1));
        if (err == NULL) {
            Tcl_AppendResult(interp, "unknown error", (char *)NULL);
        } else {
            Tcl_AppendResult(interp, Tcl_GetString(err), (char *)NULL);
            Tcl_BounceRefCount(err);
        }
        goto error;
    }

    const char *pagesizeMetadataKey = "cookfs.pagesize";
    const char *smallfilesizeMetadataKey = "cookfs.smallfilesize";
    const char *smallfilebufferMetadataKey = "cookfs.smallfilebuffer";

    Tcl_Obj *tmpObj = NULL;

    if (props->pagesize == -1) {
        CookfsLog(printf("prop pagesize is not defined, try to check"
            " fsindex metadata"));
        tmpObj = Cookfs_FsindexGetMetadata(index, pagesizeMetadataKey);
        if (tmpObj == NULL || Tcl_GetWideIntFromObj(NULL, tmpObj,
            &props->pagesize) != TCL_OK || props->pagesize <= 0)
        {
            CookfsLog(printf("pagesize metadata doesn't exists"
                " or malformed, set to default"));
            props->pagesize = COOKFS_PROP_DEFAULT_PAGESIZE;
        } else {
            CookfsLog(printf("got pagesize from metadata"));
            goto skipSetPagesizeMetadata;
        }
    } else {
        CookfsLog(printf("prop pagesize is defined"));
    }

    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
    }
    tmpObj = Tcl_NewWideIntObj(props->pagesize);
    Cookfs_FsindexSetMetadata(index, pagesizeMetadataKey, tmpObj);

skipSetPagesizeMetadata:

    CookfsLog(printf("prop pagesize: %" TCL_LL_MODIFIER "d",
        props->pagesize));
    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
        tmpObj = NULL;
    }

    if (props->smallfilesize == -1) {
        CookfsLog(printf("prop smallfilesize is not defined, try to check"
            " fsindex metadata"));
        tmpObj = Cookfs_FsindexGetMetadata(index, smallfilesizeMetadataKey);
        if (tmpObj == NULL || Tcl_GetWideIntFromObj(NULL, tmpObj,
            &props->smallfilesize) != TCL_OK || props->smallfilesize < 0)
        {
            CookfsLog(printf("smallfilesize metadata doesn't exists"
                " or malformed, set to default"));
            props->smallfilesize = COOKFS_PROP_DEFAULT_SMALLFILESIZE;
        } else {
            CookfsLog(printf("got smallfilesize from metadata"));
            goto skipSetSmallfilesizeMetadata;
        }
    } else {
        CookfsLog(printf("prop smallfilesize is defined"));
    }

    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
    }
    tmpObj = Tcl_NewWideIntObj(props->smallfilesize);
    Cookfs_FsindexSetMetadata(index, smallfilesizeMetadataKey, tmpObj);

skipSetSmallfilesizeMetadata:

    CookfsLog(printf("prop smallfilesize: %" TCL_LL_MODIFIER "d",
        props->smallfilesize));
    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
        tmpObj = NULL;
    }

    if (props->smallfilebuffer == -1) {
        CookfsLog(printf("prop smallfilebuffer is not defined, try to check"
            " fsindex metadata"));
        tmpObj = Cookfs_FsindexGetMetadata(index, smallfilebufferMetadataKey);
        if (tmpObj == NULL || Tcl_GetWideIntFromObj(NULL, tmpObj,
            &props->smallfilebuffer) != TCL_OK || props->smallfilebuffer < 0)
        {
            CookfsLog(printf("smallfilebuffer metadata doesn't exists"
                " or malformed, set to default"));
            props->smallfilebuffer = COOKFS_PROP_DEFAULT_SMALLFILEBUFFER;
        } else {
            CookfsLog(printf("got smallfilebuffer from metadata"));
            goto skipSetSmallfilebufferMetadata;
        }
    } else {
        CookfsLog(printf("prop smallfilebuffer is defined"));
    }

    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
    }
    tmpObj = Tcl_NewWideIntObj(props->smallfilebuffer);
    Cookfs_FsindexSetMetadata(index, smallfilebufferMetadataKey, tmpObj);

skipSetSmallfilebufferMetadata:

    CookfsLog(printf("prop smallfilebuffer: %" TCL_LL_MODIFIER "d",
        props->smallfilebuffer));
    if (tmpObj != NULL) {
        Tcl_BounceRefCount(tmpObj);
        tmpObj = NULL;
    }

    const char *pagehashMetadataKey = "cookfs.pagehash";

    if (pages == NULL) {
        goto skipPagesBootstrap;
    }

    if (Cookfs_PagesGetLength(pages)) {
        CookfsLog(printf("pages contain data"));
        Tcl_Obj *pagehashActual = Cookfs_FsindexGetMetadata(index,
            pagehashMetadataKey);
        if (pagehashActual == NULL) {
            CookfsLog(printf("metadata doesn't contain pagehash, the default"
                " algo will be used"));
        } else {
            Tcl_IncrRefCount(pagehashActual);
            CookfsLog(printf("got pagehash from metadata [%s]",
                Tcl_GetString(pagehashActual)));
            CookfsLog(printf("set pagehash for pages"));
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

        CookfsLog(printf("pages don't contain data"));

#ifdef COOKFS_USETCLCMDS
        if (props->bootstrap != NULL) {
            CookfsLog(printf("bootstrap is specified"));
            Tcl_Size bootstrapLength;
            Tcl_GetByteArrayFromObj(props->bootstrap, &bootstrapLength);
            if (!bootstrapLength) {
                CookfsLog(printf("bootstrap is empty"));
            } else {
                CookfsLog(printf("add bootstrap"));
                int idx = Cookfs_PageAddTclObj(pages, props->bootstrap, &err);
                if (idx < 0) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("Unable to add"
                        " bootstrap: %s", (err == NULL ? "unknown error" :
                        Tcl_GetString(err))));
                    if (err != NULL) {
                        Tcl_BounceRefCount(err);
                    }
                    goto error;
                }
            }
        } else {
            CookfsLog(printf("bootstrap is not specified"));
        }
#endif

        if (props->pagehash != COOKFS_HASH_DEFAULT) {
            CookfsLog(printf("set pagehash for pages"));
            Cookfs_PagesSetHash(pages, props->pagehash);
        }
        Tcl_Obj *pagehashActual = Cookfs_PagesGetHashAsObj(pages);
        CookfsLog(printf("set pagehash in metadata"));
        Cookfs_FsindexSetMetadata(index, pagehashMetadataKey, pagehashActual);
        Tcl_BounceRefCount(pagehashActual);

    }

skipPagesBootstrap:

    if (props->setmetadata != NULL) {
        CookfsLog(printf("setmetadata is specified"));
        Tcl_Obj **metadataKeyVal;
        Tcl_Size metadataCount;
        if (Tcl_ListObjGetElements(interp, props->setmetadata, &metadataCount,
            &metadataKeyVal) != TCL_OK)
        {
            CookfsLog(printf("could not convert setmetadata to a list"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not convert"
                " setmetadata option \"%s\" to list",
                Tcl_GetString(props->setmetadata)));
            goto error;
        }
        CookfsLog(printf("setmetadata was converted to list with %"
            TCL_SIZE_MODIFIER "d length", metadataCount));
        if ((metadataCount % 2) != 0) {
            CookfsLog(printf("setmetadata list size is not even"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("setmetadata requires"
                " a list with an even number of elements, but got \"%s\"",
                Tcl_GetString(props->setmetadata)));
            goto error;
        }
        for (Tcl_Size i = 0; i < metadataCount; i++) {
            Tcl_Obj *key = metadataKeyVal[i++];
            Tcl_Obj *val = metadataKeyVal[i];
            CookfsLog(printf("setmetadata [%s] = [%s]", Tcl_GetString(key),
                Tcl_GetString(val)));
            Cookfs_FsindexSetMetadata(index, Tcl_GetString(key), val);
        }
    }

    CookfsLog(printf("creating the writer object"));
    writer = Cookfs_WriterInit(interp, pages, index, props->smallfilebuffer,
         props->smallfilesize, props->pagesize, props->writetomemory);
    if (writer == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
            " writer object", -1));
        goto error;
    }

    CookfsLog(printf("creating the vfs object"));
    // If writetomemory is specified, create writable VFS
    vfs = Cookfs_VfsInit(interp, localActual, props->volume,
        (props->nodirectorymtime ? 0 : 1),
        ((!props->writetomemory && props->readonly) ? 1 : 0), props->shared,
        pages, index, writer);
    if (vfs == NULL) {
        CookfsLog(printf("failed to create the vfs object"));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to create"
            " vfs object", -1));
        goto error;
    }

    // We don't need localActual anymore
    Tcl_DecrRefCount(localActual);
    localActual = NULL;

    CookfsLog(printf("add mount point..."));
    if (!Cookfs_CookfsAddVfs(interp, vfs)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Unable to add"
            " the mount point", -1));
        goto error;
    }

#ifdef COOKFS_USETCLCMDS
    if (!props->noregister) {
        CookfsLog(printf("registering the vfs in tclvfs..."));
        if (Cookfs_VfsRegisterInTclvfs(vfs) != TCL_OK) {
            Cookfs_CookfsRemoveVfs(interp, vfs);
            // We have an error message from Tclvfs in interp result
            CookfsLog(printf("failed to register vfs in tclvfs"));
            goto error;
        }
    } else {
        CookfsLog(printf("no need to register the vfs in tclvfs"));
    }
#endif

    Tcl_ResetResult(interp);

    if (!props->nocommand) {

        char cmd[128];
        sprintf(cmd, "::cookfs::c::vfs::mount%p", (void *)vfs);

        CookfsLog(printf("creating vfs command handler..."));
        vfs->commandToken = Tcl_CreateObjCommand(interp, cmd,
            CookfsMountHandleCmd, vfs, CookfsMountHandleCmdDeleteProc);

        Tcl_SetObjResult(interp, Tcl_NewStringObj(cmd, -1));

        CookfsLog(printf("ok [%s]", cmd));

    } else {
        CookfsLog(printf("ok (no cmd)"));
    }

    if (freeProps) {
        Cookfs_VfsPropsFree(props);
    }

    Cookfs_FsindexUnlock(index);
    if (pages != NULL) {
        Cookfs_PagesUnlock(pages);
    }

    return TCL_OK;

error:

    if (freeProps) {
        Cookfs_VfsPropsFree(props);
    }
    if (archiveActual != NULL) {
        Tcl_DecrRefCount(archiveActual);
    }
    if (localActual != NULL) {
        Tcl_DecrRefCount(localActual);
    }

    if (index != NULL) {
        Cookfs_FsindexUnlock(index);
    }

    if (pages != NULL) {
        Cookfs_PagesUnlock(pages);
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
        if (props->fsindexobject == NULL && index != NULL) {
#else
        if (index != NULL) {
#endif
            Cookfs_FsindexUnlockHard(index);
            Cookfs_FsindexFini(index);
        }
        // If no pages object was specified and a pages object was created by
        // this procedure, release the pages object.
#ifdef COOKFS_USETCLCMDS
        if (props->pagesobject == NULL && pages != NULL) {
#else
        if (pages != NULL) {
#endif
            Cookfs_PagesUnlockHard(pages);
            Cookfs_PagesFini(pages);
        }
    }
    return TCL_ERROR;

}

static int CookfsUnmountCmd(ClientData clientData, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{
    UNUSED(clientData);

    CookfsLog(printf("ENTER args count:%d", objc));

    Tcl_Obj *arg;
#ifdef COOKFS_USETCLCMDS
    if (objc < 2 || objc > 3 ||
        (objc == 3 && strcmp(Tcl_GetString(objv[1]), "-unregister") != 0))
    {
        CookfsLog(printf("wrong # args"));
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
        CookfsLog(printf("wrong # args"));
        Tcl_WrongNumArgs(interp, 1, objv, "fsid|local");
        return TCL_ERROR;
    }
    arg = objv[1];
#endif

    CookfsLog(printf("unmount [%s]", Tcl_GetString(arg)));

    Cookfs_Vfs *vfs = NULL;

    // First try checking if the argument is mountid
    if (sscanf(Tcl_GetString(arg), "::cookfs::c::vfs::mount%p",
        (void **)&vfs) == 1)
    {
        if (!Cookfs_CookfsIsVfsExist(vfs)) {
            CookfsLog(printf("given argument is invalid fsid"));
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("given argument \"%s\""
                " is invalid fsid", Tcl_GetString(arg)));
            return TCL_ERROR;
        } else {
            CookfsLog(printf("given argument is a fsid"));
        }
    } else {
        CookfsLog(printf("given argument is not a fsid"));
    }

    // If it was not found above, then check if argument is a mount point
    if (vfs == NULL) {
        vfs = Cookfs_CookfsFindVfs(arg, -1);
        if (vfs == NULL) {
            CookfsLog(printf("given argument is not a mount path"));
        } else {
            CookfsLog(printf("given argument is a mount path, mount"
                " struct [%p]", (void *)vfs));
        }
    }

    // If we could not find the mount again, try normalized path
    if (vfs == NULL) {
        Tcl_Obj *normalized = Tcl_FSGetNormalizedPath(interp, arg);
        if (normalized == NULL) {
            CookfsLog(printf("could not convert given argument to"
                " normalized path"));
        } else {
            CookfsLog(printf("check for normalized path [%s]",
                Tcl_GetString(normalized)));
            vfs = Cookfs_CookfsFindVfs(normalized, -1);
            if (vfs == NULL) {
                CookfsLog(printf("given argument is not a normalized"
                    " mount path"));
            } else {
                CookfsLog(printf("given argument is a mount path, mount"
                    " struct [%p]", (void *)vfs));
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
        CookfsLog(printf("the mount point is already in a terminating"
            " state"));
        return TCL_OK;
    }

#ifdef COOKFS_USETCLCMDS
    // If we were called with -unregister switch, then we are in unregister
    // callback from tclvfs. Cancel vfs registration status to avoid
    // double unregistration.
    if (objc == 3) {
        CookfsLog(printf("-unregister switch present, cancel tclvfs"
            " registration status"));
        vfs->isRegistered = 0;
    }
#endif

    // Try to remove the mount point from mount list
    CookfsLog(printf("remove the mount point"));
    vfs = Cookfs_CookfsRemoveVfs(interp, vfs);
    // If vfs is NULL, then Cookfs_CookfsRemoveVfs() could not find
    // the vfs in the vfs list. Return error.
    if (vfs == NULL) {
        CookfsLog(printf("got NULL"));
        return TCL_ERROR;
    }

    // Terminate the mount point
    CookfsLog(printf("terminate the mount point"));
    Tcl_WideInt pagesCloseOffset;
    if (Cookfs_VfsFini(interp, vfs, &pagesCloseOffset) != TCL_OK) {
        CookfsLog(printf("termination failed"));
        return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pagesCloseOffset));

    CookfsLog(printf("return ok and [%" TCL_LL_MODIFIER "d]",
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
#ifdef COOKFS_USECCRYPTO
static Cookfs_MountHandleCommandProc CookfsMountHandleCommandPassword;
#endif /* COOKFS_USETCLCMDS */
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
#endif /* COOKFS_USETCLCMDS */
#ifdef COOKFS_USECCRYPTO
        "password",
#endif /* COOKFS_USECCRYPTO */
        "getmetadata", "setmetadata", "aside", "writetomemory", "filesize",
        "smallfilebuffersize", "compression", "writeFiles", "optimizelist",
        NULL
    };
    enum commands {
#ifdef COOKFS_USETCLCMDS
        cmdGetpages, cmdGetindex, cmdGetwriter,
#endif /* COOKFS_USETCLCMDS */
#ifdef COOKFS_USECCRYPTO
        cmdPassword,
#endif /* COOKFS_USECCRYPTO */
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
#ifdef COOKFS_USECCRYPTO
    case cmdPassword:
        return CookfsMountHandleCommandPassword(vfs, interp, objc, objv);
#endif /* COOKFS_USECCRYPTO */
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
    return Cookfs_FsindexCmdForward(COOKFS_FSINDEX_FORWARD_COMMAND_GETMETADATA,
        vfs->index, interp, objc, objv);
}

static int CookfsMountHandleCommandSetmetadata(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (Cookfs_VfsIsReadonly(vfs)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Archive is read-only", -1));
        return TCL_ERROR;
    }
    return Cookfs_FsindexCmdForward(COOKFS_FSINDEX_FORWARD_COMMAND_SETMETADATA,
        vfs->index, interp, objc, objv);
}

static int CookfsMountHandleCommandAside(Cookfs_Vfs *vfs, Tcl_Interp *interp,
    int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("enter"));

    if (objc != 3) {
        CookfsLog(printf("ERR: wrong # args"));
        Tcl_WrongNumArgs(interp, 2, objv, "filename");
        return TCL_ERROR;
    }

    int rc = TCL_ERROR;
    Tcl_Obj *fileset_activeObj = NULL;

    // Write lock writer/fsindex/pages
    Tcl_Obj *err = NULL;
    if (Cookfs_WriterLockWrite(vfs->writer, &err)) {
        if (Cookfs_FsindexLockWrite(vfs->index, &err)) {
            rc = TCL_OK;
        } else {
            Cookfs_WriterUnlock(vfs->writer);
        }
    }

    if (rc != TCL_OK) {
failedToLock:
        if (err == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to aquire"
                " the locks", -1));
        } else {
            Tcl_SetObjResult(interp, err);
        }
        return TCL_ERROR;
    }

    if (Cookfs_WriterGetWritetomemory(vfs->writer)) {
        CookfsLog(printf("ERROR: write to memory option enabled"));
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Write to memory option"
            " enabled; not creating add-aside archive", -1));
        goto error;
    } else {
        CookfsLog(printf("writer writetomemory: false"));
    }

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("vfs is in readonly mode, no need to purge writer"
            " or update index"));
    } else {

        CookfsLog(printf("purge writer..."));
        // TODO: pass a pointer to err instead of NULL and handle
        // the corresponding error message
        if (Cookfs_WriterPurge(vfs->writer, 0, NULL) != TCL_OK) {
            goto error;
        }

        // Update fsindex to copy the actual index to the pages aside
        if (Cookfs_FsindexIncrChangeCount(vfs->index, 0) == 0) {
            CookfsLog(printf("index was not changed, not need to update it"));
        } else {
            CookfsLog(printf("dump index..."));
            Tcl_Obj *exportObjTcl = Cookfs_FsindexToObject(vfs->index);
            if (exportObjTcl == NULL) {
                CookfsLog(printf("failed to get index dump"));
            } else {
                Tcl_IncrRefCount(exportObjTcl);
                Cookfs_PageObj exportObj =
                    Cookfs_PageObjNewFromByteArray(exportObjTcl);
                Tcl_DecrRefCount(exportObjTcl);
                if (exportObj == NULL) {
                    CookfsLog(printf("failed to convert index dump"));
                } else {
                    Cookfs_PageObjIncrRefCount(exportObj);
                    CookfsLog(printf("store index..."));
                    if (!Cookfs_PagesLockWrite(vfs->pages, &err)) {
                        Cookfs_PageObjDecrRefCount(exportObj);
                        goto failedToLock;
                    }
                    Cookfs_PagesSetIndex(vfs->pages, exportObj);
                    Cookfs_PagesUnlock(vfs->pages);
                    Cookfs_PageObjDecrRefCount(exportObj);
                }
            }
        }

    }

    const char *fileset_activeStr = Cookfs_FsindexFileSetGetActive(vfs->index);
    if (fileset_activeStr == NULL) {
        CookfsLog(printf("current index doesn't have an active fileset"));
        fileset_activeObj = NULL;
    } else {
        // Let's create a copy of the active fileset in Tcl object.
        // We may destroy the old index below, thus fileset_activeStr pointer
        // will be not actual.
        CookfsLog(printf("save the active fileset: [%s]", fileset_activeStr));
        fileset_activeObj = Tcl_NewStringObj(fileset_activeStr, -1);
        Tcl_IncrRefCount(fileset_activeObj);
    }

    CookfsLog(printf("run pages aside..."));
    if (Cookfs_PagesCmdForward(COOKFS_PAGES_FORWARD_COMMAND_ASIDE,
        vfs->pages, interp, objc, objv) != TCL_OK)
    {
        goto error;
    }

    CookfsLog(printf("refresh index..."));
    Cookfs_PageObj indexDataObj = NULL;
    if (Cookfs_PagesLockRead(vfs->pages, NULL)) {
        indexDataObj = Cookfs_PagesGetIndex(vfs->pages);
        Cookfs_PagesUnlock(vfs->pages);
    }
    if (indexDataObj == NULL) {
        CookfsLog(printf("got NULL as index data"));
    } else {
        Cookfs_PageObjIncrRefCount(indexDataObj);
        CookfsLog(printf("got index data %" TCL_SIZE_MODIFIER "d bytes",
            Cookfs_PageObjSize(indexDataObj)));
        if (!Cookfs_PageObjSize(indexDataObj)) {
            Cookfs_PageObjDecrRefCount(indexDataObj);
            indexDataObj = NULL;
        }
    }
    if (indexDataObj == NULL) {
        Cookfs_FsindexCleanup(vfs->index);
    } else {
        Cookfs_FsindexFromBytes(interp, vfs->index, indexDataObj->buf,
            Cookfs_PageObjSize(indexDataObj));
        Cookfs_PageObjDecrRefCount(indexDataObj);
        if (fileset_activeObj != NULL) {
            // TODO: we should not ignore possible error when setting
            // an active fileset. There's also the question of whether
            // to use write mode and allow a fileset to be created
            // if it doesn't exist.
            Cookfs_FsindexFileSetSelect(vfs->index,
                Tcl_GetString(fileset_activeObj), 0, NULL);
        }
    }

    CookfsLog(printf("set writable mode"));
    Cookfs_VfsSetReadonly(vfs, 0);

    CookfsLog(printf("ok"));

    goto done;

error:
    rc = TCL_ERROR;

done:
    if (fileset_activeObj != NULL) {
        Tcl_DecrRefCount(fileset_activeObj);
    }
    Cookfs_FsindexUnlock(vfs->index);
    Cookfs_WriterUnlock(vfs->writer);
    return rc;
}

static int CookfsMountHandleCommandWritetomemory(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    if (!Cookfs_WriterLockWrite(vfs->writer, NULL)) {
        return TCL_ERROR;
    }
    Cookfs_WriterSetWritetomemory(vfs->writer, 1);
    Cookfs_VfsSetReadonly(vfs, 0);
    Cookfs_WriterUnlock(vfs->writer);
    return TCL_OK;
}

static int CookfsMountHandleCommandFilesize(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    if (!Cookfs_PagesLockRead(vfs->pages, NULL)) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(Cookfs_GetFilesize(
        vfs->pages)));
    Cookfs_PagesUnlock(vfs->pages);
    return TCL_OK;
}

static int CookfsMountHandleCommandSmallfilebuffersize(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
    }
    if (!Cookfs_WriterLockRead(vfs->writer, NULL)) {
        return TCL_ERROR;
    }
    Tcl_WideInt size = Cookfs_WriterGetSmallfilebuffersize(vfs->writer);
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(size));
    Cookfs_WriterUnlock(vfs->writer);
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
        // TODO: pass a pointer to err variable instead of NULL and handle
        // the corresponding error message
        if (!Cookfs_WriterLockWrite(vfs->writer, NULL)) {
            return TCL_ERROR;
        }
        if (Cookfs_WriterPurge(vfs->writer, 1, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    int rc = Cookfs_PagesCmdForward(COOKFS_PAGES_FORWARD_COMMAND_COMPRESSION,
        vfs->pages, interp, objc, objv);

    if (objc == 3) {
        Cookfs_WriterUnlock(vfs->writer);
    }

    return rc;
}

#ifdef COOKFS_USECCRYPTO
static int CookfsMountHandleCommandPassword(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "password");
        return TCL_ERROR;
    }

    //always purge small files cache when password changes
    // TODO: pass a pointer to err variable instead of NULL and handle
    // the corresponding error message
    //
    // Perhaps when using key or key-index encryption, we should not perform
    // a cleanup, but simply change the password. But in this case we should
    // add here a complex logic: check current encryption type and check if
    // password is already defined and we pass here not empty password, or
    // oppositive case when encryption is not enabled and we pass empty
    // password. I'm not sure it's worth implementing this logic, as such
    // cases should be very rare.
    //
    if (!Cookfs_WriterLockWrite(vfs->writer, NULL)) {
        return TCL_ERROR;
    }
    int ret = Cookfs_WriterPurge(vfs->writer, 1, NULL);
    // Make sure the writer is locked while we change the password
    if (ret == TCL_OK) {
        ret = Cookfs_PagesCmdForward(COOKFS_PAGES_FORWARD_COMMAND_PASSWORD,
            vfs->pages, interp, objc, objv);
    }
    Cookfs_WriterUnlock(vfs->writer);

    return ret;
}
#endif /* COOKFS_USECCRYPTO */

static int CookfsMountHandleCommandWritefiles(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    return Cookfs_WriterCmdForward(COOKFS_WRITER_FORWARD_COMMAND_WRITE,
        vfs->writer, interp, objc, objv);
}

static int CookfsMountHandleCommandOptimizelist(Cookfs_Vfs *vfs,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

    CookfsLog(printf("enter; objc: %d", objc));

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "base filelist");
        return TCL_ERROR;
    }

    Tcl_Size i;
    int rc = TCL_OK;

    Tcl_Obj **fileTails;
    Tcl_Size fileCount;
    if (Tcl_ListObjGetElements(interp, objv[3], &fileCount, &fileTails)
        != TCL_OK)
    {
        return TCL_ERROR;
    }

    Cookfs_Pages *pages = vfs->pages;
    Cookfs_Fsindex *index = vfs->index;

    if (!Cookfs_FsindexLockRead(index, NULL)) {
        return TCL_ERROR;
    }
    if (!Cookfs_PagesLockRead(pages, NULL)) {
        Cookfs_FsindexUnlock(index);
        return TCL_ERROR;
    }

    int pagesLen = Cookfs_PagesGetLength(pages);

    if (!pagesLen) {
        CookfsLog(printf("there is no pages, return the list as is"));
        Tcl_SetObjResult(interp, objv[2]);
        goto done;
    }

    CookfsLog(printf("alloc pageFiles"));
    Tcl_Obj **pageFiles = ckalloc(sizeof(Tcl_Obj *) * pagesLen);
    if (pageFiles == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to alloc"
            " pageFiles", -1));
        rc = TCL_ERROR;
        goto done;
    }

    for (i = 0; i < pagesLen; i++) {
        pageFiles[i] = NULL;
    }

    Tcl_Obj *largeFiles = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(largeFiles);

    Tcl_Obj *baseTemplate = Tcl_NewListObj(1, &objv[2]);
    Tcl_IncrRefCount(baseTemplate);

    CookfsLog(printf("checking %" TCL_SIZE_MODIFIER "d files", fileCount));
    for (i = 0; i < fileCount; i++) {

        Tcl_Obj *fileTail = fileTails[i];
        CookfsLog(printf("checking file [%s]", Tcl_GetString(fileTail)));

        // Construct full path
        Tcl_Obj *fullName = Tcl_DuplicateObj(baseTemplate);
        Tcl_IncrRefCount(fullName);

        Tcl_ListObjAppendElement(NULL, fullName, fileTail);

        Tcl_Obj *fullNameJoined = Tcl_FSJoinPath(fullName, -1);
        Tcl_IncrRefCount(fullNameJoined);

        CookfsLog(printf("full path: [%s]", Tcl_GetString(fullNameJoined)));

        Cookfs_PathObj *fullNameSplit = Cookfs_PathObjNewFromTclObj(fullNameJoined);
        Cookfs_PathObjIncrRefCount(fullNameSplit);

        Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, fullNameSplit);
        Cookfs_PathObjDecrRefCount(fullNameSplit);

        Tcl_Obj *listToAdd = NULL;
        int pageNum = -1;

        if (entry == NULL) {
            CookfsLog(printf("got NULL entry"));
            listToAdd = largeFiles;
        } else if (Cookfs_FsindexEntryGetBlockCount(entry) != 1) {
            CookfsLog(printf("fileBlocks [%d] is not 1",
                Cookfs_FsindexEntryGetBlockCount(entry)));
            listToAdd = largeFiles;
        } else {
            // Check if the file has correct page number
            Cookfs_FsindexEntryGetBlock(entry, 0, &pageNum, NULL, NULL);
            if (pageNum < 0 || pageNum >= pagesLen) {
                CookfsLog(printf("incorrect page number: %d", pageNum));
                listToAdd = largeFiles;
            }
        }

        if (listToAdd == NULL) {
            if (pageFiles[pageNum] == NULL) {
                pageFiles[pageNum] = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(pageFiles[pageNum]);
            }
            listToAdd = pageFiles[pageNum];
            CookfsLog(printf("add to small file list, page: %d", pageNum));
        } else {
            CookfsLog(printf("add to large file list"));
        }

        Tcl_ListObjAppendElement(NULL, listToAdd, fileTail);

        // Cleanup
        Tcl_DecrRefCount(fullNameJoined);
        Tcl_DecrRefCount(fullName);

    }

    Tcl_DecrRefCount(baseTemplate);

    CookfsLog(printf("create a small file list"));
    Tcl_Obj *smallFiles = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(smallFiles);

    for (i = 0; i < pagesLen; i++) {
        if (pageFiles[i] != NULL) {
            CookfsLog(printf("add files from page %" TCL_SIZE_MODIFIER
                "d to small file list", i));
            Tcl_ListObjAppendList(interp, smallFiles, pageFiles[i]);
            Tcl_DecrRefCount(pageFiles[i]);
        }
    }

    ckfree(pageFiles);

    CookfsLog(printf("add the large files to the small files"));
    Tcl_ListObjAppendList(interp, smallFiles, largeFiles);
    Tcl_DecrRefCount(largeFiles);

    Tcl_SetObjResult(interp, smallFiles);
    CookfsLog(printf("ok [%s]", Tcl_GetString(smallFiles)));
    Tcl_DecrRefCount(smallFiles);

done:
    Cookfs_PagesUnlock(pages);
    Cookfs_FsindexUnlock(index);
    return rc;
}
