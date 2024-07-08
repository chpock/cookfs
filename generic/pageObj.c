/*
 * pageObj.c
 *
 * Provides functions creating and managing a page object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pageObj.h"

void Cookfs_PageObjIncrRefCount(Cookfs_PageObj pg) {
    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(pg -
        sizeof(Cookfs_PageObjStruct));
    // CookfsLog(printf("Cookfs_PageObjIncrRefCount: %p", (void *)pg));
#ifdef TCL_THREADS
    Tcl_MutexLock(&ps->mx);
#endif /* TCL_THREADS */
    ps->refCount++;
    // CookfsLog(printf("Cookfs_PageObjIncrRefCount: %p - count:%d",
    //    (void *)pg, ps->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&ps->mx);
#endif /* TCL_THREADS */
}

void Cookfs_PageObjDecrRefCount(Cookfs_PageObj pg) {
    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(pg -
        sizeof(Cookfs_PageObjStruct));
    // CookfsLog(printf("Cookfs_PageObjDecrRefCount: release %p", (void *)pg));
#ifdef TCL_THREADS
    Tcl_MutexLock(&ps->mx);
#endif /* TCL_THREADS */
    ps->refCount--;
    // CookfsLog(printf("Cookfs_PageObjDecrRefCount: %p - count:%d",
    //     (void *)pg, ps->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&ps->mx);
#endif /* TCL_THREADS */
    if (!ps->refCount) {
#ifdef TCL_THREADS
        Tcl_MutexFinalize(&ps->mx);
#endif /* TCL_THREADS */
        // CookfsLog(printf("Cookfs_PageObjDecrRefCount: release %p", (void *)pg));
        ckfree(ps);
    }
}

static Tcl_Size Cookfs_PageObjCalculateSize(Tcl_Size size) {
    // Align physical page size to 16 bytes. This will be usefull for
    // AES encryption. If the size is already 16 bytes aligned, add another
    // 16 bytes.
    Tcl_Size bufferSize = size + COOKFS_PAGEOBJ_BLOCK_SIZE -
        (size % COOKFS_PAGEOBJ_BLOCK_SIZE);
    CookfsLog2(printf("want bytes %" TCL_SIZE_MODIFIER "d; alloc %"
        TCL_SIZE_MODIFIER "d bytes + %u bytes", size, bufferSize,
        (unsigned int)sizeof(Cookfs_PageObjStruct)));
    return bufferSize;
}

Cookfs_PageObj Cookfs_PageObjAlloc(Tcl_Size size) {
    CookfsLog2(printf("enter..."));
    Tcl_Size bufferSize = Cookfs_PageObjCalculateSize(size);
    Cookfs_PageObj p = ckalloc(bufferSize + sizeof(Cookfs_PageObjStruct));
    if (p != NULL) {
        Cookfs_PageObjStruct *s = (Cookfs_PageObjStruct *)p;
        s->bufferSize = bufferSize;
        s->effectiveSize = size;
        s->refCount = 0;
#ifdef TCL_THREADS
        s->mx = NULL;
#endif /* TCL_THREADS */
#ifdef COOKFS_USECCRYPT
        memset(s->IV, 0, COOKFS_PAGEOBJ_BLOCK_SIZE);
#endif /* COOKFS_USECCRYPT */
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

#ifdef COOKFS_USECCRYPT

Cookfs_PageObj Cookfs_PageObjNewFromByteArrayIV(Tcl_Obj *obj) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(obj, &size);
    if (size < COOKFS_PAGEOBJ_BLOCK_SIZE) {
        return NULL;
    }
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(size - COOKFS_PAGEOBJ_BLOCK_SIZE);
    if (rc != NULL) {
        memcpy(rc, bytes + COOKFS_PAGEOBJ_BLOCK_SIZE, size -
            COOKFS_PAGEOBJ_BLOCK_SIZE);
        memcpy(Cookfs_PageObjGetIV(rc), bytes, COOKFS_PAGEOBJ_BLOCK_SIZE);
    }
    return rc;
}

// Below functions modify pageobj. They are not thread-safe and
// must not be used for pageobjs that may be shared across threads.

static void Cookfs_PageObjEnsureNotShared(Cookfs_PageObj pg) {
    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(pg -
        sizeof(Cookfs_PageObjStruct));
    if (ps->refCount) {
        Tcl_Panic("Critical error: attempt to use shared PageObj where it"
            " is not allowed.");
    }
    return;
}

int Cookfs_PageObjRealloc(Cookfs_PageObj *pgPtr, Tcl_Size size) {

    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)((*pgPtr) -
        sizeof(Cookfs_PageObjStruct));

    CookfsLog2(printf("realloc to %" TCL_SIZE_MODIFIER "d bytes; current"
        " effectiveSize=%" TCL_SIZE_MODIFIER "d, bufferSize=%"
        TCL_SIZE_MODIFIER "d", size, ps->effectiveSize, ps->bufferSize));

    Cookfs_PageObjEnsureNotShared(*pgPtr);

    if (size < ps->bufferSize) {
        ps->effectiveSize = size;
        CookfsLog2(printf("bufferSize is enough, set effectiveSize to %"
            TCL_SIZE_MODIFIER "d", size));
        return TCL_OK;
    }

    CookfsLog2(printf("bufferSize is not enough, try to realloc..."));

    Tcl_Size bufferSize = Cookfs_PageObjCalculateSize(size);

    Cookfs_PageObj result = attemptckrealloc(ps, bufferSize +
        sizeof(Cookfs_PageObjStruct));

    if (result == NULL) {
        CookfsLog2(printf("ERROR: failed to realloc"));
        return TCL_ERROR;
    }

    ps = (Cookfs_PageObjStruct *)result;
    ps->bufferSize = bufferSize;
    ps->effectiveSize = size;

    result += sizeof(Cookfs_PageObjStruct);

    *pgPtr = result;

    CookfsLog2(printf("return %p", (void *)result));

    return TCL_OK;

}

void Cookfs_PageObjAddPadding(Cookfs_PageObj pg) {

    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(pg -
        sizeof(Cookfs_PageObjStruct));

    CookfsLog2(printf("enter..."));

    unsigned char pad_byte = COOKFS_PAGEOBJ_BLOCK_SIZE - (ps->effectiveSize %
        COOKFS_PAGEOBJ_BLOCK_SIZE);

    CookfsLog2(printf("add pad_byte [0x%x] and set effectiveSize to %"
        TCL_SIZE_MODIFIER "d; current effectiveSize=%" TCL_SIZE_MODIFIER
        "d, bufferSize=%" TCL_SIZE_MODIFIER "d", pad_byte,
        ps->effectiveSize + pad_byte, ps->effectiveSize, ps->bufferSize));

    // Cookfs_PageObjAlloc() and Cookfs_PageObjRealloc() always allocate
    // enough buffer to add paddings, so here we don't check if the buffer
    // size is sufficient.

    Cookfs_PageObjEnsureNotShared(pg);

    unsigned char *end_pointer = &pg[ps->effectiveSize];
    for (unsigned char counter = pad_byte; counter > 0; counter--) {
        *(end_pointer++) = pad_byte;
    }

    ps->effectiveSize += pad_byte;

    return;

}

int Cookfs_PageObjRemovePadding(Cookfs_PageObj pg) {

    Cookfs_PageObjStruct *ps = (Cookfs_PageObjStruct *)(pg -
        sizeof(Cookfs_PageObjStruct));

    CookfsLog2(printf("enter..."));

    Cookfs_PageObjEnsureNotShared(pg);

    unsigned char pad_byte = pg[ps->effectiveSize - 1];

    CookfsLog2(printf("pad_byte is [0x%x]; current effectiveSize=%"
        TCL_SIZE_MODIFIER "d, bufferSize=%" TCL_SIZE_MODIFIER "d", pad_byte,
        ps->effectiveSize, ps->bufferSize));

    // Make sure the current buffer length is greater than pad_byte so we don't
    // go outside the buffer during padding check. Actually, it means that
    // the padding is wrong.
    if (pad_byte > ps->effectiveSize) {
        CookfsLog2(printf("ERROR: ps->effectiveSize is too small"));
        return TCL_ERROR;
    }

    // Make sure pad_byte is not zero and is less than or equal to the block size.
    // We cannot have a valid pad_byte that violates this condition.
    if (pad_byte == 0 || pad_byte > COOKFS_PAGEOBJ_BLOCK_SIZE) {
        CookfsLog2(printf("ERROR: pad_byte is incorrect"));
        return TCL_ERROR;
    }

    // Check padding if it is longer than 1 byte. If it is 1 byte, the obtained
    // pad_byte is the complete padding we have.
    if (pad_byte > 1) {
        unsigned char *end_pointer = &pg[ps->effectiveSize - 1];
        for (unsigned char counter = pad_byte - 1; counter > 0; counter--) {
            if (*(--end_pointer) != pad_byte) {
                CookfsLog2(printf("ERROR: wrong byte at %u position; actual"
                    " [0x%x] expected [0x%x]", counter, *end_pointer, pad_byte));
                return TCL_ERROR;
            }
        }
    }

    ps->effectiveSize -= pad_byte;

    CookfsLog2(printf("set effectiveSize to %" TCL_SIZE_MODIFIER "d",
        ps->effectiveSize));

    return TCL_OK;

}

#endif /* COOKFS_USECCRYPT */
