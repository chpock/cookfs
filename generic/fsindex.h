/*
   (c) 2010 Wojciech Kocjan, Pawel Salawa
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_FSINDEX_H
#define COOKFS_FSINDEX_H 1

#define COOKFS_NUMBLOCKS_DIRECTORY -1
#define COOKFS_FSINDEX_TABLE_MAXENTRIES 8

#define COOKFS_USEHASH_DEFAULT 0

/* only handle Fsindex code if enabled in configure */
#ifdef COOKFS_USECFSINDEX

typedef struct Cookfs_Fsindex Cookfs_Fsindex;

/* all filenames are stored in UTF-8 */
typedef struct Cookfs_FsindexEntry {
    char *fileName;
    unsigned char fileNameLen;
    Tcl_WideInt fileTime;
    int fileBlocks;
    Cookfs_Fsindex *isFileBlocksInitialized;
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
    Tcl_HashTable metadataHash;
    int *blockIndex;
    int blockIndexSize;
    Tcl_WideInt changeCount;
    Tcl_Interp *interp;
    Tcl_Command commandToken;
    int isDead;
} Cookfs_Fsindex;

Cookfs_Fsindex *Cookfs_FsindexGetHandle(Tcl_Interp *interp, const char *cmdName);

Cookfs_Fsindex *Cookfs_FsindexInit(Cookfs_Fsindex *i);
void Cookfs_FsindexFini(Cookfs_Fsindex *i);
void Cookfs_FsindexCleanup(Cookfs_Fsindex *i);

/* TODO: move these to non-public API somehow? */
Cookfs_FsindexEntry *Cookfs_FsindexEntryAlloc(int fileNameLength, int numBlocks, int useHash);
void Cookfs_FsindexEntryFree(Cookfs_FsindexEntry *e);

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Tcl_Obj *pathList);
Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Tcl_Obj *pathList, int numBlocks);
Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_FsindexEntry *currentNode, char *pathTailStr, int pathTailLen, int numBlocks);
int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Tcl_Obj *pathList);
int Cookfs_FsindexUnsetRecursive(Cookfs_Fsindex *i, Tcl_Obj *pathList);

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Tcl_Obj *pathList, int *itemCountPtr);
Cookfs_FsindexEntry **Cookfs_FsindexListEntry(Cookfs_FsindexEntry *dirNode, int *itemCountPtr);
void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items);

Tcl_Obj *Cookfs_FsindexGetMetadata(Cookfs_Fsindex *i, const char *paramName);
void Cookfs_FsindexSetMetadata(Cookfs_Fsindex *i, const char *paramName, Tcl_Obj *data);
int Cookfs_FsindexUnsetMetadata(Cookfs_Fsindex *i, const char *paramName);
int Cookfs_FsindexGetBlockUsage(Cookfs_Fsindex *i, int idx);
void Cookfs_FsindexModifyBlockUsage(Cookfs_Fsindex *i, int idx, int count);

Cookfs_FsindexEntry *CookfsFsindexFindElement(const Cookfs_Fsindex *i, Tcl_Obj *pathList, int listSize);

Tcl_WideInt Cookfs_FsindexIncrChangeCount(Cookfs_Fsindex *i, int count);
void Cookfs_FsindexResetChangeCount(Cookfs_Fsindex *i);

int Cookfs_FsindexEntryIsPending(Cookfs_FsindexEntry *e);
int Cookfs_FsindexEntryIsDirectory(Cookfs_FsindexEntry *e);

void Cookfs_FsindexUpdateEntryBlock(Cookfs_Fsindex *i, Cookfs_FsindexEntry *e,
    int blockNumber, int blockIndex, int blockOffset, int blockSize);
void Cookfs_FsindexUpdateEntryFileSize(Cookfs_FsindexEntry *e,
    Tcl_WideInt fileSize);

void Cookfs_FsindexUpdatePendingEntry(Cookfs_Fsindex *i, Cookfs_FsindexEntry *e,
    int blockIndex, int blockOffset);

#endif /* COOKFS_USECFSINDEX */

#endif /* COOKFS_FSINDEX_H */
