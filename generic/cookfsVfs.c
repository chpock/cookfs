/*
 * cookfsVfs.c
 *
 * Code for providing Cookfs VFS layer in pure C
 * 
 * CURRENTLY NOT USED
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 *
 * Code partially based on tclvfs generic/vfs.c source code
 */

static Tcl_FSStatProc CookfsStat;
static Tcl_FSAccessProc CookfsAccess;
static Tcl_FSOpenFileChannelProc CookfsOpenFileChannel;
static Tcl_FSMatchInDirectoryProc CookfsMatchInDirectory;
static Tcl_FSDeleteFileProc CookfsDeleteFile;
static Tcl_FSCreateDirectoryProc CookfsCreateDirectory;
static Tcl_FSRemoveDirectoryProc CookfsRemoveDirectory; 
static Tcl_FSFileAttrStringsProc CookfsFileAttrStrings;
static Tcl_FSFileAttrsGetProc CookfsFileAttrsGet;
static Tcl_FSFileAttrsSetProc CookfsFileAttrsSet;
static Tcl_FSUtimeProc CookfsUtime;
static Tcl_FSPathInFilesystemProc CookfsPathInFilesystem;
static Tcl_FSFilesystemPathTypeProc CookfsFilesystemPathType;
static Tcl_FSFilesystemSeparatorProc CookfsFilesystemSeparator;
static Tcl_FSFreeInternalRepProc CookfsFreeInternalRep;
static Tcl_FSDupInternalRepProc CookfsDupInternalRep;
static Tcl_FSListVolumesProc CookfsListVolumes;

static Tcl_Filesystem cookfsFilesystem = {
    "cookfs",
    sizeof(Tcl_Filesystem),
    TCL_FILESYSTEM_VERSION_1,
    &CookfsPathInFilesystem,
    &CookfsDupInternalRep,
    &CookfsFreeInternalRep,
    /* No internal to normalized, since we don't create any
     * pure 'internal' Tcl_Obj path representations */
    NULL,
    /* No create native rep function, since we don't use it
     * or 'Tcl_FSNewNativePath' */
    NULL,
    /* Normalize path isn't needed - we assume paths only have
     * one representation */
    NULL,
    &CookfsFilesystemPathType,
    &CookfsFilesystemSeparator,
    &CookfsStat,
    &CookfsAccess,
    &CookfsOpenFileChannel,
    &CookfsMatchInDirectory,
    &CookfsUtime,
    /* We choose not to support symbolic links inside our vfs's */
    NULL,
    &CookfsListVolumes,
    NULL,
    NULL,
    NULL,
    &CookfsCreateDirectory,
    &CookfsRemoveDirectory, 
    &CookfsDeleteFile,
    /* No copy file - fallback will occur at Tcl level */
    NULL,
    /* No rename file - fallback will occur at Tcl level */
    NULL,
    /* No copy directory - fallback will occur at Tcl level */
    NULL, 
    /* Use stat for lstat */
    NULL,
    /* No load - fallback on core implementation */
    NULL,
    /* We don't need a getcwd or chdir - fallback on Tcl's versions */
    NULL,
    NULL
};

static int 
CookfsPathInFilesystem(Tcl_Obj *pathPtr, ClientData *clientDataPtr) {
    Tcl_Obj *normedObj;
    int len, splitPosition;
    char *normed;
    VfsNativeRep *nativeRep;
    Vfs_InterpCmd *interpCmd = NULL;
    
    if (TclInExit()) {
        /* 
         * Even Tcl_FSGetNormalizedPath may fail due to lack of system
         * encodings, so we just say we can't handle anything if we are
         * in the middle of the exit sequence.  We could perhaps be
         * more subtle than this!
         */
        return TCLVFS_POSIXERROR;
    }

    normedObj = Tcl_FSGetNormalizedPath(NULL, pathPtr);
    if (normedObj == NULL) {
        return TCLVFS_POSIXERROR;
    }
    normed = Tcl_GetStringFromObj(normedObj, &len);
    splitPosition = len;

    /* 
     * Find the most specific mount point for this path.
     * Mount points are specified by unique strings, so
     * we have to use a unique normalised path for the
     * checks here.
     * 
     * Given mount points are paths, 'most specific' means
     * longest path, so we scan from end to beginning
     * checking for valid mount points at each separator.
     */
    while (1) {
        /* 
         * We need this test here both for an empty string being
         * passed in above, and so that if we are testing a unix
         * absolute path /foo/bar we will come around the loop
         * with splitPosition at 0 for the last iteration, and we
         * must return then.
         */
        if (splitPosition == 0) {
            return TCLVFS_POSIXERROR;
        }
        
        /* Is the path up to 'splitPosition' a valid moint point? */
        interpCmd = Vfs_FindMount(normedObj, splitPosition);
        if (interpCmd != NULL) break;

        while (normed[--splitPosition] != VFS_SEPARATOR) {
            if (splitPosition == 0) {
                /* 
                 * We've reached the beginning of the string without
                 * finding a mount, so we've failed.
                 */
                return TCLVFS_POSIXERROR;
            }
        }
        
        /* 
         * We now know that normed[splitPosition] is a separator.
         * However, we might have mounted a root filesystem with a
         * name (for example 'ftp://') which actually includes a
         * separator.  Therefore we test whether the path with
         * a separator is a mount point.
         * 
         * Since we must have decremented splitPosition at least once
         * already (above) 'splitPosition+1 <= len' so this won't
         * access invalid memory.
         */
        interpCmd = Vfs_FindMount(normedObj, splitPosition+1);
        if (interpCmd != NULL) {
            splitPosition++;
            break;
        }
    }
    
    /* 
     * If we reach here we have a valid mount point, since the
     * only way to escape the above loop is through a 'break' when
     * an interpCmd is non-NULL.
     */
    nativeRep = (VfsNativeRep*) ckalloc(sizeof(VfsNativeRep));
    nativeRep->splitPosition = splitPosition;
    nativeRep->fsCmd = interpCmd;
    *clientDataPtr = (ClientData)nativeRep;
    return TCL_OK;
}
