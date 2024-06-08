/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_WRITER_H
#define COOKFS_WRITER_H 1

/* Tcl public API */

typedef enum {
    COOKFS_WRITER_SOURCE_FILE,
    COOKFS_WRITER_SOURCE_CHANNEL,
    COOKFS_WRITER_SOURCE_OBJECT,
    COOKFS_WRITER_SOURCE_BUFFER
} Cookfs_WriterDataSource;

typedef struct Cookfs_WriterBuffer {
    void *buffer;
    Tcl_WideInt bufferSize;
    Tcl_Obj *pathObj;
    Tcl_WideInt mtime;
    Cookfs_FsindexEntry *entry;

    Tcl_Obj *sortKey;
    const void *sortKeyStr;
    Tcl_Size sortKeyLen;

    int pageBlock;
    int pageOffset;

    struct Cookfs_WriterBuffer *next;
} Cookfs_WriterBuffer;

typedef struct Cookfs_Writer {

    Tcl_Interp *interp;
    Tcl_Command commandToken;
    int fatalError;
    int isDead;

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

} Cookfs_Writer;

Cookfs_Writer *Cookfs_WriterGetHandle(Tcl_Interp *interp, const char *cmdName);

Cookfs_Writer *Cookfs_WriterInit(Tcl_Interp* interp,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Tcl_WideInt smallfilebuffer, Tcl_WideInt smallfilesize,
    Tcl_WideInt pagesize, int writetomemory);

void Cookfs_WriterFini(Cookfs_Writer *w);

int Cookfs_WriterPurge(Cookfs_Writer *w, Tcl_Obj **err);

Tcl_Obj *Cookfs_WriterGetBufferObj(Cookfs_Writer *w, int blockNumber);
const void *Cookfs_WriterGetBuffer(Cookfs_Writer *w, int blockNumber,
    Tcl_WideInt *blockSize);

int Cookfs_WriterAddFile(Cookfs_Writer *w, Tcl_Obj *pathObj,
    Cookfs_WriterDataSource dataType, void *data, Tcl_WideInt dataSize,
    Tcl_Obj **err);

int Cookfs_WriterRemoveFile(Cookfs_Writer *w, Cookfs_FsindexEntry *entry);

int Cookfs_WriterGetWritetomemory(Cookfs_Writer *w);
void Cookfs_WriterSetWritetomemory(Cookfs_Writer *w, int status);

Tcl_WideInt Cookfs_WriterGetSmallfilebuffersize(Cookfs_Writer *w);

#endif /* COOKFS_WRITER_H */
