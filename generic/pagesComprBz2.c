/*
 * pagesComprBz2.c
 *
 * Provides bzip2 functions for pages compression
 *
 * (c) 2010-2011 Wojciech Kocjan, Pawel Salawa
 * (c) 2011-2014 Wojciech Kocjan
 * (c) 2024 Konstantin Kushnir
 */

#include "../bzip2/bzlib.h"
#include "cookfs.h"
#include "pages.h"
#include "pagesInt.h"
#include "pagesCompr.h"
#include "pagesComprBz2.h"

/*
 *----------------------------------------------------------------------
 *
 * CookfsReadPageBz2 --
 *
 *	Read bzip2 compressed page
 *
 * Results:
 *	Binary data as Tcl_Obj
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_PageObj CookfsReadPageBz2(Cookfs_Pages *p, int size, Tcl_Obj **err) {
    UNUSED(err);
    int destSize;
    int count;
    unsigned char *source;
    Tcl_Obj *data;

    data = Tcl_NewObj();
    Tcl_IncrRefCount(data);
    count = Tcl_ReadChars(p->fileChannel, data, size, 0);

    if (count != size) {
	Tcl_DecrRefCount(data);
	return NULL;
    }

    source = Tcl_GetByteArrayFromObj(data, NULL);
    if (source == NULL) {
	Tcl_DecrRefCount(data);
	CookfsLog(printf("CookfsReadPageBz2: Tcl_GetByteArrayFromObj failed"))
	return NULL;
    }

    Cookfs_Binary2Int(source, &destSize, 1);
    Cookfs_PageObj destObj = Cookfs_PageObjAlloc(destSize);
    if (destObj == NULL) {
	Tcl_DecrRefCount(data);
	CookfsLog(printf("CookfsReadPageBz2: failed to alloc"))
	return NULL;
    }

    CookfsLog(printf("CookfsReadPageBz2: uncompressed size=%d from %d", destSize, size))

    if (BZ2_bzBuffToBuffDecompress((char *) destObj, (unsigned int *) &destSize, (char *) source + 4, (unsigned int) size - 4, 0, 0) != BZ_OK) {
	Tcl_DecrRefCount(data);
	Cookfs_PageObjIncrRefCount(destObj);
	Cookfs_PageObjDecrRefCount(destObj);
	CookfsLog(printf("CookfsReadPageBz2: BZ2_bzBuffToBuffDecompress failed"))
	return NULL;
    }

    Tcl_DecrRefCount(data);

    return destObj;
}

/*
 *----------------------------------------------------------------------
 *
 * CookfsWritePageBz2 --
 *
 *	Write page using bzip2 compression
 *
 * Results:
 *	Number of bytes written; -1 in case compression failed or
 *	compressing was not efficient enough (see SHOULD_COMPRESS macro)
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int CookfsWritePageBz2(Cookfs_Pages *p, unsigned char *bytes, int origSize) {
    int size;
    unsigned char *source;
    unsigned char *dest;
    Tcl_Obj *destObj;

    int level = p->fileCompressionLevel;
    if (level < 1) {
        level = 1;
    } else if (level >= 255) {
        level = 9;
    }

    source = bytes;
    destObj = Tcl_NewByteArrayObj(NULL, 0);
    size = origSize * 2 + 1024;
    Tcl_SetByteArrayLength(destObj, size + 4);
    dest = Tcl_GetByteArrayFromObj(destObj, NULL);

    Cookfs_Int2Binary(&origSize, (unsigned char *) dest, 1);
    if (BZ2_bzBuffToBuffCompress((char *) (dest + 4), (unsigned int *) &size, (char *) source, (unsigned int) origSize, level, 0, 0) != BZ_OK) {
	CookfsLog(printf("Cookfs_WritePage: BZ2_bzBuffToBuffCompress failed"))
	return -1;
    }

    CookfsLog(printf("Cookfs_WritePage: size=%d (to %d)", origSize, size))
    size += 4;
    Tcl_SetByteArrayLength(destObj, size);

    Tcl_IncrRefCount(destObj);
    if (SHOULD_COMPRESS(p, origSize, size)) {
	CookfsWriteCompression(p, COOKFS_COMPRESSION_BZ2);
	Tcl_WriteObj(p->fileChannel, destObj);
    }  else  {
	size = -1;
    }
    Tcl_DecrRefCount(destObj);

    return size;
}

