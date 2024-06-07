/*
 * pageObj.c
 *
 * Provides functions creating and managing a page object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

Cookfs_PageObj Cookfs_PageObjAlloc(Tcl_Size size) {
    // Align physical page size to 16 bytes. This will be usefull for
    // AES encryption.
    Tcl_Size bufferSize = size + (16 - (size % 16));
    CookfsLog(printf("Cookfs_PageObjAlloc: want bytes %" TCL_SIZE_MODIFIER "d"
        " alloc %" TCL_SIZE_MODIFIER "d bytes", size, bufferSize));
    Cookfs_PageObj p = ckalloc(bufferSize + sizeof(Cookfs_PageObjStruct));
    if (p != NULL) {
        Cookfs_PageObjStruct *s = (Cookfs_PageObjStruct *)p;
        s->bufferSize = bufferSize;
        s->effectiveSize = size;
        s->refCount = 0;
        p += sizeof(Cookfs_PageObjStruct);
    }
    CookfsLog(printf("Cookfs_PageObjAlloc: return %p", (void *)p));
    return p;
}

Cookfs_PageObj Cookfs_PageObjNewFromByteArray(Tcl_Obj *obj) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(obj, &size);
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(size);
    if (rc != NULL) {
        memcpy(rc, bytes, size);
    }
    return rc;
}

