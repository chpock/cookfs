/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGES_H
#define COOKFS_PAGES_H 1

#include "pageObj.h"
#include "pgindex.h"

typedef struct _Cookfs_Pages Cookfs_Pages;

typedef enum {
    COOKFS_PAGES_PART_HEAD,
    COOKFS_PAGES_PART_DATA,
    COOKFS_PAGES_PART_TAIL
} Cookfs_PagesPartsType;

Cookfs_Pages *Cookfs_PagesGetHandle(Tcl_Interp *interp, const char *cmdName);

Cookfs_Pages *Cookfs_PagesInit(Tcl_Interp *interp, Tcl_Obj *fileName,
    int fileReadOnly, int baseCompression, int baseCompressionLevel,
    int currentCompression, int currentCompressionLevel,
    Tcl_Obj *password, int encryptKey, int encryptLevel,
    char *fileSignature, int useFoffset, Tcl_WideInt foffset, int isAside,
#if defined(COOKFS_USECALLBACKS)
    int asyncDecompressQueueSize, Tcl_Obj *compressCommand,
    Tcl_Obj *decompressCommand, Tcl_Obj *asyncCompressCommand,
    Tcl_Obj *asyncDecompressCommand,
#endif /* COOKFS_USECALLBACKS */
    Tcl_Obj **err);

Tcl_WideInt Cookfs_PagesClose(Cookfs_Pages *p);
Tcl_WideInt Cookfs_GetFilesize(Cookfs_Pages *p);
void Cookfs_PagesFini(Cookfs_Pages *p);
int Cookfs_PageAddRaw(Cookfs_Pages *p, unsigned char *bytes, int objLength, Tcl_Obj **err);
int Cookfs_PageAdd(Cookfs_Pages *p, Cookfs_PageObj dataObj, Tcl_Obj **err);
int Cookfs_PageAddTclObj(Cookfs_Pages *p, Tcl_Obj *dataObj, Tcl_Obj **err);
Cookfs_PageObj Cookfs_PageGet(Cookfs_Pages *p, int index, int weight, Tcl_Obj **err);
Cookfs_PageObj Cookfs_PageCacheGet(Cookfs_Pages *p, int index, int update, int weight);
void Cookfs_PageCacheSet(Cookfs_Pages *p, int idx, Cookfs_PageObj obj, int weight);
#define Cookfs_PageGetHead(p) Cookfs_PagesGetPartObj((p), COOKFS_PAGES_PART_HEAD)
Tcl_Obj *Cookfs_PageGetHeadMD5(Cookfs_Pages *p);
Tcl_Obj *Cookfs_PageGetTail(Cookfs_Pages *p);
Tcl_Obj *Cookfs_PageGetTailMD5(Cookfs_Pages *p);
void Cookfs_PagesSetCacheSize(Cookfs_Pages *p, int size);
int Cookfs_PagesGetCacheSize(Cookfs_Pages *p);
/* Not used as for now
int Cookfs_PagesGetAlwaysCompress(Cookfs_Pages *p);
*/
void Cookfs_PagesSetAlwaysCompress(Cookfs_Pages *p, int alwaysCompress);
Cookfs_CompressionType Cookfs_PagesGetCompression(Cookfs_Pages *p,
    int *fileCompressionLevel);
Tcl_Obj *Cookfs_PagesGetCompressionObj(Cookfs_Pages *p);
void Cookfs_PagesSetCompression(Cookfs_Pages *p,
    Cookfs_CompressionType fileCompression, int fileCompressionLevel);

/* Not used as for now
int Cookfs_PagesIsReadonly(Cookfs_Pages *p);
*/

int Cookfs_PagesSetPassword(Cookfs_Pages *p, Tcl_Obj *passObj);

int Cookfs_PagesGetLength(Cookfs_Pages *p);

void Cookfs_PagesSetAside(Cookfs_Pages *p, Cookfs_Pages *aside);

void Cookfs_PagesSetIndex(Cookfs_Pages *p, Cookfs_PageObj dataIndex);
Cookfs_PageObj Cookfs_PagesGetIndex(Cookfs_Pages *p);

Tcl_WideInt Cookfs_PagesGetPageOffset(Cookfs_Pages *p, int idx);

int Cookfs_PagesSetMaxAge(Cookfs_Pages *p, int maxAge);
int Cookfs_PagesTickTock(Cookfs_Pages *p);
int Cookfs_PagesIsCached(Cookfs_Pages *p, int index);

int Cookfs_PagesIsEncrypted(Cookfs_Pages *p, int index);

Tcl_Obj *Cookfs_PagesGetHashAsObj(Cookfs_Pages *p);
void Cookfs_PagesSetHash(Cookfs_Pages *p, Cookfs_HashType pagehash);
int Cookfs_PagesSetHashByObj(Cookfs_Pages *p, Tcl_Obj *pagehash,
    Tcl_Interp *interp);

int Cookfs_PagesGetPageSize(Cookfs_Pages *p, int index);
int Cookfs_PagesGetPageSizeCompressed(Cookfs_Pages *p, int index);
Tcl_Obj *Cookfs_PagesGetPageCompressionObj(Cookfs_Pages *p, int index);

void Cookfs_PagesCalculateHash(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size size, unsigned char *output);

int Cookfs_PagesPartsExist(Cookfs_Pages *p);
Tcl_WideInt Cookfs_PagesGetPartOffset(Cookfs_Pages *p,
    Cookfs_PagesPartsType part);
Tcl_WideInt Cookfs_PagesGetPartSize(Cookfs_Pages *p,
    Cookfs_PagesPartsType part);
Tcl_Obj *Cookfs_PagesGetPartObj(Cookfs_Pages *p,
    Cookfs_PagesPartsType part);
Tcl_WideInt Cookfs_PagesPutPartToChannel(Cookfs_Pages *p,
    Cookfs_PagesPartsType part, Tcl_Channel chan);

int Cookfs_PageAddStamp(Cookfs_Pages *p, Tcl_WideInt size);

Tcl_Obj *Cookfs_PagesGetInfo(Cookfs_Pages *p, int num);
Tcl_Obj *Cookfs_PagesGetInfoSpecial(Cookfs_Pages *p,
    Cookfs_PgIndexSpecialPageType type);

Tcl_Obj *Cookfs_PagesGetFilenameObj(Cookfs_Pages *p);

#ifdef COOKFS_USECCRYPTO
int Cookfs_PagesIsEncryptionActive(Cookfs_Pages *p);
int Cookfs_PagesIsEncryptkey(Cookfs_Pages *p);
int Cookfs_PagesGetEncryptlevel(Cookfs_Pages *p);
#endif /* COOKFS_USECCRYPTO */

int Cookfs_PagesUnlock(Cookfs_Pages *p);
int Cookfs_PagesLockRW(int isWrite, Cookfs_Pages *p, Tcl_Obj **err);
#define Cookfs_PagesLockWrite(p,err) Cookfs_PagesLockRW(1,(p),(err))
#define Cookfs_PagesLockRead(p,err) Cookfs_PagesLockRW(0,(p),(err))

int Cookfs_PagesLockHard(Cookfs_Pages *p);
int Cookfs_PagesUnlockHard(Cookfs_Pages *p);
int Cookfs_PagesLockSoft(Cookfs_Pages *p);
int Cookfs_PagesUnlockSoft(Cookfs_Pages *p);

void Cookfs_PagesLockExclusive(Cookfs_Pages *p);

int Cookfs_HashFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    Cookfs_HashType *hashPtr);

#endif /* COOKFS_PAGES_H */
