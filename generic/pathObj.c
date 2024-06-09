/*
 * pageObj.c
 *
 * Provides functions creating and managing a path object
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

Cookfs_PathObj *Cookfs_PathObjNewFromTclObj(Tcl_Obj *path) {
    Tcl_Size pathLength;
    const char *pathStr = Tcl_GetStringFromObj(path, &pathLength);
    return Cookfs_PathObjNewFromStr(pathStr, pathLength);
}

Cookfs_PathObj *Cookfs_PathObjNewFromStr(const char* pathStr,
    Tcl_Size pathLength)
{

    CookfsLog(printf("Cookfs_PathObjNewFromStr: new from [%s]", pathStr));

    Tcl_Size i;

    if (pathLength == -1) {
        pathLength = strlen(pathStr);
    }

    // First, let's find out how many names are in the path, separated by
    // the filesystem separator.
    int elementCount;
    if (pathLength) {
        elementCount = 1;
        for (i = 0; i < pathLength; i++) {
            if (pathStr[i] == '/') {
                elementCount++;
            }
        }
    } else {
        elementCount = 0;
    }
    // CookfsLog(printf("Cookfs_PathObjNewFromStr: element count: %d",
    //     elementCount));

    // We need to alloc our object now. We need the following buffer:
    //     Cookfs_PathObj +
    //     Cookfs_PathObjElement * elementCount +
    //     file name + \0 +
    //     filename0 + \0 (See comment about filename0 in pathObj.h)
    Cookfs_PathObj *rc = ckalloc(sizeof(Cookfs_PathObj) +
        sizeof(Cookfs_PathObjElement) * elementCount + (pathLength + 1) * 2);
    if (rc == NULL) {
        Tcl_Panic("failed to alloc pathObj");
        // Tcl_Panic will not return, but we have to return here something to
        // shutup cppcheck.
        return NULL;
    }

    // Fill general properties
    rc->refCount = 0;
    rc->fullName = (char *)rc + sizeof(Cookfs_PathObj) +
        sizeof(Cookfs_PathObjElement) * elementCount;
    // See comment about filename0 in pathObj.h
    rc->fullName0 = rc->fullName + pathLength + 1;
    memcpy(rc->fullName0, pathStr, pathLength + 1);
    memcpy(rc->fullName, pathStr, pathLength + 1);
    rc->fullNameLength = pathLength;
    rc->elementCount = elementCount;

    // See comment about name0 in pathObj.h

    if (pathLength) {
        // Fill elements
        char *lastElementStr = rc->fullName;
        char *lastElementStr0 = rc->fullName0;
        int currentElement = 0;
        int currentElementLength = 0;
        for (i = 0; i < pathLength; i++) {
            if (pathStr[i] != '/') {
                // The current character is not the end of an element.
                currentElementLength++;
                continue;
            }
            // Save information about the previous element
            // CookfsLog(printf("Cookfs_PathObjNewFromStr: found element #%d"
            //     " length %d", currentElement, currentElementLength));
            rc->fullName0[i] = '\0';
            rc->element[currentElement].name = lastElementStr;
            rc->element[currentElement].name0 = lastElementStr0;
            rc->element[currentElement].length = currentElementLength;
            // Reset current element
            lastElementStr = &rc->fullName[i + 1];
            lastElementStr0 = &rc->fullName0[i + 1];
            currentElement++;
            currentElementLength = 0;
        }
        // The current state is corresponds to the tail element
        rc->tailName = lastElementStr;
        rc->tailNameLength = currentElementLength;
        // CookfsLog(printf("Cookfs_PathObjNewFromStr: tail element #%d [%s]"
        //     " length %d", currentElement, lastElementStr,
        //     currentElementLength));
        // Also, save information about the last element
        rc->element[currentElement].name = lastElementStr;
        rc->element[currentElement].name0 = lastElementStr0;
        rc->element[currentElement].length = currentElementLength;
    } else {
        // CookfsLog(printf("Cookfs_PathObjNewFromStr: the path is empty"));
        rc->tailName = rc->fullName;
        rc->tailNameLength = rc->fullNameLength;
    }

    // CookfsLog(printf("Cookfs_PathObjNewFromStr: return %p", (void *)rc));
    return rc;

}

