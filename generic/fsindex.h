/*
   (c) 2010 Wojciech Kocjan, Pawel Salawa
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_FSINDEX_H
#define COOKFS_FSINDEX_H 1

typedef struct _Cookfs_Fsindex Cookfs_Fsindex;
typedef struct _Cookfs_FsindexEntry Cookfs_FsindexEntry;

typedef void (Cookfs_FsindexForeachProc)(Cookfs_FsindexEntry *e, ClientData clientData);

Cookfs_Fsindex *Cookfs_FsindexGetHandle(Tcl_Interp *interp, const char *cmdName);

Cookfs_Fsindex *Cookfs_FsindexInit(Tcl_Interp *interp, Cookfs_Fsindex *i);
void Cookfs_FsindexFini(Cookfs_Fsindex *i);
void Cookfs_FsindexCleanup(Cookfs_Fsindex *i);

Cookfs_FsindexEntry *Cookfs_FsindexGet(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj);
Cookfs_FsindexEntry *Cookfs_FsindexSet(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int numBlocks);
Cookfs_FsindexEntry *Cookfs_FsindexSetDirectory(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj);
Cookfs_FsindexEntry *Cookfs_FsindexSetInDirectory(Cookfs_FsindexEntry *currentNode, char *pathTailStr, int pathTailLen, int numBlocks);
int Cookfs_FsindexUnset(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj);
int Cookfs_FsindexUnsetRecursive(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj);

Cookfs_FsindexEntry **Cookfs_FsindexList(Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int *itemCountPtr);
Cookfs_FsindexEntry **Cookfs_FsindexListEntry(Cookfs_FsindexEntry *dirNode, int *itemCountPtr);
void Cookfs_FsindexListFree(Cookfs_FsindexEntry **items);

Tcl_Obj *Cookfs_FsindexGetMetadata(Cookfs_Fsindex *i, const char *paramName);
void Cookfs_FsindexSetMetadata(Cookfs_Fsindex *i, const char *paramName, Tcl_Obj *data);
void Cookfs_FsindexSetMetadataRaw(Cookfs_Fsindex *i, const char *paramName,
    const unsigned char *dataPtr, Tcl_Size dataSize);
int Cookfs_FsindexUnsetMetadata(Cookfs_Fsindex *i, const char *paramName);

int Cookfs_FsindexGetBlockUsage(Cookfs_Fsindex *i, int idx);
void Cookfs_FsindexModifyBlockUsage(Cookfs_Fsindex *i, int idx, int count);

Cookfs_FsindexEntry *CookfsFsindexFindElement(const Cookfs_Fsindex *i, Cookfs_PathObj *pathObj, int listSize);

Tcl_WideInt Cookfs_FsindexIncrChangeCount(Cookfs_Fsindex *i, int count);
void Cookfs_FsindexResetChangeCount(Cookfs_Fsindex *i);

int Cookfs_FsindexEntryIsPending(Cookfs_FsindexEntry *e);
int Cookfs_FsindexEntryIsDirectory(Cookfs_FsindexEntry *e);
int Cookfs_FsindexEntryIsEmptyDirectory(Cookfs_FsindexEntry *e);
int Cookfs_FsindexEntryIsInactive(Cookfs_FsindexEntry *e);

int Cookfs_FsindexEntryGetBlockCount(Cookfs_FsindexEntry *e);

void Cookfs_FsindexEntrySetBlock(Cookfs_FsindexEntry *e,
    int blockNumber, int pageIndex, int pageOffset, int pageSize);
int Cookfs_FsindexEntryGetBlock(Cookfs_FsindexEntry *e,
    int blockNumber, int *pageNum, int *pageOffset, int *pageSize);
void Cookfs_FsindexEntryIncrBlockPageIndex(Cookfs_FsindexEntry *e,
    int blockNumber, int change);

void Cookfs_FsindexEntrySetFileSize(Cookfs_FsindexEntry *e,
    Tcl_WideInt fileSize);
Tcl_WideInt Cookfs_FsindexEntryGetFilesize(Cookfs_FsindexEntry *e);

void Cookfs_FsindexEntrySetFileTime(Cookfs_FsindexEntry *e,
    Tcl_WideInt fileTime);
Tcl_WideInt Cookfs_FsindexEntryGetFileTime(Cookfs_FsindexEntry *e);

const char *Cookfs_FsindexEntryGetFileName(Cookfs_FsindexEntry *e,
    unsigned char *fileNameLen);

void Cookfs_FsindexForeach(Cookfs_Fsindex *i, Cookfs_FsindexForeachProc *proc, ClientData clientData);

int Cookfs_FsindexEntryUnlock(Cookfs_FsindexEntry *e);
int Cookfs_FsindexEntryLock(Cookfs_FsindexEntry *e);

int Cookfs_FsindexUnlock(Cookfs_Fsindex *i);
int Cookfs_FsindexLockRW(int isWrite, Cookfs_Fsindex *i, Tcl_Obj **err);
#define Cookfs_FsindexLockWrite(i,err) Cookfs_FsindexLockRW(1,(i),(err))
#define Cookfs_FsindexLockRead(i,err) Cookfs_FsindexLockRW(0,(i),(err))

int Cookfs_FsindexLockHard(Cookfs_Fsindex *i);
int Cookfs_FsindexUnlockHard(Cookfs_Fsindex *i);
int Cookfs_FsindexLockSoft(Cookfs_Fsindex *i);
int Cookfs_FsindexUnlockSoft(Cookfs_Fsindex *i);

void Cookfs_FsindexLockExclusive(Cookfs_Fsindex *i);

#endif /* COOKFS_FSINDEX_H */
