/*
 * vfsDriver.c
 *
 * Provides implementation for cookfs VFS
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include <unistd.h>
#include <sys/stat.h>
#include <utime.h>
#include <fcntl.h>

// Cached internal representation of a cookfs entry
typedef struct cookfsInternalRep {
    Cookfs_Vfs *vfs;
    Tcl_Obj *relativePathObj;
    int relativePathObjLen;
} cookfsInternalRep;

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
//static Tcl_FSFileAttrStringsProc CookfsFileAttrStrings;
//static Tcl_FSFileAttrsGetProc CookfsFileAttrsGet;
//static Tcl_FSFileAttrsSetProc CookfsFileAttrsSet;
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
    NULL, /* fileAttrStringsProc */
    NULL, /* fileAttrsGetProc */
    NULL, /* fileAttrsSetProc */
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

static int CookfsPathInFilesystem(Tcl_Obj *pathPtr,
    ClientData *clientDataPtr)
{
    //CookfsLog(printf("CookfsPathInFilesystem: start; looking for [%s]",
    //    Tcl_GetString(pathPtr)));

    Tcl_Obj *normPathObj = Tcl_FSGetNormalizedPath(NULL, pathPtr);
    if (normPathObj == NULL) {
    //    CookfsLog(printf("CookfsPathInFilesystem: could not normalize"
    //        " the path."));
        return -1;
    }

    int normPathLen;
    char *normPathStr = Tcl_GetStringFromObj(normPathObj, &normPathLen);
    if (normPathLen == 0) {
    //    CookfsLog(printf("CookfsPathInFilesystem: an empty path given."));
        return -1;
    }

    int normPathCut = normPathLen;
    Cookfs_Vfs *vfs;

    while (normPathCut) {
        //CookfsLog(printf("CookfsPathInFilesystem: checking the"
        //    " path [%s] cut at [%d]", normPathStr, normPathCut));
        vfs = Cookfs_CookfsFindVfs(normPathObj, normPathCut);
        if (vfs != NULL) {
            break;
        }
        // Search for the previous file separator
        while (normPathStr[--normPathCut] != VFS_SEPARATOR
            && normPathCut > 0) {}
    }

    // We could not found the path in the VFS
    if (!normPathCut) {
        //CookfsLog(printf("CookfsPathInFilesystem: return -1"));
        return -1;
    }

    // We have found the path in the VFS

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)ckalloc(sizeof(cookfsInternalRep));
    if (internalRep == NULL) {
        //CookfsLog(printf("CookfsPathInFilesystem: panic! could not create"
        //    " internalRep."));
        return -1;
    }

    internalRep->vfs = vfs;

    // Trim the first VFS_SEPARATOR if there is one
    if (normPathCut != normPathLen) {
        normPathCut++;
    }

    //CookfsLog(printf("CookfsPathInFilesystem: computed relative path [%s]",
    //    normPathStr + normPathCut));

    Tcl_Obj *normRelativePathObj = Tcl_NewStringObj(normPathStr + normPathCut,
        normPathLen - normPathCut);
    internalRep->relativePathObj = Tcl_FSSplitPath(normRelativePathObj,
        &internalRep->relativePathObjLen);
    Tcl_IncrRefCount(internalRep->relativePathObj);
    Tcl_IncrRefCount(normRelativePathObj);
    Tcl_DecrRefCount(normRelativePathObj);

    *clientDataPtr = (ClientData)internalRep;
    //CookfsLog(printf("CookfsPathInFilesystem: return found entry as"
    //    " an internalRep [%p]", (void *)internalRep));
    return TCL_OK;
}

static ClientData CookfsDupInternalRep(ClientData clientData) {
    cookfsInternalRep *internalRep = (cookfsInternalRep *)clientData;
    cookfsInternalRep *internalCopyRep =
        (cookfsInternalRep *)ckalloc(sizeof(cookfsInternalRep));
    if (internalCopyRep != NULL) {
        internalCopyRep->vfs = internalRep->vfs;
        internalCopyRep->relativePathObj = internalRep->relativePathObj;
        Tcl_IncrRefCount(internalCopyRep->relativePathObj);
    }
    CookfsLog(printf("CookfsDupInternalRep: copy from [%p] to [%p]",
        (void *)internalRep, (void *)internalCopyRep));
    return (ClientData)internalCopyRep;
}

static void CookfsFreeInternalRep(ClientData clientData) {
    CookfsLog(printf("CookfsFreeInternalRep: release [%p]",
        (void *)clientData));
    cookfsInternalRep *internalRep = (cookfsInternalRep *)clientData;
    if (internalRep != NULL) {
        Tcl_DecrRefCount(internalRep->relativePathObj);
        ckfree((char *)internalRep);
    }
}

static Tcl_Obj *CookfsFilesystemPathType(Tcl_Obj *pathPtr) {
    UNUSED(pathPtr);
    CookfsLog(printf("CookfsFilesystemPathType: return [cookfs]"));
    return Tcl_NewStringObj("cookfs", -1);
}

static Tcl_Obj *CookfsFilesystemSeparator(Tcl_Obj *pathPtr) {
    UNUSED(pathPtr);
    CookfsLog(printf("CookfsFilesystemSeparator: return [/]"));
    char sep = VFS_SEPARATOR;
    return Tcl_NewStringObj(&sep, 1);
}

static int CookfsStat(Tcl_Obj *pathPtr, Tcl_StatBuf *bufPtr) {
    CookfsLog(printf("CookfsStat: path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. Stat() should only be called for files
    // belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsStat: something really wrong,"
            " return an error"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return -1;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;
    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    if (entry == NULL) {
        CookfsLog(printf("CookfsStat: could not find the entry,"
            " return an error"));
        Tcl_SetErrno(ENOENT);
        return -1;
    }

    memset(bufPtr, 0, sizeof(*bufPtr));

    if (Cookfs_FsindexEntryIsDirectory(entry)) {
        // Fill the Tcl_StatBuf for a directory
        bufPtr->st_mode = 040777;
        bufPtr->st_size = 0;
        CookfsLog(printf("CookfsStat: return stats for a directory"));
    } else {
        // Fill the Tcl_StatBuf for a file
        bufPtr->st_mode = 0100777;
        bufPtr->st_size = entry->data.fileInfo.fileSize;
        CookfsLog(printf("CookfsStat: return stats for a file"));
    }
    // Tcl variant of Cookfs returns mtime as 0 for the root directory
    if (internalRep->relativePathObjLen == 0) {
        bufPtr->st_mtime = 0;
        bufPtr->st_ctime = 0;
        bufPtr->st_atime = 0;
    } else {
        bufPtr->st_mtime = entry->fileTime;
        bufPtr->st_ctime = entry->fileTime;
        bufPtr->st_atime = entry->fileTime;
    }
    bufPtr->st_nlink = 1;

    return 0;
}

static int CookfsAccess(Tcl_Obj *pathPtr, int mode) {
    CookfsLog(printf("CookfsAccess: path [%s] mode [%d]",
        Tcl_GetString(pathPtr), mode));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. Access() should only be called for files
    // belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsAccess: something really wrong,"
            " return false"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return -1;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;

    // If write mode is requested, but the mount is in readonly mode, return
    // an error
    if ((mode & W_OK) && Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsAccess: vfs is in a readonly mode,"
            " return false"));
        Tcl_SetErrno(EROFS);
        return -1;
    }

    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    // The entry has been found
    if (entry != NULL) {
        // Directories are readonly
        if ((mode & W_OK) && Cookfs_FsindexEntryIsDirectory(entry)) {
            CookfsLog(printf("CookfsAccess: the path is directory,"
                " write access denied, return false"));
            Tcl_SetErrno(EISDIR);
            return -1;
        }
        // Allow read/write access to files and read access to directories
        CookfsLog(printf("CookfsAccess: return true"));
        return 0;
    }

    // Could not find the entry, return an error
    CookfsLog(printf("CookfsAccess: could not find the entry, return false"));
    Tcl_SetErrno(ENOENT);
    return -1;
}

static Tcl_Channel CookfsOpenFileChannel(Tcl_Interp *interp, Tcl_Obj *pathPtr,
    int mode, int permissions)
{

    UNUSED(permissions);

    CookfsLog(printf("CookfsOpenFileChannel: path [%s] mode [%d]"
        " permissions [%d]", Tcl_GetString(pathPtr), mode, permissions));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. OpenFileChannel() should only be called
    // for files belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsOpenFileChannel: something really wrong,"
            " return false"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        goto posixerror;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;
    Cookfs_Pages *pages = vfs->pages;
    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    Tcl_Channel channel;

    if (entry != NULL) {
        // The entry already exists

        // Check if entry is a directory
        if (Cookfs_FsindexEntryIsDirectory(entry)) {
            CookfsLog(printf("CookfsOpenFileChannel: the path is"
                " a directory"));
            Tcl_SetErrno(EISDIR);
            goto posixerror;
        }

        // Use readerchannel for readonly mode
        if (mode == O_RDONLY) {

            // If entry blocks are in small file buffer, then open the file
            // using writerchannel
            if (Cookfs_FsindexEntryIsPending(entry)) {
                CookfsLog(printf("CookfsOpenFileChannel: the file is in"
                    " a pending state, open it using writerchannel"));
                channel = Cookfs_CreateWriterchannel(pages, index,
                    vfs->writer->commandObj, NULL, 0, entry, interp);
            } else {
                CookfsLog(printf("CookfsOpenFileChannel: the file is NOT in"
                    " a pending state, open it using readerchannel"));
                channel = Cookfs_CreateReaderchannel(pages, index, NULL, entry,
                    interp, NULL);
            }

            if (channel == NULL) {
                CookfsLog(printf("CookfsOpenFileChannel: got NULL channel"));
                Tcl_SetErrno(EINTR); // Interrupted system call
                goto posixerror;
            }

            goto done;

        }

    } else {
        // The entry doesn't exist

        // The file must exists is mode is O_RDONLY
        if (mode == O_RDONLY) {
            CookfsLog(printf("CookfsOpenFileChannel: file doesn't exist"));
            Tcl_SetErrno(ENOENT);
            goto posixerror;
        }

        // Check if parent exists
        Cookfs_FsindexEntry *entryParent = CookfsFsindexFindElement(index,
            internalRep->relativePathObj, internalRep->relativePathObjLen - 1);
        if (entryParent == NULL) {
            CookfsLog(printf("CookfsOpenFileChannel: parent directory"
                " doesn't exist"));
            Tcl_SetErrno(ENOENT);
            goto posixerror;
        }

        // Throw an error if parent is not a directory
        if (!Cookfs_FsindexEntryIsDirectory(entryParent)) {
            CookfsLog(printf("CookfsOpenFileChannel: parent is not"
                " a directory"));
            Tcl_SetErrno(ENOTDIR);
            goto posixerror;
        }
    }

    // If we're here, we need to open the file in write mode,
    // i.e.: mode & O_WRONLY || mode & O_RDWR

    // Make sure that VFS is not in RO mode
    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsOpenFileChannel: filesystem is in"
            " readonly mode, return an error"));
        Tcl_SetErrno(EROFS);
        goto posixerror;
    }

    // If we want to open the file from scratch, than don't pass the entry
    // to Cookfs_CreateWriterchannel()
    if (mode & O_TRUNC) {
        entry = NULL;
    }

    channel = Cookfs_CreateWriterchannel(pages, index,
        vfs->writer->commandObj, internalRep->relativePathObj,
        internalRep->relativePathObjLen, entry, interp);

    if (channel == NULL) {
        CookfsLog(printf("CookfsOpenFileChannel: got NULL from"
            " Cookfs_CreateWriterchannel()"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        goto posixerror;
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
    CookfsLog(printf("CookfsOpenFileChannel: ok"));
    return channel;

posixerror:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't open \"%s\": %s",
        Tcl_GetString(pathPtr), Tcl_PosixError(interp)));
    return NULL;
}

static int CookfsMatchInDirectory(Tcl_Interp *interp, Tcl_Obj *returnPtr,
    Tcl_Obj *pathPtr, CONST char *pattern, Tcl_GlobTypeData *types)
{

    UNUSED(interp);

    if (pattern == NULL) {
        CookfsLog(printf("CookfsMatchInDirectory: check if path exists [%s]",
            Tcl_GetString(pathPtr)));
    } else {
        CookfsLog(printf("CookfsMatchInDirectory: check path [%s] for"
            " pattern [%s]", Tcl_GetString(pathPtr), pattern));
    }

    int wanted;
    if (types == NULL) {
        wanted = TCL_GLOB_TYPE_DIR | TCL_GLOB_TYPE_FILE;
        CookfsLog(printf("CookfsMatchInDirectory: no types specified"));
    } else {
        wanted = types->type;
        CookfsLog(printf("CookfsMatchInDirectory: types [%d]", wanted));
        // If we are not looking for files/dirs/mounts, then just return.
        // We have only those entries.
        if ((wanted & (TCL_GLOB_TYPE_DIR | TCL_GLOB_TYPE_FILE |
            TCL_GLOB_TYPE_MOUNT)) == 0)
        {
            CookfsLog(printf("CookfsMatchInDirectory: there are no known"
                " types, return empty list"));
            return TCL_OK;
        }
    }

    // This is a special case where type == TCL_GLOB_TYPE_MOUNT
    // Tcl is looking for a list of mount points that match the given pattern.
    if (wanted & TCL_GLOB_TYPE_MOUNT) {
        CookfsLog(printf("CookfsMatchInDirectory: check mount points"));
        Cookfs_CookfsSearchVfsToListObj(pathPtr, pattern, returnPtr);
        CookfsLog(printf("CookfsMatchInDirectory: ok"));
        return TCL_OK;
    }

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // The path does not belong to cookfs, just return empty result
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsMatchInDirectory: the path is not in cookfs,"
            " return empty result"));
        return TCL_OK;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;
    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    // We could not find the file entry, just return empty result
    if (entry == NULL) {
        CookfsLog(printf("CookfsMatchInDirectory: could not find the path"
            " in cookfs, return empty result"));
        return TCL_OK;
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
            CookfsLog(printf("CookfsMatchInDirectory: return result"
                " - exists"));
            Tcl_ListObjAppendElement(NULL, returnPtr, pathPtr);
        } else {
            CookfsLog(printf("CookfsMatchInDirectory: return result"
                " - doesn't exist"));
        }
        return TCL_OK;
    }

    // If we are here, then Tcl wants to check for entries in the given
    // directory. But first, let's make sure the path is a directory.
    // If it is not, return an empty result.
    if (!isDirectory) {
        CookfsLog(printf("CookfsMatchInDirectory: the path is not a directory,"
            " return empty result"));
        return TCL_OK;
    }

    int foundCount;
    Cookfs_FsindexEntry **foundList = Cookfs_FsindexListEntry(entry,
        &foundCount);

    Tcl_Obj *prefix = NULL;

    for (int i = 0; i < foundCount; i++) {

        Cookfs_FsindexEntry *entryCur = foundList[i];

        // First check for child entry type
        int isDirectory = Cookfs_FsindexEntryIsDirectory(entryCur);
        if (
            !(((wanted & TCL_GLOB_TYPE_DIR) && isDirectory) ||
            ((wanted & TCL_GLOB_TYPE_FILE) && !isDirectory)))
        {
            CookfsLog(printf("CookfsMatchInDirectory: child entry [%s]"
                " has wrong type", entryCur->fileName));
            continue;
        }

        // Now check if child entry matches the pattern
        if (!Tcl_StringCaseMatch(entryCur->fileName, pattern, 0)) {
            CookfsLog(printf("CookfsMatchInDirectory: child entry [%s]"
                " doesn't match pattern", entryCur->fileName));
            continue;
        }

        CookfsLog(printf("CookfsMatchInDirectory: child entry [%s]"
            " is OK", entryCur->fileName));

        // Prepare an object to be used as a prefix for the retrieved records.
        if (prefix == NULL) {
            prefix = Tcl_DuplicateObj(pathPtr);
            Tcl_IncrRefCount(prefix);
            char sep = VFS_SEPARATOR;
            Tcl_AppendToObj(prefix, &sep, 1);
        }

        // Join prefix + current entry and add it to the results
        Tcl_Obj *obj = Tcl_DuplicateObj(prefix);
        Tcl_AppendToObj(obj, entryCur->fileName, entryCur->fileNameLen);
        Tcl_ListObjAppendElement(NULL, returnPtr, obj);

    }

    if (prefix != NULL) {
        Tcl_DecrRefCount(prefix);
    }
    Cookfs_FsindexListFree(foundList);

    return TCL_OK;
}

static int CookfsUtime(Tcl_Obj *pathPtr, struct utimbuf *tval) {
    CookfsLog(printf("CookfsUtime: path [%s] time [%ld]",
        Tcl_GetString(pathPtr), tval->modtime));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. Utime() should only be called for files
    // belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsUtime: something really wrong,"
            " return an error"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return -1;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsUtime: filesystem is in readonly mode,"
            " return an error"));
        Tcl_SetErrno(EROFS);
        return -1;
    }

    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    if (entry == NULL) {
        CookfsLog(printf("CookfsUtime: could not find the entry,"
            " return an error"));
        Tcl_SetErrno(ENOENT);
        return -1;
    }

    entry->fileTime = tval->modtime;
    Cookfs_FsindexIncrChangeCount(index, 1);

    return 0;
}

static Tcl_Obj *CookfsListVolumes(void) {
    Tcl_Obj *ret = Cookfs_CookfsGetVolumesList();
    // the ref counter will be decremented by Tcl core
    if (ret != NULL) {
        Tcl_IncrRefCount(ret);
    }
    // CookfsLog(printf("CookfsListVolumes: return [%s]",
    //    (ret == NULL ? "NULL" : "SET")));
    return ret;
}

static int CookfsCreateDirectory(Tcl_Obj *pathPtr) {

    CookfsLog(printf("CookfsCreateDirectory: path [%s]",
        Tcl_GetString(pathPtr)));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. CreateDirectory() should only be called
    // for files belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsCreateDirectory: something really wrong,"
            " return an error"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsCreateDirectory: filesystem is in readonly"
            " mode, return an error"));
        Tcl_SetErrno(EROFS);
        return TCL_ERROR;
    }

    Cookfs_Fsindex *index = vfs->index;

    // Try to create the directory entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexSet(index,
        internalRep->relativePathObj, COOKFS_NUMBLOCKS_DIRECTORY);

    if (entry == NULL) {
        CookfsLog(printf("CookfsCreateDirectory: could not create"
            " the directory entry, return an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        return TCL_ERROR;
    }

    if (vfs->isCurrentDirTime) {
        Tcl_Time now;
        Tcl_GetTime(&now);
        entry->fileTime = now.sec;
    } else {
        entry->fileTime = 0;
    }

    return TCL_OK;

}

static int CookfsRemoveDirectory(Tcl_Obj *pathPtr, int recursive,
    Tcl_Obj **errorPtr)
{

    CookfsLog(printf("CookfsRemoveDirectory: path [%s] recursive?%d",
        Tcl_GetString(pathPtr), recursive));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. RemoveDirectory() should only be called
    // for files belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsRemoveDirectory: something really wrong,"
            " return an error"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsRemoveDirectory: filesystem is in"
            " readonly mode, return an error"));
        Tcl_SetErrno(EROFS);
        return TCL_ERROR;
    }

    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    if (entry == NULL) {
        CookfsLog(printf("CookfsRemoveDirectory: could not find the entry,"
            " return an error"));
        Tcl_SetErrno(ENOENT);
        goto returnError;
    }

    // Throw an error if the entry is not a directory
    if (!Cookfs_FsindexEntryIsDirectory(entry)) {
        CookfsLog(printf("CookfsRemoveDirectory: is not a directory,"
            " return an error"));
        Tcl_SetErrno(ENOTDIR);
        goto returnError;
    }

    // Check if the directory is not empty
    if (!recursive && entry->data.dirInfo.childCount) {
        CookfsLog(printf("CookfsRemoveDirectory: the directory is not empty,"
            " return an error"));
        Tcl_SetErrno(EEXIST);
        goto returnError;
    }

    int result = Cookfs_FsindexUnsetRecursive(index,
        internalRep->relativePathObj);

    // Check to see if anything's wrong
    if (!result) {
        CookfsLog(printf("CookfsRemoveDirectory: internal error,"
            " return an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        goto returnError;
    }

    CookfsLog(printf("CookfsRemoveDirectory: OK"));

    return TCL_OK;

returnError:

    *errorPtr = pathPtr;
    Tcl_IncrRefCount(*errorPtr);
    return TCL_ERROR;

}

static int CookfsDeleteFile(Tcl_Obj *pathPtr) {

    CookfsLog(printf("CookfsDeleteFile: path [%s]", Tcl_GetString(pathPtr)));

    cookfsInternalRep *internalRep =
        (cookfsInternalRep *)Tcl_FSGetInternalRep(pathPtr, &cookfsFilesystem);

    // Something is really wrong. DeleteFile() should only be called for files
    // belonging to CookFS. But here we got NULL CookFS mount.
    if (internalRep == NULL) {
        CookfsLog(printf("CookfsDeleteFile: something really wrong,"
            " return an error"));
        Tcl_SetErrno(ENODEV); // Operation not supported by device
        return TCL_ERROR;
    }

    Cookfs_Vfs *vfs = internalRep->vfs;

    if (Cookfs_VfsIsReadonly(vfs)) {
        CookfsLog(printf("CookfsDeleteFile: filesystem is in readonly mode,"
            " return an error"));
        Tcl_SetErrno(EROFS);
        return TCL_ERROR;
    }

    Cookfs_Fsindex *index = vfs->index;

    // Try to find the file entry
    Cookfs_FsindexEntry *entry = Cookfs_FsindexGet(index,
        internalRep->relativePathObj);

    if (entry == NULL) {
        CookfsLog(printf("CookfsDeleteFile: could not find the entry,"
            " return an error"));
        Tcl_SetErrno(ENOENT);
        return TCL_ERROR;
    }

    // Throw an error if the entry is a directory
    if (Cookfs_FsindexEntryIsDirectory(entry)) {
        CookfsLog(printf("CookfsDeleteFile: is not a file,"
            " return an error"));
        Tcl_SetErrno(EISDIR);
        return TCL_ERROR;
    }

    int result = Cookfs_FsindexUnset(index, internalRep->relativePathObj);

    // Check to see if anything's wrong
    if (!result) {
        CookfsLog(printf("CookfsDeleteFile: internal error,"
            " return an error"));
        Tcl_SetErrno(EINTR); // Interrupted system call
        return TCL_ERROR;
    }

    CookfsLog(printf("CookfsDeleteFile: OK"));

    return TCL_OK;

}
