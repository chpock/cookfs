/*
 * pagesCompr.c
 *
 * Provides functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pagesCompr.h"
#include "pagesInt.h"
#include "pagesComprZlib.h"
#if defined(COOKFS_USECALLBACKS)
#include "pagesComprCustom.h"
#endif /* COOKFS_USECALLBACKS */
#ifdef COOKFS_USEBZ2
#include "pagesComprBz2.h"
#endif
#ifdef COOKFS_USELZMA
#include "pagesComprLzma.h"
#endif
#ifdef COOKFS_USEZSTD
#include "pagesComprZstd.h"
#endif
#ifdef COOKFS_USEBROTLI
#include "pagesComprBrotli.h"
#endif

#if defined(COOKFS_USECALLBACKS)
/* declarations of static and/or internal functions */
static Tcl_Obj **CookfsCreateCompressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr, int additionalElements);
#endif /* COOKFS_USECALLBACKS */
#ifdef USE_VFS_COMMANDS_FOR_ZIP
static int CookfsCheckCommandExists(Tcl_Interp *interp, const char *commandName);
#endif

/* compression data */
const char *cookfsCompressionOptions[] = {
    "none",
    "zlib",
#ifdef COOKFS_USEBZ2
    "bz2",
#endif
#ifdef COOKFS_USELZMA
    "lzma",
#endif
#ifdef COOKFS_USEZSTD
    "zstd",
#endif
#ifdef COOKFS_USEBROTLI
    "brotli",
#endif
#if defined(COOKFS_USECALLBACKS)
    "custom",
#endif /* COOKFS_USECALLBACKS */
    NULL
};

const int cookfsCompressionOptionMap[] = {
    COOKFS_COMPRESSION_NONE,
    COOKFS_COMPRESSION_ZLIB,
#ifdef COOKFS_USEBZ2
    COOKFS_COMPRESSION_BZ2,
#endif
#ifdef COOKFS_USELZMA
    COOKFS_COMPRESSION_LZMA,
#endif
#ifdef COOKFS_USEZSTD
    COOKFS_COMPRESSION_ZSTD,
#endif
#ifdef COOKFS_USEBROTLI
    COOKFS_COMPRESSION_BROTLI,
#endif
#if defined(COOKFS_USECALLBACKS)
    COOKFS_COMPRESSION_CUSTOM,
#endif /* COOKFS_USECALLBACKS */
    -1 /* dummy entry */
};

const char *Cookfs_CompressionGetName(Cookfs_CompressionType compression) {
    switch (compression) {
    case COOKFS_COMPRESSION_DEFAULT:
        return "default";
    case COOKFS_COMPRESSION_NONE:
        return "none";
    case COOKFS_COMPRESSION_ZLIB:
        return "zlib";
    case COOKFS_COMPRESSION_BZ2:
        return "bz2";
    case COOKFS_COMPRESSION_LZMA:
        return "lzma";
    case COOKFS_COMPRESSION_ZSTD:
        return "zstd";
    case COOKFS_COMPRESSION_BROTLI:
        return "brotli";
    case COOKFS_COMPRESSION_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

static int Cookfs_CompressionGetDefaultLevel(Cookfs_CompressionType compression) {
    switch (compression) {
    case COOKFS_COMPRESSION_NONE:
        return 0;
    case COOKFS_COMPRESSION_ZLIB:
        return COOKFS_DEFAULT_COMPRESSION_LEVEL_ZLIB;
#ifdef COOKFS_USEBZ2
    case COOKFS_COMPRESSION_BZ2:
        return COOKFS_DEFAULT_COMPRESSION_LEVEL_BZ2;
#endif
#ifdef COOKFS_USELZMA
    case COOKFS_COMPRESSION_LZMA:
        return COOKFS_DEFAULT_COMPRESSION_LEVEL_LZMA;
#endif
#ifdef COOKFS_USEZSTD
    case COOKFS_COMPRESSION_ZSTD:
        return COOKFS_DEFAULT_COMPRESSION_LEVEL_ZSTD;
#endif
#ifdef COOKFS_USEBROTLI
    case COOKFS_COMPRESSION_BROTLI:
        return COOKFS_DEFAULT_COMPRESSION_LEVEL_BROTLI;
#endif
    default:
        return 255;
    }
}

Tcl_Obj *Cookfs_CompressionToObj(Cookfs_CompressionType compression,
    int compressionLevel)
{
    if (compressionLevel == -1 || compressionLevel ==
        Cookfs_CompressionGetDefaultLevel(compression))
    {
        // cppcheck-suppress knownArgument
        return Tcl_NewStringObj(Cookfs_CompressionGetName(compression), -1);
    } else {
        return Tcl_ObjPrintf("%s:%d", Cookfs_CompressionGetName(compression),
            compressionLevel);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_CompressionFromObj --
 *
 *      Returns an integer compression ID based on string in the corresponding
 *      Tcl object. If an error occurs, returns TCL_ERROR and an appropriate
 *      error message if the interp is not NULL.
 *
 * Results:
 *      TCL_OK if compression ID was successfully converted from Tcl_Obj.
 *
 * Side effects:
 *      If NULL is specified as the Tcl object, the default compression is
 *      returned.
 *
 *----------------------------------------------------------------------
 */

int Cookfs_CompressionFromObj(Tcl_Interp *interp, Tcl_Obj *obj,
    Cookfs_CompressionType *compressionPtr, int *compressionLevelPtr)
{
    CookfsLog(printf("from [%s]", obj == NULL ?
        "<NULL>" : Tcl_GetString(obj)));

#ifdef COOKFS_USELZMA
    int compression = COOKFS_COMPRESSION_LZMA;
#else
    int compression = COOKFS_COMPRESSION_ZLIB;
#endif
    int compressionLevel = -1;

    if (obj != NULL) {

        Tcl_Obj *method;
        Tcl_Obj *level = NULL;

        Tcl_Size len;
        char *str = Tcl_GetStringFromObj(obj, &len);
        char *colonPos = strrchr(str, ':');
        if (colonPos == NULL) {
            // If no colon is found, assume obj is a compression method
            method = Tcl_DuplicateObj(obj);
            CookfsLog(printf("only method is specified"));
        } else if (colonPos[1] == '\0') {
            // If colos is in the last position, create a new object
            // and delete the last character
            method = Tcl_NewStringObj(str, len - 1);
            CookfsLog(printf("method and an empty level is specified"));
        } else {
            // Split obj to method obj (before the colon) and level obj
            // (after the colon)
            method = Tcl_NewStringObj(str, colonPos - str);
            level = Tcl_NewStringObj(++colonPos, -1);
            Tcl_IncrRefCount(level);
            CookfsLog(printf("method [%s] and level [%s] are specified",
                Tcl_GetString(method), Tcl_GetString(level)));
        }
        Tcl_IncrRefCount(method);

        if (Tcl_GetIndexFromObj(interp, method,
            (const char **)cookfsCompressionOptions, "compression", 0,
            &compression) != TCL_OK)
        {
            CookfsLog(printf("failed to detect compression method"));
            goto error;
        }
        /* map compression from cookfsCompressionOptionMap */
        compression = cookfsCompressionOptionMap[compression];
        CookfsLog(printf("detected compression: %d", compression));

        if (level == NULL) {
            goto nolevel;
        }

        if (Tcl_GetIntFromObj(interp, level, &compressionLevel) != TCL_OK) {
            goto error;
        }
        if (compressionLevel < -1 || compressionLevel > 255) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("the compression level"
                    " is expected to be an integer between -1 and 255,"
                    " but got \"%d\"", compressionLevel));
            }
            goto error;
        }
        Tcl_DecrRefCount(level);

nolevel:
        Tcl_DecrRefCount(method);
        goto done;

error:
        Tcl_DecrRefCount(method);
        if (level != NULL) {
            Tcl_DecrRefCount(level);
        }
        return TCL_ERROR;
    }

done:
    *compressionPtr = compression;
    if (compressionLevel == -1) {
        compressionLevel = Cookfs_CompressionGetDefaultLevel(compression);
    }
    *compressionLevelPtr = compressionLevel;
    CookfsLog(printf("return method %d [%s] level [%d]", (int)compression,
        Cookfs_CompressionGetName(compression), compressionLevel));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesInitCompr --
 *
 *      Initializes pages compression handling for specified pages object
 *
 *      Invoked as part of Cookfs_PagesInit()
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesInitCompr(Cookfs_Pages *rc) {
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    if (CookfsCheckCommandExists(rc->interp, "::zlib")) {
        Tcl_Obj *zlibString;
        zlibString = Tcl_NewStringObj("::zlib", -1);
        Tcl_IncrRefCount(zlibString);
        /* use built-in zlib command for (de)compressing */
        rc->zipCmdCompress[0] = zlibString;
        rc->zipCmdCompress[1] = Tcl_NewStringObj("deflate", -1);
        rc->zipCmdDecompress[0] = zlibString;
        rc->zipCmdDecompress[1] = Tcl_NewStringObj("inflate", -1);

        Tcl_IncrRefCount(rc->zipCmdCompress[1]);
        Tcl_IncrRefCount(rc->zipCmdDecompress[1]);

        rc->zipCmdOffset = 2;
        rc->zipCmdLength = 3;
    }  else  {
        /* initialize list for calling vfs::zip command for (de)compressing */
        rc->zipCmdCompress[0] = Tcl_NewStringObj("::vfs::zip", -1);
        rc->zipCmdCompress[1] = Tcl_NewStringObj("-mode", -1);
        rc->zipCmdCompress[2] = Tcl_NewStringObj("compress", -1);
        rc->zipCmdCompress[3] = Tcl_NewStringObj("-nowrap", -1);
        rc->zipCmdCompress[4] = Tcl_NewIntObj(1);

        rc->zipCmdDecompress[0] = rc->zipCmdCompress[0];
        rc->zipCmdDecompress[1] = rc->zipCmdCompress[1];
        rc->zipCmdDecompress[2] = Tcl_NewStringObj("decompress", -1);
        rc->zipCmdDecompress[3] = rc->zipCmdCompress[3];
        rc->zipCmdDecompress[4] = rc->zipCmdCompress[4];

        Tcl_IncrRefCount(rc->zipCmdCompress[0]);
        Tcl_IncrRefCount(rc->zipCmdCompress[1]);
        Tcl_IncrRefCount(rc->zipCmdCompress[2]);
        Tcl_IncrRefCount(rc->zipCmdCompress[3]);
        Tcl_IncrRefCount(rc->zipCmdCompress[4]);

        Tcl_IncrRefCount(rc->zipCmdDecompress[2]);

        rc->zipCmdOffset = 5;
        rc->zipCmdLength = 6;
    }
#endif
#if !defined(USE_VFS_COMMANDS_FOR_ZIP)
    UNUSED(rc);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_PagesFiniCompr --
 *
 *      Cleans up pages compression handling for specified pages object
 *
 *      Invoked as part of Cookfs_PagesFini()
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_PagesFiniCompr(Cookfs_Pages *rc) {
#ifdef USE_VFS_COMMANDS_FOR_ZIP
    /* free up memory for invoking commands */
    if (rc->zipCmdOffset == 2) {
        Tcl_DecrRefCount(rc->zipCmdCompress[0]);
        Tcl_DecrRefCount(rc->zipCmdCompress[1]);
        Tcl_DecrRefCount(rc->zipCmdDecompress[1]);
    }  else  {
        Tcl_DecrRefCount(rc->zipCmdCompress[0]);
        Tcl_DecrRefCount(rc->zipCmdCompress[1]);
        Tcl_DecrRefCount(rc->zipCmdCompress[2]);
        Tcl_DecrRefCount(rc->zipCmdCompress[3]);
        Tcl_DecrRefCount(rc->zipCmdCompress[4]);

        Tcl_DecrRefCount(rc->zipCmdDecompress[2]);
    }
#endif

#if defined(COOKFS_USECALLBACKS)
    /* clean up compress/decompress commands, if present - for non-aside pages only */
    if (!rc->isAside) {
        if (rc->compressCommandPtr != NULL) {
            Tcl_Obj **ptr;
            for (ptr = rc->compressCommandPtr; *ptr; ptr++) {
                Tcl_DecrRefCount(*ptr);
            }
            ckfree((void *) rc->compressCommandPtr);
        }
        if (rc->decompressCommandPtr != NULL) {
            Tcl_Obj **ptr;
            for (ptr = rc->decompressCommandPtr; *ptr; ptr++) {
                Tcl_DecrRefCount(*ptr);
            }
            ckfree((void *) rc->decompressCommandPtr);
        }
        if (rc->asyncCompressCommandPtr != NULL) {
            Tcl_Obj **ptr;
            for (ptr = rc->asyncCompressCommandPtr; *ptr; ptr++) {
                Tcl_DecrRefCount(*ptr);
            }
            ckfree((void *) rc->asyncCompressCommandPtr);
        }
        if (rc->asyncDecompressCommandPtr != NULL) {
            Tcl_Obj **ptr;
            for (ptr = rc->asyncDecompressCommandPtr; *ptr; ptr++) {
                Tcl_DecrRefCount(*ptr);
            }
            ckfree((void *) rc->asyncDecompressCommandPtr);
        }
    }
#else
    UNUSED(rc);
#endif /* COOKFS_USECALLBACKS */
}


#if defined(COOKFS_USECALLBACKS)
/*
 *----------------------------------------------------------------------
 *
 * Cookfs_SetCompressCommands --
 *
 *      Set compress, decompress and async compress commands
 *      for specified pages object
 *
 *      Creates a copy of objects' list elements
 *
 * Results:
 *      TCL_OK on success; TCL_ERROR otherwise
 *
 * Side effects:
 *      May set interpreter's result to error message, if error occurred
 *
 *----------------------------------------------------------------------
 */

int Cookfs_SetCompressCommands(Cookfs_Pages *p, Tcl_Obj *compressCommand, Tcl_Obj *decompressCommand, Tcl_Obj *asyncCompressCommand, Tcl_Obj *asyncDecompressCommand) {
    Tcl_Obj **compressPtr = NULL;
    Tcl_Obj **decompressPtr = NULL;
    Tcl_Obj **asyncCompressPtr = NULL;
    Tcl_Obj **asyncDecompressPtr = NULL;
    int compressLen = 0;
    int decompressLen = 0;
    int asyncCompressLen = 0;
    int asyncDecompressLen = 0;

    /* copy compress command */
    if (compressCommand != NULL) {
        compressPtr = CookfsCreateCompressionCommand(NULL, compressCommand, &compressLen, 1);
        if (compressPtr == NULL) {
            return TCL_ERROR;
        }
    }

    /* copy decompress command */
    if (decompressCommand != NULL) {
        decompressPtr = CookfsCreateCompressionCommand(NULL, decompressCommand, &decompressLen, 1);
        if (decompressPtr == NULL) {
            return TCL_ERROR;
        }
    }

    /* copy async compress command */
    if (asyncCompressCommand != NULL) {
        asyncCompressPtr = CookfsCreateCompressionCommand(NULL, asyncCompressCommand, &asyncCompressLen, 3);
        if (asyncCompressPtr == NULL) {
            return TCL_ERROR;
        }
    }

    /* copy async decompress command */
    if (asyncDecompressCommand != NULL) {
        asyncDecompressPtr = CookfsCreateCompressionCommand(NULL, asyncDecompressCommand, &asyncDecompressLen, 3);
        if (asyncDecompressPtr == NULL) {
            return TCL_ERROR;
        }
    }

    /* store copied list and number of items in lists */
    p->compressCommandPtr = compressPtr;
    p->compressCommandLen = compressLen;
    p->decompressCommandPtr = decompressPtr;
    p->decompressCommandLen = decompressLen;
    p->asyncCompressCommandPtr = asyncCompressPtr;
    p->asyncCompressCommandLen = asyncCompressLen;
    p->asyncDecompressCommandPtr = asyncDecompressPtr;
    p->asyncDecompressCommandLen = asyncDecompressLen;

    return TCL_OK;
}
#endif /* COOKFS_USECALLBACKS */


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_ReadPage --
 *
 *      Read page and invoke proper decompression function, if requested
 *
 * Results:
 *      Page; decompressed if decompress was specified
 *      NOTE: Reference counter for the page is already incremented
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj Cookfs_ReadPage(Cookfs_Pages *p, Tcl_Size offset,
    Cookfs_CompressionType compression, int sizeCompressed,
    int sizeUncompressed, unsigned char *md5hash, int decompress,
    int encrypted, Tcl_Obj **err)
{

    p->fileLastOp = COOKFS_LASTOP_READ;

    CookfsLog(printf("page with offset %" TCL_SIZE_MODIFIER "d compression:%d"
        " sizeCompressed:%d sizeUncompressed:%d decompress:%d encrypted:%d",
        offset, (int)compression, sizeCompressed, sizeUncompressed, decompress,
        encrypted));

    assert(sizeCompressed >= 0);
    assert(sizeUncompressed >= 0);
    assert(decompress == 0 || decompress == 1);

#ifdef COOKFS_USECCRYPTO
    if (encrypted && !p->isEncryptionActive) {
        CookfsLog(printf("return ERROR (the page is encrypted, but no password"
            " is set)"));
        SET_ERROR(Tcl_NewStringObj("no password specified for decrypting", -1));
        return NULL;
    }
#else
    UNUSED(encrypted);
#endif /* COOKFS_USECCRYPTO */

    if (!decompress) {
        compression = COOKFS_COMPRESSION_NONE;
    }

    if (sizeUncompressed == 0) {
        /* if page was empty, no need to read anything */
        Cookfs_PageObj rc = Cookfs_PageObjAlloc(0);
        Cookfs_PageObjIncrRefCount(rc);
        CookfsLog(printf("return: ok (zero size)"));
        return rc;
    }

    Cookfs_PageObj dataCompressed;

    if (p->fileChannel == NULL) {

        assert(offset >= 0 && "can't use negative offset with memory"
            " mapped file");

#ifdef COOKFS_USECCRYPTO
        // If the page is encrypted, we need to copy it from the memory-mapped
        // file because we need to decrypt that data.
        if (encrypted) {
            CookfsLog(printf("(mmap) create page object (as a copy)"));
            dataCompressed = Cookfs_PageObjNewFromString(&p->fileData[offset],
                sizeCompressed);
        } else {
#endif /* COOKFS_USECCRYPTO */
            CookfsLog(printf("(mmap) create page object"));
            dataCompressed = Cookfs_PageObjNewWithoutAlloc(&p->fileData[offset],
                sizeCompressed);
#ifdef COOKFS_USECCRYPTO
        }
#endif /* COOKFS_USECCRYPTO */

        goto skipReading;

    }

    // Alloc memory for compressed data
    dataCompressed = Cookfs_PageObjAlloc(sizeCompressed);
    if (dataCompressed == NULL) {
        CookfsLog(printf("ERROR: unable to alloc %d bytes for page",
            sizeCompressed));
        SET_ERROR(Tcl_ObjPrintf("Cookfs_ReadPage(): unable to alloc %d bytes"
            " for page", sizeCompressed));
        return NULL;
    }

    CookfsLog(printf("read data..."));
    if (offset >= 0) {
        Tcl_Seek(p->fileChannel, offset, SEEK_SET);
    }

    Tcl_Size read = Tcl_Read(p->fileChannel, (char *)dataCompressed->buf,
        sizeCompressed);
    if (read != sizeCompressed) {
        CookfsLog(printf("ERROR: got only %" TCL_SIZE_MODIFIER "d bytes"
            " from channel", read));
        // cppcheck-suppress unknownMacro
        SET_ERROR(Tcl_ObjPrintf("Cookfs_ReadPage(): error while reading"
            " compressed data from page. Expected data size %d bytes,"
            " got %" TCL_SIZE_MODIFIER "d bytes", sizeCompressed, read));
        Cookfs_PageObjBounceRefCount(dataCompressed);
        return NULL;
    }

skipReading: ; // empty statement

#ifdef COOKFS_USECCRYPTO

    if (encrypted) {
        CookfsLog(printf("decrypt the page..."));
        Cookfs_PageObjSetIV(dataCompressed, md5hash);
        if (Cookfs_AesDecrypt(dataCompressed, p->encryptionKey) != TCL_OK) {
            Cookfs_PageObjBounceRefCount(dataCompressed);
            CookfsLog(printf("return: ERROR (failed to decrypt the page)"));
decryptionFailed:
            SET_ERROR(Tcl_NewStringObj("failed to verify read data,"
                " the decryption password is incorrect or the archive"
                " is corrupted", -1));
            return NULL;
        }
        sizeCompressed = Cookfs_PageObjSize(dataCompressed);
    }

#endif /* COOKFS_USECCRYPTO */

    Cookfs_PageObj dataUncompressed;

    if (compression == COOKFS_COMPRESSION_NONE) {
        dataUncompressed = dataCompressed;
        CookfsLog(printf("use raw data"));
        goto skipUncompress;
    }

    // Alloc memory for decompressed data
    dataUncompressed = Cookfs_PageObjAlloc(sizeUncompressed);
    if (dataUncompressed == NULL) {
        CookfsLog(printf("ERROR: unable to alloc %d bytes for page",
            sizeUncompressed));
        SET_ERROR(Tcl_ObjPrintf("Cookfs_ReadPage(): unable to alloc %d bytes"
                " for page", sizeUncompressed));
        Cookfs_PageObjBounceRefCount(dataCompressed);
        return NULL;
    }

    CookfsLog(printf("uncompress data..."));

    int rc = TCL_ERROR;

    if (err != NULL && *err != NULL) {
        Tcl_BounceRefCount(*err);
        *err = NULL;
    }

    switch (compression) {
    case COOKFS_COMPRESSION_ZLIB:
        rc = CookfsReadPageZlib(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#if defined(COOKFS_USECALLBACKS)
    case COOKFS_COMPRESSION_CUSTOM:
        rc = CookfsReadPageCustom(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#endif /* COOKFS_USECALLBACKS */
#ifdef COOKFS_USEBZ2
    case COOKFS_COMPRESSION_BZ2:
        rc = CookfsReadPageBz2(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#endif /* COOKFS_USEBZ2 */
#ifdef COOKFS_USELZMA
    case COOKFS_COMPRESSION_LZMA:
        rc = CookfsReadPageLzma(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#endif /* COOKFS_USELZMA */
#ifdef COOKFS_USEZSTD
    case COOKFS_COMPRESSION_ZSTD:
        rc = CookfsReadPageZstd(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#endif /* COOKFS_USEZSTD */
#ifdef COOKFS_USEBROTLI
    case COOKFS_COMPRESSION_BROTLI:
        rc = CookfsReadPageBrotli(p, dataCompressed->buf, sizeCompressed,
            dataUncompressed->buf, sizeUncompressed, err);
        break;
#endif /* COOKFS_USEBROTLI */
    default:
        assert(1 && "Unsupported compression");
        break;
    }

    Cookfs_PageObjBounceRefCount(dataCompressed);

    if (rc != TCL_OK) {
        Cookfs_PageObjBounceRefCount(dataUncompressed);
        if (err == NULL) {
            CookfsLog(printf("return: ERROR"));
        } else {
            if (*err == NULL) {
                *err = Tcl_NewStringObj("decompression failed", -1);
                CookfsLog(printf("return: ERROR (decompression failed)"));
            } else {
                CookfsLog(printf("return: ERROR (%s)", Tcl_GetString(*err)));
            }
        }
        return NULL;
    }

skipUncompress: ; // empty statement

    if (!decompress) {
        CookfsLog(printf("we were called without decompression, skip hash"
            " verification"));
        // If we don't do decompression of the page, when we cannot check
        // its hash.
        goto skipHashCheck;
    }

    unsigned char md5sum_current[16];

    // It is possible that we should not check the page hash. For example,
    // if the archive was generated by the cookfswriter.tcl script.
    // In this case, the MD5 hash will be filled with zeros.
    // So, now we will fill md5sum_current with zeros and then compare it
    // with the given hash. If they match, then the given hash is filled
    // with zeros and we will not check it.
    memset(md5sum_current, 0, sizeof(md5sum_current));
    if (memcmp(md5sum_current, md5hash, 16) == 0) {
        CookfsLog(printf("zero hash was given, skip hash verification"));
        // If we don't do decompression of the page, when we cannot check
        // its hash.
        goto skipHashCheck;
    }

    Cookfs_PagesCalculateHash(p, dataUncompressed->buf,
        Cookfs_PageObjSize(dataUncompressed), md5sum_current);

    if (memcmp(md5sum_current, md5hash, 16) != 0) {
        Cookfs_PageObjBounceRefCount(dataUncompressed);
        CookfsLog(printf("return: ERROR (hash doesn't match)"));
#ifdef COOKFS_USECCRYPTO
        if (encrypted) {
            goto decryptionFailed;
        } else {
#endif /* COOKFS_USECCRYPTO */
            SET_ERROR(Tcl_NewStringObj("failed to verify read data, archive"
                " may be corrupted", -1));
#ifdef COOKFS_USECCRYPTO
        }
#endif /* COOKFS_USECCRYPTO */
        return NULL;
    }

skipHashCheck:

    // CookfsDump(dataUncompressed, Cookfs_PageObjSize(dataUncompressed));

    Cookfs_PageObjIncrRefCount(dataUncompressed);
    CookfsLog(printf("return: ok"));
    return dataUncompressed;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_SeekToPage --
 *
 *      Seek to offset where specified page is / should be
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void Cookfs_SeekToPage(Cookfs_Pages *p, int idx) {
    Tcl_WideInt offset = Cookfs_PagesGetPageOffset(p, idx);
    Tcl_Seek(p->fileChannel, offset, SEEK_SET);
    CookfsLog(printf("seek to page %d offset %" TCL_LL_MODIFIER "d", idx,
        offset))
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_WritePage --
 *
 *      Optionally compress and write page data
 *
 *      If bytesCompressed is specified, the page is written as
 *      compressed or uncompressed, depending on size
 *
 * Results:
 *      Size of page after compression
 *
 * Side effects:
 *      pgCompressed will be released if its refcount is zero
 *
 *----------------------------------------------------------------------
 */

Tcl_Size Cookfs_WritePage(Cookfs_Pages *p, int idx, unsigned char *bytes,
    Tcl_Size sizeUncompressed, unsigned char *md5hash,
    Cookfs_PageObj pgCompressed)
{

    CookfsLog(printf("page index #%d, original size: %" TCL_SIZE_MODIFIER "d",
        idx, sizeUncompressed));

    // Add initial stamp if needed
    Cookfs_PageAddStamp(p, 0);

    CookfsLog(printf("fileLastOp: %d", p->fileLastOp));
    /* if last operation was not write, we need to seek
     * to make sure we're at location where we should be writing */
    if ((idx >= 0) && (p->fileLastOp != COOKFS_LASTOP_WRITE)) {
        p->fileLastOp = COOKFS_LASTOP_WRITE;
        Cookfs_SeekToPage(p, idx);
    }

    Cookfs_CompressionType resultCompression = p->currentCompression;
    int resultCompressionLevel = p->currentCompressionLevel;
    Tcl_Size resultSize;

    if (sizeUncompressed <= 0) {
        CookfsLog(printf("data size is zero, skip compression"));
        resultSize = 0;
        goto done;
    }

    if (pgCompressed != NULL) {
        CookfsLog(printf("compression data is specified, skip compression"));
        goto skipCompression;
    }

    switch (resultCompression) {
    case COOKFS_COMPRESSION_ZLIB:
        pgCompressed = CookfsWritePageZlib(p, bytes, sizeUncompressed);
        break;
#if defined(COOKFS_USECALLBACKS)
    case COOKFS_COMPRESSION_CUSTOM:
        pgCompressed = CookfsWritePageCustom(p, bytes, sizeUncompressed);
        break;
#endif /* COOKFS_USECALLBACKS */
#ifdef COOKFS_USEBZ2
    case COOKFS_COMPRESSION_BZ2:
        pgCompressed = CookfsWritePageBz2(p, bytes, sizeUncompressed);
        break;
#endif /* COOKFS_USEBZ2 */
#ifdef COOKFS_USELZMA
    case COOKFS_COMPRESSION_LZMA:
        pgCompressed = CookfsWritePageLzma(p, bytes, sizeUncompressed);
        break;
#endif /* COOKFS_USELZMA */
#ifdef COOKFS_USEZSTD
    case COOKFS_COMPRESSION_ZSTD:
        pgCompressed = CookfsWritePageZstd(p, bytes, sizeUncompressed);
        break;
#endif /* COOKFS_USEZSTD */
#ifdef COOKFS_USEBROTLI
    case COOKFS_COMPRESSION_BROTLI:
        pgCompressed = CookfsWritePageBrotli(p, bytes, sizeUncompressed);
        break;
#endif /* COOKFS_USEZSTD */
    default:
        assert(1 && "Unsupported compression");
        break;
    };

skipCompression:

    if (pgCompressed != NULL) {
        CookfsLog(printf("got %" TCL_SIZE_MODIFIER "d bytes from compression"
            " engine", Cookfs_PageObjSize(pgCompressed)));
        if (!SHOULD_COMPRESS(p, sizeUncompressed,
            Cookfs_PageObjSize(pgCompressed)))
        {
            CookfsLog(printf("compression is inefficient, store as"
                " uncompressed"));
            Cookfs_PageObjBounceRefCount(pgCompressed);
            pgCompressed = NULL;
        }
    }

    if (pgCompressed == NULL) {
        CookfsLog(printf("there is no compressed data, create output buffer"
            " from original data"));
        pgCompressed = Cookfs_PageObjNewFromString(bytes, sizeUncompressed);
        if (pgCompressed == NULL) {
            Tcl_Panic("Cookfs_WritePage(): failed to alloc");
            // Just to make cppcheck happy. It doesn't realize that Tcl_Panic
            // closes the application.
            return 0;
        }
        resultCompression = COOKFS_COMPRESSION_NONE;
        resultCompressionLevel = 0;
    }

#ifdef COOKFS_USECCRYPTO
    if (p->isEncryptionActive) {
        CookfsLog(printf("encrypt the page..."));
        Cookfs_PageObjSetIV(pgCompressed, md5hash);
        Cookfs_AesEncrypt(pgCompressed, p->encryptionKey);
    }
#else
    UNUSED(md5hash);
#endif /* COOKFS_USECCRYPTO */

    resultSize = Cookfs_PageObjSize(pgCompressed);

    // CookfsDump(pgCompressed, resultSize);

    Tcl_Size written = Tcl_Write(p->fileChannel,
        (const char *)pgCompressed->buf, resultSize);

    CookfsLog(printf("wrote %" TCL_SIZE_MODIFIER "d bytes", written));

    if (written != resultSize) {
        Tcl_Panic("Cookfs_WritePage(): failed to write");
    }

    Cookfs_PageObjBounceRefCount(pgCompressed);

done:
    Cookfs_PgIndexSetCompression(p->pagesIndex, idx, resultCompression,
        resultCompressionLevel);
    Cookfs_PgIndexSetSizeCompressed(p->pagesIndex, idx, resultSize);
    return resultSize;
}


int Cookfs_WritePageObj(Cookfs_Pages *p, int idx, Cookfs_PageObj data,
    unsigned char *md5hash)
{
    CookfsLog(printf("data: %p", (void *)data->buf));
    return Cookfs_WritePage(p, idx, data->buf, Cookfs_PageObjSize(data),
        md5hash, NULL);
}

int Cookfs_WriteTclObj(Cookfs_Pages *p, int idx, Tcl_Obj *data, Tcl_Obj *compressedData) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(data, &size);
    CookfsLog(printf("data: %p", (void *)bytes));
    // This function is only used to store the compression result for asynchronous
    // compression. In this case, the page to be stored is already registered
    // in pgindex. Thus, we can use md5hash info from pgindex.
    return Cookfs_WritePage(p, idx, bytes, size,
        Cookfs_PgIndexGetHashMD5(p->pagesIndex, idx),
        Cookfs_PageObjNewFromByteArray(compressedData));
}

/* definitions of static and/or internal functions */

#if defined(COOKFS_USECALLBACKS)

/*
 *----------------------------------------------------------------------
 *
 * CookfsCreateCompressionCommand --
 *
 *      Copy specified command and store length of entire command
 *
 * Results:
 *      Pointer to array of Tcl_Obj for the command; placeholder for
 *      actual data to (de)compress is added as last element of the list
 *
 * Side effects:
 *      In case of error, interp will be set with proper error message
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj **CookfsCreateCompressionCommand(Tcl_Interp *interp, Tcl_Obj *cmd, int *lenPtr, int additionalElements) {
    Tcl_Obj **rc;
    Tcl_Obj **listObjv;
    Tcl_Size listObjc;
    Tcl_Size i;

    /* get command as list of attributes */
    if (Tcl_ListObjGetElements(interp, cmd, &listObjc, &listObjv) != TCL_OK) {
        return NULL;
    }

    rc = (Tcl_Obj **) ckalloc(sizeof(Tcl_Obj *) * (listObjc + additionalElements));
    for (i = 0; i < listObjc; i++) {
        rc[i] = listObjv[i];
        Tcl_IncrRefCount(rc[i]);
    }
    rc[listObjc] = NULL;
    *lenPtr = listObjc + additionalElements;
    return rc;
}
#endif /* COOKFS_USECALLBACKS */


#ifdef USE_VFS_COMMANDS_FOR_ZIP

/*
 *----------------------------------------------------------------------
 *
 * CookfsCheckCommandExists --
 *
 *      Checks whether specified command exists, providing Tcl 8.4
 *      backwards compatibility
 *
 * Results:
 *      non-zero if command exists; 0 otherwise
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int CookfsCheckCommandExists(Tcl_Interp *interp, const char *commandName)
{
    int rc;
    Tcl_CmdInfo info;

    rc = Tcl_GetCommandInfo(interp, commandName, &info);

    return rc;
}

#endif

