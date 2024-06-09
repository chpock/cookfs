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
} Cookfs_PageObjStruct;


#define Cookfs_PageObjIncrRefCount(p) \
    ((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->refCount++

/*
#define Cookfs_PageObjIncrRefCount(p) \
    { \
        ((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
            sizeof(Cookfs_PageObjStruct)))->refCount++; \
        CookfsLog(printf("Cookfs_PageObjIncrRefCount: %p", (void *)p)); \
    }
*/


#define Cookfs_PageObjDecrRefCount(p) \
    { \
        Cookfs_PageObjStruct *tmp = (Cookfs_PageObjStruct *)( \
            (Cookfs_PageObj)(p) - sizeof(Cookfs_PageObjStruct)); \
        if (!(--tmp->refCount)) { ckfree(tmp); } \
    }

/*
#define Cookfs_PageObjDecrRefCount(p) \
    { \
        CookfsLog(printf("Cookfs_PageObjDecrRefCount: %p", (void *)p)); \
        Cookfs_PageObjStruct *tmp = (Cookfs_PageObjStruct *)( \
            (Cookfs_PageObj)(p) - sizeof(Cookfs_PageObjStruct)); \
        if (!(--tmp->refCount)) { \
            ckfree(tmp); \
            CookfsLog(printf("Cookfs_PageObjDecrRefCount: release %p", (void *)p)); \
        } \
    }
*/

#define Cookfs_PageObjSize(p) \
    (((Cookfs_PageObjStruct *)((Cookfs_PageObj)(p) - \
        sizeof(Cookfs_PageObjStruct)))->effectiveSize)

#define Cookfs_PageObjCopyAsByteArray(p) \
    Tcl_NewByteArrayObj(p, Cookfs_PageObjSize(p))

Cookfs_PageObj Cookfs_PageObjAlloc(Tcl_Size size);
Cookfs_PageObj Cookfs_PageObjNewFromByteArray(Tcl_Obj *obj);

#endif /* COOKFS_PAGEOBJ_H */
