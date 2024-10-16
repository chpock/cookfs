/*
 * vfsVfs.c
 *
 * Provides methods for manipulating VFS attributes in cookfs
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "vfsAttributes.h"

typedef struct ThreadSpecificData {
    int initialized;
    Tcl_Obj *attrs_list[COOKFS_VFS_ATTRIBUTE_SET_COUNT];
    Tcl_Obj *attr_vfs_value;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

typedef int (Cookit_AttributeGetProc)(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj **result_ptr);
typedef int (Cookit_AttributeSetProc)(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj *value);

static const unsigned char
    cookfs_attribute_set_count[COOKFS_VFS_ATTRIBUTE_SET_COUNT] =
{
    5, 1, 1
};

static const Cookfs_VfsAttribute attr_arr_vfs[] = {
    COOKFS_VFS_ATTRIBUTE_VFS,
    COOKFS_VFS_ATTRIBUTE_HANDLE,
    COOKFS_VFS_ATTRIBUTE_PAGES,
    COOKFS_VFS_ATTRIBUTE_METADATA,
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

static ThreadSpecificData *CookfsGetThreadSpecificData(void);

Cookfs_VfsAttribute Cookfs_VfsAttributeGetFromSet(
    Cookfs_VfsAttributeSetType attr_set, int index)
{
    return cookfs_attribute_set2attribute[attr_set][index];
}

Tcl_Obj *Cookfs_VfsAttributeList(Cookfs_VfsAttributeSetType attr_set) {

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    return tsdPtr->attrs_list[attr_set];

}

static int Cookfs_AttrGet_Vfs(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);
    UNUSED(vfs);

    ThreadSpecificData *tsdPtr = CookfsGetThreadSpecificData();

    *result_ptr = tsdPtr->attr_vfs_value;

    return TCL_OK;

}

static int Cookfs_AttrGet_Handle(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);

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
    Tcl_Obj **result_ptr)
{
    UNUSED(interp);
    *result_ptr = Cookfs_VfsFilesetGet(vfs);
    return TCL_OK;
}

static int Cookfs_AttrGet_Metadata(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj **result_ptr)
{
    UNUSED(interp);
    *result_ptr = Cookfs_FsindexGetMetadataAllKeys(vfs->index);
    return TCL_OK;
}

static int Cookfs_AttrGet_Pages(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Tcl_Obj **result_ptr)
{

    UNUSED(interp);

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
    Tcl_Obj *value)
{

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
    Tcl_Obj *value)
{
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
    Tcl_Obj *value)
{

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

static const struct {
    const char *name;
    Cookit_AttributeGetProc *get_proc;
    Cookit_AttributeSetProc *set_proc;
} cookfs_attribute_map[COOKFS_VFS_ATTRIBUTE_COUNT] = {

    [COOKFS_VFS_ATTRIBUTE_VFS] = {
        "-vfs",      Cookfs_AttrGet_Vfs,      NULL
    },
    [COOKFS_VFS_ATTRIBUTE_HANDLE] = {
        "-handle",   Cookfs_AttrGet_Handle,   NULL
    },
    [COOKFS_VFS_ATTRIBUTE_PAGES] = {
        "-pages",    Cookfs_AttrGet_Pages,    Cookfs_AttrSet_Pages
    },
    [COOKFS_VFS_ATTRIBUTE_METADATA] = {
        "-metadata", Cookfs_AttrGet_Metadata, Cookfs_AttrSet_Metadata
    },
    [COOKFS_VFS_ATTRIBUTE_FILESET] = {
        "-fileset",  Cookfs_AttrGet_Fileset,  Cookfs_AttrSet_Fileset
    }

};

int Cookfs_VfsAttributeGet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Tcl_Obj **result_ptr)
{
    return cookfs_attribute_map[attr].get_proc(interp, vfs, result_ptr);
}

int Cookfs_VfsAttributeSet(Tcl_Interp *interp, Cookfs_Vfs *vfs,
    Cookfs_VfsAttribute attr, Tcl_Obj *value)
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

    return proc(interp, vfs, value);

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

    if (tsdPtr->attr_vfs_value != NULL) {
        Tcl_DecrRefCount(tsdPtr->attr_vfs_value);
        tsdPtr->attr_vfs_value = NULL;
    }

    CookfsLog(printf("ok"));
}
