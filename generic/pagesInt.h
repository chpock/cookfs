/*
   (c) 2010 Wojciech Kocjan, Pawel Salawa
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_PAGESINT_H
#define COOKFS_PAGESINT_H 1

#include "pgindex.h"

#ifdef COOKFS_USECCRYPTO
#include "crypto.h"
#endif /* COOKFS_USECCRYPTO */

// File mapping
#ifdef _WIN32

// Include Windows headers for file mapping
#define WIN32_LEAN_AND_MEAN
#ifndef STRICT
#define STRICT // See MSDN Article Q83456
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

#else

// POSIX file mapping
#include <sys/mman.h>

#endif /* _WIN32 */

enum {
    COOKFS_LASTOP_UNKNOWN = 0,
    COOKFS_LASTOP_READ,
    COOKFS_LASTOP_WRITE
};

#ifdef COOKFS_USECCRYPTO

enum {
    COOKFS_ENCRYPT_NONE = 0,
    COOKFS_ENCRYPT_FILE,
    COOKFS_ENCRYPT_KEY,
    COOKFS_ENCRYPT_KEY_INDEX
};

#define COOKFS_ENCRYPT_PASSWORD_SALT_SIZE 16
#define COOKFS_ENCRYPT_KEY_AND_HASH_SIZE (COOKFS_ENCRYPT_KEY_SIZE + COOKFS_ENCRYPT_IV_SIZE)

#endif /* COOKFS_USECCRYPTO */

#define COOKFS_SIGNATURE_LENGTH 7
#define COOKFS_MAX_CACHE_PAGES 256
#define COOKFS_DEFAULT_CACHE_PAGES 4
#define COOKFS_MAX_PRELOAD_PAGES 8
#define COOKFS_MAX_CACHE_AGE 50

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
#ifdef COOKFS_USECCRYPTO
    int encryption;
    int encryptionLevel;

    unsigned char encryptionKey[COOKFS_ENCRYPT_KEY_AND_HASH_SIZE];

    unsigned char passwordSalt[COOKFS_ENCRYPT_PASSWORD_SALT_SIZE];

    unsigned char encryptionEncryptedKeyIV[COOKFS_ENCRYPT_IV_SIZE];
    unsigned char encryptionEncryptedKey[COOKFS_ENCRYPT_KEY_AND_HASH_SIZE];

    int isPasswordSet;
    int isEncryptionActive;
    int isKeyDecrypted;
#endif /* COOKFS_USECCRYPTO */
    /* file */
    int isAside;
    int fileReadOnly;
    Cookfs_CompressionType baseCompression;
    int baseCompressionLevel;
    Cookfs_CompressionType currentCompression;
    int currentCompressionLevel;
    unsigned char fileSignature[COOKFS_SIGNATURE_LENGTH];
    int isFirstWrite;
    unsigned char fileStamp[COOKFS_SIGNATURE_LENGTH];

    Tcl_Channel fileChannel;
#ifdef _WIN32
    HANDLE fileHandle;
#endif
    Tcl_WideInt fileLength;
    unsigned char *fileData;

    int fileLastOp;
    int useFoffset;
    Tcl_WideInt foffset;
    int shouldTruncate;
    Cookfs_HashType pageHash;

    /* index */
    int pagesUptodate;
    int indexChanged;

    /* pages */
    Tcl_WideInt dataInitialOffset;
    Cookfs_PgIndex *pagesIndex;
    Cookfs_PageObj dataIndex;
    int dataPagesIsAside;
    Cookfs_Pages *dataAsidePages;

    /* compression information */
    int alwaysCompress;
#if defined(COOKFS_USECALLBACKS)
    int compressCommandLen;
    Tcl_Obj **compressCommandPtr;
    int decompressCommandLen;
    Tcl_Obj **decompressCommandPtr;
    int asyncCompressCommandLen;
    Tcl_Obj **asyncCompressCommandPtr;
    int asyncDecompressCommandLen;
    Tcl_Obj **asyncDecompressCommandPtr;
#endif /* COOKFS_USECALLBACKS */

    /* cache */
    int cacheSize;
    int cacheMaxAge;
    Cookfs_CacheEntry cache[COOKFS_MAX_CACHE_PAGES];

#if defined(COOKFS_USECALLBACKS)
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
#endif /* COOKFS_USECALLBACKS */
};

#ifdef TCL_THREADS
#define Cookfs_PagesWantRead(p) Cookfs_RWMutexWantRead((p)->mx);
#define Cookfs_PagesWantWrite(p) Cookfs_RWMutexWantWrite((p)->mx);
#else
#define Cookfs_PagesWantRead(p) {}
#define Cookfs_PagesWantWrite(p) {}
#endif /* TCL_THREADS */


#endif /* COOKFS_PAGESINT_H */
