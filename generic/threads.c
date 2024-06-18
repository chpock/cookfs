/*
 * threads.c
 *
 * Provides support for threads
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

#ifdef TCL_THREADS

struct _Cookfs_RWMutex {
    Tcl_Mutex mx;
    Tcl_Condition condWrite;
    Tcl_Condition condRead;
    int numRWait;
    int numWWait;
    int numLocks;
    Tcl_ThreadId threadId;
};

Cookfs_RWMutex Cookfs_RWMutexInit(void) {
    Cookfs_RWMutex mx = ckalloc(sizeof(struct _Cookfs_RWMutex));
    mx->numRWait = 0;
    mx->numWWait = 0;
    mx->numLocks = 0;
    mx->mx = NULL;
    mx->condWrite = NULL;
    mx->condRead = NULL;
    mx->threadId = NULL;
    return mx;
}

void Cookfs_RWMutexWantRead(Cookfs_RWMutex mx) {
    Tcl_MutexLock(&mx->mx);
    assert((mx->threadId != NULL || mx->numLocks != 0) && "Want read or write lock");
    assert((mx->threadId == NULL || mx->threadId == Tcl_GetCurrentThread()) && "Wrong threadId");
    Tcl_MutexUnlock(&mx->mx);
}

void Cookfs_RWMutexWantWrite(Cookfs_RWMutex mx) {
    Tcl_MutexLock(&mx->mx);
    assert((mx->threadId != NULL || mx->numLocks == -1) && "Want write lock");
    assert((mx->threadId == NULL || mx->threadId == Tcl_GetCurrentThread()) && "Wrong threadId");
    Tcl_MutexUnlock(&mx->mx);
}

void Cookfs_RWMutexFini(Cookfs_RWMutex mx) {
    Tcl_ConditionFinalize(&mx->condWrite);
    Tcl_ConditionFinalize(&mx->condRead);
    Tcl_MutexFinalize(&mx->mx);
    ckfree(mx);
}

int Cookfs_RWMutexLockRead(Cookfs_RWMutex mx) {
    int ret = 1;
    Tcl_MutexLock(&mx->mx);
    if (mx->threadId != NULL) {
        // If mutex is exclusively locked, then allow all if threadId matches,
        // and disallow all for other threads.
        if (mx->threadId != Tcl_GetCurrentThread()) {
            ret = 0;
        }
    } else {
        while (mx->numLocks < 0) {
            mx->numRWait++;
            Tcl_ConditionWait(&mx->condRead, &mx->mx, NULL);
            mx->numRWait--;
            // If we detect an exclusive lock mode after the condRead event,
            // we return false. We do not expect a thread that aquired
            // an exclusive lock to wait for conditions to be met.
            if (mx->threadId != NULL) {
                ret = 0;
                goto done;
            }
        }
        mx->numLocks++;
    }
done:
    Tcl_MutexUnlock(&mx->mx);
    return ret;
}

int Cookfs_RWMutexLockWrite(Cookfs_RWMutex mx) {
    int ret = 1;
    Tcl_MutexLock(&mx->mx);
    if (mx->threadId != NULL) {
        // If mutex is exclusively locked, then allow all if threadId matches,
        // and disallow all for other threads.
        if (mx->threadId != Tcl_GetCurrentThread()) {
            ret = 0;
        }
    } else {
        while (mx->numLocks != 0) {
            mx->numWWait++;
            Tcl_ConditionWait(&mx->condWrite, &mx->mx, NULL);
            mx->numWWait--;
        }
        mx->numLocks = -1;
    }
    Tcl_MutexUnlock(&mx->mx);
    return ret;
}

int Cookfs_RWMutexLockExclusive(Cookfs_RWMutex mx) {
    if (!Cookfs_RWMutexLockWrite(mx)) {
        return 0;
    }
    assert((mx->threadId == NULL || mx->threadId == Tcl_GetCurrentThread()) && "Wrong threadId");
    mx->threadId = Tcl_GetCurrentThread();
    return 1;
}

void Cookfs_RWMutexUnlock(Cookfs_RWMutex mx) {
    Tcl_MutexLock(&mx->mx);
    // The mutex should be readlocked (numLocks >= 1) or writelocked
    // (numLocks == -1). All other cases mean an error. For example, when
    // Unlock() is called on not locked mutex.
    assert(mx->numLocks > 0 || mx->numLocks == -1 || mx->threadId != NULL);
    // Allow to call Unlock() on an exclusively locked mutex only for thread
    // that owns thix mutex.
    assert((mx->threadId == NULL || mx->threadId == Tcl_GetCurrentThread()) && "Wrong threadId");
    if (mx->numLocks <= 0) {
        mx->numLocks = 0;
    } else {
        mx->numLocks--;
    }
    if (mx->numWWait) {
        Tcl_ConditionNotify(&mx->condWrite);
    } else if (mx->numRWait) {
        Tcl_ConditionNotify(&mx->condRead);
    }
    Tcl_MutexUnlock(&mx->mx);
}

#endif /* TCL_THREADS */
