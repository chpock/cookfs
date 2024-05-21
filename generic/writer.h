/* (c) 2024 Konstantin Kushnir */

#ifndef COOKFS_WRITER_H
#define COOKFS_WRITER_H 1

/* Tcl public API */

typedef struct Cookfs_Writer {
    Tcl_Interp *interp;
    Tcl_Obj *commandObj;
} Cookfs_Writer;

Cookfs_Writer *Cookfs_WriterInit(Tcl_Interp* interp,
    Cookfs_Pages *pages, Cookfs_Fsindex *index,
    Tcl_WideInt smallfilebuffer, Tcl_WideInt smallfilesize,
    Tcl_WideInt pagesize, int writetomemory);

void Cookfs_WriterFini(Cookfs_Writer *w);

int Cookfs_WriterPurge(Cookfs_Writer *w);

int Cookfs_WriterGetWritetomemory(Cookfs_Writer *w, int *status);
int Cookfs_WriterSetWritetomemory(Cookfs_Writer *w, int status);

int Cookfs_WriterGetSmallfilebuffersize(Cookfs_Writer *w,
    Tcl_WideInt *size);

int Cookfs_WriterSetIndex(Cookfs_Writer *w, Cookfs_Fsindex *index);

#endif /* COOKFS_WRITER_H */
