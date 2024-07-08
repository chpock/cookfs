/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGEOBJ_H
#define COOKFS_PAGEOBJ_H 1

// Define the number of bytes by which the allocated memory for pageObj
// will be aligned. It is currently equal to an AES block (16 bytes).
// This alligment allows to avoid memory reallocation during pageObj
// encryption. Also, this number will be used as IV member size of
// pageObj structure.

#ifdef COOKFS_USECCRYPT
#include "../7zip/C/Aes.h"
#define COOKFS_PAGEOBJ_BLOCK_SIZE AES_BLOCK_SIZE
#else
#define COOKFS_PAGEOBJ_BLOCK_SIZE 16
#endif /* COOKFS_USECCRYPT */

typedef unsigned char* Cookfs_PageObj;

typedef struct Cookfs_PageObjStruct {
    Tcl_Size bufferSize;
    Tcl_Size effectiveSize;
    int refCount;
#ifdef TCL_THREADS
    Tcl_Mutex mx;
#endif /* TCL_THREADS */
    // This structure member should be at the end. We will use IV as
    // a pointer to get a pointer to the area (IV+data).
#ifdef COOKFS_USECCRYPT
    unsigned char IV[COOKFS_PAGEOBJ_BLOCK_SIZE];
#endif /* COOKFS_USECCRYPT */
} Cookfs_PageObjStruct;

void Cookfs_PageObjIncrRefCount(Cookfs_PageObj pg);
void Cookfs_PageObjDecrRefCount(Cookfs_PageObj pg);

#define Cookfs_PageObjBounceRefCount(pg) \
    Cookfs_PageObjIncrRefCount((pg)); Cookfs_PageObjDecrRefCount((pg))

#define Cookfs_PageObjSize(p) \
    (((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->effectiveSize)

#define Cookfs_PageObjCopyAsByteArray(p) \
    Tcl_NewByteArrayObj(p, Cookfs_PageObjSize(p))

Cookfs_PageObj Cookfs_PageObjAlloc(Tcl_Size size);
Cookfs_PageObj Cookfs_PageObjNewFromByteArray(Tcl_Obj *obj);

#ifdef COOKFS_USECCRYPT

Cookfs_PageObj Cookfs_PageObjNewFromByteArrayIV(Tcl_Obj *obj);

#define Cookfs_PageObjGetIV(p) \
    (((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->IV)

#define Cookfs_PageObjSetIV(p,iv) \
    memcpy(Cookfs_PageObjGetIV(p), (iv), COOKFS_PAGEOBJ_BLOCK_SIZE)

#define Cookfs_PageObjSizeIV(p) \
    (((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->effectiveSize + \
        COOKFS_PAGEOBJ_BLOCK_SIZE)

#define Cookfs_PageObjCopyAsByteArrayIV(p) \
    Tcl_NewByteArrayObj(Cookfs_PageObjGetIV(p), Cookfs_PageObjSizeIV(p))

// There functions modify pageobj. They are not thread-safe and
// must not be used for pageobjs that may be shared across threads.

int Cookfs_PageObjRealloc(Cookfs_PageObj *pgPtr, Tcl_Size size);
void Cookfs_PageObjAddPadding(Cookfs_PageObj pg);
int Cookfs_PageObjRemovePadding(Cookfs_PageObj pg);

#endif /* COOKFS_USECCRYPT */

#endif /* COOKFS_PAGEOBJ_H */
