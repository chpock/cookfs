/*
 * pgindex.c
 *
 * Provides functions creating and managing a Cookfs_PgIndex object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pgindex.h"
#include "pagesCompr.h"

// How many entries are allocated at one time to reduce re-allocation
// of memory.
#define COOKFS_PGINDEX_ALLOC_SIZE 256

struct Cookfs_PgIndexEntry {
    Cookfs_CompressionType compression;
    int compressionLevel;
    int encryption;
    unsigned char hashMD5[16];
    int sizeCompressed;
    int sizeUncompressed;
    Tcl_WideInt offset;
};

struct _Cookfs_PgIndex {
    int pagesCount;
    int pagesAllocated;
    Cookfs_PgIndexEntry *data;
    Cookfs_PgIndexEntry special[COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_COUNT];
};

TYPEDEF_ENUM_COUNT(Cookfs_PgIndexPageInfoKeys, COOKFS_PGINDEX_INFO_KEY_COUNT,
    COOKFS_PGINDEX_INFO_KEY_OFFSET,
    COOKFS_PGINDEX_INFO_KEY_SIZEUNCOMPRESSED,
    COOKFS_PGINDEX_INFO_KEY_SIZECOMPRESSED,
    COOKFS_PGINDEX_INFO_KEY_ENCRYPTED,
    COOKFS_PGINDEX_INFO_KEY_COMPRESSION,
    COOKFS_PGINDEX_INFO_KEY_INDEX
);

static const char *info_key_string[COOKFS_PGINDEX_INFO_KEY_COUNT] = {
    "offset", "sizeUncompressed", "sizeCompressed", "encrypted",
    "compression", "index"
};

typedef struct ThreadSpecificData {
    Tcl_Obj *info_key[COOKFS_PGINDEX_INFO_KEY_COUNT];
    int initialized;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKeyCookfs;

#define TCL_TSD_INIT(keyPtr) \
    (ThreadSpecificData *)Tcl_GetThreadData((keyPtr), sizeof(ThreadSpecificData))

void CookfsPgIndexThreadExit(ClientData clientData) {

    UNUSED(clientData);

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    for (int i = 0; i < COOKFS_PGINDEX_INFO_KEY_COUNT; i++) {
        if (tsdPtr->info_key[i] != NULL) {
            Tcl_DecrRefCount(tsdPtr->info_key[i]);
            tsdPtr->info_key[i] = NULL;
        }
    }

}

Tcl_Obj *Cookfs_PgIndexGetInfo(Cookfs_PgIndex *pgi, int num) {

    Cookfs_PgIndexEntry *pge;
    int specialIndex;

    const char *specialIndexName[COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_COUNT] = {
        [COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_PGINDEX] = "pgindex",
        [COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_FSINDEX] = "fsindex"
    };

    if (num >= 0) {
        CookfsLog(printf("info about page #%d", num));
        assert(num < pgi->pagesCount);
        pge = pgi->data + num;
    } else {

        specialIndex = -1 - num;

        CookfsLog(printf("info about special page %s (#%d)",
            specialIndex == COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_PGINDEX ?
                "PGINDEX" :
            specialIndex == COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_PGINDEX ?
                "FSINDEX" :
            "UNKNOWN", specialIndex));

        assert(specialIndex >= 0 &&
            specialIndex < COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_COUNT);

        pge = &pgi->special[specialIndex];

    }

    ThreadSpecificData *tsdPtr = TCL_TSD_INIT(&dataKeyCookfs);

    if (!tsdPtr->initialized) {
        Tcl_CreateThreadExitHandler(CookfsPgIndexThreadExit, NULL);
        tsdPtr->initialized = 1;
    }

    int i;

    if (tsdPtr->info_key[0] == NULL) {
        for (i = 0; i < COOKFS_PGINDEX_INFO_KEY_COUNT; i++) {
            tsdPtr->info_key[i] = Tcl_NewStringObj(info_key_string[i], -1);
            Tcl_IncrRefCount(tsdPtr->info_key[i]);
        }
    }

    Tcl_Obj *result = Tcl_NewDictObj();

    for (i = 0; i < COOKFS_PGINDEX_INFO_KEY_COUNT; i++) {

        Tcl_Obj *val;

        switch ((Cookfs_PgIndexPageInfoKeys)i) {
        case COOKFS_PGINDEX_INFO_KEY_OFFSET:
            val = Tcl_NewWideIntObj(num < 0 ? pge->offset :
                Cookfs_PgIndexGetStartOffset(pgi, num));
            break;
        case COOKFS_PGINDEX_INFO_KEY_SIZEUNCOMPRESSED:
            val = Tcl_NewIntObj(pge->sizeUncompressed);
            break;
        case COOKFS_PGINDEX_INFO_KEY_SIZECOMPRESSED:
            val = Tcl_NewIntObj(pge->sizeCompressed);
            break;
        case COOKFS_PGINDEX_INFO_KEY_ENCRYPTED:
            val = Tcl_NewBooleanObj(pge->encryption);
            break;
        case COOKFS_PGINDEX_INFO_KEY_COMPRESSION:
            val = Cookfs_CompressionToObj(pge->compression,
                pge->compressionLevel);
            break;
        case COOKFS_PGINDEX_INFO_KEY_INDEX:
            if (num < 0) {
                val = Tcl_NewStringObj(specialIndexName[specialIndex], -1);
            } else {
                val = Tcl_NewIntObj(num);
            }
            break;
        }

        Tcl_DictObjPut(NULL, result, tsdPtr->info_key[i], val);

    }

    return result;

}

unsigned char *Cookfs_PgIndexGetHashMD5(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->hashMD5;
}

Cookfs_CompressionType Cookfs_PgIndexGetCompression(Cookfs_PgIndex *pgi,
    int num)
{
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->compression;
}

int Cookfs_PgIndexGetEncryption(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->encryption;
}

int Cookfs_PgIndexGetCompressionLevel(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->compressionLevel;
}

int Cookfs_PgIndexGetSizeCompressed(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->sizeCompressed;
}

int Cookfs_PgIndexGetSizeUncompressed(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->sizeUncompressed;
}

Tcl_WideInt Cookfs_PgIndexGetEndOffset(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    if (pge->sizeCompressed < 0) {
        Tcl_Panic("could not calculate end offset of the page #%d because its"
            " size is unknown", num);
    }
    return Cookfs_PgIndexGetStartOffset(pgi, num) + pge->sizeCompressed;
}

Tcl_WideInt Cookfs_PgIndexGetStartOffset(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num <= pgi->pagesCount);
    // If we want to get the offset of the first page, it is always 0.
    // Also, this case will work if the total number of pages is zero and
    // we want to get the 0th page offset.
    if (num == 0) {
        return 0;
    }
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    // If we want to get an offset beyond the available pages, that means we
    // want to get the offset of the end of the last page. If we want to get
    // the offset of an existing page, but it is not yet known, we will
    // calculate it based on the previous page.
    if (num >= pgi->pagesCount || pge->offset < 0) {
        Tcl_WideInt offsetEndPrevious = Cookfs_PgIndexGetEndOffset(pgi, num - 1);
        // If we want to get an offset beyond the available pages, just return
        // the offset of the end of the last page.
        if (num >= pgi->pagesCount) {
            return offsetEndPrevious;
        }
        // If we are here, that means we are getting an offset of an existing
        // page. We have to update it in the page entry.
        pge->offset = offsetEndPrevious;
    }
    return pge->offset;
}

void Cookfs_PgIndexSetCompression(Cookfs_PgIndex *pgi, int num,
    Cookfs_CompressionType compression, int compressionLevel)
{
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    CookfsLog(printf("page#%d: set compression %d, compression level %d",
        num, compression, compressionLevel));
    pge->compression = compression;
    pge->compressionLevel = compressionLevel;
}

void Cookfs_PgIndexSetEncryption(Cookfs_PgIndex *pgi, int num, int encryption) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    CookfsLog(printf("page#%d: set encryption %d", num, encryption));
    pge->encryption = encryption;
}

void Cookfs_PgIndexSetSizeCompressed(Cookfs_PgIndex *pgi, int num,
    int sizeCompressed)
{
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    CookfsLog(printf("page#%d: set compressed size %d", num,
        sizeCompressed));
    pge->sizeCompressed = sizeCompressed;
}

int Cookfs_PgIndexSearchByMD5(Cookfs_PgIndex *pgi, unsigned char *hashMD5,
    int sizeUncompressed, int *index)
{
    int currentIndex = *index;
    Cookfs_PgIndexEntry *pge = pgi->data + currentIndex;
    while (currentIndex < pgi->pagesCount) {
        if (pge->sizeUncompressed == sizeUncompressed &&
            memcmp(pge->hashMD5, hashMD5, 16) == 0)
        {
            *index = currentIndex;
            return 1;
        }
        currentIndex++;
        pge++;
    }
    return 0;
}

Cookfs_PgIndex *Cookfs_PgIndexInit(unsigned int initialPagesCount) {

    CookfsLog(printf("enter, want to allocate %u page entries",
        initialPagesCount));

    unsigned int allocPagesCount = initialPagesCount;

    if (allocPagesCount < COOKFS_PGINDEX_ALLOC_SIZE) {
        allocPagesCount = COOKFS_PGINDEX_ALLOC_SIZE;
        CookfsLog(printf("extend the requested page entries count to %u",
            allocPagesCount));
    }

    Cookfs_PgIndex *pgi = (Cookfs_PgIndex *)ckalloc(sizeof(struct _Cookfs_PgIndex));
    if (pgi == NULL) {
        CookfsLog(printf("ERROR: failed to alloc Cookfs_PgIndex"));
        return NULL;
    }

    pgi->data = (Cookfs_PgIndexEntry *)ckalloc(sizeof(Cookfs_PgIndexEntry)
        * allocPagesCount);
    if (pgi->data == NULL) {
        CookfsLog(printf("ERROR: failed to alloc Cookfs_PgIndex->data"));
        ckfree(pgi);
        return NULL;
    }

    pgi->pagesCount = initialPagesCount;
    pgi->pagesAllocated = allocPagesCount;

    for (int i = 0; i < COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_COUNT; i++) {
        pgi->special[i].compression = COOKFS_COMPRESSION_NONE;
        pgi->special[i].compressionLevel = 0;
        pgi->special[i].encryption = 0;
        pgi->special[i].sizeCompressed = -1;
        pgi->special[i].sizeUncompressed = -1;
        pgi->special[i].offset = -1;
    }

    CookfsLog(printf("return: ok [%p]", (void *)pgi));
    return pgi;

}

void Cookfs_PgIndexFini(Cookfs_PgIndex *pgi) {
    CookfsLog(printf("release [%p]", (void *)pgi));
    ckfree(pgi->data);
    ckfree(pgi);
}

void Cookfs_PgIndexAddPageSpecial(Cookfs_PgIndex *pgi,
    Cookfs_CompressionType compression, int compressionLevel, int encryption,
    int sizeCompressed, int sizeUncompressed, Tcl_WideInt offset,
    Cookfs_PgIndexSpecialPageType type)
{

    CookfsLog(printf("compression: %d, level: %d, encryption: %d,"
        " sizeCompressed: %d, sizeUncompressed: %d, offset: %" TCL_LL_MODIFIER
        "d, type: %s", compression, compressionLevel, encryption,
        sizeCompressed, sizeUncompressed, offset,
        type == COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_PGINDEX ? "PGINDEX" :
        type == COOKFS_PGINDEX_SPECIAL_PAGE_TYPE_FSINDEX ? "FSINDEX" :
        "UNKNOWN"));

    pgi->special[type].compression = compression;
    pgi->special[type].compressionLevel = compressionLevel;
    pgi->special[type].encryption = encryption;
    pgi->special[type].sizeCompressed = sizeCompressed;
    pgi->special[type].sizeUncompressed = sizeUncompressed;
    pgi->special[type].offset = offset;

}

int Cookfs_PgIndexAddPage(Cookfs_PgIndex *pgi,
    Cookfs_CompressionType compression, int compressionLevel, int encryption,
    int sizeCompressed, int sizeUncompressed, unsigned char *hashMD5)
{

    CookfsLog(printf("enter - compression: %d, level: %d, encryption: %d,"
        " sizeCompressed: %d, sizeUncompressed: %d, MD5[" PRINTF_MD5_FORMAT
        "]", compression, compressionLevel, encryption, sizeCompressed,
        sizeUncompressed, PRINTF_MD5_VAR(hashMD5)));

    if (pgi->pagesCount >= pgi->pagesAllocated) {

        CookfsLog(printf("need to realloc from %d to %d", pgi->pagesAllocated,
            pgi->pagesAllocated + COOKFS_PGINDEX_ALLOC_SIZE));

        pgi->pagesAllocated += COOKFS_PGINDEX_ALLOC_SIZE;

        pgi->data = (Cookfs_PgIndexEntry *)ckrealloc(pgi->data,
            sizeof(Cookfs_PgIndexEntry) * pgi->pagesAllocated);

        if (pgi->data == NULL) {
            Tcl_Panic("Cookfs_PgIndexAddPage() failed to alloc");
            return -1;
        }

    }

    Cookfs_PgIndexEntry *pge = pgi->data + pgi->pagesCount;

    pge->compression = compression;
    pge->compressionLevel = compressionLevel;
    pge->encryption = encryption;
    pge->sizeCompressed = sizeCompressed;
    pge->sizeUncompressed = sizeUncompressed;

    if (pgi->pagesCount == 0) {
        pge->offset = 0;
    } else {
        // The actual offset will be calculated later when it is needed.
        pge->offset = -1;
    }

    memcpy(pge->hashMD5, hashMD5, 16);

    CookfsLog(printf("return: ok - page#%d", pgi->pagesCount));

    // Return the current value of pagesCount as the page index, and then
    // increment the total number of pages.
    return pgi->pagesCount++;

}

// 1   byte  - compression
// 1   byte  - compression level
// 1   byte  - encryption
// 4   bytes - sizeCompressed
// 4   bytes - sizeUncompressed
// 16  bytes - hashMD5
// Total: 27 bytes
#define COOKFS_PGINDEX_RECORD_SIZE 27

Cookfs_PgIndex *Cookfs_PgIndexImport(unsigned char *bytes, int size,
    Tcl_Obj **err)
{

    CookfsLog(printf("import from buffer %p size %d", (void *)bytes,
        size));

    // We need at least 4 bytes in the buffer for number of page entries.
    if (size < 4) {
        CookfsLog(printf("ERROR: the buffet size is less than 4 bytes"));
        goto malformed;
    }

    unsigned int pagesCount;
    Cookfs_Binary2Int(bytes, (int *)&pagesCount, 1);
    bytes += 4;
    CookfsLog(printf("total number of pages: %u", pagesCount));

    // The buffer must have exactly 4 (pagesCount) +
    // COOKFS_PGINDEX_RECORD_SIZE*pagesCount bytes. Otherwise, consider it
    // malformed.
    if ((unsigned int)size != (4 + pagesCount * COOKFS_PGINDEX_RECORD_SIZE)) {
        CookfsLog(printf("ERROR: not expected amount of bytes in buffer,"
            " expected: 4 + number_of_pages * %d = %d",
            (int)COOKFS_PGINDEX_RECORD_SIZE,
            (int)(4 + pagesCount * COOKFS_PGINDEX_RECORD_SIZE)));
        goto malformed;
    }

    goto not_malformed;

malformed:
    if (err != NULL) {
        *err = Tcl_NewStringObj("pages entry index is malformed", -1);
    }
    return NULL;

not_malformed: ; // empty statement

    Cookfs_PgIndex *pgi = Cookfs_PgIndexInit(pagesCount);
    if (pgi == NULL) {
        if (err != NULL) {
            *err = Tcl_ObjPrintf("failed to alloc pages index with %u entries",
                pagesCount);
        }
        return NULL;
    }

    Cookfs_PgIndexEntry *pge = pgi->data;
    Tcl_WideInt offset = 0;
    for (unsigned int i = 0; i < pagesCount; i++, pge++) {

        pge->compression = (Cookfs_CompressionType)bytes[0 + i];
        pge->compressionLevel = bytes[(1 * pagesCount) + i];
        pge->encryption = bytes[(2 * pagesCount) + i];
        Cookfs_Binary2Int(&bytes[(3 * pagesCount) + (i * 4)], &pge->sizeCompressed, 1);
        Cookfs_Binary2Int(&bytes[(7 * pagesCount) + (i * 4)], &pge->sizeUncompressed, 1);
        memcpy(pge->hashMD5, &bytes[(11 * pagesCount) + (i * 16)], 16);

        pge->offset = offset;
        offset += pge->sizeCompressed;

        CookfsLog(printf("import entry #%u - compression: %d, level: %d,"
            " encryption: %d, sizeCompressed: %d, sizeUncompressed: %d,"
            " MD5[" PRINTF_MD5_FORMAT "]", i, (int)pge->compression,
            pge->compressionLevel, pge->encryption, pge->sizeCompressed,
            pge->sizeUncompressed, PRINTF_MD5_VAR(pge->hashMD5)));

    }

    CookfsLog(printf("return: ok"));

    return pgi;

}

Cookfs_PageObj Cookfs_PgIndexExport(Cookfs_PgIndex *pgi) {

    unsigned int pagesCount = pgi->pagesCount;

    CookfsLog(printf("enter, export %u page entries", pagesCount));

    Cookfs_PageObj pgo = Cookfs_PageObjAlloc(4 + pagesCount *
        COOKFS_PGINDEX_RECORD_SIZE);
    if (pgo == NULL) {
        Tcl_Panic("Cookfs_PgIndexExport(): could not alloc page object");
        // This return is for cppcheck
        return NULL;
    }

    unsigned char *buf = pgo->buf;

    Cookfs_Int2Binary((int *)&pagesCount, buf, 1);

    // Pre-calculate the indexes to avoid these calculations inside the loop
    int idxCompressionLevel = 1 * pagesCount;
    int idxEncryption = 2 * pagesCount;
    int idxSizeCompressed = 3 * pagesCount;
    int idxSizeUncompressed = 7 * pagesCount;
    int idxHash = 11 * pagesCount;

    // Skip pages count in the first 4 bytes of the buffer
    buf += 4;

    Cookfs_PgIndexEntry *pge = pgi->data;
    for (unsigned int i = 0; i < pagesCount; i++, pge++) {
        buf[0 + i] = (unsigned char)pge->compression;
        buf[idxCompressionLevel + i] = pge->compressionLevel;
        buf[idxEncryption + i] = pge->encryption;
        Cookfs_Int2Binary(&pge->sizeCompressed, &buf[idxSizeCompressed + (i * 4)], 1);
        Cookfs_Int2Binary(&pge->sizeUncompressed, &buf[idxSizeUncompressed + (i * 4)], 1);
        memcpy(&buf[idxHash + (i * 16)], pge->hashMD5, 16);

        CookfsLog(printf("export entry #%u - compression: %d, level: %d,"
            " encryption: %d, sizeCompressed: %d, sizeUncompressed: %d,"
            " MD5[" PRINTF_MD5_FORMAT "]", i, (int)pge->compression,
            pge->compressionLevel, pge->encryption, pge->sizeCompressed,
            pge->sizeUncompressed, PRINTF_MD5_VAR(pge->hashMD5)));
    }

    CookfsLog(printf("return: ok"));

    return pgo;

}

int Cookfs_PgIndexGetLength(Cookfs_PgIndex *pgi) {
    return pgi->pagesCount;
}
