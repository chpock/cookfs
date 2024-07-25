/*
   (c) 2024 Konstantin Kushnir
*/

#ifndef COOKFS_THREADS_H
#define COOKFS_THREADS_H 1

typedef struct _Cookfs_RWMutex *Cookfs_RWMutex;

Cookfs_RWMutex Cookfs_RWMutexInit(void);
void Cookfs_RWMutexFini(Cookfs_RWMutex mx);

int Cookfs_RWMutexLockRead(Cookfs_RWMutex mx);
int Cookfs_RWMutexLockWrite(Cookfs_RWMutex mx);
void Cookfs_RWMutexUnlock(Cookfs_RWMutex mx);

int Cookfs_RWMutexLockExclusive(Cookfs_RWMutex mx);

void Cookfs_RWMutexWantRead(Cookfs_RWMutex mx);
void Cookfs_RWMutexWantWrite(Cookfs_RWMutex mx);

int Cookfs_RWMutexGetLocks(Cookfs_RWMutex mx);

#endif /* COOKFS_THREADS_H */
