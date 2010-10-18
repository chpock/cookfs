/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#include "cookfs.h"

#define COOKFS_FSINDEX_MAXENTRYSIZE 8192
#define COOKFS_FSINDEX_BUFFERINCREASE 65536

#define COOKFS_FSINDEX_HEADERSTRING "CFS2.200"
#define COOKFS_FSINDEX_HEADERLENGTH 8

static int CookfsFsindexExportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, Tcl_Obj *result, int objOffset);
static int CookfsFsindexImportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, unsigned char *bytes, int objLength, int objOffset);

Tcl_Obj *Cookfs_FsindexToObject(Cookfs_Fsindex *fsIndex) {
    Tcl_Obj *result;
    int objSize;

    result = Tcl_NewByteArrayObj((unsigned char *) COOKFS_FSINDEX_HEADERSTRING, COOKFS_FSINDEX_HEADERLENGTH);
    Tcl_SetByteArrayLength(result, COOKFS_FSINDEX_BUFFERINCREASE);

    objSize = CookfsFsindexExportDirectory(fsIndex, fsIndex->rootItem, result, COOKFS_FSINDEX_HEADERLENGTH);
    Tcl_SetByteArrayLength(result, objSize);

    return result;
}

Cookfs_Fsindex *Cookfs_FsindexFromObject(Tcl_Obj *o) {
    int objLength;
    int i;
    unsigned char *bytes;
    
    Cookfs_Fsindex *result;
    
    result = Cookfs_FsindexInit();
    
    if (result == NULL) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to initialize Fsindex object"))
        return NULL;
    }

    bytes = Tcl_GetByteArrayFromObj(o, &objLength);

    if (objLength < COOKFS_FSINDEX_HEADERLENGTH) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to compare header 1"))
        return NULL;
    }
    if (memcmp(bytes, COOKFS_FSINDEX_HEADERSTRING, COOKFS_FSINDEX_HEADERLENGTH) != 0) {
        CookfsLog(printf("Cookfs_FsindexFromObject - unable to compare header 2"))
        return NULL;
    }

    i = CookfsFsindexImportDirectory(result, result->rootItem, bytes, objLength, 8);
    
    CookfsLog(printf("Import done - %d vs %d", i, objLength))
    
    return result;
}

/* format:
  <4:numChildren>
    <1:fileNameLength><X:fileName><1:null>
    <8:fileTime>
    <4:numBlocks>
    file: <12xX:data>
    dir:  (children - starting with <4:numChildren>)
*/
static int CookfsFsindexExportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, Tcl_Obj *result, int objOffset) {
    Tcl_HashSearch hashSearch;
    Tcl_HashEntry *hashEntry;
    Cookfs_FsindexEntry *itemNode;

    unsigned char *bytes;
    int objLength;

    bytes = Tcl_GetByteArrayFromObj(result, &objLength);

    Cookfs_Int2Binary(&entry->data.dirInfo.childCount, bytes + objOffset, 1);
    objOffset += 4;

    /* TODO: clean up export - separate function */
    if (entry->data.dirInfo.isHash) {
	hashEntry = Tcl_FirstHashEntry(&entry->data.dirInfo.dirData.children, &hashSearch);
	while (hashEntry != NULL) {
	    itemNode = (Cookfs_FsindexEntry *) Tcl_GetHashValue(hashEntry);
	    hashEntry = Tcl_NextHashEntry(&hashSearch);

	    if (objLength < (objOffset + COOKFS_FSINDEX_MAXENTRYSIZE)) {
		objLength += COOKFS_FSINDEX_BUFFERINCREASE;
		bytes = Tcl_SetByteArrayLength(result, objLength);
	    }

	    bytes[objOffset] = itemNode->fileNameLen;
	    objOffset++;

	    CookfsLog(printf("Copying filename \"%s\" (%d bytes at %d)", itemNode->fileName, ((int) itemNode->fileNameLen), objOffset))

	    memcpy(bytes + objOffset, itemNode->fileName, itemNode->fileNameLen);
	    objOffset += itemNode->fileNameLen;
	    bytes[objOffset] = 0;
	    objOffset ++;

	    CookfsLog(printf("DEBUG 1 - %d", objOffset))
	    Cookfs_WideInt2Binary(&itemNode->fileTime, bytes + objOffset, 1);
	    objOffset += 8;

	    CookfsLog(printf("DEBUG 2 - %d", objOffset))
	    Cookfs_Int2Binary(&itemNode->fileBlocks, bytes + objOffset, 1);
	    objOffset += 4;

	    CookfsLog(printf("DEBUG 3 - %d", objOffset))
	    if (itemNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
		objOffset = CookfsFsindexExportDirectory(fsIndex, itemNode, result, objOffset);
	    }  else  {
		Cookfs_Int2Binary(itemNode->data.fileInfo.fileBlockOffsetSize, bytes + objOffset, itemNode->fileBlocks * 3);
		objOffset += itemNode->fileBlocks * 12;
	    }
	    CookfsLog(printf("DEBUG 4 - %d", objOffset))
	}
    }  else  {
	Cookfs_FsindexEntry *itemNode;
	int i;
	for (i = 0 ; i < COOKFS_FSINDEX_TABLE_MAXENTRIES; i++) {
	    itemNode = entry->data.dirInfo.dirData.childTable[i];
	    if (itemNode != NULL) {
		if (objLength < (objOffset + COOKFS_FSINDEX_MAXENTRYSIZE)) {
		    objLength += COOKFS_FSINDEX_BUFFERINCREASE;
		    bytes = Tcl_SetByteArrayLength(result, objLength);
		}

		bytes[objOffset] = itemNode->fileNameLen;
		objOffset++;

		CookfsLog(printf("Copying filename \"%s\" (%d bytes at %d)", itemNode->fileName, ((int) itemNode->fileNameLen), objOffset))

		memcpy(bytes + objOffset, itemNode->fileName, itemNode->fileNameLen);
		objOffset += itemNode->fileNameLen;
		bytes[objOffset] = 0;
		objOffset ++;

		CookfsLog(printf("DEBUG 1 - %d", objOffset))
		Cookfs_WideInt2Binary(&itemNode->fileTime, bytes + objOffset, 1);
		objOffset += 8;

		CookfsLog(printf("DEBUG 2 - %d", objOffset))
		Cookfs_Int2Binary(&itemNode->fileBlocks, bytes + objOffset, 1);
		objOffset += 4;

		CookfsLog(printf("DEBUG 3 - %d", objOffset))
		if (itemNode->fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
		    objOffset = CookfsFsindexExportDirectory(fsIndex, itemNode, result, objOffset);
		}  else  {
		    Cookfs_Int2Binary(itemNode->data.fileInfo.fileBlockOffsetSize, bytes + objOffset, itemNode->fileBlocks * 3);
		    objOffset += itemNode->fileBlocks * 12;
		}
		CookfsLog(printf("DEBUG 4 - %d", objOffset))
	    }
	}
    }
    
    return objOffset;
}

/* format:
  <4:numChildren>
    <1:fileNameLength><X:fileName>
    <8:fileTime>
    <4:numBlocks>
    file: <12xX:data>
    dir:  (children - starting with <4:numChildren>)
*/

/* TODO: protect against malicious index information */
static int CookfsFsindexImportDirectory(Cookfs_Fsindex *fsIndex, Cookfs_FsindexEntry *entry, unsigned char *bytes, int objLength, int objOffset) {
    int fileId;
    int fileBlocks;
    char *fileName;
    int fileNameLength;
    int childCount;
    Tcl_WideInt fileTime;
    Tcl_WideInt fileSize;
    Cookfs_FsindexEntry *itemNode;

    Cookfs_Binary2Int(bytes + objOffset, &childCount, 1);
    objOffset += 4;
    CookfsLog(printf("CookfsFsindexImportDirectory init"))

    /* TODO: optimize to skip static array if child count is big enough */
    CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT BEGIN (%d childCount)", childCount))
    for (fileId = 0; fileId < childCount; fileId++) {
	CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT ITER %d/%d", fileId, childCount))
        fileNameLength = bytes[objOffset];
        objOffset++;
        fileName = (char *) (bytes + objOffset);
        objOffset += fileNameLength + 1;

        Cookfs_Binary2WideInt(bytes + objOffset, &fileTime, 1);
        objOffset += 8;
        Cookfs_Binary2Int(bytes + objOffset, &fileBlocks, 1);
        objOffset += 4;
        CookfsLog(printf("CookfsFsindexImportDirectory importing %s (%d blocks)", fileName, fileBlocks))

	itemNode = Cookfs_FsindexSetInDirectory(fsIndex, entry, fileName, fileNameLength, fileBlocks);
	itemNode->fileTime = fileTime;

        if (fileBlocks == COOKFS_NUMBLOCKS_DIRECTORY) {
            objOffset = CookfsFsindexImportDirectory(fsIndex, itemNode, bytes, objLength, objOffset);
            if (objOffset < 0) {
                CookfsLog(printf("CookfsFsindexImportDirectory - failure - rolling back"))
                return objOffset;
            }
        }  else  {
            int i;
            Cookfs_Binary2Int(bytes + objOffset, itemNode->data.fileInfo.fileBlockOffsetSize, fileBlocks * 3);
            objOffset += fileBlocks * 12;
            for (i = 0, fileSize = 0; i < fileBlocks; i++) {
                fileSize += itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 2];
		CookfsLog(printf("CookfsFsindexImportDirectory - %d/%d/%d", 
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 0],
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 1],
		    itemNode->data.fileInfo.fileBlockOffsetSize[i*3 + 2]))
            }
            itemNode->data.fileInfo.fileSize = fileSize;
        }
    }
    CookfsLog(printf("CookfsFsindexImportDirectory - IMPORT END"))
    return objOffset;
}
