/*
   (c) 2010 Wojciech Kocjan, Pawel Salawa
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGESINT_H
#define COOKFS_PAGESINT_H 1

enum {
    COOKFS_LASTOP_UNKNOWN = 0,
    COOKFS_LASTOP_READ,
    COOKFS_LASTOP_WRITE
};

enum {
    COOKFS_HASH_MD5 = 0,
    COOKFS_HASH_CRC32
};

#define COOKFS_SIGNATURE_LENGTH 7
#define COOKFS_MAX_CACHE_PAGES 256
#define COOKFS_DEFAULT_CACHE_PAGES 4
#define COOKFS_MAX_PRELOAD_PAGES 8
#define COOKFS_MAX_CACHE_AGE 50

#define COOKFS_PAGES_ASIDE 0x10000000
#define COOKFS_PAGES_MASK  0x0fffffff
#define COOKFS_PAGES_ISASIDE(value) (((value) & COOKFS_PAGES_ASIDE) == COOKFS_PAGES_ASIDE)

#define COOKFS_PAGES_MAX_ASYNC          64

typedef struct Cookfs_AsyncPage {
    int pageIdx;
    Tcl_Obj *pageContents;
} Cookfs_AsyncPage;

typedef struct Cookfs_CacheEntry {
    int pageIdx;
    int weight;
    int age;
    Cookfs_PageObj pageObj;
} Cookfs_CacheEntry;

struct _Cookfs_Pages {
#ifdef TCL_THREADS
    Cookfs_RWMutex mx;
    Tcl_Mutex mxLockSoft;
    Tcl_Mutex mxCache;
    Tcl_Mutex mxIO;
    Tcl_ThreadId threadId;
#endif /* TCL_THREADS */
    /* main interp */
    int isDead;
    int lockHard;
    int lockSoft;
    Tcl_Interp *interp;
    Tcl_Command commandToken;
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    Tcl_Obj *zipCmdCrc[2];
    Tcl_Obj *zipCmdCompress[6];
    Tcl_Obj *zipCmdDecompress[6];
    int zipCmdOffset;
    int zipCmdLength;
#endif /* USE_VFS_COMMANDS_FOR_ZIP */
    /* file */
    int isAside;
    int fileReadOnly;
    int fileCompression;
    int fileCompressionLevel;
    unsigned char fileSignature[COOKFS_SIGNATURE_LENGTH];
    int isFirstWrite;
    unsigned char fileStamp[COOKFS_SIGNATURE_LENGTH];
    Tcl_Channel fileChannel;
    int fileLastOp;
    int useFoffset;
    Tcl_WideInt foffset;
    int shouldTruncate;
    int pageHash;

    /* index */
    int pagesUptodate;
    int indexChanged;

    /* pages */
    Tcl_WideInt dataInitialOffset;
    Tcl_WideInt dataAllPagesSize;
    int dataNumPages;
    int dataPagesDataSize;
    int *dataPagesSize;
    unsigned char *dataPagesMD5;
    Cookfs_PageObj dataIndex;
    int dataPagesIsAside;
    Cookfs_Pages *dataAsidePages;

    /* compression information */
    int alwaysCompress;
    int compressCommandLen;
    Tcl_Obj **compressCommandPtr;
    int decompressCommandLen;
    Tcl_Obj **decompressCommandPtr;
    int asyncCompressCommandLen;
    Tcl_Obj **asyncCompressCommandPtr;
    int asyncDecompressCommandLen;
    Tcl_Obj **asyncDecompressCommandPtr;

    /* cache */
    int cacheSize;
    int cacheMaxAge;
    Cookfs_CacheEntry cache[COOKFS_MAX_CACHE_PAGES];

    /* async compress */
    Tcl_Obj *asyncCommandProcess;
    Tcl_Obj *asyncCommandWait;
    Tcl_Obj *asyncCommandFinalize;
    int asyncPageSize;
    Cookfs_AsyncPage asyncPage[COOKFS_PAGES_MAX_ASYNC];

    /* async decompress */
    int asyncDecompressQueue;
    int asyncDecompressQueueSize;
    int asyncDecompressIdx[COOKFS_MAX_PRELOAD_PAGES];
};

#ifdef TCL_THREADS
#define Cookfs_PagesWantRead(p) Cookfs_RWMutexWantRead((p)->mx);
#define Cookfs_PagesWantWrite(p) Cookfs_RWMutexWantWrite((p)->mx);
#else
#define Cookfs_PagesWantRead(p) {}
#define Cookfs_PagesWantWrite(p) {}
#endif /* TCL_THREADS */


#endif /* COOKFS_PAGESINT_H */
