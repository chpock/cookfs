/*
 * common.h
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#ifndef COOKFS_COMMON_H
#define COOKFS_COMMON_H 1

#define MD5_DIGEST_SIZE 16

unsigned char *Cookfs_Binary2Int(unsigned char *input, int *output, int count);
unsigned char *Cookfs_Int2Binary(int *input, unsigned char *output, int count);

unsigned char *Cookfs_Binary2WideInt(unsigned char *input, Tcl_WideInt *output, int count);
unsigned char *Cookfs_WideInt2Binary(Tcl_WideInt *input, unsigned char *output, int count);

void Cookfs_MD5(unsigned char *buf, Tcl_Size len, unsigned char digest[16]);
Tcl_Obj *Cookfs_MD5FromObj(Tcl_Obj *obj);

#endif /* COOKFS_COMMON_H */
