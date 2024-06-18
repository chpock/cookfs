/*
 * pageObj.c
 *
 * Provides functions creating and managing a page object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pageObj.h"

void Cookfs_PageObjIncrRefCount(Cookfs_PageObj p) {
    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(p -
        sizeof(Cookfs_PageObjStruct));
    // CookfsLog(printf("Cookfs_PageObjIncrRefCount: %p", (void *)p));
#ifdef TCL_THREADS
    Tcl_MutexLock(&ps->mx);
#endif /* TCL_THREADS */
    ps->refCount++;
    // CookfsLog(printf("Cookfs_PageObjIncrRefCount: %p - count:%d", (void *)p,
    //     ps->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&ps->mx);
#endif /* TCL_THREADS */
}

void Cookfs_PageObjDecrRefCount(Cookfs_PageObj p) {
    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(p -
        sizeof(Cookfs_PageObjStruct));
    // CookfsLog(printf("Cookfs_PageObjDecrRefCount: release %p", (void *)p));
#ifdef TCL_THREADS
    Tcl_MutexLock(&ps->mx);
#endif /* TCL_THREADS */
    ps->refCount--;
    // CookfsLog(printf("Cookfs_PageObjDecrRefCount: %p - count:%d", (void *)p,
    //     ps->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&ps->mx);
#endif /* TCL_THREADS */
    if (!ps->refCount) {
#ifdef TCL_THREADS
        Tcl_MutexFinalize(&ps->mx);
#endif /* TCL_THREADS */
        // CookfsLog(printf("Cookfs_PageObjDecrRefCount: release %p", (void *)p));
        ckfree(ps);
    }
}

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
#ifdef TCL_THREADS
        s->mx = NULL;
#endif /* TCL_THREADS */
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

