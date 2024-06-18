/*
   (c) 2010 Wojciech Kocjan, Pawel Salawa
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_FSINDEXINT_H
#define COOKFS_FSINDEXINT_H 1

#define COOKFS_NUMBLOCKS_DIRECTORY -1
#define COOKFS_FSINDEX_TABLE_MAXENTRIES 8

#define COOKFS_USEHASH_DEFAULT 0

/* all filenames are stored in UTF-8 */
struct _Cookfs_FsindexEntry {
    char *fileName;
    unsigned char fileNameLen;
    Tcl_WideInt fileTime;
    int fileBlocks;
    Cookfs_Fsindex *isFileBlocksInitialized;
    Cookfs_Fsindex *fsindex;
    int refcount;
#ifdef TCL_THREADS
    Tcl_Mutex mxRefCount;
#endif /* TCL_THREADS */
    int isInactive;
    Cookfs_FsindexEntry *next;
    union {
        struct {
            Tcl_WideInt fileSize;
            int fileBlockOffsetSize[1];
        } fileInfo;
	struct {
	    union {
		Tcl_HashTable children;
		Cookfs_FsindexEntry *childTable[COOKFS_FSINDEX_TABLE_MAXENTRIES];
	    } dirData;
	    char isHash;
	    int childCount;
	} dirInfo;
    } data;
    /* this stores series of 3 values: block number, block offset, size of element */
};

struct _Cookfs_Fsindex {
    Cookfs_FsindexEntry *rootItem;
    Cookfs_FsindexEntry *inactiveItems;
    Tcl_HashTable metadataHash;
    int *blockIndex;
    int blockIndexSize;
    Tcl_WideInt changeCount;
    Tcl_Interp *interp;
    Tcl_Command commandToken;
    int isDead;
#ifdef TCL_THREADS
    Tcl_ThreadId threadId;
    Cookfs_RWMutex mx;
    Tcl_Mutex mxLockSoft;
#endif /* TCL_THREADS */
    int lockHard;
    int lockSoft;
};

#ifdef TCL_THREADS
#define Cookfs_FsindexWantRead(i) Cookfs_RWMutexWantRead((i)->mx);
#define Cookfs_FsindexWantWrite(i) Cookfs_RWMutexWantWrite((i)->mx);
#define Cookfs_FsindexEntryWantRead(e) Cookfs_FsindexWantRead((e)->fsindex)
#define Cookfs_FsindexEntryWantWrite(e) Cookfs_FsindexWantWrite((e)->fsindex)
#else
#define Cookfs_FsindexWantRead(i) {}
#define Cookfs_FsindexWantWrite(i) {}
#define Cookfs_FsindexEntryWantRead(e) {}
#define Cookfs_FsindexEntryWantWrite(e) {}
#endif

#endif /* COOKFS_FSINDEXINT_H */
