/*
 * common.c
 *
 * Provides common functionality for Cookfs C-based code.
 *
 * (c) 2010 Wojciech Kocjan, Pawel Salawa
 */

#include "cookfs.h"


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_Binary2Int --
 *
 *	Converts count integers from platform independant (high endian)
 *	stored at input into an array of platform dependant integers
 *	at output.
 *
 * Results:
 *	Pointer to bytes following count integers at input:
 *	result = input + count * 4
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

unsigned char *Cookfs_Binary2Int(unsigned char *input, int *output, int count) {
    int b;
    while (count > 0) {
        b = (input[0] << 24)
            | (input[1] << 16)
            | (input[2] << 8)
            | input[3];
        *output++ = b;
        input += 4;
        count--;
    }
    return input;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_Int2Binary --
 *
 *	Converts count integers from platform dependant stored at
 *	input into an array of platform independant (high endian)
 *	integers at output.
 *
 * Results:
 *	Pointer to output following count integers stored at output:
 *	result = output + count * 4
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

unsigned char *Cookfs_Int2Binary(int *input, unsigned char *output, int count) {
    while (count > 0) {
        output[0] = ((*input & 0xff000000) >> 24);
        output[1] = ((*input & 0x00ff0000) >> 16);
        output[2] = ((*input & 0x0000ff00) >> 8);
        output[3] = ((*input & 0x000000ff));
        count--;
        output += 4;
        input++;
    }
    return output;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_Binary2WideInt --
 *
 *	Converts count wide integers from platform independant
 *	(high endian) stored at input into an array of platform
 *	dependant wide integers at output.
 *
 * Results:
 *	Pointer to bytes following count wide integers at input:
 *	result = input + count * 8
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

unsigned char *Cookfs_Binary2WideInt(unsigned char *input, Tcl_WideInt *output, int count) {
    Tcl_WideInt v;
    while (count > 0) {
        v = input[0]; v = v << 8;
        v |= input[1]; v = v << 8;
        v |= input[2]; v = v << 8;
        v |= input[3]; v = v << 8;
        v |= input[4]; v = v << 8;
        v |= input[5]; v = v << 8;
        v |= input[6]; v = v << 8;
        v |= input[7];
        *output = v;
        output++;
        count--;
        input += 8;
    }
    return input;
}


/*
 *----------------------------------------------------------------------
 *
 * Cookfs_WideInt2Binary --
 *
 *	Converts count wide integers from platform dependant stored at
 *	input into an array of platform independant (high endian)
 *	wide integers at output.
 *
 * Results:
 *	Pointer to output following count wide integers stored at output:
 *	result = output + count * 8
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

unsigned char *Cookfs_WideInt2Binary(Tcl_WideInt *input, unsigned char *output, int count) {
    Tcl_WideInt v;
    while (count > 0) {
        v = *input;
        output[7] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[6] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[5] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[4] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[3] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[2] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[1] = v & ((Tcl_WideInt) 0x000000ff); v = v >> 8;
        output[0] = v & ((Tcl_WideInt) 0x000000ff);
        count--;
        output += 8;
        input++;
    }
    return output;
}

