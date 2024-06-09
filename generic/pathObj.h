/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PATHOBJ_H
#define COOKFS_PATHOBJ_H 1

/* TODO:

   As for now, fsindex uses Tcl hash table with zero-end strings as keys.

   This means, that filename and its parts must be with zero-end.

   This will not work with filenames containing a NULL char inside.

   This also forces us to maintain an extra copy of the full name where we
   have null-terminated path elements.

   So, in Cookfs_PathObj we have fullName0 - it is a copy of fullName, but
   elements there are null-terminated. And we have name0 in
   Cookfs_PathObjElement with null-terminated path elements.

   We should avoid their usage as much as possible. And once fsindex is
   optimized to use strings with null bytes, those fields will be removed.

*/

typedef struct Cookfs_PathObjElement {
    char *name;
    char *name0;
    int length;
} Cookfs_PathObjElement;

typedef struct Cookfs_PathObj {
    int refCount;
    char *fullName;
    char *fullName0;
    int fullNameLength;
    char *tailName;
    int tailNameLength;
    int elementCount;
    Cookfs_PathObjElement element[];
} Cookfs_PathObj;



#define Cookfs_PathObjIncrRefCount(p) \
    (p)->refCount++

/*
#define Cookfs_PathObjIncrRefCount(p) \
    { \
        (p)->refCount++; \
        CookfsLog(printf("Cookfs_PathObjIncrRefCount: %p", (void *)p)); \
    }
*/

#define Cookfs_PathObjDecrRefCount(p) \
    { \
        if (!(--p->refCount)) { ckfree(p); } \
    }

/*
#define Cookfs_PathObjDecrRefCount(p) \
    { \
        CookfsLog(printf("Cookfs_PathObjDecrRefCount: %p", (void *)p)); \
        if (!(--p->refCount)) { \
            ckfree(p); \
            CookfsLog(printf("Cookfs_PathObjDecrRefCount: release %p", (void *)p)); \
        } \
    }
*/

Cookfs_PathObj *Cookfs_PathObjNewFromTclObj(Tcl_Obj *path);
Cookfs_PathObj *Cookfs_PathObjNewFromStr(const char* pathStr,
    Tcl_Size pathLength);

#endif /* COOKFS_PATHOBJ_H */
