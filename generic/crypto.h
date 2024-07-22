/* (c) 2014 Konstantin Kushnir */

#ifndef COOKFS_CRYPTO_H
#define COOKFS_CRYPTO_H 1

#include "pageObj.h"

// Size of encryption key in bytes. We use AES265, so it is defined
// as 32 bytes.

#define COOKFS_ENCRYPT_KEY_SIZE 32
#define COOKFS_ENCRYPT_IV_SIZE 16

void Cookfs_CryptoInit(void);

void Cookfs_RandomGenerate(Tcl_Interp *interp, unsigned char *buf, Tcl_Size size);
void Cookfs_Pbkdf2Hmac(unsigned char *secret, Tcl_Size secretSize,
    unsigned char *salt, Tcl_Size saltSize, unsigned int iterations,
    unsigned int dklen, unsigned char *output);

void Cookfs_AesEncryptRaw(unsigned char *buffer, Tcl_Size bufferSize,
    unsigned char *iv, unsigned char *key);
void Cookfs_AesDecryptRaw(unsigned char *buffer, Tcl_Size bufferSize,
    unsigned char *iv, unsigned char *key);

void Cookfs_AesEncrypt(Cookfs_PageObj pg, unsigned char *key);
int Cookfs_AesDecrypt(Cookfs_PageObj pg, unsigned char *key);

#endif /* COOKFS_CRYPTO_H */
