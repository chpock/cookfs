/*
 * pages.c
 *
 * Provides functions for using pages
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#if defined(COOKFS_USECALLBACKS)
#include "pagesAsync.h"
#endif /* COOKFS_USECALLBACKS */

// For ptrdiff_t type
#include <stddef.h>

// 1  byte  - base compression
// 1  byte  - base compression level
// 1  byte  - encryption
// 26 bytes - pgindex info (1 byte compression + 1 byte level + 16 bytes MD5 hash + 4 bytes size compressed + 4 bytes size uncompressed )
// 26 bytes - fsindex info (1 byte compression + 1 byte level + 16 bytes MD5 hash + 4 bytes size compressed + 4 bytes size uncompressed )
// 7  bytes - signature
// Total: 62 bytes
#define COOKFS_SUFFIX_BYTES (1 + 1 + 1 + 26 * 2 + COOKFS_SIGNATURE_LENGTH)
// Offsets
#define COOKFS_SUFFIX_OFFSET_BASE_COMPRESSION    0
#define COOKFS_SUFFIX_OFFSET_BASE_LEVEL           (COOKFS_SUFFIX_OFFSET_BASE_COMPRESSION + 1)
#define COOKFS_SUFFIX_OFFSET_ENCRYPTION           (COOKFS_SUFFIX_OFFSET_BASE_LEVEL + 1)
#define COOKFS_SUFFIX_OFFSET_PGINDEX_COMPRESSION  (COOKFS_SUFFIX_OFFSET_ENCRYPTION + 1)
#define COOKFS_SUFFIX_OFFSET_PGINDEX_LEVEL        (COOKFS_SUFFIX_OFFSET_PGINDEX_COMPRESSION + 1)
#define COOKFS_SUFFIX_OFFSET_PGINDEX_HASH         (COOKFS_SUFFIX_OFFSET_PGINDEX_LEVEL + 1)
#define COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_COMPR   (COOKFS_SUFFIX_OFFSET_PGINDEX_HASH + 16)
#define COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_UNCOMPR (COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_COMPR + 4)
#define COOKFS_SUFFIX_OFFSET_FSINDEX_COMPRESSION  (COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_UNCOMPR + 4)
#define COOKFS_SUFFIX_OFFSET_FSINDEX_LEVEL        (COOKFS_SUFFIX_OFFSET_FSINDEX_COMPRESSION + 1)
#define COOKFS_SUFFIX_OFFSET_FSINDEX_HASH         (COOKFS_SUFFIX_OFFSET_FSINDEX_LEVEL + 1)
#define COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_COMPR   (COOKFS_SUFFIX_OFFSET_FSINDEX_HASH + 16)
#define COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_UNCOMPR (COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_COMPR + 4)
#define COOKFS_SUFFIX_OFFSET_SIGNATURE            (COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_UNCOMPR + 4)

// stamp size in bytes = COOKFS_SIGNATURE_LENGTH + <file size> (8 bytes)
#define COOKFS_STAMP_BYTES (COOKFS_SIGNATURE_LENGTH + 8)
// read by 512kb chunks
#define COOKFS_SEARCH_STAMP_CHUNK 524288
// max read 10mb
#define COOKFS_SEARCH_STAMP_MAX_READ 10485760

#ifdef COOKFS_USECCRYPTO
// number of iterations for PBKDF2-HMAC-SHA256
#define COOKFS_ENCRYPT_ITERATIONS_K1 (4096*2)
#define COOKFS_ENCRYPT_ITERATIONS_K2 (4096*20)
#define COOKFS_ENCRYPT_LEVEL_MAX 31
#endif /* COOKFS_USECCRYPTO */

#define COOKFS_ENCRYPT_LEVEL_DEFAULT 15

#define COOKFS_PAGES_ERRORMSG "Unable to create Cookfs object"

/* declarations of static and/or internal functions */
static Cookfs_PageObj CookfsPagesPageGetInt(Cookfs_Pages *p, int index, Tcl_Obj **err);
static void CookfsPagesPageCacheMoveToTop(Cookfs_Pages *p, int index);
static int CookfsReadIndex(Tcl_Interp *interp, Cookfs_Pages *p, Tcl_Obj *password, int *is_abort, Tcl_Obj **err);
static void CookfsTruncateFileIfNeeded(Cookfs_Pages *p, Tcl_WideInt targetOffset);
static Tcl_WideInt Cookfs_PageSearchStamp(Cookfs_Pages *p);
static void Cookfs_PagesFree(Cookfs_Pages *p);

static const char *const pagehashNames[] = { "md5", "crc32", NULL };

int Cookfs_PagesLockRW(int isWrite, Cookfs_Pages *p, Tcl_Obj **err) {
    int ret = 1;
#ifdef TCL_THREADS
    CookfsLog(printf("try to %s lock...", isWrite ? "WRITE" : "READ"));
    if (isWrite) {
        ret = Cookfs_RWMutexLockWrite(p->mx);
    } else {
        ret = Cookfs_RWMutexLockRead(p->mx);
    }
    if (ret && p->isDead == 1) {
        // If object is terminated, don't allow everything.
        ret = 0;
        Cookfs_RWMutexUnlock(p->mx);
    }
    if (!ret) {
        CookfsLog(printf("FAILED to %s lock", isWrite ? "WRITE" : "READ"));
        if (err != NULL) {
            *err = Tcl_NewStringObj("stalled pages object detected", -1);
        }
    } else {
        CookfsLog(printf("ok - %s lock (%d)", isWrite ? "WRITE" : "READ",
            Cookfs_RWMutexGetLocks(p->mx)));
    }
#else
    UNUSED(isWrite);
    UNUSED(p);
    UNUSED(err);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_PagesUnlock(Cookfs_Pages *p) {
#ifdef TCL_THREADS
    Cookfs_RWMutexUnlock(p->mx);
    CookfsLog(printf("ok (%d)", Cookfs_RWMutexGetLocks(p->mx)));
#else
    UNUSED(p);
#endif /* TCL_THREADS */
    return 1;
}

int Cookfs_PagesLockHard(Cookfs_Pages *p) {
    p->lockHard = 1;
    return 1;
}

int Cookfs_PagesUnlockHard(Cookfs_Pages *p) {
    p->lockHard = 0;
    return 1;
}

int Cookfs_PagesLockSoft(Cookfs_Pages *p) {
    int ret = 1;
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    if (p->isDead) {
        ret = 0;
    } else {
        p->lockSoft++;
    }
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_PagesUnlockSoft(Cookfs_Pages *p) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    assert(p->lockSoft > 0);
    p->lockSoft--;
    if (p->isDead == 1) {
        Cookfs_PagesFree(p);
    } else {
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    }
    return 1;
}

void Cookfs_PagesLockExclusive(Cookfs_Pages *p) {
#ifdef TCL_THREADS
    CookfsLog(printf("try to lock exclusive..."));
    Cookfs_RWMutexLockExclusive(p->mx);
    CookfsLog(printf("ok"));
#else
    UNUSED(p);
#endif /* TCL_THREADS */
}

int Cookfs_PagesGetLength(Cookfs_Pages *p) {
    Cookfs_PagesWantRead(p);
    return Cookfs_PgIndexGetLength(p->pagesIndex);
}

#ifdef COOKFS_USECCRYPTO

int Cookfs_PagesIsEncryptionActive(Cookfs_Pages *p) {
    CookfsLog(printf("return: %d", p->isEncryptionActive));
    return p->isEncryptionActive;
}

#endif /* COOKFS_USECCRYPTO */

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetHashAsObj --
 *
 *      Gets the current hashing algorithm for the page object
 *
 * Results:
 *      Tcl_Obj with zero refcount value corresponding to the hash name
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PagesGetHashAsObj(Cookfs_Pages *p) {
    Cookfs_PagesWantRead(p);
    // Due to unknown reason, cppcheck gives here the following:
    //     Argument 'pagehashNames[p->pageHash],-1' to function tcl_NewStringObj
    //     is always -1. It does not matter what value 'p->pageHash'
    //     has. [knownArgument]
    // This sounds like a bug in cppcheck.
    // cppcheck-suppress knownArgument
    return Tcl_NewStringObj(pagehashNames[p->pageHash], -1);
}

void Cookfs_PagesSetHash(Cookfs_Pages *p, Cookfs_HashType pagehash) {
    Cookfs_PagesWantWrite(p);
    p->pageHash = pagehash;
}

int Cookfs_PagesSetHashByObj(Cookfs_Pages *p, Tcl_Obj *pagehash,
    Tcl_Interp *interp)
{
    Cookfs_PagesWantWrite(p);
    return Cookfs_HashFromObj(interp, pagehash, &p->pageHash);
}

int Cookfs_HashFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    Cookfs_HashType *hashPtr)
{
    if (obj == NULL) {
        *hashPtr = COOKFS_HASH_MD5;
    } else {
        int idx;
        if (Tcl_GetIndexFromObj(interp, obj, pagehashNames, "hash", TCL_EXACT,
            &idx) != TCL_OK)
        {
            return TCL_ERROR;
        }
        *hashPtr = (Cookfs_HashType)idx;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesIsReadonly --
 *
 *      Checks if the given pages object is in readonly mode.
 *
 * Results:
 *      Returns a boolean value corresponding to the readonly mode for
 *      given pages object.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

/* Not used as for now

int Cookfs_PagesIsReadonly(Cookfs_Pages *p) {
    return p->fileReadOnly;
}

*/

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetHandle --
 *
 *	Returns pages handle from provided Tcl command name
 *
 * Results:
 *	Pointer to Cookfs_Pages or NULL in case of failure
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Pages *Cookfs_PagesGetHandle(Tcl_Interp *interp, const char *cmdName) {
    Tcl_CmdInfo cmdInfo;

    /* TODO: verify command suffix etc */

    if (!Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
	return NULL;
    }

    /* if we found proper Tcl command, its objClientData is Cookfs_Pages */
    return (Cookfs_Pages *) (cmdInfo.objClientData);
}

#ifdef COOKFS_USECCRYPTO

static int Cookfs_PagesDecryptKey(Cookfs_Pages *p, Tcl_Obj *passObj) {

    CookfsLog(printf("enter, password: [%s]", (passObj == NULL ?
        "NULL" : "SET")));

    // This function can be called ONLY when:
    // * encryption is not active
    // * password has not yet been set
    // * we have read the encrypted key from the archive, but have not yet
    //   decrypted it
    // * we use key-based encryption
    assert(p->isEncryptionActive == 0);
    assert(p->isPasswordSet == 0);
    assert(p->isKeyDecrypted == 0);
    assert(p->encryption == COOKFS_ENCRYPT_KEY || p->encryption == COOKFS_ENCRYPT_KEY_INDEX);

    if (passObj == NULL || !Tcl_GetCharLength(passObj)) {
        CookfsLog(printf("ERROR: password is NULL or an empty string"));
        return TCL_ERROR;
    }

    Tcl_Size passLen;
    unsigned char *passStr = Tcl_GetByteArrayFromObj(passObj, &passLen);

    unsigned int iterations = (p->encryptionLevel <= 15 ?
        (p->encryptionLevel * COOKFS_ENCRYPT_ITERATIONS_K1) :
        (p->encryptionLevel * COOKFS_ENCRYPT_ITERATIONS_K2));

    unsigned char passEncrypted[COOKFS_ENCRYPT_KEY_SIZE];

    CookfsLog(printf("generate an encryption key based on the specified"
        " password"));

    Cookfs_Pbkdf2Hmac(passStr, passLen, p->passwordSalt,
        COOKFS_ENCRYPT_PASSWORD_SALT_SIZE, iterations,
        COOKFS_ENCRYPT_KEY_SIZE, passEncrypted);

    CookfsLog(printf("decrypt the key by encrypted password"));

    memcpy(p->encryptionKey, p->encryptionEncryptedKey,
        COOKFS_ENCRYPT_KEY_AND_HASH_SIZE);

    Cookfs_AesDecryptRaw(p->encryptionKey, COOKFS_ENCRYPT_KEY_AND_HASH_SIZE,
        p->encryptionEncryptedKeyIV, passEncrypted);

    if (memcmp(&p->encryptionKey[COOKFS_ENCRYPT_KEY_SIZE],
        p->encryptionEncryptedKeyIV, COOKFS_ENCRYPT_IV_SIZE) != 0)
    {
        CookfsLog(printf("return: ERROR (failed to validate the unencrypted"
            " key)"));
        return TCL_ERROR;
    }

    p->isPasswordSet = 1;
    p->isEncryptionActive = 1;
    p->isKeyDecrypted = 1;

    CookfsLog(printf("return: ok"));

    return TCL_OK;

}

int Cookfs_PagesSetPassword(Cookfs_Pages *p, Tcl_Obj *passObj) {

    CookfsLog(printf("enter, password: [%s]", (passObj == NULL ?
        "NULL" : "SET")));

    Cookfs_PagesWantWrite(p);

#if defined(COOKFS_USECALLBACKS)
    // ensure all async pages are written
    while(Cookfs_AsyncCompressWait(p, 1)) {};
#endif /* COOKFS_USECALLBACKS */

    if (passObj == NULL || !Tcl_GetCharLength(passObj)) {
        CookfsLog(printf("reset password as it is NULL or an empty string"));
        p->isEncryptionActive = 0;
        return TCL_OK;
    }

    // If we are trying to set a password for key-based encryption
    // and isKeyDecrypted is false, then we have opened the archive
    // without a password and want to unlock the encryption key.
    // We need to should use Cookfs_PagesDecryptKey() for that.
    if (p->encryption == COOKFS_ENCRYPT_KEY && !p->isKeyDecrypted) {
        return Cookfs_PagesDecryptKey(p, passObj);
    }

    if (p->encryption == COOKFS_ENCRYPT_NONE) {
        p->encryption = COOKFS_ENCRYPT_FILE;
    }

    Tcl_Size passLen;
    unsigned char *passStr = Tcl_GetByteArrayFromObj(passObj, &passLen);

    unsigned int iterations = (p->encryptionLevel <= 15 ?
        (p->encryptionLevel * COOKFS_ENCRYPT_ITERATIONS_K1) :
        (p->encryptionLevel * COOKFS_ENCRYPT_ITERATIONS_K2));

    if (p->encryption == COOKFS_ENCRYPT_FILE) {

        CookfsLog(printf("generate an encryption key based on the specified"
            " password"));

        Cookfs_Pbkdf2Hmac(passStr, passLen, p->passwordSalt,
            COOKFS_ENCRYPT_PASSWORD_SALT_SIZE, iterations,
            COOKFS_ENCRYPT_KEY_SIZE, p->encryptionKey);

    } else {

        unsigned char passEncrypted[COOKFS_ENCRYPT_KEY_SIZE];

        CookfsLog(printf("generate an encryption key based on the specified"
            " password"));

        Cookfs_Pbkdf2Hmac(passStr, passLen, p->passwordSalt,
            COOKFS_ENCRYPT_PASSWORD_SALT_SIZE, iterations,
            COOKFS_ENCRYPT_KEY_SIZE, passEncrypted);

        CookfsLog(printf("encrypt key by encrypted password"));

        memcpy(p->encryptionEncryptedKey, p->encryptionKey,
            COOKFS_ENCRYPT_KEY_AND_HASH_SIZE);

        Cookfs_AesEncryptRaw(p->encryptionEncryptedKey,
            COOKFS_ENCRYPT_KEY_AND_HASH_SIZE, p->encryptionEncryptedKeyIV,
            passEncrypted);

        // Do not mark pages as modified if we are in readonly mode.
        if (!p->fileReadOnly) {
            p->pagesUptodate = 0;
        }

    }

    p->isPasswordSet = 1;
    p->isEncryptionActive = 1;

    return TCL_OK;

}

#endif /* COOKFS_USECCRYPTO */



/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesInit --
 *
 *	Initializes new pages instance
 *
 *	Takes file name as Tcl_Obj
 *
 *	If fileReadOnly is non-zero, file must exist and be a readable
 *	cookfs archive; if fileReadOnly is zero, if file is not a
 *	cookfs archive or does not exist, new one is created/appended at
 *	the end of existing file
 *
 *	fileCompression indicates compression for fsindex storage and
 *	newly created pages;
 *	if compresion is set to COOKFS_COMPRESSION_CUSTOM, compressCommand and
 *	decompressCommand need to be specified and cookfs will invoke these
 *	commands when needed
 *
 *	If specified, asyncCompressCommand will be used for custom compression
 *	to handle the 'async compression' contract
 *
 *	fileSignature is only meant for advanced users; it allows specifying
 *	custom pages signature, which can be used to create non-standard
 *	pages storage
 *
 *	If useFoffset is non-zero, foffset is used as indicator to where
 *	end of cookfs archive is; it can be used to store cookfs at location
 *	other than end of file
 *
 * Results:
 *	Pointer to new instance; NULL in case of error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
Cookfs_Pages *Cookfs_PagesInit(Tcl_Interp *interp, Tcl_Obj *fileName,
    int fileReadOnly, int baseCompression, int baseCompressionLevel,
    int currentCompression, int currentCompressionLevel,
    Tcl_Obj *password, int encryptKey, int encryptLevel,
    char *fileSignature, int useFoffset, Tcl_WideInt foffset,
    int isAside,
#if defined(COOKFS_USECALLBACKS)
    int asyncDecompressQueueSize,
    Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand,
    Tcl_Obj *asyncCompressCommand, Tcl_Obj *asyncDecompressCommand,
#endif /* COOKFS_USECALLBACKS */
    Tcl_Obj **err)
{

    UNUSED(err);

#ifdef COOKFS_USECCRYPTO
    if (password != NULL && !Tcl_GetCharLength(password)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": password value must not be an empty string", -1));
        }
        return NULL;
    }
#endif /* COOKFS_USECCRYPTO */

    Cookfs_Pages *rc = (Cookfs_Pages *) ckalloc(sizeof(Cookfs_Pages));
    int i;

    /* initialize basic information */
    rc->lockHard = 0;
    rc->lockSoft = 0;
    rc->isDead = 0;
    rc->interp = interp;
    rc->commandToken = NULL;
    rc->isAside = isAside;
    Cookfs_PagesInitCompr(rc);

#if defined(COOKFS_USECALLBACKS)
    if (Cookfs_SetCompressCommands(rc, compressCommand, decompressCommand, asyncCompressCommand, asyncDecompressCommand) != TCL_OK) {
	if (interp != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG ": unable to initialize compression", -1));
	}
	ckfree((void *) rc);
	return NULL;
    }
#endif /* COOKFS_USECALLBACKS */

#ifdef TCL_THREADS
    /* initialize thread locks */
    rc->mx = Cookfs_RWMutexInit();
    rc->mxCache = NULL;
    rc->mxIO = NULL;
    rc->mxLockSoft = NULL;
    rc->threadId = Tcl_GetCurrentThread();
#endif /* TCL_THREADS */

    /* initialize structure */
    rc->isFirstWrite = 0;
    rc->useFoffset = useFoffset;
    rc->foffset = foffset;
    rc->fileReadOnly = fileReadOnly;
    rc->alwaysCompress = 0;
    if (fileSignature != NULL) {
        memcpy(rc->fileSignature, fileSignature, 7);
    } else {
        // Split the signature into 2 strings so we don't find that whole string
        // when searching for the signature
        memcpy(rc->fileSignature, "CFS", 3);
        memcpy(rc->fileSignature + 3, "0003", 4);
    }
    // Split the stamp into 2 strings so we don't find that whole string
    // when searching for the stamp
    memcpy(rc->fileStamp, "CFS", 3);
    memcpy(rc->fileStamp + 3, "S003", 4);

    /* initialize parameters */
    rc->fileLastOp = COOKFS_LASTOP_UNKNOWN;
    rc->baseCompression = baseCompression;
    rc->baseCompressionLevel = baseCompressionLevel;
    rc->currentCompression = currentCompression;
    rc->currentCompressionLevel = currentCompressionLevel;
#ifdef COOKFS_USECCRYPTO
    rc->encryption = -1;
    rc->isPasswordSet = 0;
    rc->isEncryptionActive = 0;
    rc->isKeyDecrypted = 0;
    if (encryptLevel < 0) {
        rc->encryptionLevel = COOKFS_ENCRYPT_LEVEL_DEFAULT;
    } else if (encryptLevel > COOKFS_ENCRYPT_LEVEL_MAX) {
        rc->encryptionLevel = COOKFS_ENCRYPT_LEVEL_MAX;
    } else {
        rc->encryptionLevel = encryptLevel;
    }
#else
    UNUSED(encryptLevel);
    UNUSED(encryptKey);
    UNUSED(password);
#endif /* COOKFS_USECCRYPTO */
    rc->pagesIndex = NULL;
    rc->dataAsidePages = NULL;
    rc->dataPagesIsAside = isAside;
    rc->dataIndex = NULL;

#if defined(COOKFS_USECALLBACKS)
    rc->asyncPageSize = 0;
    rc->asyncDecompressQueue = 0;
    // TODO: un-hardcode!
    rc->asyncDecompressQueueSize = asyncDecompressQueueSize;

    if ((asyncCompressCommand != NULL) || (asyncDecompressCommand != NULL)) {
	rc->asyncCommandProcess = Tcl_NewStringObj("process", -1);
	rc->asyncCommandWait = Tcl_NewStringObj("wait", -1);
	rc->asyncCommandFinalize = Tcl_NewStringObj("finalize", -1);
	Tcl_IncrRefCount(rc->asyncCommandProcess);
	Tcl_IncrRefCount(rc->asyncCommandWait);
	Tcl_IncrRefCount(rc->asyncCommandFinalize);
    }  else  {
	rc->asyncCommandProcess = NULL;
	rc->asyncCommandWait = NULL;
	rc->asyncCommandFinalize = NULL;
    }
#endif /* COOKFS_USECALLBACKS */

    rc->pageHash = COOKFS_HASH_MD5;
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    rc->zipCmdCrc[0] = Tcl_NewStringObj("::cookfs::getCRC32", -1);
    Tcl_IncrRefCount(rc->zipCmdCrc[0]);
#endif

    /* initialize cache */
    for (i = 0; i < COOKFS_MAX_CACHE_PAGES; i++) {
        rc->cache[i].pageObj = NULL;
        rc->cache[i].pageIdx = -1;
        rc->cache[i].weight = 0;
        rc->cache[i].age = 0;
    }
    rc->cacheSize = 0;
    rc->cacheMaxAge = COOKFS_MAX_CACHE_AGE;

    // initialize file
    rc->fileChannel = NULL;
    rc->fileData = NULL;
    rc->fileLength = -1;
#ifdef _WIN32
    rc->fileHandle = INVALID_HANDLE_VALUE;
#endif

    CookfsLog(printf("Opening file %s as %s with compression %d level %d",
        Tcl_GetStringFromObj(fileName, NULL), (rc->fileReadOnly ? "rb" : "ab+"),
        baseCompression, baseCompressionLevel));

    /* open file for reading / writing */
    CookfsLog(printf("Tcl_FSOpenFileChannel"))

    /* clean up interpreter result prior to calling Tcl_FSOpenFileChannel() */
    if (interp != NULL) {
        Tcl_ResetResult(interp);
    }

    rc->fileChannel = Tcl_FSOpenFileChannel(interp, fileName,
	(rc->fileReadOnly ? "rb" : "ab+"), 0666);

    if (rc->fileChannel == NULL) {
	/* convert error message from previous error */
	if (interp != NULL) {
	    char errorBuffer[4096];
	    const char *errorMessage;
	    errorMessage = Tcl_GetStringResult(interp);
	    if (errorMessage == NULL) {
		/* default error if none is provided */
		errorMessage = "unable to open file";
	    }  else if (strlen(errorMessage) > 4000) {
		/* make sure not to overflow the buffer */
		errorMessage = "unable to open file";
	    }
	    sprintf(errorBuffer, COOKFS_PAGES_ERRORMSG ": %s", errorMessage);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(errorBuffer, -1));
	}
        CookfsLog(printf("cleaning up"))
	Cookfs_PagesFini(rc);
	return NULL;
    }

    if (!rc->fileReadOnly) {
        CookfsLog(printf("skip mmap - file is not in readonly mode"));
        goto skipMMap;
    }

    void *handle;
    if (Tcl_GetChannelHandle(rc->fileChannel, TCL_READABLE, &handle)
        != TCL_OK)
    {
        CookfsLog(printf("skip mmap - could not get handle from chan"));
        goto skipMMap;
    }

    rc->fileLength = Tcl_Seek(rc->fileChannel, 0, SEEK_END);

    if (rc->fileLength < 0) {
        CookfsLog(printf("skip mmap - failed to get file size"));
        goto skipMMap;
    }

    if (rc->fileLength == 0) {
        CookfsLog(printf("skip mmap - could not mmap an empty file"));
        goto skipMMap;
    }

#ifdef _WIN32

    rc->fileHandle = CreateFileMappingW((HANDLE)handle, NULL, PAGE_READONLY,
        0, 0, NULL);

    if (rc->fileHandle == INVALID_HANDLE_VALUE) {
        CookfsLog(printf("skip mmap - CreateFileMappingW() failed"));
        goto skipMMap;
    }

    rc->fileData = (unsigned char *)MapViewOfFile(rc->fileHandle, FILE_MAP_READ,
        0, 0, 0);

    if (rc->fileData == NULL) {
        CloseHandle(rc->fileHandle);
        rc->fileHandle = INVALID_HANDLE_VALUE;
        CookfsLog(printf("skip mmap - MapViewOfFile() failed"));
        goto skipMMap;
    }

#else

    rc->fileData = (unsigned char *)mmap(0, rc->fileLength, PROT_READ,
        MAP_SHARED, (ptrdiff_t)handle, 0);

    if (rc->fileData == MAP_FAILED) {
        rc->fileData = NULL;
        CookfsLog(printf("skip mmap - mmap() failed"));
        goto skipMMap;
    }

#endif /* _WIN32 */

    CookfsLog(printf("the file has been successfully mapped to memory"));

    CookfsLog(printf("close channel"));
    Tcl_Close(interp, rc->fileChannel);

    rc->fileChannel = NULL;

skipMMap:

    /* read index or fail */
    Cookfs_PagesLockWrite(rc, NULL);
    Tcl_Obj *index_err = NULL;
    int is_abort = 0;
    int indexRead = CookfsReadIndex(interp, rc, password, &is_abort, &index_err);
    Cookfs_PagesUnlock(rc);
    if (!indexRead) {
	if (rc->fileReadOnly || is_abort) {
	    if (index_err != NULL) {
	        if (interp != NULL) {
	            Tcl_SetObjResult(interp, index_err);
	        } else {
	            Tcl_BounceRefCount(index_err);
	        }
	    }
	    goto error;
	}
	rc->isFirstWrite = 1;
	// We can safely use here rc->fileChannel, since we have already
	// checked for read-only mode above and returned an error for
	// this case. If we are here, we can be sure that the read-write
	// mode is active. In read-write mode we have an open channel,
	// but have not a memory mapped file.
	rc->dataInitialOffset = Tcl_Seek(rc->fileChannel, 0, SEEK_END);
	rc->pagesUptodate = 0;
	rc->indexChanged = 1;
	rc->shouldTruncate = 1;
	CookfsLog(printf("Index not read!"));
	// Reset the interpreter error message from CookfsReadIndex().
	// We are going to create a new archive.
        if (interp != NULL) {
            Tcl_ResetResult(interp);
        }
        if (index_err != NULL) {
            Tcl_BounceRefCount(index_err);
        }
    }  else  {
	rc->pagesUptodate = 1;
	rc->indexChanged = 0;
	rc->shouldTruncate = 1;
    }

#ifdef COOKFS_USECCRYPTO

    // If we are opening an existing archive, the encryption should already be
    // initialized.
    if (rc->encryption != -1) {
        CookfsLog(printf("encryption type has already been initialized"))
        goto skipEncryption;
    }

    if (password == NULL) {
        if (encryptKey) {
            rc->encryption = COOKFS_ENCRYPT_KEY;
        } else {
            rc->encryption = COOKFS_ENCRYPT_NONE;
        }
    } else {
        if (encryptKey) {
            rc->encryption = COOKFS_ENCRYPT_KEY_INDEX;
        } else {
            rc->encryption = COOKFS_ENCRYPT_FILE;
        }
    }

    if (rc->encryption != COOKFS_ENCRYPT_NONE) {

        Cookfs_RandomGenerate(interp, rc->passwordSalt,
            COOKFS_ENCRYPT_PASSWORD_SALT_SIZE);

        if (rc->encryption != COOKFS_ENCRYPT_FILE) {

            Cookfs_RandomGenerate(interp, rc->encryptionKey,
                COOKFS_ENCRYPT_KEY_SIZE);

            Cookfs_RandomGenerate(interp, rc->encryptionEncryptedKeyIV,
                COOKFS_ENCRYPT_IV_SIZE);

            memcpy(&rc->encryptionKey[COOKFS_ENCRYPT_KEY_SIZE],
                rc->encryptionEncryptedKeyIV, COOKFS_ENCRYPT_IV_SIZE);

            rc->isKeyDecrypted = 1;

        }

    }

skipEncryption:

    if (!rc->isPasswordSet && password != NULL) {
        Cookfs_PagesLockWrite(rc, NULL);
        int res = Cookfs_PagesSetPassword(rc, password);
        Cookfs_PagesUnlock(rc);
        if (res != TCL_OK) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": could not decrypt the encryption key with the specified"
                " password", -1));
            goto error;
        }
    }

#endif /* COOKFS_USECCRYPTO */

    if (rc->pagesIndex == NULL) {
        CookfsLog(printf("pgindex is not defined, initialize a new one"));
        rc->pagesIndex = Cookfs_PgIndexInit(0);
    }

    if (rc->baseCompression == -1 || rc->baseCompressionLevel == -1) {
        // Cookfs_CompressionFromObj() returns the default compression
        // type/level when NULL is passed as input compression name.
        Cookfs_CompressionFromObj(NULL, NULL, &rc->baseCompression,
            &rc->baseCompressionLevel);
        CookfsLog(printf("base compression is not defined, setting to"
            " the default: compression: %d level %d", rc->baseCompression,
            rc->baseCompressionLevel));
    } else {
        CookfsLog(printf("base compression is defined: compression: %d"
            " level %d", rc->baseCompression, rc->baseCompressionLevel));
    }

    if (rc->currentCompression == -1 || rc->currentCompressionLevel == -1) {
        rc->currentCompression = rc->baseCompression;
        rc->currentCompressionLevel = rc->baseCompressionLevel;
        CookfsLog(printf("current compression is not defined, setting to"
            " the same values as base: compression: %d level %d",
            rc->currentCompression, rc->currentCompressionLevel));
    } else {
        CookfsLog(printf("current compression is defined: compression: %d"
            " level %d", rc->baseCompression, rc->baseCompressionLevel));
    }

    return rc;

error:

    rc->pagesUptodate = 1;
    rc->indexChanged = 0;
    rc->shouldTruncate = 0;
    Cookfs_PagesFini(rc);
    return NULL;

}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesClose --
 *
 *	Write and close cookfs pages object; object is not yet deleted
 *
 * Results:
 *	Offset to end of data
 *
 * Side effects:
 *	Any attempts to write afterwards might end up in segfault
 *
 *----------------------------------------------------------------------
 */

Tcl_WideInt Cookfs_PagesClose(Cookfs_Pages *p) {

    if (p->fileChannel == NULL) {
        if (p->fileData == NULL) {
            // We have neither a channel nor a mapped file. Just return.
            goto done;
        } else {
            // We have a mapped file. We are in read-only mode if we have
            // a mapped file. Skip saving file changes and go to
            // the unmapping step.
            goto unmap;
        }
    }

    CookfsLog(printf("pages up to date = %d, Index changed = %d", p->pagesUptodate, p->indexChanged))
    /* if changes were made, save them to disk */
    if ((!p->pagesUptodate) || (p->indexChanged)) {
        unsigned char buf[COOKFS_SUFFIX_BYTES];

#if defined(COOKFS_USECALLBACKS)
        /* ensure all async pages are written */
        while(Cookfs_AsyncCompressWait(p, 1)) {};
        while(Cookfs_AsyncDecompressWait(p, -1, 1)) {};
        Cookfs_AsyncCompressFinalize(p);
        Cookfs_AsyncDecompressFinalize(p);
#endif /* COOKFS_USECALLBACKS */

        // Add initial stamp if needed
        Cookfs_PageAddStamp(p, 0);

#ifdef COOKFS_USECCRYPTO
        // Reset encryption if no password is set. For key-based encryption
        // we just don't know how to encrypt key. For file-based encryption,
        // this is the default and will be used next time a password is set.
        if (!p->isPasswordSet) {
            p->encryption = COOKFS_ENCRYPT_NONE;
        }

        buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] = (p->encryption & 7) |
            ((p->encryptionLevel << 3) & 0xf8);

        CookfsLog(printf("level: %d", p->encryptionLevel));

        CookfsLog(printf("write encryption: %s level %d",
            ((buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] & 7)
                == COOKFS_ENCRYPT_NONE ? "NONE" :
            ((buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] & 7)
                == COOKFS_ENCRYPT_FILE ? "FILE" :
            ((buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] & 7)
                == COOKFS_ENCRYPT_KEY ? "KEY" :
            ((buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] & 7)
                == COOKFS_ENCRYPT_KEY_INDEX ? "KEY_INDEX" :
            "UNKNOWN")))),
            (buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] >> 3) & 0x1f));

        // If the encryption mode is not COOKFS_ENCRYPT_KEY_INDEX, then we
        // don't want to encrypt the indexes. Disable encryption for this case.
        // On the other hand, explicitly enable encryption.
        if (p->encryption != COOKFS_ENCRYPT_KEY_INDEX) {
            CookfsLog(printf("disable encryption as it is not"
                " COOKFS_ENCRYPT_KEY_INDEX"));
            p->isEncryptionActive = 0;
        } else {
            CookfsLog(printf("ENABLE encryption for indexes"));
            p->isEncryptionActive = 1;
        }
#else
        buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] =
            (COOKFS_ENCRYPT_LEVEL_DEFAULT << 3) & 0xf8;
#endif

        // Fill in the basic data for the file system suffix
        buf[COOKFS_SUFFIX_OFFSET_BASE_COMPRESSION] = p->baseCompression;
        buf[COOKFS_SUFFIX_OFFSET_BASE_LEVEL] = p->baseCompressionLevel;
        memcpy(&buf[COOKFS_SUFFIX_OFFSET_SIGNATURE], p->fileSignature,
            COOKFS_SIGNATURE_LENGTH);

        // Make sure we use base compression and compression level for
        // pgindex/fsindex data
        p->currentCompression = p->baseCompression;
        p->currentCompressionLevel = p->baseCompressionLevel;

        // First, we get a dump of pages index. Then we add the dump of
        // the pages index and fsindex as additional pages to the pages index.
        // This will allow us to use Cookfs_Write...() functions to write
        // data and get the properties of the written data (compression,
        // compression level, size of the written data)

        if (Cookfs_PagesGetLength(p) > 0) {

            int indexSizeCompressed;
            int indexSizeUncompressed;

            CookfsLog(printf("write pgindex data..."));
            Cookfs_PageObj pgindexExportObj = Cookfs_PgIndexExport(p->pagesIndex);
            indexSizeUncompressed = Cookfs_PageObjSize(pgindexExportObj);

            Cookfs_MD5(pgindexExportObj->buf, indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_HASH]);

            int pgindexIndex = Cookfs_PgIndexAddPage(p->pagesIndex, 0, 0, 0,
                -1, indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_HASH]);

            indexSizeCompressed = Cookfs_WritePageObj(p, pgindexIndex,
                pgindexExportObj, &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_HASH]);

            Cookfs_PageObjBounceRefCount(pgindexExportObj);

            if (indexSizeCompressed < 0) {
                Tcl_Panic("Unable to compress pgindex");
            }

            buf[COOKFS_SUFFIX_OFFSET_PGINDEX_COMPRESSION] =
                Cookfs_PgIndexGetCompression(p->pagesIndex, pgindexIndex) & 0xFF;
            buf[COOKFS_SUFFIX_OFFSET_PGINDEX_LEVEL] =
                Cookfs_PgIndexGetCompression(p->pagesIndex, pgindexIndex) & 0xFF;

            Cookfs_Int2Binary(&indexSizeCompressed,
                &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_COMPR], 1);
            Cookfs_Int2Binary(&indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_UNCOMPR], 1);

        } else {
            CookfsLog(printf("pgindex data is empty"));
            // Fill everything related to pgindex by zeros to avoid undefined
            // behavior.
            memset(&buf[COOKFS_SUFFIX_OFFSET_PGINDEX_COMPRESSION], 0,
                1 + 1 + 16 + 4 + 4);
        }

        if (p->dataIndex != NULL) {

            int indexSizeCompressed;
            int indexSizeUncompressed;

            CookfsLog(printf("write fsindex data..."));
            indexSizeUncompressed = Cookfs_PageObjSize(p->dataIndex);

            Cookfs_MD5(p->dataIndex->buf, indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_HASH]);

            int fsindexIndex = Cookfs_PgIndexAddPage(p->pagesIndex, 0, 0, 0,
                -1, indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_HASH]);

            indexSizeCompressed = Cookfs_WritePageObj(p, fsindexIndex,
                p->dataIndex, &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_HASH]);
            if (indexSizeCompressed < 0) {
                Tcl_Panic("Unable to compress fsindex");
            }

            buf[COOKFS_SUFFIX_OFFSET_FSINDEX_COMPRESSION] =
                Cookfs_PgIndexGetCompression(p->pagesIndex, fsindexIndex) & 0xFF;
            buf[COOKFS_SUFFIX_OFFSET_FSINDEX_LEVEL] =
                Cookfs_PgIndexGetCompression(p->pagesIndex, fsindexIndex) & 0xFF;

            Cookfs_Int2Binary(&indexSizeCompressed,
                &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_COMPR], 1);
            Cookfs_Int2Binary(&indexSizeUncompressed,
                &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_UNCOMPR], 1);

        } else {
            CookfsLog(printf("fsindex data is empty"));
            // Fill everything related to pgindex by zeros to avoid undefined
            // behavior.
            memset(&buf[COOKFS_SUFFIX_OFFSET_FSINDEX_COMPRESSION], 0,
                1 + 1 + 16 + 4 + 4);
        }

        CookfsLog(printf("offset write suffix: %" TCL_LL_MODIFIER "d",
            Tcl_Tell(p->fileChannel)));

        // CookfsDump(buf, COOKFS_SUFFIX_BYTES);
        Tcl_Write(p->fileChannel, (char *)buf, COOKFS_SUFFIX_BYTES);

#ifdef COOKFS_USECCRYPTO
        if (p->encryption != COOKFS_ENCRYPT_NONE && p->isPasswordSet) {

            CookfsLog(printf("writing encryption data: password salt"));
            Tcl_Write(p->fileChannel, (char *)p->passwordSalt,
                COOKFS_ENCRYPT_PASSWORD_SALT_SIZE);

            if (p->encryption != COOKFS_ENCRYPT_FILE) {

                CookfsLog(printf("writing encryption data: key IV"));
                Tcl_Write(p->fileChannel, (char *)p->encryptionEncryptedKeyIV,
                    COOKFS_ENCRYPT_IV_SIZE);

                CookfsLog(printf("writing encryption data: key"));
                Tcl_Write(p->fileChannel, (char *)p->encryptionEncryptedKey,
                    COOKFS_ENCRYPT_KEY_AND_HASH_SIZE);

            }

        } else {
            CookfsLog(printf("don't write encryption data: %s",
                (p->encryption == COOKFS_ENCRYPT_NONE ? "encryption is NONE" :
                "password is not set")));
        }
#endif /* COOKFS_USECCRYPTO */

        p->foffset = Tcl_Tell(p->fileChannel);

        CookfsTruncateFileIfNeeded(p, p->foffset);

        // Add final stamp if needed
        Cookfs_PageAddStamp(p, p->foffset);

    }

    /* close file channel */
    if (p->fileChannel != NULL) {
        CookfsLog(printf("closing channel"));
        Tcl_Close(NULL, p->fileChannel);
        p->fileChannel = NULL;
        goto done;
    }

unmap:

    CookfsLog(printf("unmap file"));

#ifdef _WIN32
    UnmapViewOfFile(p->fileData);
    CloseHandle(p->fileHandle);
    p->fileHandle = INVALID_HANDLE_VALUE;
#else
    munmap(p->fileData, p->fileLength);
#endif /* _WIN32 */

    p->fileData = NULL;

done:

    CookfsLog(printf("return: %d", ((int) ((p->foffset) & 0x7fffffff))));

    return p->foffset;

}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesFini --
 *
 *	Cleanup pages instance
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void Cookfs_PagesFree(Cookfs_Pages *p) {
    CookfsLog(printf("Cleaning up pages"))
#ifdef TCL_THREADS
    CookfsLog(printf("Cleaning up thread locks"));
    Cookfs_RWMutexFini(p->mx);
    Tcl_MutexFinalize(&p->mxCache);
    Tcl_MutexFinalize(&p->mxIO);
    Tcl_MutexUnlock(&p->mxLockSoft);
    Tcl_MutexFinalize(&p->mxLockSoft);
#endif /* TCL_THREADS */
    /* clean up storage */
    ckfree((void *) p);
}

void Cookfs_PagesFini(Cookfs_Pages *p) {
    int i;

    if (p->isDead == 1) {
        return;
    }

    if (p->lockHard) {
        CookfsLog(printf("could not remove locked object"));
        return;
    }

    Cookfs_PagesLockExclusive(p);

    CookfsLog(printf("enter"));

    CookfsLog(printf("aquire mutex"));
    // By acquisition the lockSoft mutex, we will be sure that no other
    // thread calls Cookfs_PagesUnlockSoft() that can release this object
    // while this function is running.
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    p->isDead = 1;

    Cookfs_PagesClose(p);

    /* clean up add-aside pages */
    if (p->dataAsidePages != NULL) {
        CookfsLog(printf("Release aside pages"));
	Cookfs_PagesFini(p->dataAsidePages);
        CookfsLog(printf("Aside pages have been released"));
    }

    /* clean up cache */
    CookfsLog(printf("Cleaning up cache"))
    for (i = 0; i < p->cacheSize; i++) {
	if (p->cache[i].pageObj != NULL) {
	    Cookfs_PageObjDecrRefCount(p->cache[i].pageObj);
	}
    }

#if defined(COOKFS_USECALLBACKS)
    if (p->asyncCommandProcess != NULL) {
	Tcl_DecrRefCount(p->asyncCommandProcess);
    }
    if (p->asyncCommandWait != NULL) {
	Tcl_DecrRefCount(p->asyncCommandWait);
    }
    if (p->asyncCommandFinalize != NULL) {
	Tcl_DecrRefCount(p->asyncCommandFinalize);
    }
#endif /* COOKFS_USECALLBACKS */

    /* clean up compression information */
    Cookfs_PagesFiniCompr(p);

#ifdef USE_VFS_COMMANDS_FOR_ZIP
    /* clean up zipCmdCrc command */
    Tcl_DecrRefCount(p->zipCmdCrc[0]);
#endif

    /* clean up index */
    CookfsLog(printf("Cleaning up index data"))
    if (p->dataIndex != NULL) {
        Cookfs_PageObjDecrRefCount(p->dataIndex);
    }

    /* clean up pages data */
    CookfsLog(printf("Cleaning up pages index"))
    if (p->pagesIndex != NULL) {
        Cookfs_PgIndexFini(p->pagesIndex);
    }

    if (p->commandToken != NULL) {
        CookfsLog(printf("Cleaning tcl command"));
        Tcl_DeleteCommandFromToken(p->interp, p->commandToken);
    } else {
        CookfsLog(printf("No tcl command"));
    }

    // Unlock pages now. It is possible that some threads are waiting for
    // read/write events. Let them go on and fail because of a dead object.
    Cookfs_PagesUnlock(p);

    if (p->lockSoft) {
        CookfsLog(printf("The page object is soft-locked"))
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&p->mxLockSoft);
#endif /* TCL_THREADS */
    } else {
        Cookfs_PagesFree(p);
    }
}

static Tcl_WideInt CookfsSearchString(const unsigned char *haystack,
    Tcl_Size haystackSize, const unsigned char *needle, Tcl_Size needleSize,
    int isFirstMatch)
{

    // CookfsLog(printf("haystack %p size %" TCL_SIZE_MODIFIER "d; needle %p"
    //     " size %" TCL_SIZE_MODIFIER "d; isFirstMatch: %d", (void *)haystack,
    //     haystackSize, (void *)needle, needleSize, isFirstMatch));

    Tcl_WideInt rc = -1;

    Tcl_Size remainingSize = haystackSize;
    const unsigned char *offset = haystack;

    while ((remainingSize >= needleSize) && ((offset =
        (unsigned char *)memchr(offset, needle[0],
        remainingSize - needleSize + 1)) != NULL))
    {

        // CookfsLog(printf("found byte at offset: %p (%d)", (void *)offset,
        //     (int)(offset - haystack)));

        if (memcmp(offset, needle, needleSize) == 0) {
            // CookfsLog(printf("found a match"));
            rc = (offset - haystack);
            if (isFirstMatch) {
                goto done;
            }
        }

        offset++;
        remainingSize = haystackSize - (offset - haystack);

        // CookfsLog(printf("remainingSize: %" TCL_SIZE_MODIFIER "d",
        //     remainingSize));

    }

done:

    // CookfsLog(printf("return: %" TCL_LL_MODIFIER "d", rc));
    return rc;

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageSearchStamp --
 *
 *      Trying to find the cookfs stamp that should be located in front of the archive
 *
 * Results:
 *      Expected file size if the stamp is found, or -1 otherwise
 *
 * Side effects:
 *      Changing the current offset in the channel
 *
 *----------------------------------------------------------------------
 */

static Tcl_WideInt Cookfs_PageSearchStamp(Cookfs_Pages *p) {

    CookfsLog(printf("enter"));

    Tcl_WideInt size = -1;

    // If file is memory mapped, then search in buffer
    if (p->fileChannel == NULL) {

        Tcl_Size maxSearch = (p->fileLength > COOKFS_SEARCH_STAMP_MAX_READ ?
            COOKFS_SEARCH_STAMP_MAX_READ : p->fileLength);

        size = CookfsSearchString(p->fileData, maxSearch, p->fileStamp,
            COOKFS_SIGNATURE_LENGTH, 1);

        // if we found something. However, we should check if the found offset
        // + signature length + wideint (8) size does not exceed the file size
        // to avoid buffer overflow.
        if (size >= 0 && p->fileLength >= (size + COOKFS_SIGNATURE_LENGTH + 8)) {
            Cookfs_Binary2WideInt(&p->fileData[size + COOKFS_SIGNATURE_LENGTH],
                &size, 1);
            CookfsLog(printf("return the size: %" TCL_LL_MODIFIER "d", size));
        } else {
            CookfsLog(printf("lookup total %" TCL_SIZE_MODIFIER "d bytes"
                " and could not find the stamp", maxSearch));
        }

        goto done;

    }

    unsigned char *buf = NULL;

    buf = ckalloc(COOKFS_SEARCH_STAMP_CHUNK);
    if (buf == NULL) {
        CookfsLog(printf("failed to alloc"));
        goto error;
    }

    if (Tcl_Seek(p->fileChannel, 0, SEEK_SET) == -1) {
        CookfsLog(printf("failed to seek"));
        goto error;
    }

    Tcl_WideInt read = 0;
    int bufSize = 0;
    int i;

    while (!Tcl_Eof(p->fileChannel) && (read < COOKFS_SEARCH_STAMP_MAX_READ)) {

        int wantToRead = COOKFS_SEARCH_STAMP_CHUNK - bufSize;

        if ((wantToRead + read) > COOKFS_SEARCH_STAMP_MAX_READ) {
            wantToRead = COOKFS_SEARCH_STAMP_MAX_READ - read;
        }

        CookfsLog(printf("try to read %d bytes", wantToRead));

        int readCount = Tcl_Read(p->fileChannel, (char *)buf + bufSize,
            wantToRead);

        if (!readCount) {
            CookfsLog(printf("got zero bytes, continue"));
            continue;
        }

        // A negative value of bytes read indicates an error. Stop processing
        // in this case.
        if (readCount < 0) {
            goto error;
        }

        CookfsLog(printf("got %d bytes", readCount));

        read += readCount;
        bufSize += readCount;

        // Do not look for the last 20 bytes, as a situation may arise where
        // the stamp byte is at the very end of the buffer and the WideInt
        // that should come after the stamp is not read.
        i = CookfsSearchString(buf, bufSize - 20, p->fileStamp,
            COOKFS_SIGNATURE_LENGTH, 1);

        if (i > 0) {
            goto found;
        }

        CookfsLog(printf("stamp is not found yet"));

        // Leave the last 20 bytes in the buffer. But copy it only if bufSize is 40+ bytes.
        // If it is less than 40 bytes, the destination area will overlap the source area.
        // Thus, if bufSize is less than 40 bytes, the next read round will simply add
        // new bytes from the file to the buffer.
        if (bufSize > (20 * 2)) {
            memcpy(buf, &buf[bufSize - 20], 20);
            bufSize = 20;
        }

    }

    CookfsLog(printf("read total %" TCL_LL_MODIFIER "d bytes and could"
        " not find the stamp", read));

    goto error;

found:

    Cookfs_Binary2WideInt(&buf[i + COOKFS_SIGNATURE_LENGTH], &size, 1);
    CookfsLog(printf("return the size: %" TCL_LL_MODIFIER "d", size));

error:

    if (buf != NULL) {
        ckfree(buf);
    }

done:

    return size;

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageAddStamp --
 *
 *      Adds a stamp before archive
 *
 * Results:
 *      TCL_OK on success or TCL_ERROR on failure
 *
 * Side effects:
 *      Sets the current offset in the channel immediately following the stamp
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PageAddStamp(Cookfs_Pages *p, Tcl_WideInt size) {

    CookfsLog(printf("enter, size: %" TCL_LL_MODIFIER "d", size));

    unsigned char sizeBin[8]; // 64-bit WideInt
    Cookfs_WideInt2Binary(&size, sizeBin, 1);

    if (size == 0) {
        if (!p->isFirstWrite) {
            CookfsLog(printf("return: is not the first write"));
            return TCL_OK;
        }
        CookfsLog(printf("write initial stamp"));
        if (Tcl_Seek(p->fileChannel, 0, SEEK_END) == -1) {
            CookfsLog(printf("return error, failed to seek"));
            return TCL_ERROR;
        }
        if (Tcl_Write(p->fileChannel, (const char *)p->fileStamp,
            COOKFS_SIGNATURE_LENGTH) != COOKFS_SIGNATURE_LENGTH)
        {
            CookfsLog(printf("return error, failed to write signature"));
            return TCL_ERROR;
        }
        if (Tcl_Write(p->fileChannel, (const char *)sizeBin, 8) != 8)
        {
            CookfsLog(printf("return error, failed to write size"));
            return TCL_ERROR;
        }
        p->dataInitialOffset += COOKFS_SIGNATURE_LENGTH + 8; // 7+8 = 15
        p->isFirstWrite = 0;
        // We're already in position for the next file write
        p->fileLastOp = COOKFS_LASTOP_WRITE;
    } else {
        CookfsLog(printf("write final stamp"));
        if (Tcl_Seek(p->fileChannel, p->dataInitialOffset - 8, SEEK_SET)
            == -1)
        {
            CookfsLog(printf("return error, failed to seek"));
            return TCL_ERROR;
        }
        if (Tcl_Write(p->fileChannel, (const char *)sizeBin, 8) != 8)
        {
            CookfsLog(printf("return error, failed to write size"));
            return TCL_ERROR;
        }
    }

    CookfsLog(printf("ok"));
    return TCL_OK;

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageAdd --
 *
 *      Same as Cookfs_PageAddRaw, but uses Cookfs_PageObj as the page data source.
 *
 * Results:
 *      Index that can be used in subsequent calls to Cookfs_PageGet()
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PageAdd(Cookfs_Pages *p, Cookfs_PageObj dataObj, Tcl_Obj **err) {
    return Cookfs_PageAddRaw(p, dataObj->buf, Cookfs_PageObjSize(dataObj), err);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageAddTclObj --
 *
 *      Same as Cookfs_PageAddRaw, but uses Tcl_Obj as the page data source.
 *
 * Results:
 *      Index that can be used in subsequent calls to Cookfs_PageGet()
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PageAddTclObj(Cookfs_Pages *p, Tcl_Obj *dataObj, Tcl_Obj **err) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(dataObj, &size);
    return Cookfs_PageAddRaw(p, bytes, size, err);
}

void Cookfs_PagesCalculateHash(Cookfs_Pages *p, unsigned char *bytes,
    Tcl_Size size, unsigned char *output)
{

    if (p->pageHash == COOKFS_HASH_CRC32) {

        CookfsLog(printf("calc crc32, data: %p size %" TCL_SIZE_MODIFIER "d",
            (void *)bytes, size));

        int b[4] = { 0, 0, (int)size, 0 };
#ifdef USE_VFS_COMMANDS_FOR_ZIP
        Tcl_Obj *data;
        Tcl_Obj *prevResult;

        Tcl_Obj *dataObj = Tcl_NewByteArrayObj(bytes, size);
        Tcl_IncrRefCount(dataObj);
        p->zipCmdCrc[1] = dataObj;

        prevResult = Tcl_GetObjResult(p->interp);
        Tcl_IncrRefCount(prevResult);

        if (Tcl_EvalObjv(p->interp, 2, p->zipCmdCrc, TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) == TCL_OK) {
            data = Tcl_GetObjResult(p->interp);
            Tcl_IncrRefCount(data);
            Tcl_GetIntFromObj(NULL, data, &b[3]);
            Tcl_DecrRefCount(data);
        }

        p->zipCmdCrc[1] = NULL;
        Tcl_DecrRefCount(dataObj);

        Tcl_SetObjResult(p->interp, prevResult);
        Tcl_DecrRefCount(prevResult);
#else
        b[3] = (int)Tcl_ZlibCRC32(Tcl_ZlibCRC32(0,NULL,0), bytes, size);
#endif
        /* copy to checksum memory */
        Cookfs_Int2Binary(b, output, 4);
    }  else  {

        CookfsLog(printf("calc md5, data: %p size %" TCL_SIZE_MODIFIER "d",
            (void *)bytes, size));

        Cookfs_MD5(bytes, size, output);

    }

    CookfsLog(printf("return: [" PRINTF_MD5_FORMAT "]", PRINTF_MD5_VAR(output)));

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageAddRaw --
 *
 *	Add new page or return index of existing page, if page with
 *	same content already exists
 *
 * Results:
 *	Index that can be used in subsequent calls to Cookfs_PageGet()
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PageAddRaw(Cookfs_Pages *p, unsigned char *bytes, int objLength,
    Tcl_Obj **err)
{
    Cookfs_PagesWantWrite(p);

    int idx;
    unsigned char md5sum[16];

    CookfsLog(printf("new page with [%d] bytes", objLength))

    Cookfs_PagesCalculateHash(p, bytes, objLength, md5sum);

    /* see if this entry already exists */
    CookfsLog(printf("Matching page (size=%d bytes)", objLength));
    idx = 0;
    while (Cookfs_PgIndexSearchByMD5(p->pagesIndex, md5sum, objLength, &idx)) {

#ifdef COOKFS_USECCRYPTO
        if (Cookfs_PgIndexGetEncryption(p->pagesIndex, idx) !=
            p->isEncryptionActive)
        {
            CookfsLog(printf("found page#%d that matches the hash, but"
                " encryption does not match", idx));
            goto next;
        }
#endif /* COOKFS_USECCRYPTO */

        /* even if MD5 checksums are the same, we still need to validate contents of the page */
        Cookfs_PageObj otherPageData;
        int isMatched = 0;

        CookfsLog(printf("Comparing page %d", idx));

        /* use -1000 weight as it is temporary page and we don't really need it in cache */
        otherPageData = Cookfs_PageGet(p, idx, -1000, err);
        // Do not increment refcount for otherPageData, Cookfs_PageGet()
        // returns a page with refcount=1.

        /* fail in case when decompression is not available
         *
         * if page with same checksum was found, verify its contents as we
         * do not rely on MD5 checksum - this avoids issue with MD5 collissions */
        if (otherPageData == NULL) {
#ifdef COOKFS_USECCRYPTO
            // If we are in encrypted mode, it is possible that we failed
            // to decrypt some page. Let's ignore this error. But we need to
            // free possible error message.
            //
            // Here we are checking only for COOKFS_ENCRYPT_FILE mode because
            // a decryption error is only possible in this case.
            // At the beginning of the loop, we check if the encryption mode
            // of the added page matches the encryption mode of the page found
            // by the MD5 match. If we found an encrypted page, the added page
            // should also be encrypted. This means that encryption
            // is currently enabled and a password has been set. Which means
            // we should be able to decrypt the page found by the MD5 match.
            if (p->encryption == COOKFS_ENCRYPT_FILE) {
                CookfsLog(printf("ignore the error in encryption mode"));
                if (err != NULL && *err != NULL) {
                    Tcl_BounceRefCount(*err);
                    *err = NULL;
                }
                goto next;
            }
#endif /* COOKFS_USECCRYPTO */
            CookfsLog(printf("unable to verify page with same MD5 checksum"));
            return -1;
        } else {
            // We are sure we will go out of bounds here because
            // Cookfs_PgIndexSearchByMD5() returns true only if the page
            // matches not only MD5, but also if its size is equal to
            // objLength.
            if (memcmp(bytes, otherPageData->buf, objLength) != 0) {
                CookfsLog(printf("the data doesn't match"));
            } else {
                isMatched = 1;
            }
            Cookfs_PageObjDecrRefCount(otherPageData);
        }

        if (isMatched) {
            CookfsLog(printf("Matched page (size=%d bytes) as %d", objLength, idx));
            if (p->dataPagesIsAside) {
                idx |= COOKFS_PAGES_ASIDE;
           }
           return idx;
        }
#ifdef COOKFS_USECCRYPTO
next:
#endif /* COOKFS_USECCRYPTO */
        idx++;
    }

    /* if this page has an aside page set up, ask it to add new page */
    if (p->dataAsidePages != NULL) {
	CookfsLog(printf("Sending add command to asidePages"))
        if (!Cookfs_PagesLockWrite(p->dataAsidePages, NULL)) {
            return -1;
        }
	int rc = Cookfs_PageAddRaw(p->dataAsidePages, bytes, objLength, err);
	Cookfs_PagesUnlock(p->dataAsidePages);
	return rc;
    }

    /* if file is read only, return page can't be added */
    if (p->fileReadOnly) {
	return -1;
    }

    // Real compression, compressionLevel and sizeUncompressed will be updated
    // by Cookfs_WritePage()
#ifdef COOKFS_USECCRYPTO
    idx = Cookfs_PgIndexAddPage(p->pagesIndex, 0, 0, p->isEncryptionActive,
        -1, objLength, md5sum);
#else
    idx = Cookfs_PgIndexAddPage(p->pagesIndex, 0, 0, 0,
        -1, objLength, md5sum);
#endif /* COOKFS_USECCRYPTO */

#if defined(COOKFS_USECALLBACKS)
    if (!Cookfs_AsyncPageAdd(p, idx, bytes, objLength)) {
#endif /* COOKFS_USECALLBACKS */
        int dataSize = Cookfs_WritePage(p, idx, bytes, objLength, md5sum, NULL);
        if (dataSize < 0) {
            /* TODO: if writing failed, we can't be certain of archive state - need to handle this at vfs layer somehow */
            CookfsLog(printf("Unable to compress page"))
            return -1;
        }
#if defined(COOKFS_USECALLBACKS)
    }
#endif /* COOKFS_USECALLBACKS */

    p->pagesUptodate = 0;

    if (p->dataPagesIsAside) {
	idx |= COOKFS_PAGES_ASIDE;
    }

    return idx;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGet --
 *
 *      Gets contents of a page at specified index and sets its weight in cache
 *
 * Results:
 *      Cookfs_PageObj with page data and refcount=1. It is important to return
 *      a page with non-zero refcount because this page is also managed by
 *      cache.
 *
 *      Let's imagine that some code called Cookfs_PageGet and got a page.
 *      Then after error checking, this code will call
 *      Cookfs_PageObjIncrRefCount() to lock the page. However, it is possible
 *      that this page will be removed from cache until refcount is increased.
 *      The page will be freed before its refcount is incremented.
 *      So, to avoid that, Cookfs_PageGet() shoul pre-increase refcount for
 *      caller. This means, that caller sould not call
 *      Cookfs_PageObjIncrRefCount() to lock the page. But it should call
 *      Cookfs_PageObjDecrRefCount() when page data is no longer needed.
 *
 * Side effects:
 *      May remove other pages from pages cache; if reference counter is
 *      not properly managed, objects for other pages might be invalidated
 *      while they are used by caller of this API
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_PageGet(Cookfs_Pages *p, int index, int weight,
    Tcl_Obj **err)
{
    Cookfs_PagesWantRead(p);

    Cookfs_PageObj rc;

    CookfsLog(printf("index [%d] with weight [%d]", index, weight))

#if defined(COOKFS_USECALLBACKS)
    int preloadIndex = index + 1;
    for (; preloadIndex < Cookfs_PagesGetLength(p) ; preloadIndex++) {
	if (!Cookfs_AsyncPagePreload(p, preloadIndex)) {
	    break;
	}
    }
#endif /* COOKFS_USECALLBACKS */

    /* TODO: cleanup refcount for cached vs non-cached entries */

    /* if cache is disabled, immediately get page */
    if (p->cacheSize <= 0) {
        rc = CookfsPagesPageGetInt(p, index, err);
        CookfsLog(printf("Returning directly [%p]", (void *)rc))
        goto done;
    }

#if defined(COOKFS_USECALLBACKS)
    Cookfs_AsyncDecompressWaitIfLoading(p, index);

    for (; preloadIndex < Cookfs_PagesGetLength(p) ; preloadIndex++) {
	if (!Cookfs_AsyncPagePreload(p, preloadIndex)) {
	    break;
	}
    }
#endif /* COOKFS_USECALLBACKS */

#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
    rc = Cookfs_PageCacheGet(p, index, 1, weight);
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */

    if (rc != NULL) {
        Cookfs_PageObjIncrRefCount(rc);
        CookfsLog(printf("Returning from cache [%p]", (void *)rc));
        goto done;
    }

    /* get page and store it in cache */
    rc = CookfsPagesPageGetInt(p, index, err);
    CookfsLog(printf("Returning and caching [%p]", (void *)rc))

    if (rc != NULL) {
#ifdef TCL_THREADS
        Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
        Cookfs_PageCacheSet(p, index, rc, weight);
#ifdef TCL_THREADS
        Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */
    }

done:
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageCacheGet --
 *
 *      Gets contents of a page at specified index if cached and update
 *      its weight if the argument update is true
 *
 * Results:
 *	Cookfs_PageObj with page data
 *	NULL if not cached
 *
 * Side effects:
 *	May remove other pages from pages cache; if reference counter is
 *	not properly managed, objects for other pages might be invalidated
 *	while they are used by caller of this API
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_PageCacheGet(Cookfs_Pages *p, int index, int update, int weight) {
    Cookfs_PageObj rc;
    int i;

    /* if page is disabled, immediately get page */
    if (p->cacheSize <= 0) {
	return NULL;
    }

    CookfsLog(printf("index [%d]", index))
    /* iterate through pages cache and check if it already is in memory */
    for (i = 0; i < p->cacheSize; i++) {
	if (p->cache[i].pageIdx == index) {
	    rc = p->cache[i].pageObj;
	    if (update) {
	        p->cache[i].weight = weight;
	    }
	    CookfsPagesPageCacheMoveToTop(p, i);
	    CookfsLog(printf("Returning from cache [%s]", rc == NULL ? "NULL" : "SET"))
	    return rc;
	}
    }

    CookfsLog(printf("return NULL"))
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageCacheSet --
 *
 *	Add a page to cache
 *
 * Results:
 *	None
 *
 * Side effects:
 *	May remove older items from cache
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PageCacheSet(Cookfs_Pages *p, int idx, Cookfs_PageObj obj, int weight) {

    if (p->cacheSize <= 0) {
	return;
    }

    int i;

    /* if we already have that page in cache, then set its weight and move it to top */
    CookfsLog(printf("index [%d]", idx));
    for (i = 0 ; i < p->cacheSize ; i++) {
	if (p->cache[i].pageIdx == idx) {
	    p->cache[i].weight = weight;
	    /* age will be set by CookfsPagesPageCacheMoveToTop */
	    CookfsPagesPageCacheMoveToTop(p, i);
	    return;
	}
    }

    /*
       Decide which cache element should be replaced. Let's try to find an empty
       element or an element with minimum weight or maximum age.
    */

    int newIdx = p->cacheSize - 1;
    CookfsLog(printf("initial newIdx [%d]", newIdx));

    if (p->cache[newIdx].pageObj == NULL) {
        CookfsLog(printf("use it as it is empty"));
        goto found_empty;
    }

    /* save the current weight/age for later comparison */
    int oldWeight = p->cache[newIdx].weight;
    int oldAge = p->cache[newIdx].age;

    CookfsLog(printf("iterate over existing cache entries. Old entry is with weight [%d] and age [%d]", oldWeight, oldAge));
    for (i = p->cacheSize - 2; i >= 0; i--) {
        /* use current entry if it is empty */
        if (p->cache[i].pageObj == NULL) {
            newIdx = i;
            CookfsLog(printf("found empty entry [%d]", newIdx));
            goto found_empty;
        }
        /* skip a entry if its weight is greater than the weight of the saved entry */
        if (p->cache[i].weight > oldWeight) {
            CookfsLog(printf("entry [%d] has too much weight [%d]", i, p->cache[i].weight));
            continue;
        }
        /* if weight of current entry is the same, then skip a entry if its age is
           less than or equal to the age of the saved entry */
        if (p->cache[i].weight == oldWeight && p->cache[i].age <= oldAge) {
            CookfsLog(printf("entry [%d] has too low an age [%d]", i, p->cache[i].age));
            continue;
        }
        /* we found a suitable entry for replacement */
        newIdx = i;
        oldWeight = p->cache[i].weight;
        oldAge = p->cache[i].age;
        CookfsLog(printf("a new candidate for eviction has been found - entry [%d] with weight [%d] and age [%d]", newIdx, oldWeight, oldAge));
    }

    /* release the previous entry */
    Cookfs_PageObjDecrRefCount(p->cache[newIdx].pageObj);

found_empty:
    p->cache[newIdx].pageIdx = idx;
    p->cache[newIdx].pageObj = obj;
    p->cache[newIdx].weight = weight;
    Cookfs_PageObjIncrRefCount(obj);
    CookfsLog(printf("replace entry [%d]", newIdx));
    /* age will be set by CookfsPagesPageCacheMoveToTop */
    CookfsPagesPageCacheMoveToTop(p, newIdx);
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesPageCacheMoveToTop --
 *
 *	Move specified entry in page cache to top of page cache
 *
 * Results:
 *	None
 *
 * Side effects:
 *      Resets age of the specified entry to zero.
 *
 *----------------------------------------------------------------------
 */

static void CookfsPagesPageCacheMoveToTop(Cookfs_Pages *p, int index) {

    /* reset the age of the entry as it is used now */
    p->cache[index].age = 0;

    /* if index is 0, do not do anything more */
    if (index == 0) {
        return;
    }

    /* save previous entry with specified index */
    Cookfs_CacheEntry saved = p->cache[index];

    /* move entries from 0 to index-1 to next positions */
    memmove((void *) (p->cache + 1), p->cache, sizeof(Cookfs_CacheEntry) * index);

    /* set previous entry located at index to position 0 */
    p->cache[0] = saved;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesTickTock --
 *
 *      Increases the age of all cached entries by 1
 *
 * Results:
 *      Current max age value for cache entries
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PagesTickTock(Cookfs_Pages *p) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
    int maxAge = p->cacheMaxAge;
    for (int i = 0; i < p->cacheSize; i++) {
        if (++p->cache[i].age >= maxAge) {
            p->cache[i].weight = 0;
        }
    }
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */
    return maxAge;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetMaxAge --
 *
 *      Changes max age for cache entries. If max age value is less than
 *      zero, it will be ignored.
 *
 * Results:
 *      Current max age value for cache entries
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PagesSetMaxAge(Cookfs_Pages *p, int maxAge) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
    if (maxAge >= 0) {
        p->cacheMaxAge = maxAge;
    }
    int ret = p->cacheMaxAge;
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */
    return ret;
}

int Cookfs_PagesIsEncrypted(Cookfs_Pages *p, int index) {
    Cookfs_PagesWantRead(p);
    /* if specified index is an aside-index */
    if (COOKFS_PAGES_ISASIDE(index)) {
        CookfsLog(printf("Detected get request for add-aside pages - %08x", index))
        if (p->dataPagesIsAside) {
            // if this pages instance is the aside instance, remove the
            // COOKFS_PAGES_ASIDE flag and proceed
            index = index & COOKFS_PAGES_MASK;
            CookfsLog(printf("New index = %08x", index))
        } else if (p->dataAsidePages != NULL) {
            // if this is not the aside instance, redirect to it
            CookfsLog(printf("Redirecting to add-aside pages object"))
            if (!Cookfs_PagesLockRead(p->dataAsidePages, NULL)) {
                return 0;
            }
            int rc = Cookfs_PagesIsEncrypted(p->dataAsidePages, index);
            Cookfs_PagesUnlock(p->dataAsidePages);
            return rc;
        } else {
            // if no aside instance specified, return 0
            CookfsLog(printf("No add-aside pages defined"))
            return 0;
        }
    }
    return Cookfs_PgIndexGetEncryption(p->pagesIndex, index);
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesIsCached --
 *
 *      Checks whether the specified page is cached
 *
 * Results:
 *      Returns true if the specified page is cached and false otherwise.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

int Cookfs_PagesIsCached(Cookfs_Pages *p, int index) {
#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
    int ret = 0;
    for (int i = 0; i < p->cacheSize; i++) {
        if (p->cache[i].pageIdx == index && p->cache[i].pageObj != NULL) {
            ret = 1;
            break;
        }
    }
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */
    return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetHead --
 *
 *	Get all bytes before beginning of cookfs archive
 *
 * Results:
 *	Tcl_Obj containing read bytes
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetHead(Cookfs_Pages *p) {
    Tcl_Obj *data;
    data = Tcl_NewObj();
    CookfsLog(printf("initial offset: %" TCL_LL_MODIFIER "d",
        p->dataInitialOffset));
    if (p->dataInitialOffset > COOKFS_STAMP_BYTES) {
        if (p->fileChannel == NULL) {
            Tcl_SetByteArrayObj(data, p->fileData, p->dataInitialOffset -
                COOKFS_STAMP_BYTES);
        } else {
            p->fileLastOp = COOKFS_LASTOP_UNKNOWN;
            Tcl_Seek(p->fileChannel, 0, SEEK_SET);
            int count = Tcl_ReadChars(p->fileChannel, data,
                p->dataInitialOffset - COOKFS_STAMP_BYTES, 0);
            if (count != (p->dataInitialOffset - COOKFS_STAMP_BYTES)) {
                Tcl_BounceRefCount(data);
                return NULL;
            }
        }
    }
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetHeadMD5 --
 *
 *	Get MD5 checksum of all bytes before beginning of cookfs archive
 *
 * Results:
 *	Tcl_Obj containing MD5 as hexadecimal string
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetHeadMD5(Cookfs_Pages *p) {
    Tcl_Obj *head = Cookfs_PageGetHead(p);
    Tcl_Obj *rc = Cookfs_MD5FromObj(head);
    Tcl_BounceRefCount(head);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetTail --
 *
 *	Get all bytes of cookfs archive
 *	This should not be called if archive has been modified
 *	after opening it
 *
 * Results:
 *	Tcl_Obj containing data as byte array
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetTail(Cookfs_Pages *p) {
    Tcl_Obj *data;
    CookfsLog(printf("initial offset: %" TCL_LL_MODIFIER "d",
        p->dataInitialOffset));
    if (p->fileChannel == NULL) {
        data = Tcl_NewByteArrayObj(
            &p->fileData[p->dataInitialOffset - COOKFS_STAMP_BYTES],
            p->fileLength - p->dataInitialOffset + COOKFS_STAMP_BYTES);
    } else {
        data = Tcl_NewObj();
        p->fileLastOp = COOKFS_LASTOP_UNKNOWN;
        Tcl_Seek(p->fileChannel, p->dataInitialOffset - COOKFS_STAMP_BYTES,
            SEEK_SET);
        int count = Tcl_ReadChars(p->fileChannel, data, -1, 0);
        if (count < 0) {
            Tcl_BounceRefCount(data);
            return NULL;
        }
    }
    return data;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PageGetTailMD5 --
 *
 *	Get MD5 checksum of all bytes of cookfs archive
 *	This should not be called if archive has been modified
 *	after opening it
 *
 * Results:
 *	Tcl_Obj containing MD5 as hexadecimal string
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_PageGetTailMD5(Cookfs_Pages *p) {
    /* TODO: this can consume a lot of memory for large archives */
    Tcl_Obj *tail = Cookfs_PageGetTail(p);
    Tcl_Obj *rc = Cookfs_MD5FromObj(tail);
    Tcl_BounceRefCount(tail);
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetAside --
 *
 *	Sets another pages object as commit aside pages for a base
 *	set of pages.
 *
 *	This causes p to add new pages to aside pages object, instead
 *	of appending to its own pages.
 *	It allows saving changes to read-only pages in a separate file.
 *
 *	If aside pages also contain non-zero length index information,
 *	aside index overwrites main index.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	If p contained another aside pages, these pages are cleaned up
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetAside(Cookfs_Pages *p, Cookfs_Pages *aside) {
    Cookfs_PagesWantWrite(p);
    if (p->dataAsidePages != NULL) {
	Cookfs_PagesFini(p->dataAsidePages);
    }
    p->dataAsidePages = aside;
    if (aside != NULL) {
        if (!Cookfs_PagesLockWrite(aside, NULL)) {
            p->dataAsidePages = NULL;
            return;
        }
        if (p->dataIndex == NULL) {
            CookfsLog(printf("the base page object doesn't have index"));
        } else {
            CookfsLog(printf("checking if index in add-aside archive should"
                " be overwritten."));
            Cookfs_PageObj asideIndex = Cookfs_PagesGetIndex(aside);
            if (asideIndex == NULL) {
                CookfsLog(printf("copying index from main archive to"
                    " add-aside archive."));
                Cookfs_PagesSetIndex(aside, p->dataIndex);
                CookfsLog(printf("done copying index."));
            }
        }
#ifdef COOKFS_USECCRYPTO
        // Copy encryption settings from base pages
        aside->encryption = p->encryption;
        aside->encryptionLevel = p->encryptionLevel;
        if (p->encryption != COOKFS_ENCRYPT_NONE) {
            aside->isPasswordSet = p->isPasswordSet;
            aside->isEncryptionActive = p->isEncryptionActive;
            memcpy(aside->encryptionKey, p->encryptionKey,
                sizeof(p->encryptionKey));
            memcpy(aside->passwordSalt, p->passwordSalt,
                sizeof(p->passwordSalt));
            memcpy(aside->encryptionEncryptedKeyIV,
                p->encryptionEncryptedKeyIV,
                sizeof(p->encryptionEncryptedKeyIV));
            memcpy(aside->encryptionEncryptedKey,
                p->encryptionEncryptedKey,
                sizeof(p->encryptionEncryptedKey));
        }
#endif /* COOKFS_USECCRYPTO */
	Cookfs_PagesUnlock(aside);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetIndex --
 *
 *	Sets index information that is stored as part of cookfs archive
 *	metadata
 *
 *	This is usually set with output from Cookfs_FsindexToObject()
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Reference counter for Cookfs_PageObj storing previous index is
 *	decremented; improper handling of ref count for indexes
 *	might cause crashes
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetIndex(Cookfs_Pages *p, Cookfs_PageObj dataIndex) {
    Cookfs_PagesWantWrite(p);
    if (p->dataAsidePages != NULL) {
        if (!Cookfs_PagesLockWrite(p->dataAsidePages, NULL)) {
            return;
        }
        Cookfs_PagesSetIndex(p->dataAsidePages, dataIndex);
        Cookfs_PagesUnlock(p->dataAsidePages);
    }  else  {
        if (p->dataIndex != NULL) {
            Cookfs_PageObjDecrRefCount(p->dataIndex);
        }
        p->dataIndex = dataIndex;
        p->indexChanged = 1;
        Cookfs_PageObjIncrRefCount(p->dataIndex);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetIndex --
 *
 *	Gets index information that is stored as part of cookfs archive
 *	metadata
 *
 * Results:
 *	Cookfs_PageObj storing index
 *
 *	This is passed to Cookfs_FsindexFromObject()
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_PagesGetIndex(Cookfs_Pages *p) {
    Cookfs_PagesWantRead(p);
    Cookfs_PageObj rc;
    if (p->dataAsidePages != NULL) {
        if (!Cookfs_PagesLockRead(p->dataAsidePages, NULL)) {
            rc = NULL;
        } else {
            rc = Cookfs_PagesGetIndex(p->dataAsidePages);
            Cookfs_PagesUnlock(p->dataAsidePages);
        }
    }  else  {
	rc = p->dataIndex;
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetCacheSize --
 *
 *	Changes cache size for existing pages object
 *
 * Results:
 *	None
 *
 * Side effects:
 *	May remove all pages currently in cache
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetCacheSize(Cookfs_Pages *p, int size) {
    int i;

#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxCache);
#endif /* TCL_THREADS */
    if (size < 0) {
        size = 0;
    }
    if (size > COOKFS_MAX_CACHE_PAGES) {
        size = COOKFS_MAX_CACHE_PAGES;
    }
    /* TODO: only clean up pages above min(prevSize, currentSize) */
    for (i = 0; i < COOKFS_MAX_CACHE_PAGES; i++) {
        p->cache[i].age = 0;
        p->cache[i].weight = 0;
        p->cache[i].pageIdx = -1;
        if (p->cache[i].pageObj != NULL) {
            Cookfs_PageObjDecrRefCount(p->cache[i].pageObj);
            p->cache[i].pageObj = NULL;
        }
    }
    p->cacheSize = size;
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxCache);
#endif /* TCL_THREADS */
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_GetFilesize --
 *
 *	Gets file size based on currently written pages
 *
 * Results:
 *	File size as calculated by adding sizes of all pages and
 *	dataInitialOffset
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_WideInt Cookfs_GetFilesize(Cookfs_Pages *p) {
    Cookfs_PagesWantRead(p);
    int pagesCount = Cookfs_PgIndexGetLength(p->pagesIndex);
    CookfsLog(printf("enter, total pages count: %d", pagesCount));
    Tcl_WideInt rc = Cookfs_PagesGetPageOffset(p, pagesCount);
    CookfsLog(printf("return %" TCL_LL_MODIFIER "d", rc));
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetAlwaysCompress --
 *
 *	Gets whether pages are always compressed or only compressed
 *	when their compressed size is smaller than uncompressed size
 *
 * Results:
 *	Whether pages should always be compressed
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

/* Not used as for now

int Cookfs_PagesGetAlwaysCompress(Cookfs_Pages *p) {
    return p->alwaysCompress;
}

*/

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetAlwaysCompress --
 *
 *	Set whether pages are always compressed or only compressed
 *	when their compressed size is smaller than uncompressed size
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetAlwaysCompress(Cookfs_Pages *p, int alwaysCompress) {
    Cookfs_PagesWantWrite(p);
    p->alwaysCompress = alwaysCompress;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetCompression --
 *
 *	Get file compression for subsequent compressions
 *
 * Results:
 *	Current compression identifier
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_CompressionType Cookfs_PagesGetCompression(Cookfs_Pages *p,
    int *fileCompressionLevel)
{
    Cookfs_PagesWantRead(p);
    if (fileCompressionLevel != NULL) {
        *fileCompressionLevel = p->currentCompressionLevel;
    }
    return p->currentCompression;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesSetCompression --
 *
 *	Set file compression for subsequent compressions
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesSetCompression(Cookfs_Pages *p,
    Cookfs_CompressionType fileCompression, int fileCompressionLevel)
{
    Cookfs_PagesWantWrite(p);
    if (p->currentCompression != fileCompression ||
        p->currentCompressionLevel != fileCompressionLevel)
    {
#if defined(COOKFS_USECALLBACKS)
        // ensure all async pages are written
        while(Cookfs_AsyncCompressWait(p, 1)) {};
#endif /* COOKFS_USECALLBACKS */
        p->currentCompression = fileCompression;
        p->currentCompressionLevel = fileCompressionLevel;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesGetPageOffset --
 *
 *	Calculate offset of a page from start of file
 *	(not start of cookfs archive)
 *
 * Results:
 *	File offset to beginning of a page
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_WideInt Cookfs_PagesGetPageOffset(Cookfs_Pages *p, int idx) {
    Cookfs_PagesWantRead(p);
    Tcl_WideInt rc = p->dataInitialOffset;
    if (idx != 0) {
        rc += Cookfs_PgIndexGetStartOffset(p->pagesIndex, idx);
    }
    return rc;
}


/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsPagesPageGetInt --
 *
 *	Get contents of specified page. This function does not use cache
 *	and always reads page data. It is used by Cookfs_PageGet() which
 *	also manages caching of pages.
 *
 * Results:
 *	Cookfs_PageObj with page information; NULL otherwise
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static Cookfs_PageObj CookfsPagesPageGetInt(Cookfs_Pages *p, int index,
    Tcl_Obj **err)
{
    Cookfs_PageObj buffer;

    CookfsLog(printf("index [%d]", index))
    /* if specified index is an aside-index */
    if (COOKFS_PAGES_ISASIDE(index)) {
	CookfsLog(printf("Detected get request for add-aside pages - %08x", index))
	if (p->dataPagesIsAside) {
	    /* if this pages instance is the aside instance, remove the
	     * COOKFS_PAGES_ASIDE flag and proceed */
	    index = index & COOKFS_PAGES_MASK;
	    CookfsLog(printf("New index = %08x", index))
	}  else if (p->dataAsidePages != NULL) {
	    /* if this is not the aside instance, redirect to it */
	    CookfsLog(printf("Redirecting to add-aside pages object"))
            if (!Cookfs_PagesLockRead(p->dataAsidePages, err)) {
                return NULL;
            }
	    Cookfs_PageObj rc = CookfsPagesPageGetInt(p->dataAsidePages, index,
	        err);
	    Cookfs_PagesUnlock(p->dataAsidePages);
	    return rc;
	}  else  {
	    /* if no aside instance specified, return NULL */
	    CookfsLog(printf("No add-aside pages defined"))
	    return NULL;
	}
    }

    /* if index is larger than number of pages, fail */
    if (index >= Cookfs_PagesGetLength(p)) {
	CookfsLog(printf("GetInt failed: %d >= %d", index, Cookfs_PagesGetLength(p)));
	return NULL;
    }

#if defined(COOKFS_USECALLBACKS)
    buffer = Cookfs_AsyncPageGet(p, index);
    if (buffer != NULL) {
        Cookfs_PageObjIncrRefCount(buffer);
        CookfsLog(printf("return: result from Cookfs_AsyncPageGet()"));
	return buffer;
    }
#endif /* COOKFS_USECALLBACKS */

#ifdef TCL_THREADS
    Tcl_MutexLock(&p->mxIO);
#endif /* TCL_THREADS */
    buffer = Cookfs_ReadPage(p,
        Cookfs_PagesGetPageOffset(p, index),
        Cookfs_PgIndexGetCompression(p->pagesIndex, index),
        Cookfs_PgIndexGetSizeCompressed(p->pagesIndex, index),
        Cookfs_PgIndexGetSizeUncompressed(p->pagesIndex, index),
        Cookfs_PgIndexGetHashMD5(p->pagesIndex, index),
        1,
        Cookfs_PgIndexGetEncryption(p->pagesIndex, index),
        err);
#ifdef TCL_THREADS
    Tcl_MutexUnlock(&p->mxIO);
#endif /* TCL_THREADS */
    if (buffer == NULL) {
	CookfsLog(printf("Unable to read page"))
	return NULL;
    }

    return buffer;
}

int Cookfs_PagesGetPageSize(Cookfs_Pages *p, int index) {

    // We don't require any locks here as page size is readonly information

    if (COOKFS_PAGES_ISASIDE(index)) {
        CookfsLog(printf("Detected get request for add-aside pages - %08x", index))
        if (p->dataPagesIsAside) {
            /* if this pages instance is the aside instance, remove the
             * COOKFS_PAGES_ASIDE flag and proceed */
            index = index & COOKFS_PAGES_MASK;
            CookfsLog(printf("New index = %08x", index))
        }  else if (p->dataAsidePages != NULL) {
            /* if this is not the aside instance, redirect to it */
            CookfsLog(printf("Redirecting to add-aside pages object"))
            return Cookfs_PagesGetPageSize(p->dataAsidePages, index);
        }  else  {
            /* if no aside instance specified, return NULL */
            CookfsLog(printf("No add-aside pages defined"))
            return -1;
        }
    }

    return Cookfs_PgIndexGetSizeUncompressed(p->pagesIndex, index);

}

/*
 *----------------------------------------------------------------------
 *
 * CookfsReadIndex --
 *
 *	Index contents:
 *	  (pagesMD5checksums)(pagesSizes)(indexBinaryData)(indexSuffix)
 *
 *	Page MD5 checksums - 16 bytes * number of pages; contains MD5
 *	  checksum stored as binary data (not hexadecimal)
 *
 *	Page sizes - 4 bytes * number of pages; sizes of each page
 *
 *	Index binary data - archive fsindex stored as binary data
 *	  (accessible via Cookfs_PagesGetIndex() and Cookfs_PagesSetIndex() API)
 *
 *	Index suffix - 16 bytes:
 *	  4 - size of index (compressed, bytes)
 *	  4 - number of pages
 *	  1 - default compression
 *	   7 - signature
 *
 * Results:
 *	non-zero value on success; 0 otherwise
 *
 * Side effects:
 *	May change various attributes in Cookfs_Pages structure
 *
 *----------------------------------------------------------------------
 */

static int CookfsReadIndex(Tcl_Interp *interp, Cookfs_Pages *p, Tcl_Obj *password, int *is_abort, Tcl_Obj **err) {

#ifndef COOKFS_USECCRYPTO
    UNUSED(password);
#endif /* COOKFS_USECCRYPTO */

    Tcl_WideInt seekOffset = 0;
    Tcl_WideInt lastMatch = -1;
    unsigned char readBuffer[COOKFS_SUFFIX_BYTES];
    unsigned char *buf;

    UNUSED(err);
    Tcl_Obj *local_error = NULL;

    CookfsLog(printf("use base offset: %s", (p->useFoffset ?
        "YES" : "NO")));

    if (p->dataIndex != NULL) {
        Cookfs_PageObjDecrRefCount(p->dataIndex);
        p->dataIndex = NULL;
    }

    if (p->pagesIndex != NULL) {
        Cookfs_PgIndexFini(p->pagesIndex);
        p->pagesIndex = NULL;
    }

    if (p->useFoffset && p->foffset < COOKFS_SUFFIX_BYTES) {
        // A negative foffset value is considered an error.
        // If we don't have enough bytes for the suffix, consider it an error.
        // Report all above as index not found.
        CookfsLog(printf("specified foffset is negative or less than"
            " suffix size: %" TCL_LL_MODIFIER "d", p->foffset));
        goto errorIndexNotFound;
    }

    // If we use memory mapped file
    if (p->fileChannel == NULL) {

        if (p->useFoffset) {

            // If the specified offset + suffix length exceeds the file size,
            // consider the index search failed.
            if ((p->foffset + COOKFS_SUFFIX_BYTES) > p->fileLength) {
                CookfsLog(printf("(mmap) the specified end offset %"
                    TCL_LL_MODIFIER "d + <signature length> exceeds the file"
                    " size %" TCL_LL_MODIFIER "d", p->foffset, p->fileLength));
                goto errorIndexNotFound;
            }

            // Use the buffer starting at the specified offset
            CookfsLog(printf("(mmap) use specified end offset: %"
                TCL_LL_MODIFIER "d", p->foffset));

        } else {

            // In case of failure, we will use the end of the file as
            // the base offset.
            p->foffset = p->fileLength;

            // If the file size is less than 65536 bytes, the search starts from
            // the beginning. Otherwise, the search is performed in the last
            // 65536 bytes.
            seekOffset = (p->fileLength > 65536 ? (p->fileLength - 65536) : 0);

            CookfsLog(printf("(mmap) lookup seekOffset = %" TCL_LL_MODIFIER "d",
                seekOffset));

            lastMatch = CookfsSearchString(&p->fileData[seekOffset],
                (p->fileLength - seekOffset), p->fileSignature,
                COOKFS_SIGNATURE_LENGTH, 0);

            if (lastMatch < 0) {
                CookfsLog(printf("(mmap) lookup failed"));
                goto checkStamp;
            }

            CookfsLog(printf("(mmap) lookup done seekOffset = %" TCL_LL_MODIFIER
                "d", seekOffset + lastMatch + COOKFS_SIGNATURE_LENGTH));

            if ((seekOffset + lastMatch + COOKFS_SIGNATURE_LENGTH) <
                COOKFS_SUFFIX_BYTES)
            {
                CookfsLog(printf("there are not enough bytes for suffix"));
                goto errorIndexNotFound;
            }

            p->foffset = seekOffset + lastMatch + COOKFS_SIGNATURE_LENGTH;

        }

        buf = &p->fileData[p->foffset - COOKFS_SUFFIX_BYTES];
        goto checkSignature;

    }

    /* seek to beginning of suffix */
    if (!p->useFoffset) {

        // if endoffset not specified, read last 64k of file and find
        // last occurrence of signature
        seekOffset = Tcl_Seek(p->fileChannel, 0, SEEK_END);

        // In case of failure, we will use the end of the file as
        // the base offset.
        p->foffset = seekOffset;

        if (seekOffset > 65536) {
            seekOffset -= 65536;
        } else {
            seekOffset = 0;
        }

        CookfsLog(printf("lookup seekOffset = %" TCL_LL_MODIFIER "d",
            seekOffset));
        Tcl_Seek(p->fileChannel, seekOffset, SEEK_SET);

        Tcl_Obj *byteObj = Tcl_NewObj();
        Tcl_IncrRefCount(byteObj);

        if (Tcl_ReadChars(p->fileChannel, byteObj, 65536, 0) > 0) {

            Tcl_Size size;
            unsigned char *bytes = Tcl_GetByteArrayFromObj(byteObj, &size);

            lastMatch = CookfsSearchString(bytes, size, p->fileSignature,
                COOKFS_SIGNATURE_LENGTH, 0);

        } else {
            CookfsLog(printf("failed to read from the file"));
        }

        Tcl_DecrRefCount(byteObj);

        if (lastMatch < 0) {
            CookfsLog(printf("lookup failed"));

checkStamp: ; // empty statement

            Tcl_WideInt expectedSize = Cookfs_PageSearchStamp(p);
            if (expectedSize != -1) {
                SET_ERROR(Tcl_ObjPrintf("The archive appears to be corrupted"
                    " or truncated. Expected archive size is %" TCL_LL_MODIFIER
                    "d bytes or larger.", expectedSize));
            } else {
                SET_ERROR(Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                    ": signature not found", -1));
            }

            return 0;
        }

        p->foffset = seekOffset + lastMatch + COOKFS_SIGNATURE_LENGTH;
        CookfsLog(printf("lookup done seekOffset = %" TCL_LL_MODIFIER "d",
            p->foffset));

    } else {
        CookfsLog(printf("use specified end offset: %" TCL_LL_MODIFIER "d",
            p->foffset));
    }

    seekOffset = Tcl_Seek(p->fileChannel, p->foffset - COOKFS_SUFFIX_BYTES,
        SEEK_SET);

    /* if seeking fails, we assume no index exists */
    if (seekOffset < 0) {

        CookfsLog(printf("Unable to seek for index suffix"));

errorIndexNotFound:

        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": index not found", -1));
        }
        return 0;
    }

    /* read suffix from end of cookfs archive */
    int count = Tcl_Read(p->fileChannel, (char *)readBuffer, COOKFS_SUFFIX_BYTES);
    if (count != COOKFS_SUFFIX_BYTES) {
        CookfsLog(printf("Failed to read entire index tail: %d / %d", count,
            COOKFS_SUFFIX_BYTES));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": unable to read index suffix", -1));
        }
        return 0;
    }

    buf = readBuffer;

checkSignature:

    if (memcmp(&buf[COOKFS_SUFFIX_OFFSET_SIGNATURE], p->fileSignature,
        COOKFS_SIGNATURE_LENGTH) != 0)
    {
        CookfsLog(printf("Invalid file signature found"));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": invalid file signature", -1));
        }
        return 0;
    }

    // If we are here, then we have successfully read the archive suffix and
    // should expect that the file to be opened is a cookfs archive.
    // In case of any error below, we should report it as an error.
    // Uplevel function is designed to create a new archive if we open a file
    // in read-write mode and some error occurs while reading indexes.
    // By setting is_abort to 1, we instruct Cookfs_PagesInit() not to attempt
    // to start a new archive in the specified file, but to return an error.
    *is_abort = 1;

    /* get default compression and encryption */
    p->baseCompression = buf[COOKFS_SUFFIX_OFFSET_BASE_COMPRESSION] & 0xff;
    p->baseCompressionLevel = buf[COOKFS_SUFFIX_OFFSET_BASE_LEVEL] & 0xff;

    int isIndexEncrypted = 0;

#ifdef COOKFS_USECCRYPTO
    p->encryption = buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] & 0x7;
    p->encryptionLevel = (buf[COOKFS_SUFFIX_OFFSET_ENCRYPTION] >> 3) & 0x1f;

    CookfsLog(printf("encryption: %s level %d",
        (p->encryption == COOKFS_ENCRYPT_NONE ? "NONE" :
        (p->encryption == COOKFS_ENCRYPT_FILE ? "FILE" :
        (p->encryption == COOKFS_ENCRYPT_KEY ? "KEY" :
        (p->encryption == COOKFS_ENCRYPT_KEY_INDEX ? "KEY_INDEX" :
        "UNKNOWN")))), p->encryptionLevel));

    if (p->encryption == COOKFS_ENCRYPT_NONE) {
        goto skipEncryption;
    }

    if (p->encryption == COOKFS_ENCRYPT_KEY_INDEX) {
        if (password == NULL || !Tcl_GetCharLength(password)) {
            CookfsLog(printf("password for key-index encryption is missing"));
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                    ": the required password for the encrypted archive is missing",
                    -1));
            }
            return 0;
        }
        isIndexEncrypted = 1;
    }

    CookfsLog(printf("read password salt"));

    if (p->fileChannel == NULL) {
        // If we use memory mapped file, make sure we have enough bytes after
        // the end of the archive.
        if ((p->foffset + COOKFS_ENCRYPT_PASSWORD_SALT_SIZE) > p->fileLength) {
            CookfsLog(printf("(mmap) not enough bytes to read password"
                " salt"));
            goto failedReadPasswordSalt;
        }

        memcpy(p->passwordSalt, &p->fileData[p->foffset],
            COOKFS_ENCRYPT_PASSWORD_SALT_SIZE);

        goto skipReadPasswordSalt;

    }

    count = Tcl_Read(p->fileChannel, (char *)p->passwordSalt,
        COOKFS_ENCRYPT_PASSWORD_SALT_SIZE);

    if (count != COOKFS_ENCRYPT_PASSWORD_SALT_SIZE) {

        CookfsLog(printf("failed to read password salt"));

failedReadPasswordSalt:

        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": unable to read password salt", -1));
        }
        return 0;
    }

skipReadPasswordSalt:

    if (p->encryption != COOKFS_ENCRYPT_FILE) {

        CookfsLog(printf("read encryption key IV"));

        if (p->fileChannel == NULL) {
            // If we use memory mapped file, make sure we have enough bytes after
            // the end of the archive.
            if ((p->foffset + COOKFS_ENCRYPT_PASSWORD_SALT_SIZE +
                COOKFS_ENCRYPT_IV_SIZE + COOKFS_ENCRYPT_KEY_AND_HASH_SIZE)
                > p->fileLength)
            {
                CookfsLog(printf("(mmap) not enough bytes to read encryption"
                    " key"));
                if (interp != NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(
                        COOKFS_PAGES_ERRORMSG ": unable to read encryption key"
                        " IV", -1));
                }
                return 0;
            }

            memcpy(p->encryptionEncryptedKeyIV,
                &p->fileData[p->foffset + COOKFS_ENCRYPT_PASSWORD_SALT_SIZE],
                COOKFS_ENCRYPT_IV_SIZE);

            memcpy(p->encryptionEncryptedKey,
                &p->fileData[p->foffset + COOKFS_ENCRYPT_PASSWORD_SALT_SIZE +
                    COOKFS_ENCRYPT_IV_SIZE],
                COOKFS_ENCRYPT_KEY_AND_HASH_SIZE);

            goto skipReadEncryptionKey;
        }

        count = Tcl_Read(p->fileChannel, (char *)p->encryptionEncryptedKeyIV,
            COOKFS_ENCRYPT_IV_SIZE);
        if (count != COOKFS_ENCRYPT_IV_SIZE) {
            CookfsLog(printf("failed to read encryption key IV"));
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                    ": unable to read encryption key IV", -1));
            }
            return 0;
        }

        CookfsLog(printf("read encryption key"));
        count = Tcl_Read(p->fileChannel, (char *)p->encryptionEncryptedKey,
            COOKFS_ENCRYPT_KEY_AND_HASH_SIZE);
        if (count != COOKFS_ENCRYPT_KEY_AND_HASH_SIZE) {
            CookfsLog(printf("failed to read encryption key"));
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                    ": unable to read encryption key", -1));
            }
            return 0;
        }

skipReadEncryptionKey:

        if (password != NULL && Tcl_GetCharLength(password)) {
            if (Cookfs_PagesDecryptKey(p, password) != TCL_OK) {
                CookfsLog(printf("failed to decrypt the encryption key"));
                if (interp != NULL) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(
                        COOKFS_PAGES_ERRORMSG ": could not decrypt"
                        " the encryption key with the specified password",
                        -1));
                }
                return 0;
            }
        }

    }

skipEncryption: ; // empty statement

#endif /* COOKFS_USECCRYPTO */

    /* get pgindex and fsindex sizes */
    int pgindexSizeCompressed;
    Cookfs_Binary2Int(&buf[COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_COMPR],
        &pgindexSizeCompressed, 1);

    int fsindexSizeCompressed;
    Cookfs_Binary2Int(&buf[COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_COMPR],
        &fsindexSizeCompressed, 1);

    // Validate the sizes of pgindex and fsindex. We must have enough bytes
    // in the file before the cookfs suffix.
    if ((pgindexSizeCompressed + fsindexSizeCompressed +
        COOKFS_SUFFIX_BYTES) > p->foffset)
    {
        CookfsLog(printf("there are enough bytes in the file, pgindex size:"
            " %d, fsindex size: %d, suffix size: %d, suffix offset: %"
            TCL_LL_MODIFIER "d", pgindexSizeCompressed, fsindexSizeCompressed,
            (int)COOKFS_SUFFIX_BYTES, p->foffset));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": failed to read index", -1));
        }
        return 0;
    }

    if (pgindexSizeCompressed == 0 && fsindexSizeCompressed == 0) {
        CookfsLog(printf("both pgindex and fsindex are empty and skipped"));
        goto skipFsindex;
    }

    if (p->fileChannel == NULL) {
        goto skipSeekToIndexData;
    }

    CookfsLog(printf("try to seek to index data..."));

    /* seek to beginning of index data */
    if (Tcl_Seek(p->fileChannel, p->foffset - COOKFS_SUFFIX_BYTES -
        pgindexSizeCompressed - fsindexSizeCompressed, SEEK_SET) < 0)
    {
        CookfsLog(printf("unable to seek to index data"));
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": unable to seek to index data", -1));
        }
        return 0;
    }

skipSeekToIndexData:

    if (pgindexSizeCompressed == 0) {
        CookfsLog(printf("pgindex is empty and skipped"));
        goto skipPgindex;
    }

    CookfsLog(printf("read pgindex, size: %d", pgindexSizeCompressed));

    int pgindexSizeUncompressed;
    Cookfs_Binary2Int(&buf[COOKFS_SUFFIX_OFFSET_PGINDEX_SIZE_UNCOMPR],
        &pgindexSizeUncompressed, 1);
    int pgindexCompression = buf[COOKFS_SUFFIX_OFFSET_PGINDEX_COMPRESSION]
        & 0xff;
    unsigned char *pgindexHashMD5 = &buf[COOKFS_SUFFIX_OFFSET_PGINDEX_HASH];

    Tcl_Size pgindexOffset;
    // If we are using a memory-mapped file, calculate the pgindex offset.
    // Otherwise, we use -1 as an offset to read subsequent data from the file.
    if (p->fileChannel == NULL) {
        pgindexOffset = p->foffset - COOKFS_SUFFIX_BYTES -
            pgindexSizeCompressed - fsindexSizeCompressed;
    } else {
        pgindexOffset = -1;
    }

    Cookfs_PageObj pgindexDataObj = Cookfs_ReadPage(p, pgindexOffset,
        pgindexCompression, pgindexSizeCompressed, pgindexSizeUncompressed,
        pgindexHashMD5, 1, isIndexEncrypted, &local_error);

    if (pgindexDataObj == NULL) {
        CookfsLog(printf("unable to read or decompress pgindex"));
        goto pgindexReadError;
    }

    p->pagesIndex = Cookfs_PgIndexImport(pgindexDataObj->buf,
        Cookfs_PageObjSize(pgindexDataObj), NULL);

    Cookfs_PageObjDecrRefCount(pgindexDataObj);

    if (p->pagesIndex == NULL) {
        local_error = Tcl_NewStringObj("error while parsing pages index", -1);
        goto pgindexReadError;
    }

    // We have successfully read pgindex. Let's continue with fsindex.

    goto skipPgindex;

pgindexReadError:

    if (interp != NULL) {
        if (local_error != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(COOKFS_PAGES_ERRORMSG
                ": unable to read pages index - %s",
                Tcl_GetString(local_error)));
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": unable to read pages index", -1));
        }
    }
    if (local_error != NULL) {
        Tcl_BounceRefCount(local_error);
    }
    return 0;

skipPgindex:

    if (fsindexSizeCompressed == 0) {
        CookfsLog(printf("fsindex is empty and skipped"));
        goto skipFsindex;
    }

    CookfsLog(printf("read fsindex, size: %d", fsindexSizeCompressed));

    int fsindexSizeUncompressed;
    Cookfs_Binary2Int(&buf[COOKFS_SUFFIX_OFFSET_FSINDEX_SIZE_UNCOMPR],
        &fsindexSizeUncompressed, 1);
    int fsindexCompression = buf[COOKFS_SUFFIX_OFFSET_FSINDEX_COMPRESSION]
        & 0xff;
    unsigned char *fsindexHashMD5 = &buf[COOKFS_SUFFIX_OFFSET_FSINDEX_HASH];

    Tcl_Size fsindexOffset;
    // If we are using a memory-mapped file, calculate the pgindex offset.
    // Otherwise, we use -1 as an offset to read subsequent data from the file.
    if (p->fileChannel == NULL) {
        fsindexOffset = p->foffset - COOKFS_SUFFIX_BYTES -
            fsindexSizeCompressed;
    } else {
        fsindexOffset = -1;
    }

    p->dataIndex = Cookfs_ReadPage(p, fsindexOffset, fsindexCompression,
        fsindexSizeCompressed, fsindexSizeUncompressed, fsindexHashMD5, 1,
        isIndexEncrypted, &local_error);

    if (p->dataIndex == NULL) {
        CookfsLog(printf("unable to read or decompress fsindex"));
        goto fsindexReadError;
    }

    // We have successfully read fsindex. Let's continue.

    goto skipFsindex;

fsindexReadError:

    if (interp != NULL) {
        if (local_error != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(COOKFS_PAGES_ERRORMSG
                ": unable to read files index - %s",
                Tcl_GetString(local_error)));
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": unable to read files index", -1));
        }
    }
    if (local_error != NULL) {
        Tcl_BounceRefCount(local_error);
    }
    return 0;

skipFsindex:

    // Calculate the initial offset for the pages. First, let's find the offset
    // of the end of the pages based on the end offset and subtracting
    // the size of pgindex/fsindex, as well as subtracting the size of
    // the cookfs suffix.
    p->dataInitialOffset = p->foffset - pgindexSizeCompressed -
        fsindexSizeCompressed - COOKFS_SUFFIX_BYTES;

    // If we have page data, subtract the size of all pages.
    if (p->pagesIndex != NULL) {
        p->dataInitialOffset -= Cookfs_PgIndexGetStartOffset(p->pagesIndex,
            Cookfs_PgIndexGetLength(p->pagesIndex));
    }

    if (p->dataInitialOffset < 0) {
        CookfsLog(printf("ERROR: file doesn't have enough bytes for all"
            " pages, calculated initial offset is %" TCL_LL_MODIFIER "d",
            p->dataInitialOffset));
        p->dataInitialOffset = 0;
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(COOKFS_PAGES_ERRORMSG
                ": file does not contain enough bytes for all pages", -1));
        }
        return 0;
    }

    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsTruncateFileIfNeeded --
 *
 *	Truncate pages file if needed
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static void CookfsTruncateFileIfNeeded(Cookfs_Pages *p, Tcl_WideInt targetOffset) {
#ifdef USE_TCL_TRUNCATE
    if (p->shouldTruncate == 1) {
	/* TODO: only truncate if current size is larger than what it should be */
	Tcl_TruncateChannel(p->fileChannel, targetOffset);
	/* TODO: truncate is still possible using ftruncate() */
	p->shouldTruncate = 0;
	CookfsLog(printf("Truncating to %d", (int) targetOffset));
    }
#endif
}


