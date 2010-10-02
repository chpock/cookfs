/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef FSINDEX_H
#define FSINDEX_H 1

#define COOKFS_NUMBLOCKS_DIRECTORY -1

/* all filenames are stored in UTF-8 */
typedef struct Cookfs_FsindexEntry {
    char *fileName;
    unsigned char fileNameLen;
    Tcl_WideInt fileTime;
    int fileBlocks;
    union {
        struct {
            Tcl_WideInt fileSize;
            char *memoryBlock;
            int fileBlockOffsetSize[1];
        } fileInfo;
        struct {
            Tcl_HashTable children;
            int childCount;
        } dirInfo;
    } data;
    /* this stores series of 3 values: block number, block offset, size of element */
} Cookfs_FsindexEntry;

typedef struct Cookfs_Fsindex {
    Cookfs_FsindexEntry *rootItem;
} Cookfs_Fsindex;

Cookfs_Fsindex *Cookfs_FsindexInit();
void Cookfs_FsindexFini(Cookfs_Fsindex *i);

/* TODO: move these to non-public API somehow? */
Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(int fileNameLength, int numBlocks);
void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e);

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Tcl_Obj *pathList);
Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Tcl_Obj *pathList, int numBlocks);
int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList);

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Tcl_Obj *pathList, int *itemCountPtr);
void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items);

#endif
