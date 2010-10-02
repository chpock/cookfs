/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#ifndef COOKFS_PAGES_H
#define COOKFS_PAGES_H 1

unsigned char *Cookfs_Binary2Int(unsigned char *bytes, int *values, int count);
unsigned char *Cookfs_Int2Binary(int *values, unsigned char *bytes, int count);

unsigned char *Cookfs_Binary2WideInt(unsigned char *bytes, Tcl_WideInt *values, int count);
unsigned char *Cookfs_WideInt2Binary(Tcl_WideInt *values, unsigned char *bytes, int count);

void Cookfs_MD5(unsigned char *buf, unsigned int len, unsigned char digest[16]);
Tcl_Obj *Cookfs_MD5FromObj(Tcl_Obj *obj);

#endif
