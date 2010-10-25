/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_FSINDEX_H
#define COOKFS_FSINDEX_H 1

#define COOKFS_NUMBLOCKS_DIRECTORY -1
#define COOKFS_FSINDEX_TABLE_MAXENTRIES 8

#define COOKFS_USEHASH_DEFAULT 0

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

/* all filenames are stored in UTF-8 */
typedef struct Cookfs_FsindexEntry {
    char *fileName;
    unsigned char fileNameLen;
    Tcl_WideInt fileTime;
    int fileBlocks;
    union {
        struct {
            Tcl_WideInt fileSize;
            int fileBlockOffsetSize[1];
        } fileInfo;
	struct {
	    union {
		Tcl_HashTable children;
		struct Cookfs_FsindexEntry *childTable[COOKFS_FSINDEX_TABLE_MAXENTRIES];
	    } dirData;
	    char isHash;
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
Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(int fileNameLength, int numBlocks, int useHash);
void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e);

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Tcl_Obj *pathList);
Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Tcl_Obj *pathList, int numBlocks);
Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_Fsindex *i, Cookfs_FsindexEntry *currentNode, char *pathTailStr, int pathTailLen, int numBlocks);
int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList);

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Tcl_Obj *pathList, int *itemCountPtr);
void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items);

#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEX_H */
