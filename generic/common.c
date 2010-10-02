/* (c) 2010 Wojciech Kocjan, Pawel Salawa */

#include "cookfs.h"

unsigned char *Cookfs_Binary2Int(unsigned char *bytes, int *values, int count) {
    int b;
    while (count > 0) {
        b = (bytes[0] << 24)
            | (bytes[1] << 16)
            | (bytes[2] << 8)
            | bytes[3];
        *values++ = b;
        bytes += 4;
        count--;
    }
    return bytes;
}

unsigned char *Cookfs_Int2Binary(int *values, unsigned char *bytes, int count) {
    while (count > 0) {
        bytes[0] = ((*values & 0xff000000) >> 24);
        bytes[1] = ((*values & 0x00ff0000) >> 16);
        bytes[2] = ((*values & 0x0000ff00) >> 8);
        bytes[3] = ((*values & 0x000000ff));
        count--;
        bytes += 4;
        values++;
    }
    return bytes;
}

unsigned char *Cookfs_Binary2WideInt(unsigned char *bytes, Tcl_WideInt *values, int count) {
    Tcl_WideInt v;
    while (count > 0) {
        v = bytes[0]; v = v << 8;
        v |= bytes[1]; v = v << 8;
        v |= bytes[2]; v = v << 8;
        v |= bytes[3]; v = v << 8;
        v |= bytes[4]; v = v << 8;
        v |= bytes[5]; v = v << 8;
        v |= bytes[6]; v = v << 8;
        v |= bytes[7];
        *values = v;
        values++;
        count--;
        bytes += 8;
    }
    return bytes;
}

unsigned char *Cookfs_WideInt2Binary(Tcl_WideInt *values, unsigned char *bytes, int count) {
    Tcl_WideInt v;
    while (count > 0) {
        v = *values;
        bytes[7] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[6] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[5] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[4] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[3] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[2] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[1] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        bytes[0] = v & ((Tcl_WideInt) 0x000000ff);
        count--;
        bytes += 8;
        values++;
    }
    return bytes;
}
