/*
 * fsindexIO.c
 *
 * Provides mechanisms to export/import fsindex from/to binary data
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 * (c) 2024 Konstantin Kushnir
 */


#include "cookfs.h"

#define COOKFS_FSINDEX_MAXENTRYSIZE 8192
#define COOKFS_FSINDEX_BUFFERINCREASE 65536

#define COOKFS_FSINDEX_HEADERSTRING "CFS2.200"
#define COOKFS_FSINDEX_HEADERLENGTH 8

/* declarations of static and/or internal functions */
static int CookfsFsindexExportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, Tcl_Obj *result, int objOffset);
static int CookfsFsindexImportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, unsigned char *bytes, int objLength, int objOffset);
static int CookfsFsindexExportMetadata(Cookfs_Fsindex *fsIndex, Tcl_Obj *result, int objOffset);
static int CookfsFsindexImportMetadata(Cookfs_Fsindex *fsIndex, unsigned char *bytes, int objLength, int objOffset);

/* binary data directory format:
 *  <4:numChildren>
 *    <1:fileNameLength><X:fileName><1:null>
 *    <8:fileTime>
 *    <4:numBlocks>
 *    file: <12xX:data>
 *    dir:  (children - starting with <4:numChildren>)
 *
 *  all file names are exported as UTF-8 string
 *
 *  subdirectories are inlined completely and recursively
 */

#ifdef COOKFS_USECPAGES

Cookfs_PageObj Cookfs_FsindexToPageObj(Cookfs_Fsindex *fsindex) {
    Cookfs_PageObj rc;
    Tcl_Obj *obj = Cookfs_FsindexToObject(fsindex);
    if (obj != NULL) {
        Tcl_IncrRefCount(obj);
        rc = Cookfs_PageObjNewFromByteArray(obj);
        Tcl_DecrRefCount(obj);
    } else {
        rc = NULL;
    }
    return rc;
}

#endif /* COOKFS_USECPAGES */

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexToObject --
 *
 *	Creates a byte array object with fsindex information
 *	stored as platform-independant binary data
 *
 * Results:
 *	Binary data as Tcl_Obj; NULL in case of error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *fsindex) {
    Tcl_Obj *result;
    int objSize;

    /* create initial byte array object with header string and
     * extend its size specified buffer increase */
    result = Tcl_NewByteArrayObj((unsigned char *) COOKFS_FSINDEX_HEADERSTRING, COOKFS_FSINDEX_HEADERLENGTH);
    Tcl_SetByteArrayLength(result, COOKFS_FSINDEX_BUFFERINCREASE);

    /* export root entry into newly created object; all subdirectories
     * are created recursively */
    objSize = CookfsFsindexExportDirectory(fsindex, fsindex->rootItem, result, COOKFS_FSINDEX_HEADERLENGTH);
    Tcl_SetByteArrayLength(result, objSize);

    /* export metadata */
    objSize = CookfsFsindexExportMetadata(fsindex, result, objSize);
    Tcl_SetByteArrayLength(result, objSize);

    Cookfs_FsindexResetChangeCount(fsindex);

    return result;
}

#ifdef COOKFS_USECPAGES

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexFromPages --
 *
 *	Imports fsindex information from pages object
 *
 * Results:
 *	Pointer to imported Cookfs_Fsindex; NULL in case of import error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexFromPages(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Cookfs_Pages *pages) {

    CookfsLog(printf("Cookfs_FsindexFromPages: fsindex [%p] pages [%p]",
        (void *)fsindex, (void *)pages));

    Cookfs_Fsindex *rc;

    CookfsLog(printf("Cookfs_FsindexFromPages: get index data from pages..."));
    Cookfs_PageObj indexDataObj = Cookfs_PagesGetIndex(pages);

    Tcl_Size indexDataLen;
    if (indexDataObj == NULL) {
        CookfsLog(printf("Cookfs_FsindexFromPages: got NULL as index data"));
        indexDataLen = 0;
    } else {
        Cookfs_PageObjIncrRefCount(indexDataObj);
        indexDataLen = Cookfs_PageObjSize(indexDataObj);
        CookfsLog(printf("Cookfs_FsindexFromPages: got index data %"
            TCL_SIZE_MODIFIER "d bytes", indexDataLen));
        // If data is empty, then free the object now as at the end we
        // will release the object only if indexDataLen > 0
        if (!indexDataLen) {
            Cookfs_PageObjDecrRefCount(indexDataObj);
        }
    }

    if (indexDataLen) {
        CookfsLog(printf("Cookfs_FsindexFromPages: import from the object..."));
        rc = Cookfs_FsindexFromPageObj(interp, fsindex, indexDataObj);
        Cookfs_PageObjDecrRefCount(indexDataObj);
    } else {
        if (fsindex == NULL) {
            CookfsLog(printf("Cookfs_FsindexFromPages: create a new clean index"));
            rc = Cookfs_FsindexInit(interp, NULL);
        } else {
            rc = fsindex;
            Cookfs_FsindexCleanup(rc);
            CookfsLog(printf("Cookfs_FsindexFromPages: cleanup the old index"));
        }
    }

    CookfsLog(printf("Cookfs_FsindexFromPages: return [%p]", (void *)rc));
    return rc;
}

Cookfs_Fsindex *Cookfs_FsindexFromPageObj(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Cookfs_PageObj o) {
    return Cookfs_FsindexFromBytes(interp, fsindex, o, Cookfs_PageObjSize(o));
}

#endif /* COOKFS_USECPAGES */

Cookfs_Fsindex *Cookfs_FsindexFromTclObj(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, Tcl_Obj *o) {
    Tcl_Size size;
    unsigned char *bytes = Tcl_GetByteArrayFromObj(o, &size);
    return Cookfs_FsindexFromBytes(interp, fsindex, bytes, size);

}

/*
 *----------------------------------------------------------------------
 *
 * Cookfs_FsindexFromBytes --
 *
 *	Imports fsindex information from raw buffer storing platform-independant
 *	binary data
 *
 * Results:
 *	Pointer to imported Cookfs_Fsindex; NULL in case of import error
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Cookfs_Fsindex *Cookfs_FsindexFromBytes(Tcl_Interp *interp, Cookfs_Fsindex *fsindex, unsigned char *bytes, Tcl_Size size) {
    int i;
    Cookfs_Fsindex *result;

    CookfsLog(printf("Cookfs_FsindexFromObject - BEGIN"))

    /* cleanup fsindex if it is not NULL */
    if (fsindex != NULL) {
        Cookfs_FsindexCleanup(fsindex);
    }

    /* initialize empty fsindex */
    result = Cookfs_FsindexInit(interp, fsindex);

    if (result == NULL) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to initialize Fsindex object"))
        return NULL;
    }

    if (size < COOKFS_FSINDEX_HEADERLENGTH) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to compare header 1"))
        return NULL;
    }
    if (memcmp(bytes, COOKFS_FSINDEX_HEADERSTRING, COOKFS_FSINDEX_HEADERLENGTH) != 0) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to compare header 2"))
        return NULL;
    }

    /* import root entry; import of subdirectories happens recursively */
    i = CookfsFsindexImportDirectory(result, result->rootItem, bytes, size, 8);

    CookfsLog(printf("Cookfs_FsindexFromObject - Import directory done -"
        " %d vs %" TCL_SIZE_MODIFIER "d", i, size))
    if (i < size) {
        // cppcheck-suppress unreadVariable symbolName=i
        i = CookfsFsindexImportMetadata(result, bytes, size, i);
    }

    CookfsLog(printf("Cookfs_FsindexFromObject - Import metadata done -"
        " %d vs %" TCL_SIZE_MODIFIER "d", i, size))

    Cookfs_FsindexResetChangeCount(result);

    CookfsLog(printf("Cookfs_FsindexFromObject - END"))

    /* return imported fsindex */
    return result;
}

/* definitions of static and/or internal functions */

/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexExportDirectory --
 *
 *	Exports a single directory to byte array object at specified offset
 *
 * Results:
 *	Offset pointing to end of binary data in the result object
 *	This can be used to appending subsequent entries
 *	by parent directory export
 *
 * Side effects:
 *	Byte array may have its size changed, therefore depending on
 *	location of bytes (such as from Tcl_GetByteArrayFromObj())
 *	is not recommended
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexExportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, Tcl_Obj *result, int objOffset) {
    Cookfs_FsindexEntry *itemNode;

    unsigned char *bytes;
    Tcl_Size objLength;

    /* get pointer to bytes in memory */
    bytes = Tcl_GetByteArrayFromObj(result, &objLength);

    Cookfs_Int2Binary(&entry->data.dirInfo.childCount, bytes + objOffset, 1);
    objOffset += 4;

    if (entry->data.dirInfo.isHash) {
	Tcl_HashSearch hashSearch;
	/* iterate using hash table */
	Tcl_HashEntry *hashEntry = Tcl_FirstHashEntry(&entry->data.dirInfo.dirData.children, &hashSearch);
	while (hashEntry != NULL) {
	    /* get next item in hash table */
	    itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	    hashEntry = Tcl_NextHashEntry(&hashSearch);

	    /* if size of object might not be sufficient to store current page */
	    if (objLength < (objOffset + COOKFS_FSINDEX_MAXENTRYSIZE)) {
		objLength += COOKFS_FSINDEX_BUFFERINCREASE;
		bytes = Tcl_SetByteArrayLength(result, objLength);
	    }

	    /* copy filename to binary data; append \0 */
	    bytes[objOffset] = itemNode->fileNameLen;
	    objOffset++;

	    CookfsLog(printf("Copying filename \"%s\" (%d bytes at %d)", itemNode->fileName, ((int) itemNode->fileNameLen), objOffset))

	    memcpy(bytes + objOffset, itemNode->fileName, itemNode->fileNameLen);
	    objOffset += itemNode->fileNameLen;
	    bytes[objOffset] = 0;
	    objOffset ++;

	    /* add file modification time and number of blocks */
	    CookfsLog(printf("DEBUG 1 - %d", objOffset))
	    Cookfs_WideInt2Binary(&itemNode->fileTime, bytes + objOffset, 1);
	    objOffset += 8;

	    CookfsLog(printf("DEBUG 2 - %d", objOffset))
	    Cookfs_Int2Binary(&itemNode->fileBlocks, bytes + objOffset, 1);
	    objOffset += 4;

	    CookfsLog(printf("DEBUG 3 - %d", objOffset))
	    if (itemNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
		/* add child directory's structure inlined; append
		 * rest of current directory after child's export data */
		objOffset = CookfsFsindexExportDirectory(fsIndex, itemNode, result, objOffset);
		bytes = Tcl_GetByteArrayFromObj(result, &objLength);
	    }  else  {
		/* reallocate memory if will not store all block-offset-size triplets */
		if (objLength < (objOffset + (itemNode->fileBlocks * 12))) {
		    objLength += COOKFS_FSINDEX_BUFFERINCREASE;
		    bytes = Tcl_SetByteArrayLength(result, objLength);
		}

		/* add block-offset-size triplets and increase offset*/
		Cookfs_Int2Binary(itemNode->data.fileInfo.fileBlockOffsetSize, bytes + objOffset, itemNode->fileBlocks * 3);
		objOffset += itemNode->fileBlocks * 12;
	    }
	    CookfsLog(printf("DEBUG 4 - %d", objOffset))
	}
    }  else  {
	int i;
	for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
	    itemNode = entry->data.dirInfo.dirData.childTable[i];
	    if (itemNode != NULL) {
		/* if size of object might not be sufficient to store current page */
		if (objLength < (objOffset + COOKFS_FSINDEX_MAXENTRYSIZE)) {
		    objLength += COOKFS_FSINDEX_BUFFERINCREASE;
		    bytes = Tcl_SetByteArrayLength(result, objLength);
		}

		/* copy filename to binary data; append \0 */
		bytes[objOffset] = itemNode->fileNameLen;
		objOffset++;

		CookfsLog(printf("Copying filename \"%s\" (%d bytes at %d)", itemNode->fileName, ((int) itemNode->fileNameLen), objOffset))

		memcpy(bytes + objOffset, itemNode->fileName, itemNode->fileNameLen);
		objOffset += itemNode->fileNameLen;
		bytes[objOffset] = 0;
		objOffset ++;

		/* add file modification time and number of blocks */
		CookfsLog(printf("DEBUG 1 - %d", objOffset))
		Cookfs_WideInt2Binary(&itemNode->fileTime, bytes + objOffset, 1);
		objOffset += 8;

		CookfsLog(printf("DEBUG 2 - %d", objOffset))
		Cookfs_Int2Binary(&itemNode->fileBlocks, bytes + objOffset, 1);
		objOffset += 4;

		CookfsLog(printf("DEBUG 3 - %d", objOffset))
		if (itemNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
		    /* add child directory's structure inlined; append
		     * rest of current directory after child's export data */
		    objOffset = CookfsFsindexExportDirectory(fsIndex, itemNode, result, objOffset);
		    bytes = Tcl_GetByteArrayFromObj(result, &objLength);
		}  else  {
		    /* reallocate memory if will not store all block-offset-size triplets */
		    if (objLength < (objOffset + (itemNode->fileBlocks * 12))) {
			objLength += COOKFS_FSINDEX_BUFFERINCREASE;
			bytes = Tcl_SetByteArrayLength(result, objLength);
		    }

		    /* add block-offset-size triplets and increase offset*/
		    Cookfs_Int2Binary(itemNode->data.fileInfo.fileBlockOffsetSize, bytes + objOffset, itemNode->fileBlocks * 3);
		    objOffset += itemNode->fileBlocks * 12;
		}
		CookfsLog(printf("DEBUG 4 - %d", objOffset))
	    }
	}
    }

    return objOffset;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexImportDirectory --
 *
 *	Imports a directory from specified bytes array
 *	starting at specified offset
 *
 *	TODO: protect against malicious index information
 *
 * Results:
 *	Offset specifying end of imported data; can be used by parent
 *	entry import to continue processing its data
 *	-1 in case of import error
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexImportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, unsigned char *bytes, int objLength, int objOffset) {
    /*  */
    int childCount;

    /* get number of children */
    Cookfs_Binary2Int(bytes + objOffset, &childCount, 1);
    objOffset += 4;
    CookfsLog(printf("CookfsFsindexImportDirectory init"))

    /* TODO: optimize to skip static array if child count is big enough */

    /* import all children in current directory */
    CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT BEGIN (%d childCount)", childCount))
    for (int fileId = 0; fileId < childCount; fileId++) {
	char *fileName;
	int fileNameLength;
	int fileBlocks;
	Tcl_WideInt fileTime;
	Cookfs_FsindexEntry *itemNode;

	CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT ITER %d/%d", fileId, childCount))
	/* get file name from binary data */
        fileNameLength = bytes[objOffset];
        objOffset++;
        fileName = (char *) (bytes + objOffset);
        objOffset += fileNameLength + 1;

	/* get file modification time and number of blocks */
        Cookfs_Binary2WideInt(bytes + objOffset, &fileTime, 1);
        objOffset += 8;
        Cookfs_Binary2Int(bytes + objOffset, &fileBlocks, 1);
        objOffset += 4;
        CookfsLog(printf("CookfsFsindexImportDirectory importing %s (%d blocks)", fileName, fileBlocks))

	/* create fsindex entry and set its modification time */
	itemNode = Cookfs_FsindexSetInDirectory(entry, fileName, fileNameLength, fileBlocks);
	itemNode->fileTime = fileTime;

        if (fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
	    /* for directory, import child recursively and get offset to continue import from */
            objOffset = CookfsFsindexImportDirectory(fsIndex, itemNode, bytes, objLength, objOffset);

	    /* if child import failed, pass the error to caller */
            if (objOffset < 0) {
                CookfsLog(printf("CookfsFsindexImportDirectory - failure - rolling back"))
                return objOffset;
            }
        }  else  {
	    /* copy all block-offset-size triplets from binary data to newly created entry */
	    Tcl_WideInt fileSize = 0;
            Cookfs_Binary2Int(bytes + objOffset, itemNode->data.fileInfo.fileBlockOffsetSize, fileBlocks * 3);
            objOffset += fileBlocks * 12;
            for (int i = 0; i < fileBlocks; i++) {
                fileSize += itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 2];
		Cookfs_FsindexModifyBlockUsage(fsIndex, itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 0], 1);
		CookfsLog(printf("CookfsFsindexImportDirectory - %d/%d/%d",
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 0],
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 1],
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 2]))
            }
	    /* calculate and store file size */
            itemNode->data.fileInfo.fileSize = fileSize;
            itemNode->isFileBlocksInitialized = fsIndex;
        }
    }

    /* return current offset to caller */
    CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT END"))
    return objOffset;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexExportMetadata --
 *
 *	Exports metadata from fsindex into a binary object
 *	starting at specified offset
 *
 *	TODO: protect against malicious index information
 *
 * Results:
 *	Offset specifying end of imported data; can be used by
 *	export to continue processing its data
 *	-1 in case of import error
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */

static int CookfsFsindexExportMetadata(Cookfs_Fsindex *fsIndex, Tcl_Obj *result, int objOffset) {
    Tcl_HashEntry *hashEntry;
    Tcl_HashSearch hashSearch;
    Tcl_Size objSize;
    int objInitialOffset = objOffset;
    int objSizeChanged;
    unsigned char *resultData;
    int count = 0;

    resultData = Tcl_GetByteArrayFromObj(result, &objSize);

    objSizeChanged = 0;
    while (objSize < (objOffset + 4)) {
        objSize += COOKFS_FSINDEX_BUFFERINCREASE;
        objSizeChanged = 1;
    }
    if (objSizeChanged)
        resultData = Tcl_SetByteArrayLength(result, objSize);

    objOffset += 4;

    for (hashEntry = Tcl_FirstHashEntry(&fsIndex->metadataHash, &hashSearch); hashEntry != NULL; hashEntry = Tcl_NextHashEntry(&hashSearch)) {

        const char *paramName = Tcl_GetHashKey(&fsIndex->metadataHash, hashEntry);
        CookfsLog(printf("CookfsFsindexExportMetadata - exporting key %s", paramName))

        unsigned int keySize = strlen(paramName);

        unsigned char *valueData = Tcl_GetHashValue(hashEntry);
        CookfsLog(printf("CookfsFsindexExportMetadata - data: %p", valueData));
        Tcl_Size valueSize = *((Tcl_Size *)valueData);
        valueData += sizeof(Tcl_Size);
        CookfsLog(printf("CookfsFsindexExportMetadata - size: %"
            TCL_SIZE_MODIFIER "d", valueSize));

        int size = keySize + 1 + valueSize;
        count++;

        /* calculate target object size, set new size if needed */
        objSizeChanged = 0;
        while (objSize < (objOffset + size + 4)) {
            objSize += COOKFS_FSINDEX_BUFFERINCREASE;
            objSizeChanged = 1;
        }

        if (objSizeChanged) {
            Tcl_SetByteArrayLength(result, objSize);
            resultData = Tcl_GetByteArrayFromObj(result, NULL);
        }

        /* export sizes */
        Cookfs_Int2Binary(&size, resultData + objOffset, 1);
        objOffset += 4;

        /* export parameter name */
        memcpy(resultData + objOffset, paramName, keySize + 1);
        objOffset += keySize + 1;

        /* export value */
        memcpy(resultData + objOffset, valueData, valueSize);
        objOffset += valueSize;
    }
    CookfsLog(printf("CookfsFsindexExportMetadata - Number of keys: %d", count))
    Cookfs_Int2Binary(&count, resultData + objInitialOffset, 1);

    return objOffset;
}


/*
 *----------------------------------------------------------------------
 *
 * CookfsFsindexImportMetadata --
 *
 *	Imports metadata from fsindex into a binary object
 *	starting at specified offset
 *
 *	TODO: protect against malicious index information
 *
 * Results:
 *	Offset specifying end of imported data; can be used by
 *	import to continue processing its data
 *	-1 in case of import error
 *
 * Side effects:
 *	TODO
 *
 *----------------------------------------------------------------------
 */


static int CookfsFsindexImportMetadata(Cookfs_Fsindex *fsIndex, unsigned char *bytes, int objLength, int objOffset) {
    int count;

    if ((objOffset + 4) > objLength) {
        return objLength;
    }
    Cookfs_Binary2Int(bytes + objOffset, &count, 1);
    CookfsLog(printf("CookfsFsindexImportMetadata - Number of keys: %d", count))
    objOffset += 4;

    for (int i = 0; i < count; i++) {
        int keySize;
        int valueSize;
        const char *paramName;
        const unsigned char *valueData;
        int size;
        /* check if we are not reading out of bounds */
        if ((objOffset + 4) > objLength) {
            return -1;
        }
        Cookfs_Binary2Int(bytes + objOffset, &size, 1);
        objOffset += 4;
        if ((objOffset + size) > objLength) {
            return -1;
        }
        paramName = (const char *) bytes + objOffset;
        keySize = strlen(paramName);
        valueData = bytes + objOffset + (keySize + 1);
        valueSize = size - (keySize + 1);
        Cookfs_FsindexSetMetadataRaw(fsIndex, paramName, valueData, valueSize);
        objOffset += size;
    }
    return objOffset;
}

