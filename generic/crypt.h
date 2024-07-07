/* (c) 2014 Konstantin Kushnir */

#ifndef COOKFS_CRYPT_H
#define COOKFS_CRYPT_H 1

void Cookfs_CryptInit(void);

void Cookfs_RandomGenerate(Tcl_Interp *interp, unsigned char *buf, Tcl_Size size);
void Cookfs_Pbkdf2Hmac(unsigned char *secret, Tcl_Size secretSize,
    unsigned char *salt, Tcl_Size saltSize, unsigned int iterations,
    unsigned int dklen, unsigned char *output);

#endif /* COOKFS_CRYPT_H */
