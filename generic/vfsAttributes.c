/*
 * vfsVfs.c
 *
 * Provides methods for manipulating VFS attributes in cookfs
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "vfsAttributes.h"
#include "vfsCmd.h"
#include "pagesCompr.h"
#include <errno.h>

typedef struct ThreadSpecificData {
    int initialized;
    Tcl_Obj *attrs_list[COOKFS_VFS_ATTRIBUTE_SET_COUNT];
    Tcl_Obj *attr_vfs_value;
    Tcl_Obj *attr_value_empty;
    Tcl_Obj *attr_value_true;
    Tcl_Obj *attr_value_false;
    Tcl_Obj *attr_value_compression_none;
    Tcl_Obj *attr_part_head;
    Tcl_Obj *attr_part_data;
    Tcl_Obj *attr_part_tail;
    Tcl_Obj *attr_block_page;
    Tcl_Obj *attr_block_offset;
    Tcl_Obj *attr_block_size;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

typedef int (Cookit_AttributeGetProc)(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr);
typedef int (Cookit_AttributeSetProc)(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value);

static const int attr_arr_vfs[] = {
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_HANDLE,
    COOKFS_VFS_ATTRIBUTE_PAGES,
    COOKFS_VFS_ATTRIBUTE_METADATA,
    COOKFS_VFS_ATTRIBUTE_FILESET,
    COOKFS_VFS_ATTRIBUTE_ARCHIVE,
    COOKFS_VFS_ATTRIBUTE_WRITETOMEMORY,
    COOKFS_VFS_ATTRIBUTE_READONLY,
    COOKFS_VFS_ATTRIBUTE_SMALLFILEBUFFERSIZE,
    COOKFS_VFS_ATTRIBUTE_CACHESIZE,
    COOKFS_VFS_ATTRIBUTE_VOLUME,
    COOKFS_VFS_ATTRIBUTE_COMPRESSION,
#ifdef TCL_THREADS
    COOKFS_VFS_ATTRIBUTE_SHARED,
#endif /* TCL_THREADS */
#ifdef COOKFS_USECCRYPTO
    COOKFS_VFS_ATTRIBUTE_PASSWORD,
    COOKFS_VFS_ATTRIBUTE_ENCRYPTKEY,
    COOKFS_VFS_ATTRIBUTE_ENCRYPTLEVEL,
#endif /* COOKFS_USECCRYPTO */
    COOKFS_VFS_ATTRIBUTE_PARTS,
    COOKFS_VFS_ATTRIBUTE_RELATIVE,
    -1
};

static const int attr_arr_file[] = {
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_UNCOMPSIZE,
    COOKFS_VFS_ATTRIBUTE_COMPSIZE,
    COOKFS_VFS_ATTRIBUTE_COMPRESSION,
    COOKFS_VFS_ATTRIBUTE_MOUNT,
    COOKFS_VFS_ATTRIBUTE_PENDING,
    COOKFS_VFS_ATTRIBUTE_BLOCKS,
    COOKFS_VFS_ATTRIBUTE_RELATIVE,
    -1
};

static const int attr_arr_directory[] = {
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_MOUNT,
    COOKFS_VFS_ATTRIBUTE_RELATIVE,
    -1
};

static const int *const
    cookfs_attribute_set2attribute[COOKFS_VFS_ATTRIBUTE_SET_COUNT] =
{
    attr_arr_vfs, attr_arr_file, attr_arr_directory
};

static ThreadSpecificData *CookfsGetThreadSpecificData(void);

Cookfs_VfsAttribute Cookfs_VfsAttributeGetFromSet(
    Cookfs_VfsAttributeSetType attr_set, int index)
{
    return
        (Cookfs_VfsAttribute)cookfs_attribute_set2attribute[attr_set][index];
}

Tcl_Obj *Cookfs_VfsAttributeList(Cookfs_VfsAttributeSetType attr_set) {

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    return tsdPtr->attrs_list[attr_set];

}

static int Cookfs_AttrGet_Archive(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    if (vfs->pages == NULL) {
        ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();
        *result_ptr = tsdPtr->attr_value_empty;
    } else {
        *result_ptr = Cookfs_PagesGetFilenameObj(vfs->pages);
    }

    return TCL_OK;

}

#ifdef COOKFS_USECCRYPTO

static int Cookfs_AttrGet_Password(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    if (vfs->pages == NULL) {
        *result_ptr = tsdPtr->attr_value_false;
    } else {
        *result_ptr = Cookfs_PagesIsEncryptionActive(vfs->pages) ?
            tsdPtr->attr_value_true : tsdPtr->attr_value_false;
    }
    CookfsLog(printf("return: %s", Tcl_GetString(*result_ptr)));

    return TCL_OK;

}

static int Cookfs_AttrSet_Password(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    if (CookfsMountHandleCommandPasswordImpl(vfs, interp, value) != TCL_OK) {
        return TCL_ERROR;
    }

    if (interp != NULL) {

        ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

        Tcl_Obj *result = Cookfs_PagesIsEncryptionActive(vfs->pages) ?
            tsdPtr->attr_value_true : tsdPtr->attr_value_false;

        Tcl_SetObjResult(interp, result);

    }

    return TCL_OK;

}

static int Cookfs_AttrGet_Encryptkey(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    if (vfs->pages == NULL) {
        *result_ptr = tsdPtr->attr_value_false;
    } else {
        *result_ptr = Cookfs_PagesIsEncryptkey(vfs->pages) ?
            tsdPtr->attr_value_true : tsdPtr->attr_value_false;
    }

    return TCL_OK;

}

static int Cookfs_AttrGet_Encryptlevel(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    if (vfs->pages == NULL) {
        *result_ptr = tsdPtr->attr_value_false;
    } else {
        int level = Cookfs_PagesGetEncryptlevel(vfs->pages);
        *result_ptr = Tcl_NewIntObj(level);
    }

    return TCL_OK;

}

#endif /* COOKFS_USECCRYPTO */

static int Cookfs_AttrGet_Readonly(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = Cookfs_VfsIsReadonly(vfs) ?
        tsdPtr->attr_value_true : tsdPtr->attr_value_false;

    return TCL_OK;

}

static int Cookfs_AttrGet_Writetomemory(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = Cookfs_WriterGetWritetomemory(vfs->writer) ?
        tsdPtr->attr_value_true : tsdPtr->attr_value_false;

    return TCL_OK;

}

static int Cookfs_AttrSet_Writetomemory(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    Tcl_Obj *result = NULL;
    int rc = TCL_OK;

    int status;
    if (Tcl_GetBooleanFromObj(interp, value, &status) != TCL_OK) {
        return TCL_ERROR;
    }

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    if (Cookfs_WriterGetWritetomemory(vfs->writer)) {

        if (status) {
            goto returnTrue;
        }

        if (interp != NULL) {
            result = Tcl_NewStringObj("unable to disable writetomemory mode"
                " when it is already enabled", -1);
        }
        goto error;

    }

    // If we are here, then current writetomemory status is false

    if (!status) {
        result = tsdPtr->attr_value_false;
        goto done;
    }

    if (!Cookfs_WriterLockWrite(vfs->writer, NULL)) {
        goto error;
    }
    Cookfs_WriterSetWritetomemory(vfs->writer, 1);
    Cookfs_VfsSetReadonly(vfs, 0);
    Cookfs_WriterUnlock(vfs->writer);

returnTrue:

    result = tsdPtr->attr_value_true;
    goto done;

error:

    rc = TCL_ERROR;

done:

    if (result != NULL && interp != NULL) {
        Tcl_SetObjResult(interp, result);
    }

    return rc;

}

static int Cookfs_AttrGet_Smallfilebuffersize(Tcl_Interp *interp,
    Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    if (!Cookfs_WriterLockRead(vfs->writer, NULL)) {
        return TCL_ERROR;
    }

    Tcl_WideInt size = Cookfs_WriterGetSmallfilebuffersize(vfs->writer);
    *result_ptr = Tcl_NewWideIntObj(size);

    Cookfs_WriterUnlock(vfs->writer);

    return TCL_OK;

}

static int Cookfs_AttrGet_Cachesize(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    int cachesize;
    if (vfs->pages == NULL) {
        cachesize = 0;
    } else {
        cachesize = Cookfs_PagesGetCacheSize(vfs->pages);
    }

    *result_ptr = Tcl_NewIntObj(cachesize);

    return TCL_OK;

}

static int Cookfs_AttrSet_Cachesize(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    int cachesize;
    if (Tcl_GetIntFromObj(interp, value, &cachesize) != TCL_OK) {
        return TCL_ERROR;
    }

    if (vfs->pages == NULL) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unable to set"
                " cache size on a writetomemory VFS", -1));
        }
        return TCL_ERROR;
    }

    Cookfs_PagesSetCacheSize(vfs->pages, cachesize);
    if (interp != NULL) {
        Tcl_SetObjResult(interp, value);
    }

    return TCL_OK;

}

static int Cookfs_AttrGet_Volume(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = Cookfs_VfsIsVolume(vfs) ?
        tsdPtr->attr_value_true : tsdPtr->attr_value_false;

    return TCL_OK;

}

#ifdef TCL_THREADS

static int Cookfs_AttrGet_Shared(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = Cookfs_VfsIsShared(vfs) ?
        tsdPtr->attr_value_true : tsdPtr->attr_value_false;

    return TCL_OK;

}

#endif /* TCL_THREADS */

static int Cookfs_AttrGet_Compression(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS ||
        entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    if (entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE) {

        if (Cookfs_FsindexEntryIsPending(entry)) {
            *result_ptr = tsdPtr->attr_value_compression_none;
            return TCL_OK;
        }

        int page_num;
        Cookfs_FsindexEntryGetBlock(entry, 0, &page_num, NULL, NULL);

        // Cookfs_PagesGetPageCompressionObj() may return NULL. It may happen
        // if the file is stored in aside pages and aside pages object is
        // not available right now. There is no good solution what to do in
        // this case. We should not return an error, because it will block
        // all attributes from being retrieved from this file. Thus, let's
        // return "nothing" (NULL) in this case.
        *result_ptr = Cookfs_PagesGetPageCompressionObj(vfs->pages, page_num);

        return TCL_OK;

    }

    if (vfs->pages == NULL) {
        *result_ptr = tsdPtr->attr_value_compression_none;
    } else {
        if (!Cookfs_PagesLockRead(vfs->pages, NULL)) {
            return TCL_ERROR;
        };
        *result_ptr = Cookfs_PagesGetCompressionObj(vfs->pages);
        Cookfs_PagesUnlock(vfs->pages);
    }

    return TCL_OK;

}

static int Cookfs_AttrSet_Compression(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS ||
        entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);
    UNUSED(entry);

    if (entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE) {
        Tcl_SetErrno(EROFS);
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("attribute \"%s\" is"
                " read-only", "-compression"));
        }
        return TCL_ERROR;
    }

    if (vfs->pages == NULL) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unable to set"
                " compression on a writetomemory VFS", -1));
        }
        return TCL_ERROR;
    }

    if (Cookfs_VfsIsReadonly(vfs)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unable to set compression on"
                " a readonly VFS", -1));
        }
        return TCL_ERROR;
    }

    int compression;
    int compressionLevel;
    if (Cookfs_CompressionFromObj(interp, value, &compression,
        &compressionLevel) != TCL_OK)
    {
        return TCL_ERROR;
    }

    // always purge small files cache when compression changes

    if (!Cookfs_WriterLockWrite(vfs->writer, NULL)) {
        return TCL_ERROR;
    }

    if (Cookfs_WriterPurge(vfs->writer, 0, NULL) != TCL_OK) {
        return TCL_ERROR;
    }

    if (!Cookfs_PagesLockWrite(vfs->pages, NULL)) {
        Cookfs_WriterUnlock(vfs->writer);
        return TCL_ERROR;
    };

    Cookfs_PagesSetCompression(vfs->pages, compression, compressionLevel);

    if (interp != NULL) {
        Tcl_SetObjResult(interp, Cookfs_PagesGetCompressionObj(vfs->pages));
    }

    Cookfs_PagesUnlock(vfs->pages);
    Cookfs_WriterUnlock(vfs->writer);
    return TCL_OK;

}

static int Cookfs_AttrGet_Parts(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();
    Tcl_WideInt headsize, datasize, tailsize;

    if (vfs->pages == NULL || !Cookfs_PagesPartsExist(vfs->pages)) {
        headsize = -1;
        datasize = -1;
        tailsize = -1;
    } else {
        headsize = Cookfs_PagesGetPartSize(vfs->pages, COOKFS_PAGES_PART_HEAD);
        datasize = Cookfs_PagesGetPartSize(vfs->pages, COOKFS_PAGES_PART_DATA);
        tailsize = Cookfs_PagesGetPartSize(vfs->pages, COOKFS_PAGES_PART_TAIL);
    }

    Tcl_Obj *result = Tcl_NewDictObj();
    Tcl_DictObjPut(NULL, result, tsdPtr->attr_part_head,
        Tcl_NewWideIntObj(headsize));
    Tcl_DictObjPut(NULL, result, tsdPtr->attr_part_data,
        Tcl_NewWideIntObj(datasize));
    Tcl_DictObjPut(NULL, result, tsdPtr->attr_part_tail,
        Tcl_NewWideIntObj(tailsize));

    *result_ptr = result;

    return TCL_OK;

}

static int Cookfs_AttrSet_Parts(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    if (vfs->pages == NULL) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unable to get"
                " archive parts from a writetomemory VFS", -1));
        }
        return TCL_ERROR;
    }

    if (!Cookfs_PagesPartsExist(vfs->pages)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("the archive has not"
                " yet been saved to disk", -1));
        }
        return TCL_ERROR;
    }

    Tcl_Obj *result = NULL;
    int rc = TCL_OK;

    Tcl_Obj **objv;
    Tcl_Size objc;
    if (Tcl_ListObjGetElements(interp, value, &objc, &objv) != TCL_OK) {
        goto error;
    }

    // If empty value specified or more than 2 list elements, then consider
    // it as an error
    if (objc < 1 || objc > 2) {
        if (interp != NULL) {
            result = Tcl_NewStringObj("a list of 1 or 2 elements is expected"
                " as an argument to the -parts attribute", -1);
        }
        goto error;
    }

    static const struct {
        // cppcheck-suppress unusedStructMember
        const char *name;
        Cookfs_PagesPartsType part;
    } parts[] = {
        { "head", COOKFS_PAGES_PART_HEAD },
        { "data", COOKFS_PAGES_PART_DATA },
        { "tail", COOKFS_PAGES_PART_TAIL },
        { NULL }
    };

    int idx;
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], parts, sizeof(parts[0]),
        "archive part type", 0, &idx) != TCL_OK)
    {
        return TCL_ERROR;
    }

    Cookfs_PagesPartsType part = parts[idx].part;

    if (objc == 1) {

        // If we have only 1 element, then we need to return a part.
        // If we have no interp, then there is nothing to do.
        if (interp == NULL) {
            goto done;
        }

        result = Cookfs_PagesGetPartObj(vfs->pages, part);

        if (result == NULL) {
            result = Tcl_NewStringObj("error while getting a part", -1);
            goto error;
        }

        goto done;

    }

    // If we are here, then we want to store a part to a channel
    int chan_mode;
    Tcl_Channel chan = Tcl_GetChannel(interp, Tcl_GetString(objv[1]),
        &chan_mode);

    if (chan == NULL) {
        if (interp != NULL) {
            result = Tcl_ObjPrintf("bad channel name \"%s\"",
                Tcl_GetString(objv[1]));
        }
        goto error;
    }
    if ((chan_mode & TCL_WRITABLE) == 0) {
        if (interp != NULL) {
            result = Tcl_ObjPrintf("channel \"%s\" is not opened for writing",
                Tcl_GetString(objv[1]));
        }
        goto error;
    }

    Tcl_WideInt written = Cookfs_PagesPutPartToChannel(vfs->pages,
        part, chan);

    if (written < 0) {
        if (interp != NULL) {
            result = Tcl_NewStringObj("filed to write the part to the channel",
                -1);
        }
        goto error;
    }

    if (interp != NULL) {
        result = Tcl_NewWideIntObj(written);
    }

    goto done;

error:

    rc = TCL_ERROR;

done:

    if (result != NULL) {
        Tcl_SetObjResult(interp, result);
    }
    return rc;

}

static int Cookfs_AttrGet_Vfs(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    UNUSED(vfs);
    UNUSED(entry_type);
    UNUSED(entry);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = tsdPtr->attr_vfs_value;

    return TCL_OK;

}

static int Cookfs_AttrGet_Handle(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

#ifdef TCL_THREADS
    if (vfs->threadId != Tcl_GetCurrentThread()) {
        CookfsLog(printf("return empty value due to wrong threadId"));
        *result_ptr = Tcl_NewObj();
    } else {
#endif /* TCL_THREADS */
        *result_ptr = CookfsGetVfsObjectCmd(interp, vfs);
        CookfsLog(printf("return value for -handle"));
#ifdef TCL_THREADS
    }
#endif /* TCL_THREADS */

    return TCL_OK;

}

static int Cookfs_AttrGet_Fileset(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{
    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);
    *result_ptr = Cookfs_VfsFilesetGet(vfs);
    return TCL_OK;
}

static int Cookfs_AttrGet_Metadata(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{
    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);
    *result_ptr = Cookfs_FsindexGetMetadataAllKeys(vfs->index);
    return TCL_OK;
}

static int Cookfs_AttrGet_Pages(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    // Get info about all pages
    Cookfs_Pages *pages = vfs->pages;

    if (!Cookfs_PagesLockRead(pages, NULL)) {
        CookfsLog(printf("failed to lock pages"));
        return TCL_ERROR;
    };

    int length = Cookfs_PagesGetLength(pages);
    Tcl_Obj *result = Tcl_NewIntObj(length);

    Cookfs_PagesUnlock(pages);

    *result_ptr = result;

    return TCL_OK;

}

static int Cookfs_AttrSet_Pages(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{

    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    // There is nothing to do if we don't have interp
    if (interp == NULL) {
        return TCL_OK;
    }

    Cookfs_Pages *pages = vfs->pages;

    if (!Cookfs_PagesLockRead(pages, NULL)) {
        CookfsLog(printf("failed to lock pages"));
        return TCL_ERROR;
    };

    Tcl_Obj *result = NULL;
    int i, rc = TCL_OK;
    int length = Cookfs_PagesGetLength(pages);

    // Get info for a specific page
    if (Tcl_GetIntFromObj(NULL, value, &i) == TCL_OK) {

        if (i < 0 || i >= length) {
            result = Tcl_ObjPrintf("bad page index \"%d\" was specified,"
                " there are %d pages in total", i, length);
            rc = TCL_ERROR;
        } else {
            result = Cookfs_PagesGetInfo(pages, i);
        }

        goto done;

    }

    static const char *const info_types[] = {
        "pgindex", "fsindex", "length", "list",
        NULL
    };

    enum info_types {
        infPginfo, infFsindex, infLength, infList
    };

    int info_type;
    if (Tcl_GetIndexFromObj(interp, value, info_types,
        "pages information type", 0, &info_type) != TCL_OK)
    {
        goto error;
    }

    switch ((enum info_types)info_type) {
    case infLength:
        result = Tcl_NewIntObj(length);
        break;
    case infList:
        result = Tcl_NewListObj(0, NULL);
        for (i = 0; i < length; i++) {
            Tcl_ListObjAppendElement(NULL, result,
                Cookfs_PagesGetInfo(pages, i));
        }
        break;
    case infPginfo:
        result = Cookfs_PagesGetInfoSpecial(pages,
            COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_PGINDEX);
        break;
    case infFsindex:
        result = Cookfs_PagesGetInfoSpecial(pages,
            COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_FSINDEX);
        break;
    }

    goto done;

error:

    rc = TCL_ERROR;

done:

    Cookfs_PagesUnlock(pages);
    if (result != NULL) {
        Tcl_SetObjResult(interp, result);
    }
    return rc;

}

static int Cookfs_AttrSet_Fileset(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    Tcl_Obj *result = NULL;
    Tcl_Obj **result_ptr = (interp == NULL ? NULL : &result);
    int rc = Cookfs_VfsFilesetSelect(vfs, value, result_ptr, result_ptr);
    CookfsLog(printf("return: %s", (rc == TCL_OK ? "OK" : "ERROR")));
    // We Cookfs_VfsFilesetSelect() will set result via result_ptr
    // cppcheck-suppress knownConditionTrueFalse
    if (rc != TCL_OK && interp != NULL && result == NULL) {
        result = Tcl_NewStringObj("unknown error", -1);
    }
    if (result != NULL) {
        Tcl_SetObjResult(interp, result);
    }
    return rc;
}

static int Cookfs_AttrSet_Metadata(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj *value)
{
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_VFS);
    UNUSED(entry_type);
    UNUSED(entry);

    Tcl_Obj *result = NULL;
    int rc = TCL_OK;

    Tcl_Obj **objv;
    Tcl_Size objc;
    if (Tcl_ListObjGetElements(interp, value, &objc, &objv) != TCL_OK) {
        goto error;
    }

    // If empty value specified or more than 2 list elements, then consider
    // it as an error
    if (objc < 1 || objc > 2) {
        if (interp != NULL) {
            result = Tcl_NewStringObj("a list of 1 or 2 elements is expected"
                " as an argument to the -metadata attribute", -1);
        }
        goto error;
    }

    // We want to get a specific metadata key
    if (objc == 1) {

        // Do nothing if we have no interp
        if (interp == NULL) {
            goto done;
        }

        result = Cookfs_FsindexGetMetadata(vfs->index, Tcl_GetString(objv[0]));

        if (result == NULL) {
            result = Tcl_ObjPrintf("could not find metadata key \"%s\"",
                Tcl_GetString(objv[0]));
            goto error;
        }

        goto done;

    }

    // We want to set a specific metadata key. But first let's check
    // that we are not in readonly mode.
    if (Cookfs_VfsIsReadonly(vfs)) {
        if (interp != NULL) {
            result = Tcl_NewStringObj("failed to set the metadata key:"
                " VFS is in readonly mode", -1);
        }
        goto error;
    }

    Cookfs_FsindexSetMetadata(vfs->index, Tcl_GetString(objv[0]), objv[1]);

    if (interp != NULL) {
        result = objv[1];
    }

    goto done;

error:

    rc = TCL_ERROR;

done:

    if (result != NULL) {
        Tcl_SetObjResult(interp, result);
    }
    return rc;

}

// cppcheck-suppress constParameterCallback
static int Cookfs_AttrGet_Mount(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE ||
        entry_type == COOKFS_VFS_ATTRIBUTE_SET_DIRECTORY);
    UNUSED(entry_type);
    UNUSED(entry);

    *result_ptr = Tcl_NewStringObj(vfs->mountStr, vfs->mountLen);

    return TCL_OK;

}

static int Cookfs_AttrGet_Pending(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);
    UNUSED(entry_type);
    UNUSED(vfs);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = Cookfs_FsindexEntryIsPending(entry) ?
        tsdPtr->attr_value_true : tsdPtr->attr_value_false;

    return TCL_OK;

}

static int Cookfs_AttrGet_Uncompsize(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);
    UNUSED(entry_type);
    UNUSED(vfs);

    *result_ptr = Tcl_NewWideIntObj(Cookfs_FsindexEntryGetFilesize(entry));

    return TCL_OK;

}

static int Cookfs_AttrGet_Compsize(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);
    UNUSED(entry_type);

    if (Cookfs_FsindexEntryIsPending(entry)) {
        return Cookfs_AttrGet_Uncompsize(interp, vfs, entry_type, entry,
            result_ptr);
    }

    Tcl_WideInt compsize = 0;

    int block_count = Cookfs_FsindexEntryGetBlockCount(entry);
    for (int block = 0; block < block_count; block++) {

        int page_num, entry_page_size, page_size_comp, page_size_uncomp;

        Cookfs_FsindexEntryGetBlock(entry, block, &page_num, NULL,
            &entry_page_size);

        page_size_uncomp = Cookfs_PagesGetPageSize(vfs->pages, page_num);
        page_size_comp = Cookfs_PagesGetPageSizeCompressed(vfs->pages,
            page_num);

        if (entry_page_size == page_size_uncomp) {
            compsize += page_size_comp;
        } else {
            compsize += (entry_page_size * page_size_comp) / page_size_uncomp;
        }

    }

    // Check that compressed size is not equal to zero to avoid division
    // by zero
    if (compsize == 0) {
        compsize = 1;
    }

    *result_ptr = Tcl_NewWideIntObj(compsize);

    return TCL_OK;

}

static int Cookfs_AttrGet_Blocks(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttributeSetType entry_type, Cookfs_FsindexEntry *entry,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    assert(entry_type == COOKFS_VFS_ATTRIBUTE_SET_FILE);
    UNUSED(entry_type);
    UNUSED(vfs);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    Tcl_Obj *result = Tcl_NewListObj(0, NULL);

    int block_count = Cookfs_FsindexEntryGetBlockCount(entry);
    int page_num, page_offset, page_size;
    for (int block = 0; block < block_count; block++) {

        Cookfs_FsindexEntryGetBlock(entry, block, &page_num, &page_offset,
            &page_size);

        Tcl_Obj *elem = Tcl_NewDictObj();

        Tcl_DictObjPut(NULL, elem, tsdPtr->attr_block_page,
            Tcl_NewIntObj(page_num));
        Tcl_DictObjPut(NULL, elem, tsdPtr->attr_block_offset,
            Tcl_NewIntObj(page_offset));
        Tcl_DictObjPut(NULL, elem, tsdPtr->attr_block_size,
            Tcl_NewIntObj(page_size));

        Tcl_ListObjAppendElement(NULL, result, elem);

    }

    *result_ptr = result;
    return TCL_OK;

}

static const struct {
    const char *name;
    Cookit_AttributeGetProc *get_proc;
    Cookit_AttributeSetProc *set_proc;
} cookfs_attribute_map[COOKFS_VFS_ATTRIBUTE_COUNT] = {
    [COOKFS_VFS_ATTRIBUTE_VFS] = {
        "-vfs",       Cookfs_AttrGet_Vfs,       NULL
    },
    [COOKFS_VFS_ATTRIBUTE_HANDLE] = {
        "-handle",    Cookfs_AttrGet_Handle,    NULL
    },
    [COOKFS_VFS_ATTRIBUTE_PAGES] = {
        "-pages",     Cookfs_AttrGet_Pages,     Cookfs_AttrSet_Pages
    },
    [COOKFS_VFS_ATTRIBUTE_METADATA] = {
        "-metadata",  Cookfs_AttrGet_Metadata,  Cookfs_AttrSet_Metadata
    },
    [COOKFS_VFS_ATTRIBUTE_FILESET] = {
        "-fileset",   Cookfs_AttrGet_Fileset,   Cookfs_AttrSet_Fileset
    },
    [COOKFS_VFS_ATTRIBUTE_ARCHIVE] = {
        "-archive",   Cookfs_AttrGet_Archive,   NULL
    },
    [COOKFS_VFS_ATTRIBUTE_WRITETOMEMORY] = {
        "-writetomemory", Cookfs_AttrGet_Writetomemory,
                          Cookfs_AttrSet_Writetomemory
    },
    [COOKFS_VFS_ATTRIBUTE_READONLY] = {
        "-readonly",  Cookfs_AttrGet_Readonly,  NULL
    },
    [COOKFS_VFS_ATTRIBUTE_SMALLFILEBUFFERSIZE] = {
        "-smallfilebuffersize", Cookfs_AttrGet_Smallfilebuffersize,
                                NULL
    },
    [COOKFS_VFS_ATTRIBUTE_CACHESIZE] = {
        "-cachesize", Cookfs_AttrGet_Cachesize, Cookfs_AttrSet_Cachesize
    },
    [COOKFS_VFS_ATTRIBUTE_VOLUME] = {
        "-volume",    Cookfs_AttrGet_Volume,    NULL
    },
    [COOKFS_VFS_ATTRIBUTE_COMPRESSION] = {
        "-compression", Cookfs_AttrGet_Compression,
                        Cookfs_AttrSet_Compression
    },
#ifdef TCL_THREADS
    [COOKFS_VFS_ATTRIBUTE_SHARED] = {
        "-shared",    Cookfs_AttrGet_Shared,    NULL
    },
#endif /* TCL_THREADS */
#ifdef COOKFS_USECCRYPTO
    [COOKFS_VFS_ATTRIBUTE_PASSWORD] = {
        "-password",  Cookfs_AttrGet_Password,  Cookfs_AttrSet_Password
    },
    [COOKFS_VFS_ATTRIBUTE_ENCRYPTKEY] = {
        "-encryptkey", Cookfs_AttrGet_Encryptkey,
                       NULL
    },
    [COOKFS_VFS_ATTRIBUTE_ENCRYPTLEVEL] = {
        "-encryptlevel", Cookfs_AttrGet_Encryptlevel,
                         NULL
    },
#endif /* COOKFS_USECCRYPTO */
    [COOKFS_VFS_ATTRIBUTE_MOUNT] = {
        "-mount",     Cookfs_AttrGet_Mount,     NULL
    },
    [COOKFS_VFS_ATTRIBUTE_PENDING] = {
        "-pending",   Cookfs_AttrGet_Pending,   NULL
    },
    [COOKFS_VFS_ATTRIBUTE_UNCOMPSIZE] = {
        "-uncompsize", Cookfs_AttrGet_Uncompsize,
                       NULL
    },
    [COOKFS_VFS_ATTRIBUTE_COMPSIZE] = {
        "-compsize",  Cookfs_AttrGet_Compsize, NULL
    },
    [COOKFS_VFS_ATTRIBUTE_BLOCKS] = {
        "-blocks",    Cookfs_AttrGet_Blocks,   NULL
    },
    [COOKFS_VFS_ATTRIBUTE_RELATIVE] = {
        "-relative",  NULL, NULL
    },
    [COOKFS_VFS_ATTRIBUTE_PARTS] = {
        "-parts",     Cookfs_AttrGet_Parts,     Cookfs_AttrSet_Parts
    }
};

int Cookfs_VfsAttributeGet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Cookfs_VfsAttributeSetType entry_type,
    Cookfs_FsindexEntry *entry, Tcl_Obj **result_ptr)
{
    Cookit_AttributeGetProc *proc = cookfs_attribute_map[attr].get_proc;

    return proc(interp, vfs, entry_type, entry, result_ptr);
}

int Cookfs_VfsAttributeSet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Cookfs_VfsAttributeSetType entry_type,
    Cookfs_FsindexEntry *entry, Tcl_Obj *value)
{

    Cookit_AttributeSetProc *proc = cookfs_attribute_map[attr].set_proc;

    if (proc == NULL) {
        Tcl_SetErrno(EROFS);
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("attribute \"%s\" is"
                " read-only", cookfs_attribute_map[attr].name));
        }
        return TCL_ERROR;
    }

    return proc(interp, vfs, entry_type, entry, value);

}

static ThreadSpecificData *CookfsGetThreadSpecificData(void) {

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    if (!tsdPtr->initialized) {
        Tcl_CreateThreadExitHandler(CookfsVfsAttributesThreadExit, NULL);
        tsdPtr->initialized = 1;
    }

    if (tsdPtr->attr_vfs_value == NULL) {

        Tcl_Obj *objv[COOKFS_VFS_ATTRIBUTE_COUNT];
        Tcl_Obj *attr[COOKFS_VFS_ATTRIBUTE_COUNT];
        unsigned char i;

        for (i = 0; i < COOKFS_VFS_ATTRIBUTE_COUNT; i++) {
            attr[i] = Tcl_NewStringObj(cookfs_attribute_map[i].name, -1);
        }

        for (i = 0; i < COOKFS_VFS_ATTRIBUTE_SET_COUNT; i++) {
            int j;
            for (j = 0; cookfs_attribute_set2attribute[i][j] != -1; j++) {
                unsigned char attr_id =
                    (Cookfs_VfsAttribute)cookfs_attribute_set2attribute[i][j];
                objv[j] = attr[attr_id];
            }
            tsdPtr->attrs_list[i] = Tcl_NewListObj(j, objv);
            Tcl_IncrRefCount(tsdPtr->attrs_list[i]);
        }

        tsdPtr->attr_vfs_value = Tcl_NewBooleanObj(1);
        Tcl_IncrRefCount(tsdPtr->attr_vfs_value);

        tsdPtr->attr_value_true = tsdPtr->attr_vfs_value;

        tsdPtr->attr_value_false = Tcl_NewBooleanObj(0);
        Tcl_IncrRefCount(tsdPtr->attr_value_false);

        tsdPtr->attr_value_empty = Tcl_NewObj();
        Tcl_IncrRefCount(tsdPtr->attr_value_empty);

        tsdPtr->attr_value_compression_none =
            Cookfs_CompressionToObj(COOKFS_COMPRESSION_NONE, -1);
        Tcl_IncrRefCount(tsdPtr->attr_value_compression_none);

        tsdPtr->attr_part_head = Tcl_NewStringObj("headsize", -1);
        Tcl_IncrRefCount(tsdPtr->attr_part_head);

        tsdPtr->attr_part_data = Tcl_NewStringObj("datasize", -1);
        Tcl_IncrRefCount(tsdPtr->attr_part_data);

        tsdPtr->attr_part_tail = Tcl_NewStringObj("tailsize", -1);
        Tcl_IncrRefCount(tsdPtr->attr_part_tail);

        tsdPtr->attr_block_page = Tcl_NewStringObj("page", -1);
        Tcl_IncrRefCount(tsdPtr->attr_block_page);

        tsdPtr->attr_block_offset = Tcl_NewStringObj("offset", -1);
        Tcl_IncrRefCount(tsdPtr->attr_block_offset);

        tsdPtr->attr_block_size = Tcl_NewStringObj("size", -1);
        Tcl_IncrRefCount(tsdPtr->attr_block_size);

    }

    return tsdPtr;

}

void CookfsVfsAttributesThreadExit(ClientData clientData) {

    UNUSED(clientData);

    CookfsLog(printf("ENTER"));

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    for (unsigned char i = 0; i < COOKFS_VFS_ATTRIBUTE_SET_COUNT; i++) {
        if (tsdPtr->attrs_list[i] != NULL) {
            Tcl_DecrRefCount(tsdPtr->attrs_list[i]);
            tsdPtr->attrs_list[i] = NULL;
        }
    }

    // tsdPtr->attr_value_true is a copy of tsdPtr->attr_vfs_value without
    // refcount increase. There is no need to release it separately.
    if (tsdPtr->attr_vfs_value != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_vfs_value);
        tsdPtr->attr_vfs_value = NULL;
        tsdPtr->attr_value_true = NULL;
    }
    if (tsdPtr->attr_value_empty != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_value_empty);
        tsdPtr->attr_value_empty = NULL;
    }
    if (tsdPtr->attr_value_false != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_value_false);
        tsdPtr->attr_value_false = NULL;
    }
    if (tsdPtr->attr_value_compression_none != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_value_compression_none);
        tsdPtr->attr_value_compression_none = NULL;
    }
    if (tsdPtr->attr_part_head != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_part_head);
        tsdPtr->attr_part_head = NULL;
    }
    if (tsdPtr->attr_part_data != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_part_data);
        tsdPtr->attr_part_data = NULL;
    }
    if (tsdPtr->attr_part_tail != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_part_tail);
        tsdPtr->attr_part_tail = NULL;
    }
    if (tsdPtr->attr_block_page != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_block_page);
        tsdPtr->attr_block_page = NULL;
    }
    if (tsdPtr->attr_block_offset != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_block_offset);
        tsdPtr->attr_block_offset = NULL;
    }
    if (tsdPtr->attr_block_size != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_block_size);
        tsdPtr->attr_block_size = NULL;
    }

    CookfsLog(printf("ok"));

}
