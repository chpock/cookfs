/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGEOBJ_H
#define COOKFS_PAGEOBJ_H 1

typedef unsigned char* Cookfs_PageObj;

typedef struct Cookfs_PageObjStruct {
    Tcl_Size bufferSize;
    Tcl_Size effectiveSize;
    int refCount;
#ifdef TCL_THREADS
    Tcl_Mutex mx;
#endif /* TCL_THREADS */
} Cookfs_PageObjStruct;

void Cookfs_PageObjIncrRefCount(Cookfs_PageObj p);
void Cookfs_PageObjDecrRefCount(Cookfs_PageObj p);

#define Cookfs_PageObjSize(p) \
    (((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->effectiveSize)

#define Cookfs_PageObjCopyAsByteArray(p) \
    Tcl_NewByteArrayObj(p, Cookfs_PageObjSize(p))

Cookfs_PageObj Cookfs_PageObjAlloc(Tcl_Size size);
Cookfs_PageObj Cookfs_PageObjNewFromByteArray(Tcl_Obj *obj);

#endif /* COOKFS_PAGEOBJ_H */
