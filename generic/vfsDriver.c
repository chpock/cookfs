/*
 * vfsDriver.c
 *
 * Provides implementation for cookfs VFS
 *
 * (c) 2024 Konstantin Kushnir
 */

// This will make statbuf from Tcl compatible with our statbuf.
// The details and related links are here:
// https://core.tcl-lang.org/tclvfs/tktview/685c6eb3d5
//#if defined(_WIN32) && !defined(_WIN64) && !defined(__MINGW_USE_VC2005_COMPAT)
//#define __MINGW_USE_VC2005_COMPAT
//#elif defined(_WIN64) && !defined(__stat64)
//#define __stat64 _stat64
//#endif

#if defined(_WIN32)
#if !defined(_WIN64) && !defined(__MINGW_USE_VC2005_COMPAT)
#define __MINGW_USE_VC2005_COMPAT
#endif /* !defined(_WIN64) && !defined(__MINGW_USE_VC2005_COMPAT) */
#if !defined(__stat64)
#define __stat64 _stat64
#endif /* !defined(__stat64) */
#endif /* defined(_WIN32) */

#include "cookfs.h"
#include "vfs.h"
#include "vfsVfs.h"
#include "vfsDriver.h"
#include "writerchannel.h"
#include "readerchannel.h"

#include <unistd.h>
#include <sys/stat.h>
#include <utime.h>
#include <fcntl.h>
#include <errno.h>

// Cached internal representation of a cookfs entry
typedef struct cookfsInternalRep {
    Cookfs_Vfs *vfs;
    Cookfs_PathObj *pathObj;
} cookfsInternalRep;

TYPEDEF_ENUM_COUNT(Cookfs_VfsAttributeSetType, COOKFS_VFS_ATTRIBUTE_SET_COUNT,
    COOKFS_VFS_ATTRIBUTE_SET_VFS,
    COOKFS_VFS_ATTRIBUTE_SET_FILE,
    COOKFS_VFS_ATTRIBUTE_SET_DIRECTORY
);

TYPEDEF_ENUM_COUNT(Cookfs_VfsAttribute, COOKFS_VFS_ATTRIBUTE_COUNT,
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_HANDLE,
    COOKFS_VFS_ATTRIBUTE_FILESET
);

static const unsigned char
    cookfs_attribute_set_count[COOKFS_VFS_ATTRIBUTE_SET_COUNT] =
{
    3, 1, 1
};

static const Cookfs_VfsAttribute attr_arr_vfs[] = {
    COOKFS_VFS_ATTRIBUTE_VFS, COOKFS_VFS_ATTRIBUTE_HANDLE,
    COOKFS_VFS_ATTRIBUTE_FILESET
};

static const Cookfs_VfsAttribute attr_arr_file[] = {
    COOKFS_VFS_ATTRIBUTE_VFS
};

static const Cookfs_VfsAttribute attr_arr_directory[] = {
    COOKFS_VFS_ATTRIBUTE_VFS
};

static const Cookfs_VfsAttribute *const
    cookfs_attribute_set2attribute[COOKFS_VFS_ATTRIBUTE_SET_COUNT] =
{
    attr_arr_vfs, attr_arr_file, attr_arr_directory
};

static const char *cookfs_attribute_names[COOKFS_VFS_ATTRIBUTE_COUNT] = {
    "-vfs", "-handle", "-fileset"
};

typedef struct ThreadSpecificData {
    int initialized;
    int initialized_objects;
    Tcl_Obj *attrs_list[COOKFS_VFS_ATTRIBUTE_SET_COUNT];
    Tcl_Obj *attr_vfs_value;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

static Tcl_FSPathInFilesystemProc CookfsPathInFilesystem;
static Tcl_FSDupInternalRepProc CookfsDupInternalRep;
static Tcl_FSFreeInternalRepProc CookfsFreeInternalRep;
static Tcl_FSFilesystemPathTypeProc CookfsFilesystemPathType;
static Tcl_FSFilesystemSeparatorProc CookfsFilesystemSeparator;
static Tcl_FSStatProc CookfsStat;
static Tcl_FSAccessProc CookfsAccess;
static Tcl_FSOpenFileChannelProc CookfsOpenFileChannel;
static Tcl_FSMatchInDirectoryProc CookfsMatchInDirectory;
static Tcl_FSUtimeProc CookfsUtime;
static Tcl_FSListVolumesProc CookfsListVolumes;
static Tcl_FSFileAttrStringsProc CookfsFileAttrStrings;
static Tcl_FSFileAttrsGetProc CookfsFileAttrsGet;
static Tcl_FSFileAttrsSetProc CookfsFileAttrsSet;
static Tcl_FSCreateDirectoryProc CookfsCreateDirectory;
static Tcl_FSRemoveDirectoryProc CookfsRemoveDirectory;
static Tcl_FSDeleteFileProc CookfsDeleteFile;

static Tcl_Filesystem cookfsFilesystem = {
    "cookfs",
    sizeof(Tcl_Filesystem),
    TCL_FILESYSTEM_VERSION_1,
    &CookfsPathInFilesystem,
    &CookfsDupInternalRep,
    &CookfsFreeInternalRep,
    NULL, /* internalToNormalizedProc */
    NULL, /* createInternalRepProc */
    NULL, /* normalizePathProc */
    &CookfsFilesystemPathType,
    &CookfsFilesystemSeparator,
    &CookfsStat,
    &CookfsAccess,
    &CookfsOpenFileChannel,
    &CookfsMatchInDirectory,
    &CookfsUtime,
    NULL, /* linkProc */
    &CookfsListVolumes,
    &CookfsFileAttrStrings,
    &CookfsFileAttrsGet,
    &CookfsFileAttrsSet,
    &CookfsCreateDirectory,
    &CookfsRemoveDirectory,
    &CookfsDeleteFile,
    NULL, /* copyFileProc */
    NULL, /* renameFileProc */
    NULL, /* copyDirectoryProc */
    NULL, /* lstatProc */
    NULL, /* loadFileProc */
    NULL, /* getCwdProc */
    NULL, /* chdirProc */
};

const Tcl_Filesystem *CookfsFilesystem(void) {
    return &cookfsFilesystem;
}

void CookfsThreadExitProc(ClientData clientData) {
    UNUSED(clientData);
    CookfsLog(printf("(driver): ENTER"));
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    if (tsdPtr->initialized_objects) {
        for (unsigned char i = 0; i < COOKFS_VFS_ATTRIBUTE_SET_COUNT; i++) {
            if (tsdPtr->attrs_list[i] != NULL) {
                Tcl_DecrRefCount(tsdPtr->attrs_list[i]);
                tsdPtr->attrs_list[i] = NULL;
            }
        }
        if (tsdPtr->attr_vfs_value != NULL) {
            Tcl_DecrRefCount(tsdPtr->attr_vfs_value);
            tsdPtr->attr_vfs_value = NULL;
        }
        tsdPtr->initialized_objects = 0;
    }
    CookfsLog(printf("(driver): ok"));
}

static ThreadSpecificData *CookfsGetThreadSpecificData(void) {
    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);
    if (!tsdPtr->initialized) {
        Tcl_CreateThreadExitHandler(CookfsThreadExitProc, NULL);
        tsdPtr->initialized = 1;
    }
    if (!tsdPtr->initialized_objects) {

        Tcl_Obj *objv[3];
        Tcl_Obj *attr[COOKFS_VFS_ATTRIBUTE_COUNT];
        unsigned char i;

        for (i = 0; i < COOKFS_VFS_ATTRIBUTE_COUNT; i++) {
            attr[i] = Tcl_NewStringObj(cookfs_attribute_names[i], -1);
        }

        for (i = 0; i < COOKFS_VFS_ATTRIBUTE_SET_COUNT; i++) {
            unsigned char j, count = cookfs_attribute_set_count[i];
            for (j = 0; j < count; j++) {
                unsigned char attr_id = cookfs_attribute_set2attribute[i][j];
                objv[j] = attr[attr_id];
            }
            tsdPtr->attrs_list[i] = Tcl_NewListObj(count, objv);
            Tcl_IncrRefCount(tsdPtr->attrs_list[i]);
        }

        tsdPtr->attr_vfs_value = Tcl_NewBooleanObj(1);
        Tcl_IncrRefCount(tsdPtr->attr_vfs_value);

        tsdPtr->initialized_objects = 1;

    }
    return tsdPtr;
}

static int CookfsPathInFilesystem(Tcl_Obj *pathPtr,
    ClientData *clientDataPtr)
{
    // CookfsLog(printf("start; looking for [%s]", Tcl_GetString(pathPtr)));

    Tcl_Obj *normPathObj = Tcl_FSGetNormalizedPath(NULL, pathPtr);
    if (normPathObj == NULL) {
        // CookfsLog(printf("could not normalize the path."));
        return -1;
    }

    Cookfs_PathObj *pathObj;
    Cookfs_Vfs *vfs = Cookfs_CookfsSplitWithVfs(normPathObj, &pathObj);

    if (vfs == NULL) {
        // CookfsLog(printf("return -1"));
        return -1;
    }

    // We have found the path in the VFS

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)ckalloc(sizeof(cookfsInternalRep));
    if (internalRep == NULL) {
        // CookfsLog(printf("panic! could not create internalRep."));
        return -1;
    }

    internalRep->vfs = vfs;
    internalRep->pathObj = pathObj;
    Cookfs_PathObjIncrRefCount(internalRep->pathObj);

    *clientDataPtr = (ClientData)internalRep;
    CookfsLog(printf("return found entry [%s] as an internalRep [%p] for"
        " Tcl object [%p]", Tcl_GetString(pathPtr), (void *)internalRep,
        (void *)pathPtr));
    return TCL_OK;
}

static ClientData CookfsDupInternalRep(ClientData clientData) {
    cookfsInternalRep *internalRep = (cookfsInternalRep *)clientData;
    cookfsInternalRep *internalCopyRep =
        (cookfsInternalRep *)ckalloc(sizeof(cookfsInternalRep));
    if (internalCopyRep != NULL) {
        internalCopyRep->vfs = internalRep->vfs;
        internalCopyRep->pathObj = internalRep->pathObj;
        Cookfs_PathObjIncrRefCount(internalRep->pathObj);
    }
    CookfsLog(printf("copy from [%p] to [%p]", (void *)internalRep,
        (void *)internalCopyRep));
    return (ClientData)internalCopyRep;
}

static void CookfsFreeInternalRep(ClientData clientData) {
    CookfsLog(printf("release [%p]", (void *)clientData));
    cookfsInternalRep *internalRep = (cookfsInternalRep *)clientData;
    if (internalRep != NULL) {
        Cookfs_PathObjDecrRefCount(internalRep->pathObj);
        ckfree((char *)internalRep);
    }
}

static Tcl_Obj *CookfsFilesystemPathType(Tcl_Obj *pathPtr) {
    UNUSED(pathPtr);
    CookfsLog(printf("return [cookfs]"));
    return Tcl_NewStringObj("cookfs", -1);
}

static Tcl_Obj *CookfsFilesystemSeparator(Tcl_Obj *pathPtr) {
    UNUSED(pathPtr);
    CookfsLog(printf("return [/]"));
    char sep = VFS_SEPARATOR;
    return Tcl_NewStringObj(&sep, 1);
}

typedef enum {
    COOKFS_LOCK_READ,
    COOKFS_LOCK_WRITE,
    COOKFS_LOCK_WRITE_SOFT
} Cookfs_FsindexLockType;

static int CookfsValidatePathAndLockFsindex(Tcl_Obj *pathPtr,
    Cookfs_FsindexLockType lockType, cookfsInternalRep **internalRepPtr)
{

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    if (internalRep == NULL) {
        CookfsLog(printf("ERROR: internalRep == NULL"));
        Tcl_SetErrno(EBADF); // Bad file descriptor
        goto error;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;
    if (!Cookfs_CookfsVfsLock(vfs)) {
        CookfsLog(printf("ERROR: failed to lock VFS"));
        Tcl_SetErrno(ENOLCK); // No locks available
        goto error;
    }

    if (lockType == COOKFS_LOCK_WRITE && Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("filesystem is in readonly mode, return an error"));
        Cookfs_CookfsVfsUnlock(vfs);
        Tcl_SetErrno(EROFS);
        return 0;
    }


    Cookfs_Fsindex *index = vfs->index;
    int lockResult = Cookfs_FsindexLockRW(
        (lockType == COOKFS_LOCK_READ ? 0 : 1), index, NULL);

    Cookfs_CookfsVfsUnlock(vfs);

    if (!lockResult) {
        CookfsLog(printf("ERROR: failed to lock fsindex"));
        Tcl_SetErrno(ENOLCK); // No locks available
        goto error;
    }

    *internalRepPtr = internalRep;

    return 1;

error:
    return 0;
}

static int CookfsStat(Tcl_Obj *pathPtr, Tcl_StatBuf *bufPtr) {
    CookfsLog(printf("path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        return -1;
    }

    int rc = 0;
    Cookfs_Fsindex *index = ir->vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    if (entry == NULL) {
        CookfsLog(printf("could not find the entry, return an error"));
        Tcl_SetErrno(ENOENT);
        rc = -1;
        goto done;
    }

    memset(bufPtr, 0, sizeof(*bufPtr));

    if (Cookfs_FsindexEntryIsDirectory(entry)) {
        // Fill the Tcl_StatBuf for a directory
        bufPtr->st_mode = 040777;
        bufPtr->st_size = 0;
        CookfsLog(printf("return stats for a directory"));
    } else {
        // Fill the Tcl_StatBuf for a file
        bufPtr->st_mode = 0100777;
        bufPtr->st_size = Cookfs_FsindexEntryGetFilesize(entry);
        CookfsLog(printf("return stats for a file"));
    }
    // Tcl variant of Cookfs returns mtime as 0 for the root directory
    if (ir->pathObj->fullNameLength == 0) {
        bufPtr->st_mtime = 0;
        bufPtr->st_ctime = 0;
        bufPtr->st_atime = 0;
    } else {
        Tcl_WideInt fileTime = Cookfs_FsindexEntryGetFileTime(entry);
        bufPtr->st_mtime = fileTime;
        bufPtr->st_ctime = fileTime;
        bufPtr->st_atime = fileTime;
    }
    bufPtr->st_nlink = 1;

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static int CookfsAccess(Tcl_Obj *pathPtr, int mode) {
    CookfsLog(printf("path [%s] mode [%d]", Tcl_GetString(pathPtr), mode));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        return -1;
    }

    int rc = 0;
    Cookfs_Vfs *vfs = ir->vfs;
    Cookfs_Fsindex *index = vfs->index;

    // If write mode is requested, but the mount is in readonly mode, return
    // an error
    if ((mode & W_OK) && Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("vfs is in a readonly mode, return false"));
        Tcl_SetErrno(EROFS);
        rc = -1;
        goto done;
    }

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    // The entry has been found
    if (entry != NULL) {
        // Directories are readonly
        if ((mode & W_OK) && Cookfs_FsindexEntryIsDirectory(entry)) {
            CookfsLog(printf("the path is directory, write access denied,"
                " return false"));
            Tcl_SetErrno(EISDIR);
            rc = -1;
            goto done;
        }
        // Allow read/write access to files and read access to directories
        CookfsLog(printf("return true"));
    } else {
        // Could not find the entry, return an error
        CookfsLog(printf("could not find the entry, return false"));
        Tcl_SetErrno(ENOENT);
        rc = -1;
    }

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static Tcl_Channel CookfsOpenFileChannel(Tcl_Interp *interp, Tcl_Obj *pathPtr,
    int mode, int permissions)
{

    UNUSED(permissions);

    CookfsLog(printf("interp [%p] path [%s] mode [%d] permissions [%d]",
        (void *)interp, Tcl_GetString(pathPtr), mode, permissions));

    cookfsInternalRep *ir;
    Cookfs_Fsindex *index = NULL;

    Tcl_Channel channel = NULL;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        goto posixerror;
    }

    Cookfs_Vfs *vfs = ir->vfs;
    Cookfs_Pages *pages = vfs->pages;
    index = vfs->index;

    int isVFSReadonly = Cookfs_VfsIsReadonly(vfs);

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    if (entry != NULL) {
        // The entry already exists

        // Check if entry is a directory
        if (Cookfs_FsindexEntryIsDirectory(entry)) {
            CookfsLog(printf("the path is a directory"));
            Tcl_SetErrno(EISDIR);
            goto posixerror;
        }

        int isEntryPending = Cookfs_FsindexEntryIsPending(entry);

        // Use readerchannel for readonly mode
        if (mode == O_RDONLY) {

            // If entry blocks are in small file buffer, then open the file
            // using writerchannel
            if (isEntryPending) {
                CookfsLog(printf("the file is in a pending state,"
                    " open it using writerchannel"));
                channel = Cookfs_CreateWriterchannel(pages, index,
                    vfs->writer, NULL, entry, interp);
            } else {
                CookfsLog(printf("the file is NOT in a pending state,"
                    " open it using readerchannel"));
                channel = Cookfs_CreateReaderchannel(pages, index, entry,
                    interp, NULL);
            }

            if (channel == NULL) {
                CookfsLog(printf("got NULL channel"));
                // We expect an error message in the results of the create
                // channel function.
                goto interperror;
            }

            goto done;

        }

    } else {
        // The entry doesn't exist

        // The file must exists when mode is O_RDONLY
        if (mode == O_RDONLY) {
            CookfsLog(printf("file doesn't exist"));
            Tcl_SetErrno(ENOENT);
            goto posixerror;
        }

        // Check if parent exists
        Cookfs_FsindexEntry *entryParent = CookfsFsindexFindElement(index,
            ir->pathObj, ir->pathObj->elementCount - 1);
        if (entryParent == NULL) {
            CookfsLog(printf("parent directory doesn't exist"));
            Tcl_SetErrno(ENOENT);
            goto posixerror;
        }

        // Throw an error if parent is not a directory
        if (!Cookfs_FsindexEntryIsDirectory(entryParent)) {
            CookfsLog(printf("parent is not a directory"));
            Tcl_SetErrno(ENOTDIR);
            goto posixerror;
        }

    }

    // If we're here, we need to open the file in write mode,
    // i.e.: mode & O_WRONLY || mode & O_RDWR

    // Make sure that VFS is not in RO mode
    if (isVFSReadonly) {
        CookfsLog(printf("filesystem is in readonly mode, return an error"));
        Tcl_SetErrno(EROFS);
        goto posixerror;
    }

    // If we want to open the file from scratch, than don't pass the entry
    // to Cookfs_CreateWriterchannel()
    if (mode & O_TRUNC) {
        entry = NULL;
    }

    channel = Cookfs_CreateWriterchannel(pages, index, vfs->writer,
        ir->pathObj, entry, interp);

    if (channel == NULL) {
        CookfsLog(printf("got NULL from Cookfs_CreateWriterchannel()"));
        // We expect an error message in the results of the create
        // channel function.
        goto interperror;
    }

    // Set the current position to the end of the file in case of O_WRONLY
    // and if the file exists
    if (entry != NULL && (mode & O_WRONLY)) {
        Tcl_Seek(channel, 0, SEEK_END);
    }

done:
    // Detach the channel from the current interp as it should be
    // a pristine channel
    Tcl_DetachChannel(interp, channel);
#if TCL_MAJOR_VERSION < 9
    // In Windows, native OpenFileChannel() sets -eofchar. Let's simulate
    //the same behaviour.
#ifdef _WIN32
    Tcl_SetChannelOption(NULL, channel, "-eofchar", "\032 {}");
#endif
#endif
    CookfsLog(printf("ok"));
    goto ret;

posixerror:

    if (interp == NULL) {
        goto ret;
    }

    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_PosixError(interp), -1));

interperror:

    // We go here if we expect the interp result to already contain an error
    // message.
    if (interp == NULL) {
        goto ret;
    }

    Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't open \"%s\": %s",
        Tcl_GetString(pathPtr), Tcl_GetStringResult(interp)));

ret:
    if (index != NULL) {
        Cookfs_FsindexUnlock(index);
    }
    CookfsLog(printf("return: %p", (void *)channel));
    return channel;
}

// cppcheck-suppress-begin constParameterCallback
static int CookfsMatchInDirectory(Tcl_Interp *interp, Tcl_Obj *returnPtr,
    Tcl_Obj *pathPtr, const char *pattern, Tcl_GlobTypeData *types)
{
// cppcheck-suppress-end constParameterCallback

    UNUSED(interp);

    if (pattern == NULL) {
        CookfsLog(printf("check if path exists [%s](tcl obj: %p)",
            Tcl_GetString(pathPtr), (void *)pathPtr));
    } else {
        CookfsLog(printf("check path [%s](tcl obj: %p) for pattern [%s]",
            Tcl_GetString(pathPtr), (void *)pathPtr, pattern));
    }

    int wanted;
    if (types == NULL) {
        wanted = TCL_GLOB_TYPE_DIR | TCL_GLOB_TYPE_FILE;
        CookfsLog(printf("no types specified"));
    } else {
        wanted = types->type;
        CookfsLog(printf("types [%d]", wanted));
        // If we are not looking for files/dirs/mounts, then just return.
        // We have only those entries.
        if ((wanted & (TCL_GLOB_TYPE_DIR | TCL_GLOB_TYPE_FILE |
            TCL_GLOB_TYPE_MOUNT)) == 0)
        {
            CookfsLog(printf("there are no known types, return empty list"));
            return TCL_OK;
        }
    }

    // This is a special case where type == TCL_GLOB_TYPE_MOUNT
    // Tcl is looking for a list of mount points that match the given pattern.
    if (wanted & TCL_GLOB_TYPE_MOUNT) {
        CookfsLog(printf("check mount points"));
        Cookfs_CookfsSearchVfsToListObj(pathPtr, pattern, returnPtr);
        CookfsLog(printf("ok"));
        return TCL_OK;
    }

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        return TCL_OK;
    }

    Cookfs_Fsindex *index = ir->vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    // We could not find the file entry, just return empty result
    if (entry == NULL) {
        CookfsLog(printf("could not find the path in cookfs, return empty"
            " result"));
        goto done;
    }

    int isDirectory = Cookfs_FsindexEntryIsDirectory(entry);

    // If pattern is NULL, Tcl simply checks if the entry exists and has
    // the specified type
    if (pattern == NULL) {
        // If Tcl wants a directory and it is a directory or Tcl wants
        // a file and it is a file, then return it
        if (
            ((wanted & TCL_GLOB_TYPE_DIR) && isDirectory) ||
            ((wanted & TCL_GLOB_TYPE_FILE) && !isDirectory))
        {
            CookfsLog(printf("return result - exists"));
            Tcl_ListObjAppendElement(NULL, returnPtr, pathPtr);
        } else {
            CookfsLog(printf("return result - doesn't exist"));
        }
        goto done;
    }

    // If we are here, then Tcl wants to check for entries in the given
    // directory. But first, let's make sure the path is a directory.
    // If it is not, return an empty result.
    if (!isDirectory) {
        CookfsLog(printf("the path is not a directory, return empty result"));
        goto done;
    }

    int foundCount;
    Cookfs_FsindexEntry **foundList = Cookfs_FsindexListEntry(entry,
        &foundCount);

    Tcl_Obj *prefix = NULL;

    for (int i = 0; i < foundCount; i++) {

        Cookfs_FsindexEntry *entryCur = foundList[i];

        // First check for child entry type
        int isChildDirectory = Cookfs_FsindexEntryIsDirectory(entryCur);
        unsigned char fileNameLen;
        const char *fileName = Cookfs_FsindexEntryGetFileName(entryCur, &fileNameLen);
        if (
            !(((wanted & TCL_GLOB_TYPE_DIR) && isChildDirectory) ||
            ((wanted & TCL_GLOB_TYPE_FILE) && !isChildDirectory)))
        {
            CookfsLog(printf("child entry [%s] has wrong type", fileName));
            continue;
        }

        // Now check if child entry matches the pattern
        if (!Tcl_StringCaseMatch(fileName, pattern, 0)) {
            CookfsLog(printf("child entry [%s] doesn't match pattern",
                fileName));
            continue;
        }

        CookfsLog(printf("child entry [%s] is OK", fileName));

        // Prepare an object to be used as a prefix for the retrieved records.
        // It should be pathPtr + filesystem separator '/'. However, we
        // should not add another file system separator if pathPtr already
        // contains one at the end. This case is possible for mounted volumes
        // like 'mount:/'. And in this case we will use pathPtr as a prefix.
        if (prefix == NULL) {
            Tcl_Size pathLen;
            const char *pathStr = Tcl_GetStringFromObj(pathPtr, &pathLen);
            if (pathLen > 0 && pathStr[pathLen - 1] != VFS_SEPARATOR) {
                prefix = Tcl_DuplicateObj(pathPtr);
                Tcl_IncrRefCount(prefix);
                char sep = VFS_SEPARATOR;
                Tcl_AppendToObj(prefix, &sep, 1);
            } else {
                prefix = pathPtr;
            }
            CookfsLog(printf("use common prefix for all matches: [%s]",
                Tcl_GetString(prefix)));
        }

        // Join prefix + current entry and add it to the results
        Tcl_Obj *obj = Tcl_DuplicateObj(prefix);
        Tcl_AppendToObj(obj, fileName, fileNameLen);
        Tcl_ListObjAppendElement(NULL, returnPtr, obj);
        CookfsLog(printf("add file to results: [%s]",
            Tcl_GetString(obj)));

    }

    // Release the prefix only if we created it and it is not the same object
    // as pathPtr.
    if (prefix != NULL && prefix != pathPtr) {
        Tcl_DecrRefCount(prefix);
    }
    Cookfs_FsindexListFree(foundList);

done:
    Cookfs_FsindexUnlock(index);
    CookfsLog(printf("ok"));
    return TCL_OK;
}

// cppcheck-suppress constParameterCallback
static int CookfsUtime(Tcl_Obj *pathPtr, struct utimbuf *tval) {
    CookfsLog(printf("path [%s] time [%lld]", Tcl_GetString(pathPtr),
        (long long int)tval->modtime));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_WRITE, &ir)) {
        return -1;
    }

    int rc = 0;

    Cookfs_Fsindex *index = ir->vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    if (entry == NULL) {
        CookfsLog(printf("could not find the entry, return an error"));
        Tcl_SetErrno(ENOENT);
        rc = -1;
        goto done;
    }

    Cookfs_FsindexEntrySetFileTime(entry, tval->modtime);
    Cookfs_FsindexIncrChangeCount(index, 1);

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static Tcl_Obj *CookfsListVolumes(void) {
    Tcl_Obj *ret = Cookfs_CookfsGetVolumesList();
    // the ref counter will be decremented by Tcl core
    if (ret != NULL) {
        Tcl_IncrRefCount(ret);
    }
    // CookfsLog(printf("return [%s]", (ret == NULL ? "NULL" : "SET")));
    return ret;
}

static int CookfsCreateDirectory(Tcl_Obj *pathPtr) {

    CookfsLog(printf("path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_WRITE, &ir)) {
        return TCL_ERROR;
    }

    int rc = TCL_OK;

    Cookfs_Vfs *vfs = ir->vfs;
    Cookfs_Fsindex *index = vfs->index;

    // Try to create the directory entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexSetDirectory(index, ir->pathObj);

    if (entry == NULL) {
        CookfsLog(printf("could not create the directory entry, return"
            " an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        rc = TCL_ERROR;
        goto done;
    }

    if (vfs->isCurrentDirTime) {
        Tcl_Time now;
        Tcl_GetTime(&now);
        Cookfs_FsindexEntrySetFileTime(entry, now.sec);
    } else {
        Cookfs_FsindexEntrySetFileTime(entry, 0);
    }

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static int CookfsRemoveDirectory(Tcl_Obj *pathPtr, int recursive,
    Tcl_Obj **errorPtr)
{

    CookfsLog(printf("path [%s] recursive?%d", Tcl_GetString(pathPtr),
        recursive));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_WRITE, &ir)) {
        return TCL_ERROR;
    }

    int rc = TCL_OK;

    Cookfs_Fsindex *index = ir->vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    if (entry == NULL) {
        CookfsLog(printf("could not find the entry, return an error"));
        Tcl_SetErrno(ENOENT);
        goto returnError;
    }

    // Throw an error if the entry is not a directory
    if (!Cookfs_FsindexEntryIsDirectory(entry)) {
        CookfsLog(printf("is not a directory, return an error"));
        Tcl_SetErrno(ENOTDIR);
        goto returnError;
    }

    // Check if the directory is not empty
    if (!recursive && !Cookfs_FsindexEntryIsEmptyDirectory(entry)) {
        CookfsLog(printf("the directory is not empty, return an error"));
        Tcl_SetErrno(EEXIST);
        goto returnError;
    }

    int result = Cookfs_FsindexUnsetRecursive(index, ir->pathObj);

    // Check to see if anything's wrong
    if (!result) {
        CookfsLog(printf("internal error, return an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        goto returnError;
    }

    CookfsLog(printf("OK"));

    goto done;

returnError:

    *errorPtr = pathPtr;
    Tcl_IncrRefCount(*errorPtr);
    rc = TCL_ERROR;

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static int CookfsDeleteFile(Tcl_Obj *pathPtr) {

    CookfsLog(printf("path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *ir;

    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_WRITE, &ir)) {
        return TCL_ERROR;
    }

    int rc = TCL_OK;

    Cookfs_Vfs *vfs = ir->vfs;
    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index, ir->pathObj);

    if (entry == NULL) {
        CookfsLog(printf("could not find the entry, return an error"));
        Tcl_SetErrno(ENOENT);
        goto returnError;
    }

    // Throw an error if the entry is a directory
    if (Cookfs_FsindexEntryIsDirectory(entry)) {
        CookfsLog(printf("is not a file, return an error"));
        Tcl_SetErrno(EISDIR);
        goto returnError;
    }

    if (Cookfs_FsindexEntryIsPending(entry)) {
        CookfsLog(printf("the entry is pending, remove it from small file"
            " buffer"));
        Cookfs_WriterLockWrite(vfs->writer, NULL);
        Cookfs_WriterRemoveFile(vfs->writer, entry);
        Cookfs_WriterUnlock(vfs->writer);
    }

    int result = Cookfs_FsindexUnset(index, ir->pathObj);

    // Check to see if anything's wrong
    if (!result) {
        CookfsLog(printf("internal error, return an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        goto returnError;
    }

    CookfsLog(printf("OK"));

    goto done;

returnError:
    rc = TCL_ERROR;

done:
    Cookfs_FsindexUnlock(index);
    return rc;
}

static Cookfs_VfsAttributeSetType CookfsAttrGetType(cookfsInternalRep *ir);
static Cookfs_VfsAttribute CookfsAttrGetByIndex(cookfsInternalRep *ir,
    int index);

static const char *const *CookfsFileAttrStrings(Tcl_Obj *pathPtr,
    Tcl_Obj **objPtrRef)
{

    CookfsLog(printf("path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *ir;
    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        return NULL;
    }

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *objPtrRef = tsdPtr->attrs_list[CookfsAttrGetType(ir)];

    Cookfs_FsindexUnlock(ir->vfs->index);
    return NULL;

}

static int CookfsFileAttrsGet(Tcl_Interp *interp, int index, Tcl_Obj *pathPtr,
    Tcl_Obj **objPtrRef)
{

    UNUSED(interp);

    CookfsLog(printf("path [%s] index:%d", Tcl_GetString(pathPtr), index));

    cookfsInternalRep *ir;
    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_READ, &ir)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't %s attributes"
                " \"%s\": %s", "get", Tcl_GetString(pathPtr),
                Tcl_ErrnoMsg(Tcl_GetErrno())));
        }
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = ir->vfs;

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    Cookfs_VfsAttribute attr = CookfsAttrGetByIndex(ir, index);

    switch (attr) {
    case COOKFS_VFS_ATTRIBUTE_VFS:
        *objPtrRef = tsdPtr->attr_vfs_value;
        CookfsLog(printf("return value for -vfs"));
        break;
    case COOKFS_VFS_ATTRIBUTE_HANDLE:
#ifdef TCL_THREADS
        if (vfs->threadId != Tcl_GetCurrentThread()) {
            CookfsLog(printf("return empty value due to wrong threadId"));
            *objPtrRef = Tcl_NewObj();
        } else {
#endif /* TCL_THREADS */
            *objPtrRef = CookfsGetVfsObjectCmd(interp, vfs);
            CookfsLog(printf("return value for -handle"));
#ifdef TCL_THREADS
        }
#endif /* TCL_THREADS */
        break;
    case COOKFS_VFS_ATTRIBUTE_FILESET:
        CookfsLog(printf("return value for -fileset"));
        *objPtrRef = Cookfs_VfsFilesetGet(vfs);
        break;
    }

    Cookfs_FsindexUnlock(vfs->index);
    return TCL_OK;
}

static int CookfsFileAttrsSet(Tcl_Interp *interp, int index, Tcl_Obj *pathPtr,
    Tcl_Obj *objPtr)
{

    CookfsLog(printf("path [%s] index:%d = [%s]", Tcl_GetString(pathPtr),
        index, Tcl_GetString(objPtr)));

    // Note: we request LOCK_WRITE mode for CookfsValidatePathAndLockFsindex()
    // and it should return an error for read-only VFS. However, here we allow
    // write-locks for fsindex even if VFS is readonly. We need this, because
    // we want to allow to change some attributes even for readonly VFS.
    // E.g. we want to allow switching of filesets.
    //
    // This means, then for individual attributes we should check additionaly
    // whether VFS is in readonly mode.
    cookfsInternalRep *ir;
    if (!CookfsValidatePathAndLockFsindex(pathPtr, COOKFS_LOCK_WRITE_SOFT,
        &ir))
    {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't %s attributes"
                " \"%s\": %s", "set", Tcl_GetString(pathPtr),
                Tcl_ErrnoMsg(Tcl_GetErrno())));
        }
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = ir->vfs;

    Cookfs_VfsAttribute attr = CookfsAttrGetByIndex(ir, index);

    int rc = TCL_ERROR;
    Tcl_Obj *result = NULL;

    switch (attr) {
    case COOKFS_VFS_ATTRIBUTE_FILESET: ; // empty statement

        Tcl_Obj **result_ptr = (interp == NULL ? NULL : &result);

        rc = Cookfs_VfsFilesetSelect(vfs, objPtr, result_ptr, result_ptr);

        if (rc != TCL_OK && interp != NULL && result == NULL) {
            result = Tcl_NewStringObj("unknown error", -1);
        }

        break;
    case COOKFS_VFS_ATTRIBUTE_VFS:
    case COOKFS_VFS_ATTRIBUTE_HANDLE:
        Tcl_SetErrno(EROFS);
        if (interp != NULL) {
            result = Tcl_ObjPrintf("attribute \"%s\" is read-only",
                cookfs_attribute_names[attr]);
        }
        break;
    }

    // We don't check here whether interp is NULL. If it is NULL, then we
    // don't use the result variable and it remains NULL.
    if (result != NULL) {
        Tcl_SetObjResult(interp, result);
    }
    Cookfs_FsindexUnlock(vfs->index);
    return rc;

}

static Cookfs_VfsAttributeSetType CookfsAttrGetType(cookfsInternalRep *ir) {
    // Check the length of the split path. If the length is zero, then
    // we want to get attributes for cookfs.
    if (ir->pathObj->fullNameLength == 0) {
        // We have a root element
        return COOKFS_VFS_ATTRIBUTE_SET_VFS;
    }

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(ir->vfs->index,
        ir->pathObj);
    if (entry != NULL && !Cookfs_FsindexEntryIsDirectory(entry)) {
        return COOKFS_VFS_ATTRIBUTE_SET_FILE;
    }

    return COOKFS_VFS_ATTRIBUTE_SET_DIRECTORY;
}

static Cookfs_VfsAttribute CookfsAttrGetByIndex(cookfsInternalRep *ir,
    int index)
{
    return cookfs_attribute_set2attribute[CookfsAttrGetType(ir)][index];
}
