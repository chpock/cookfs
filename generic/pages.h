/* (c) 2010 Pawel Salawa */

#ifndef PAGES_H
#define PAGES_H 1

enum {
    COOKFS_COMPRESS_NONE = 0,
    COOKFS_COMPRESS_ZIP = 1
};

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
    cookfsCompressionZip = 1,
    cookfsCompressionMax
};

typedef struct Cookfs_Pages {
    /* file */
    Tcl_Mutex pagesLock;
    int fileReadOnly;
    int fileCompression;
    int fileCompressionLevel;
    char fileSignature[COOKFS_SIGNATURE_LENGTH];
    Tcl_Channel fileChannel;
    int fileLastOp;

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
    
    /* cache */
    int cacheSize;
    int cachePageIdx[COOKFS_MAX_CACHE_PAGES];
    Tcl_Obj *cachePageObj[COOKFS_MAX_CACHE_PAGES];
    
} Cookfs_Pages;

Cookfs_Pages *Cookfs_PagesInit(Tcl_Obj *fileName, int fileReadOnly, int fileCompression, char *fileSignature);
void Cookfs_PagesFini(Cookfs_Pages *p);
int Cookfs_PageAdd(Cookfs_Pages *p, Tcl_Obj *dataObj);
Tcl_Obj *Cookfs_PageGet(Cookfs_Pages *p, int index);
Tcl_Obj *Cookfs_PageGetPrefix(Cookfs_Pages *p);

int CookfsReadIndex(Cookfs_Pages *p);

#endif
