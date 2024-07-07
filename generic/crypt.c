/*
 * crypt.c
 *
 * Provides implementation of cryptographic functions
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"

// for getpid()
#include <unistd.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef STRICT
#define STRICT // See MSDN Article Q83456
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
// for BCryptGenRandom()
#include <bcrypt.h>
// for NT_SUCCESS()
#include <Ntdef.h>
#endif /* _WIN32 */

#include "../7zip/C/Aes.h"
#include "../7zip/C/Sha256.h"

void Cookfs_CryptInit(void) {
    Sha256Prepare();
    AesGenTables();
}

typedef struct {
  CSha256 inner;
  CSha256 outer;
} HMAC_CTX;

static void Cookfs_HmacInit(HMAC_CTX *ctx, unsigned char *key,
    Tcl_Size keySize)
{
    unsigned char k[SHA256_BLOCK_SIZE];

    // Key longer than sha256 blockSize must be shortened
    if (keySize > SHA256_BLOCK_SIZE) {
        Sha256_Init(&ctx->inner);
        Sha256_Update(&ctx->inner, key, keySize);
        Sha256_Final(&ctx->inner, k);
        key = k;
        keySize = SHA256_DIGEST_SIZE;
    } else {
        memcpy(k, key, keySize);
    }

    // pad the key with zeros on the right
    if (SHA256_BLOCK_SIZE > keySize) {
        memset(k + keySize, 0, SHA256_BLOCK_SIZE - keySize);
    }

    unsigned char block_inner[SHA256_BLOCK_SIZE];
    unsigned char block_outer[SHA256_BLOCK_SIZE];

    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        block_inner[i] = 0x36 ^ k[i];
        block_outer[i] = 0x5c ^ k[i];
    }

    Sha256_Init(&ctx->inner);
    Sha256_Update(&ctx->inner, block_inner, sizeof(block_inner));

    Sha256_Init(&ctx->outer);
    Sha256_Update(&ctx->outer, block_outer, sizeof(block_outer));
}

static inline void Cookfs_HmacUpdate(HMAC_CTX *ctx, unsigned char *data,
    Tcl_Size dataSize)
{
    Sha256_Update(&ctx->inner, data, dataSize);
}

static inline void Cookfs_HmacFinal(HMAC_CTX *ctx, unsigned char *output) {
    Sha256_Final(&ctx->inner, output);
    Sha256_Update(&ctx->outer, output, SHA256_DIGEST_SIZE);
    Sha256_Final(&ctx->outer, output);
}

void Cookfs_Pbkdf2Hmac(unsigned char *secret, Tcl_Size secretSize,
    unsigned char *salt, Tcl_Size saltSize, unsigned int iterations,
    unsigned int dklen, unsigned char *output)
{
    CookfsLog2(printf("enter; secretSize: %" TCL_SIZE_MODIFIER "d, saltSize: %"
        TCL_SIZE_MODIFIER "d, iter: %d, dklen: %d", secretSize, saltSize,
        iterations, dklen));

    HMAC_CTX ctx;

    unsigned char md[SHA256_DIGEST_SIZE];

    int counter = 1;

    while (dklen) {
        unsigned char c[4];
        Cookfs_Int2Binary(&counter, c, 1);

        Cookfs_HmacInit(&ctx, secret, secretSize);
        Cookfs_HmacUpdate(&ctx, salt, saltSize);
        Cookfs_HmacUpdate(&ctx, c, sizeof(c));
        Cookfs_HmacFinal(&ctx, md);

        unsigned int want_copy = (dklen > SHA256_DIGEST_SIZE ?
            SHA256_DIGEST_SIZE : dklen);

        memcpy(output, md, want_copy);
        CookfsLog2(printf("want_copy: %u", want_copy));

        for (unsigned int ic = 1; ic < iterations; ic++) {
            Cookfs_HmacInit(&ctx, secret, secretSize);
            Cookfs_HmacUpdate(&ctx, md, sizeof(md));
            Cookfs_HmacFinal(&ctx, md);
            for (unsigned int i = 0; i < want_copy; i++) {
                output[i] ^= md[i];
            }
        }

        dklen -= want_copy;
        output += want_copy;
        counter++;
    }
}

void Cookfs_RandomGenerate(Tcl_Interp *interp, unsigned char *buf, Tcl_Size size) {

    CookfsLog2(printf("enter, want to generate %" TCL_SIZE_MODIFIER "d"
        " bytes", size));

    // Cleanup random destination storage
    memset(buf, 0, size);

#ifdef _WIN32
    // Use BCryptGenRandom() on Windows.
    DWORD status = BCryptGenRandom(NULL, buf, size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(NT_SUCCESS(status)) {
        CookfsLog2(printf("return result from windows-specific rng"
            " generator"));
        return;
    }
    CookfsLog2(printf("WARNING: windows-specific rng generator failed"));
    goto fallback_tcl;
#else
    // Try using /dev/urandom if it exists
    Tcl_Obj *urandom = Tcl_NewStringObj("/dev/urandom", -1);
    Tcl_IncrRefCount(urandom);
    Tcl_Channel chan = Tcl_FSOpenFileChannel(NULL, urandom, "rb", 0);
    Tcl_DecrRefCount(urandom);

    if (chan != NULL) {
        Tcl_Size read = Tcl_Read(chan, (char *)buf, size);
        Tcl_Close(NULL, chan);
        if (read == size) {
            CookfsLog2(printf("return result from /dev/urandom"));
            return;
        }
        CookfsLog2(printf("WARNING: could not read enough bytes from /dev/urandom,"
            " got only %" TCL_SIZE_MODIFIER "d bytes", read));
    }
    CookfsLog2(printf("WARNING: could not open /dev/urandom, error code: %d",
        Tcl_GetErrno()));
    goto fallback_tcl;
#endif /* _WIN32 */

fallback_tcl:

    // Fallback to Tcl random number generator if the platform-specific random
    // number generator did not work.

    if (interp != NULL) {
        CookfsLog2(printf("try to use Tcl rng generator"));
        double r;
        for (Tcl_Size i = 0; i < size; i += sizeof(r)) {
            if (Tcl_EvalEx(interp, "::tcl::mathfunc::rand", -1, 0) != TCL_OK) {
                CookfsLog2(printf("WARNING: got error from Tcl: %s",
                    Tcl_GetString(Tcl_GetObjResult(interp))));
                goto fallback_c;
            }
            if (Tcl_GetDoubleFromObj(NULL, Tcl_GetObjResult(interp), &r)
                != TCL_OK)
            {
                CookfsLog2(printf("WARNING: got something else than double"
                    " from Tcl: %s", Tcl_GetString(Tcl_GetObjResult(interp))));
                goto fallback_c;
            }
            int want_copy = (i + sizeof(r) > (unsigned)size ?
                (unsigned)size - i : sizeof(r));
            CookfsLog2(printf("copy %d bytes from Tcl rng generator",
                want_copy));
            memcpy(&buf[i], (void *)&r, want_copy);
        }
        CookfsLog2(printf("return result from Tcl rng generator"));
        return;
    } else {
        CookfsLog2(printf("WARNING: could not use Tcl rng generator, interp"
            " is NULL"));
    }

fallback_c:

    // Fallback to C random number generator if enything else did not work.
    CookfsLog2(printf("try to use C rng generator"));

    // Initializing the rng generator to the current time may not be secure
    // enough, since timestamps can be detected from the archive file or
    // files within the archive. However, the current number of
    // microseconds+pid should be sufficient.
    Tcl_Time now;
    Tcl_GetTime(&now);
    srand(now.sec ^ now.usec ^ getpid());
    int r;
    for (Tcl_Size i = 0; i < size; i += sizeof(r)) {
        r = rand();
        int want_copy = (i + sizeof(r) > (unsigned)size ?
            (unsigned)size - i : sizeof(r));
        CookfsLog2(printf("copy %d bytes from C rng generator",
            want_copy));
        memcpy(&buf[i], (void *)&r, want_copy);
    }

    CookfsLog2(printf("return result from C rng generator"));
    return;

}

