/*
 * pgindex.c
 *
 * Provides functions creating and managing a Cookfs_PgIndex object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "pgindex.h"

// How many entries are allocated at one time to reduce re-allocation
// of memory.
#define COOKFS_PGINDEX_ALLOC_SIZE 256

typedef struct Cookfs_PgIndexEntry {
    int compression;
    int compressionLevel;
    int encryption;
    unsigned char hashMD5[16];
    int sizeCompressed;
    int sizeUncompressed;
    Tcl_WideInt offset;
} Cookfs_PgIndexEntry;

struct _Cookfs_PgIndex {
    int pagesCount;
    int pagesAllocated;
    Cookfs_PgIndexEntry *data;
};

unsigned char *Cookfs_PgIndexGetHashMD5(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->hashMD5;
}

int Cookfs_PgIndexGetCompression(Cookfs_PgIndex *pgi, int num) {
    assert(num >= 0 && num < pgi->pagesCount);
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    return pge->compression;
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
    int compression, int compressionLevel)
{
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    CookfsLog2(printf("[page#%d] set compression %d, compression level %d",
        num, compression, compressionLevel));
    pge->compression = compression;
    pge->compressionLevel = compressionLevel;
}

void Cookfs_PgIndexSetSizeCompressed(Cookfs_PgIndex *pgi, int num,
    int sizeCompressed)
{
    Cookfs_PgIndexEntry *pge = pgi->data + num;
    CookfsLog2(printf("[page#%d] set compressed size %d", num,
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

    CookfsLog2(printf("enter, want to allocate %u page entries",
        initialPagesCount));

    unsigned int allocPagesCount = initialPagesCount;

    if (allocPagesCount < COOKFS_PGINDEX_ALLOC_SIZE) {
        allocPagesCount = COOKFS_PGINDEX_ALLOC_SIZE;
        CookfsLog2(printf("extend the requested page entries count to %u",
            allocPagesCount));
    }

    Cookfs_PgIndex *pgi = (Cookfs_PgIndex *)ckalloc(sizeof(struct _Cookfs_PgIndex));
    if (pgi == NULL) {
        CookfsLog2(printf("ERROR: failed to alloc Cookfs_PgIndex"));
        return NULL;
    }

    pgi->data = (Cookfs_PgIndexEntry *)ckalloc(sizeof(Cookfs_PgIndexEntry)
        * allocPagesCount);
    if (pgi->data == NULL) {
        CookfsLog2(printf("ERROR: failed to alloc Cookfs_PgIndex->data"));
        ckfree(pgi);
        return NULL;
    }

    pgi->pagesCount = initialPagesCount;
    pgi->pagesAllocated = allocPagesCount;

    CookfsLog2(printf("return: ok [%p]", (void *)pgi));
    return pgi;

}

void Cookfs_PgIndexFini(Cookfs_PgIndex *pgi) {
    CookfsLog2(printf("release [%p]", (void *)pgi));
    ckfree(pgi->data);
    ckfree(pgi);
}

#define PRINTF_MD5_FORMAT "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
#define PRINTF_MD5_VAR(x) (x)[0] ,(x)[1], (x)[2], (x)[3],  \
                          (x)[4] ,(x)[5], (x)[6], (x)[7],  \
                          (x)[8] ,(x)[9], (x)[10],(x)[11], \
                          (x)[12],(x)[13],(x)[14],(x)[15]

int Cookfs_PgIndexAddPage(Cookfs_PgIndex *pgi, int compression,
    int compressionLevel, int encryption, int sizeCompressed,
    int sizeUncompressed, unsigned char *hashMD5)
{

    CookfsLog2(printf("enter - compression: %d, level: %d, encryption: %d,"
        " sizeCompressed: %d, sizeUncompressed: %d, MD5[" PRINTF_MD5_FORMAT
        "]", compression, compressionLevel, encryption, sizeCompressed,
        sizeUncompressed, PRINTF_MD5_VAR(hashMD5)));

    if (pgi->pagesCount >= pgi->pagesAllocated) {

        CookfsLog2(printf("need to realloc from %d to %d", pgi->pagesAllocated,
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

    CookfsLog2(printf("return: ok - page#%d", pgi->pagesCount));

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

    CookfsLog2(printf("import from buffer %p size %d", (void *)bytes,
        size));

    // We need at least 4 bytes in the buffer for number of page entries.
    if (size < 4) {
        CookfsLog2(printf("ERROR: the buffet size is less than 4 bytes"));
        goto malformed;
    }

    unsigned int pagesCount;
    Cookfs_Binary2Int(bytes, (int *)&pagesCount, 1);
    bytes += 4;
    CookfsLog2(printf("total number of pages: %u", pagesCount));

    // The buffer must have exactly 4 (pagesCount) +
    // COOKFS_PGINDEX_RECORD_SIZE*pagesCount bytes. Otherwise, consider it
    // malformed.
    if ((unsigned int)size != (4 + pagesCount * COOKFS_PGINDEX_RECORD_SIZE)) {
        CookfsLog2(printf("ERROR: not expected amount of bytes in buffer,"
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

        pge->compression = bytes[0 + i];
        pge->compressionLevel = bytes[(1 * pagesCount) + i];
        pge->encryption = bytes[(2 * pagesCount) + i];
        Cookfs_Binary2Int(&bytes[(3 * pagesCount) + (i * 4)], &pge->sizeCompressed, 1);
        Cookfs_Binary2Int(&bytes[(7 * pagesCount) + (i * 4)], &pge->sizeUncompressed, 1);
        memcpy(pge->hashMD5, &bytes[(11 * pagesCount) + (i * 16)], 16);

        pge->offset = offset;
        offset += pge->sizeCompressed;

        CookfsLog2(printf("import entry #%u - compression: %d, level: %d,"
            " encryption: %d, sizeCompressed: %d, sizeUncompressed: %d,"
            " MD5[" PRINTF_MD5_FORMAT "]", i, pge->compression,
            pge->compressionLevel, pge->encryption, pge->sizeCompressed,
            pge->sizeUncompressed, PRINTF_MD5_VAR(pge->hashMD5)));

    }

    CookfsLog2(printf("return: ok"));

    return pgi;

}

Cookfs_PageObj Cookfs_PgIndexExport(Cookfs_PgIndex *pgi) {

    unsigned int pagesCount = pgi->pagesCount;

    CookfsLog2(printf("enter, export %u page entries", pagesCount));

    Cookfs_PageObj pgo = Cookfs_PageObjAlloc(4 + pagesCount *
        COOKFS_PGINDEX_RECORD_SIZE);
    if (pgo == NULL) {
        Tcl_Panic("Cookfs_PgIndexExport(): could not alloc page object");
    }

    Cookfs_Int2Binary((int *)&pagesCount, pgo, 1);

    Cookfs_PgIndexEntry *pge = pgi->data;
    for (unsigned int i = 0; i < pagesCount; i++, pge++) {
        // The first 4 bytes in the buffer are for specifying the total number
        // of pages. Thus, we need to add 4 for each offset.
        pgo[4 + 0 + i] = pge->compression;
        pgo[4 + (1 * pagesCount) + i] = pge->compressionLevel;
        pgo[4 + (2 * pagesCount) + i] = pge->encryption;
        Cookfs_Int2Binary(&pge->sizeCompressed, &pgo[4 + (3 * pagesCount) + (i * 4)], 1);
        Cookfs_Int2Binary(&pge->sizeUncompressed, &pgo[4 + (7 * pagesCount) + (i * 4)], 1);
        memcpy(&pgo[4 + (11 * pagesCount) + (i * 16)], pge->hashMD5, 16);

        CookfsLog2(printf("export entry #%u - compression: %d, level: %d,"
            " encryption: %d, sizeCompressed: %d, sizeUncompressed: %d,"
            " MD5[" PRINTF_MD5_FORMAT "]", i, pge->compression,
            pge->compressionLevel, pge->encryption, pge->sizeCompressed,
            pge->sizeUncompressed, PRINTF_MD5_VAR(pge->hashMD5)));
    }

    CookfsLog2(printf("return: ok"));

    return pgo;

}

int Cookfs_PgIndexGetLength(Cookfs_PgIndex *pgi) {
    return pgi->pagesCount;
}
