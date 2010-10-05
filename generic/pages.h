/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef PAGES_H
#define PAGES_H 1

#if 10 * TCL_MAJOR_VERSION + TCL_MINOR_VERSION < 86
#define USE_ZLIB_VFSZIP 1
#else
#define USE_ZLIB_TCL86 1
#endif

#if 10 * TCL_MAJOR_VERSION + TCL_MINOR_VERSION >= 85
#define USE_TCL_TRUNCATE 1
#endif

enum {
    COOKFS_LASTOP_UNKNOWN = 0,
    COOKFS_LASTOP_READ,
    COOKFS_LASTOP_WRITE
};

#define COOKFS_SIGNATURE_LENGTH 7
#define COOKFS_MAX_CACHE_PAGES 32
#define COOKFS_DEFAULT_CACHE_PAGES 4

extern const char *cookfsCompressionOptions[];
enum {
    cookfsCompressionNone = 0,
    cookfsCompressionZlib = 1,
#ifdef COOKFS_USEBZ2
    cookfsCompressionBz2 = 2,
#endif
    cookfsCompressionMax
};

#define COOKFS_PAGES_ASIDE 0x10000000
#define COOKFS_PAGES_MASK  0x0fffffff
#define COOKFS_PAGES_ISASIDE(value) (((value) & COOKFS_PAGES_ASIDE) == COOKFS_PAGES_ASIDE)

typedef struct Cookfs_Pages {
    /* main interp */
    Tcl_Interp *interp;
#ifdef USE_ZLIB_VFSZIP
    Tcl_Obj *zipCmdCompress[6];
    Tcl_Obj *zipCmdDecompress[6];
#endif
    /* file */
    Tcl_Mutex pagesLock;
    int fileReadOnly;
    int fileCompression;
    int fileCompressionLevel;
    char fileSignature[COOKFS_SIGNATURE_LENGTH];
    Tcl_Channel fileChannel;
    int fileLastOp;
    int useFoffset;
    Tcl_WideInt foffset;

    /* index */
    int indexUptodate;

    /* pages */
    Tcl_WideInt dataInitialOffset;
    Tcl_WideInt dataAllPagesSize;
    int dataNumPages;
    int dataPagesDataSize;
    int *dataPagesSize;
    unsigned char *dataPagesMD5;
    Tcl_Obj *dataIndex;
    int dataPagesIsAside;
    struct Cookfs_Pages *dataAsidePages;
    
    /* cache */
    int cacheSize;
    int cachePageIdx[COOKFS_MAX_CACHE_PAGES];
    Tcl_Obj *cachePageObj[COOKFS_MAX_CACHE_PAGES];
    
} Cookfs_Pages;

Cookfs_Pages *Cookfs_PagesInit(Tcl_Interp *interp, Tcl_Obj *fileName, int fileReadOnly, int fileCompression, char *fileSignature, int useFoffset, Tcl_WideInt foffset, int isAside);
void Cookfs_PagesFini(Cookfs_Pages *p);
int Cookfs_PageAdd(Cookfs_Pages *p, Tcl_Obj *dataObj);
Tcl_Obj *Cookfs_PageGet(Cookfs_Pages *p, int index);
Tcl_Obj *Cookfs_PageGetHead(Cookfs_Pages *p);
Tcl_Obj *Cookfs_PageGetHeadMD5(Cookfs_Pages *p);
Tcl_Obj *Cookfs_PageGetTail(Cookfs_Pages *p);
Tcl_Obj *Cookfs_PageGetTailMD5(Cookfs_Pages *p);

int CookfsReadIndex(Cookfs_Pages *p);

void Cookfs_PagesSetAside(Cookfs_Pages *p, Cookfs_Pages *aside);

void Cookfs_PagesSetIndex(Cookfs_Pages *p, Tcl_Obj *dataIndex);
Tcl_Obj *Cookfs_PagesGetIndex(Cookfs_Pages *p);

#endif
