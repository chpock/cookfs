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
    // CookfsLog2(printf("%p (buffer at %p)", (void *)pg, (void *)pg->buf));
#ifdef TCL_THREADS
    Tcl_MutexLock(&pg->mx);
#endif /* TCL_THREADS */
    pg->refCount++;
    // CookfsLog2(printf("%p - count:%d", (void *)pg, pg->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&pg->mx);
#endif /* TCL_THREADS */
}

void Cookfs_PageObjDecrRefCount(Cookfs_PageObj pg) {
    // CookfsLog2(printf("%p (buffer at %p)", (void *)pg, (void *)pg->buf));
#ifdef TCL_THREADS
    Tcl_MutexLock(&pg->mx);
#endif /* TCL_THREADS */
    // There should not be Cookfs_PageObjDecrRefCount() without
    // a corresponding Cookfs_PageObjIncrRefCount() that was called before it.
    // Throw an error if refcount is less than or equal to zero.
    assert(pg->refCount > 0);
    pg->refCount--;
    // CookfsLog2(printf("%p - count:%d", (void *)pg, pg->refCount));
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&pg->mx);
#endif /* TCL_THREADS */
    if (!pg->refCount) {
#ifdef TCL_THREADS
        Tcl_MutexFinalize(&pg->mx);
#endif /* TCL_THREADS */
        // CookfsLog2(printf("release %p", (void *)pg));
        ckfree(pg);
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
    // CookfsLog2(printf("enter..."));
    Tcl_Size bufferSize = Cookfs_PageObjCalculateSize(size);
    Cookfs_PageObj pg = ckalloc(bufferSize + sizeof(Cookfs_PageObjStruct));
    if (pg != NULL) {
        pg->bufferSize = bufferSize;
        pg->effectiveSize = size;
        pg->refCount = 0;
        pg->buf = ((unsigned char *)pg) + sizeof(Cookfs_PageObjStruct);
#ifdef TCL_THREADS
        pg->mx = NULL;
#endif /* TCL_THREADS */
#ifdef COOKFS_USECCRYPTO
        memset(pg->IV, 0, COOKFS_PAGEOBJ_BLOCK_SIZE);
#endif /* COOKFS_USECCRYPTO */
    }
    CookfsLog2(printf("return: %p", (void *)pg));
    return pg;
}

Cookfs_PageObj Cookfs_PageObjNewWithoutAlloc(const unsigned char *bytes,
    Tcl_Size size)
{
    Cookfs_PageObj pg = ckalloc(sizeof(Cookfs_PageObjStruct));
    if (pg != NULL) {
        pg->bufferSize = size;
        pg->effectiveSize = size;
        pg->refCount = 0;
        pg->buf = (unsigned char *)bytes;
#ifdef TCL_THREADS
        pg->mx = NULL;
#endif /* TCL_THREADS */
#ifdef COOKFS_USECCRYPTO
        memset(pg->IV, 0, COOKFS_PAGEOBJ_BLOCK_SIZE);
#endif /* COOKFS_USECCRYPTO */
    }
    CookfsLog2(printf("return: %p", (void *)pg));
    return pg;
}

Cookfs_PageObj Cookfs_PageObjNewFromString(const unsigned char *bytes,
    Tcl_Size size)
{
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(size);
    if (rc != NULL) {
        memcpy(rc->buf, bytes, size);
    }
    return rc;
}

Cookfs_PageObj Cookfs_PageObjNewFromByteArray(Tcl_Obj *obj) {
    Tcl_Size size;
    const unsigned char *bytes = Tcl_GetByteArrayFromObj(obj, &size);
    return Cookfs_PageObjNewFromString(bytes, size);
}

#ifdef COOKFS_USECCRYPTO

Cookfs_PageObj Cookfs_PageObjNewFromByteArrayIV(Tcl_Obj *obj) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(obj, &size);
    if (size < COOKFS_PAGEOBJ_BLOCK_SIZE) {
        return NULL;
    }
    Cookfs_PageObj rc = Cookfs_PageObjAlloc(size - COOKFS_PAGEOBJ_BLOCK_SIZE);
    if (rc != NULL) {
        memcpy(rc->buf, bytes + COOKFS_PAGEOBJ_BLOCK_SIZE, size -
            COOKFS_PAGEOBJ_BLOCK_SIZE);
        memcpy(Cookfs_PageObjGetIV(rc), bytes, COOKFS_PAGEOBJ_BLOCK_SIZE);
    }
    return rc;
}

// Below functions modify pageobj. They are not thread-safe and
// must not be used for pageobjs that may be shared across threads.

static void Cookfs_PageObjEnsureNotShared(Cookfs_PageObj pg) {
    if (pg->refCount) {
        Tcl_Panic("Critical error: attempt to use shared PageObj where it"
            " is not allowed.");
    }
    return;
}

int Cookfs_PageObjRealloc(Cookfs_PageObj *pgPtr, Tcl_Size size) {

    Cookfs_PageObj pg = *pgPtr;

    CookfsLog2(printf("realloc to %" TCL_SIZE_MODIFIER "d bytes; current"
        " effectiveSize=%" TCL_SIZE_MODIFIER "d, bufferSize=%"
        TCL_SIZE_MODIFIER "d", size, pg->effectiveSize, pg->bufferSize));

    Cookfs_PageObjEnsureNotShared(pg);

    if (size < pg->bufferSize) {
        pg->effectiveSize = size;
        CookfsLog2(printf("bufferSize is enough, set effectiveSize to %"
            TCL_SIZE_MODIFIER "d", size));
        return TCL_OK;
    }

    CookfsLog2(printf("bufferSize is not enough, try to realloc..."));

    Tcl_Size bufferSize = Cookfs_PageObjCalculateSize(size);

    pg = attemptckrealloc(pg, bufferSize + sizeof(Cookfs_PageObjStruct));

    if (pg == NULL) {
        CookfsLog2(printf("ERROR: failed to realloc"));
        return TCL_ERROR;
    }

    pg->bufferSize = bufferSize;
    pg->effectiveSize = size;

    *pgPtr = pg;

    CookfsLog2(printf("return %p", (void *)pg));

    return TCL_OK;

}

void Cookfs_PageObjAddPadding(Cookfs_PageObj pg) {

    CookfsLog2(printf("enter..."));

    unsigned char pad_byte = COOKFS_PAGEOBJ_BLOCK_SIZE - (pg->effectiveSize %
        COOKFS_PAGEOBJ_BLOCK_SIZE);

    CookfsLog2(printf("add pad_byte [0x%x] and set effectiveSize to %"
        TCL_SIZE_MODIFIER "d; current effectiveSize=%" TCL_SIZE_MODIFIER
        "d, bufferSize=%" TCL_SIZE_MODIFIER "d", pad_byte,
        pg->effectiveSize + pad_byte, pg->effectiveSize, pg->bufferSize));

    // Cookfs_PageObjAlloc() and Cookfs_PageObjRealloc() always allocate
    // enough buffer to add paddings, so here we don't check if the buffer
    // size is sufficient.

    Cookfs_PageObjEnsureNotShared(pg);

    unsigned char *end_pointer = &pg->buf[pg->effectiveSize];
    for (unsigned char counter = pad_byte; counter > 0; counter--) {
        *(end_pointer++) = pad_byte;
    }

    pg->effectiveSize += pad_byte;

    return;

}

int Cookfs_PageObjRemovePadding(Cookfs_PageObj pg) {

    CookfsLog2(printf("enter..."));

    Cookfs_PageObjEnsureNotShared(pg);

    unsigned char pad_byte = pg->buf[pg->effectiveSize - 1];

    CookfsLog2(printf("pad_byte is [0x%x]; current effectiveSize=%"
        TCL_SIZE_MODIFIER "d, bufferSize=%" TCL_SIZE_MODIFIER "d", pad_byte,
        pg->effectiveSize, pg->bufferSize));

    // Make sure the current buffer length is greater than pad_byte so we don't
    // go outside the buffer during padding check. Actually, it means that
    // the padding is wrong.
    if (pad_byte > pg->effectiveSize) {
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
        unsigned char *end_pointer = &pg->buf[pg->effectiveSize - 1];
        for (unsigned char counter = pad_byte - 1; counter > 0; counter--) {
            if (*(--end_pointer) != pad_byte) {
                CookfsLog2(printf("ERROR: wrong byte at %u position; actual"
                    " [0x%x] expected [0x%x]", counter, *end_pointer, pad_byte));
                return TCL_ERROR;
            }
        }
    }

    pg->effectiveSize -= pad_byte;

    CookfsLog2(printf("set effectiveSize to %" TCL_SIZE_MODIFIER "d",
        pg->effectiveSize));

    return TCL_OK;

}

#endif /* COOKFS_USECCRYPTO */
