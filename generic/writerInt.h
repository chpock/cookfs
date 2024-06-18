/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_WRITERINT_H
#define COOKFS_WRITERINT_H 1

typedef struct Cookfs_WriterBuffer {
    void *buffer;
    Tcl_WideInt bufferSize;
    Cookfs_PathObj *pathObj;
    Tcl_WideInt mtime;
    Cookfs_FsindexEntry *entry;

    Cookfs_PathObj *sortKey;
    char *sortKeyExt;
    Tcl_Size sortKeyExtLen;

    int pageBlock;
    int pageOffset;

    struct Cookfs_WriterBuffer *next;
} Cookfs_WriterBuffer;

struct _Cookfs_Writer {

    Tcl_Interp *interp;
    Tcl_Command commandToken;
    int fatalError;
    int isDead;
#ifdef TCL_THREADS
    Cookfs_RWMutex mx;
    Tcl_Mutex mxLockSoft;
    Tcl_ThreadId threadId;
#endif /* TCL_THREADS */
    int lockSoft;

    Cookfs_Pages *pages;
    Cookfs_Fsindex *index;

    int isWriteToMemory;
    Tcl_WideInt smallFileSize;
    Tcl_WideInt maxBufferSize;
    Tcl_WideInt pageSize;

    Cookfs_WriterBuffer *bufferFirst;
    Cookfs_WriterBuffer *bufferLast;
    Tcl_WideInt bufferSize;
    int bufferCount;

};

#ifdef TCL_THREADS
#define Cookfs_WriterWantRead(w) Cookfs_RWMutexWantRead((w)->mx);
#define Cookfs_WriterWantWrite(w) Cookfs_RWMutexWantWrite((w)->mx);
#else
#define Cookfs_WriterWantRead(w) {}
#define Cookfs_WriterWantWrite(w) {}
#endif /* TCL_THREADS */


#endif /* COOKFS_WRITERINT_H */
