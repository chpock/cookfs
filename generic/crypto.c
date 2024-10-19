/*
 * crypto.c
 *
 * Provides implementation of cryptographic functions
 *
 * (c) 2024 Konstantin Kushnir
 */

#include "cookfs.h"
#include "crypto.h"

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
#include <ntdef.h>
#endif /* _WIN32 */

#if HAVE_MBEDTLS
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include <mbedtls/pkcs5.h>
#else
#include "../7zip/C/Aes.h"
#include "../7zip/C/Sha256.h"
#endif /* HAVE_MBEDTLS */

void Cookfs_CryptoInit(void) {
#if !HAVE_MBEDTLS
    Sha256Prepare();
    AesGenTables();
#endif /* !HAVE_MBEDTLS */
}

#if HAVE_MBEDTLS

#define CSha256 mbedtls_sha256_context

#define SHA256_NUM_BLOCK_WORDS  16
#define SHA256_NUM_DIGEST_WORDS  8
#define SHA256_BLOCK_SIZE   (SHA256_NUM_BLOCK_WORDS * 4)
#define SHA256_DIGEST_SIZE  (SHA256_NUM_DIGEST_WORDS * 4)

#define Sha256_Init(ctx) \
    mbedtls_sha256_init(ctx); \
    mbedtls_sha256_starts((ctx), 0)

#define Sha256_Update(ctx, key, keySize) mbedtls_sha256_update((ctx), (key), (keySize))

#define Sha256_Final(ctx, k) mbedtls_sha256_finish((ctx), (k))

#endif /* HAVE_MBEDTLS */

typedef struct {
  CSha256 inner;
  CSha256 outer;
} HMAC_CTX;

#if COOKFS_PAGEOBJ_BLOCK_SIZE != AES_BLOCK_SIZE
#error Condition COOKFS_PAGEOBJ_BLOCK_SIZE == AES_BLOCK_SIZE is not true. Is is unsupported. Buffer owerflow errors are expected in Cookfs_Aes* functions.
#endif

#if COOKFS_ENCRYPT_IV_SIZE != AES_BLOCK_SIZE
#error Condition COOKFS_ENCRYPT_IV_SIZE == AES_BLOCK_SIZE is not true. Is is unsupported. Buffer owerflow errors are expected in Cookfs_Aes* functions.
#endif

void Cookfs_AesEncryptRaw(unsigned char *buffer, Tcl_Size bufferSize,
    unsigned char *iv, unsigned char *key)
{
    CookfsLog(printf("enter..."));

#if HAVE_MBEDTLS

    mbedtls_aes_context ctx;

    unsigned char _iv[AES_BLOCK_SIZE];
    memcpy(_iv, iv, AES_BLOCK_SIZE);

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, COOKFS_ENCRYPT_KEY_SIZE * 8);

    CookfsLog(printf("run mbedtls_aes_crypt_cbc() ..."));
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, bufferSize, _iv, buffer,
        buffer);

    mbedtls_aes_free(&ctx);

#else

    UInt32 ivAes[AES_NUM_IVMRK_WORDS];

    AesCbc_Init(ivAes, iv);
    // ivAes is a pointer to 32-bit integers. But IV size is defined in 8-bit
    // AES_BLOCK_SIZE blocks. So here we use an offset of 4 (32 / 8).
    Aes_SetKey_Enc(ivAes + 4, key, COOKFS_ENCRYPT_KEY_SIZE);

    CookfsLog(printf("run AesCbc_Encode() ..."));
    AesCbc_Encode(ivAes, buffer, bufferSize / AES_BLOCK_SIZE);

#endif /* HAVE_MBEDTLS */

    CookfsLog(printf("ok"));

    return;
}

void Cookfs_AesEncrypt(Cookfs_PageObj pg, unsigned char *key) {
    CookfsLog(printf("enter..."));

    Cookfs_PageObjAddPadding(pg);
    Cookfs_AesEncryptRaw(pg->buf, Cookfs_PageObjSize(pg),
        Cookfs_PageObjGetIV(pg), key);

    CookfsLog(printf("ok"));
    return;
}

void Cookfs_AesDecryptRaw(unsigned char *buffer, Tcl_Size bufferSize,
    unsigned char *iv, unsigned char *key)
{
    CookfsLog(printf("enter..."));

#if HAVE_MBEDTLS

    mbedtls_aes_context ctx;

    unsigned char _iv[AES_BLOCK_SIZE];
    memcpy(_iv, iv, AES_BLOCK_SIZE);

    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_dec(&ctx, key, COOKFS_ENCRYPT_KEY_SIZE * 8);

    CookfsLog(printf("run mbedtls_aes_crypt_cbc() ..."));
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, bufferSize, _iv, buffer,
        buffer);

    mbedtls_aes_free(&ctx);

#else

    UInt32 ivAes[AES_NUM_IVMRK_WORDS];

    AesCbc_Init(ivAes, iv);
    // ivAes is a pointer to 32-bit integers. But IV size is defined in 8-bit
    // AES_BLOCK_SIZE blocks. So here we use an offset of 4 (32 / 8).
    Aes_SetKey_Dec(ivAes + 4, key, COOKFS_ENCRYPT_KEY_SIZE);

    CookfsLog(printf("run AesCbc_Decode() ..."));
    AesCbc_Decode(ivAes, buffer, bufferSize / AES_BLOCK_SIZE);

#endif /* HAVE_MBEDTLS */

    CookfsLog(printf("ok"));

    return;
}

int Cookfs_AesDecrypt(Cookfs_PageObj pg, unsigned char *key) {

    CookfsLog(printf("enter..."));

    Cookfs_AesDecryptRaw(pg->buf, Cookfs_PageObjSize(pg),
        Cookfs_PageObjGetIV(pg), key);

    CookfsLog(printf("unpad data ..."));
    int rc = Cookfs_PageObjRemovePadding(pg);

    CookfsLog(printf("return %s", (rc == TCL_OK ? "TCL_OK" : "TCL_ERROR")));
    return rc;

}

#if HAVE_MBEDTLS

void Cookfs_Pbkdf2Hmac(unsigned char *secret, Tcl_Size secretSize,
    unsigned char *salt, Tcl_Size saltSize, unsigned int iterations,
    unsigned int dklen, unsigned char *output)
{

    CookfsLog(printf("enter; secretSize: %" TCL_SIZE_MODIFIER "d, saltSize: %"
        TCL_SIZE_MODIFIER "d, iter: %u, dklen: %u", secretSize, saltSize,
        iterations, dklen));

    mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, secret, secretSize,
        salt, saltSize, iterations, dklen, output);

}

#else

static void Cookfs_HmacInit(HMAC_CTX *ctx, unsigned char *key,
    Tcl_Size keySize)
{
    unsigned char k[SHA256_BLOCK_SIZE];

    // Key longer than sha256 blockSize must be shortened
    if (keySize > SHA256_BLOCK_SIZE) {
        Sha256_Init(&ctx->inner);
        Sha256_Update(&ctx->inner, key, keySize);
        Sha256_Final(&ctx->inner, k);
        // cppcheck-suppress unreadVariable symbolName=key
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
    CookfsLog(printf("enter; secretSize: %" TCL_SIZE_MODIFIER "d, saltSize: %"
        TCL_SIZE_MODIFIER "d, iter: %u, dklen: %u", secretSize, saltSize,
        iterations, dklen));

    HMAC_CTX ctx_init;
    HMAC_CTX ctx;

    unsigned char md[SHA256_DIGEST_SIZE];

    int counter = 1;

    Cookfs_HmacInit(&ctx_init, secret, secretSize);

    while (dklen) {
        unsigned char c[4];
        Cookfs_Int2Binary(&counter, c, 1);

        //Cookfs_HmacInit(&ctx, secret, secretSize);
        memcpy(&ctx, &ctx_init, sizeof(ctx));
        Cookfs_HmacUpdate(&ctx, salt, saltSize);
        Cookfs_HmacUpdate(&ctx, c, sizeof(c));
        Cookfs_HmacFinal(&ctx, md);

        unsigned int want_copy = (dklen > SHA256_DIGEST_SIZE ?
            SHA256_DIGEST_SIZE : dklen);

        memcpy(output, md, want_copy);
        CookfsLog(printf("want_copy: %u", want_copy));

        for (unsigned int ic = 1; ic < iterations; ic++) {
            memcpy(&ctx, &ctx_init, sizeof(ctx));
            //Cookfs_HmacInit(&ctx, secret, secretSize);
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

#endif /* HAVE_MBEDTLS */

void Cookfs_RandomGenerate(Tcl_Interp *interp, unsigned char *buf, Tcl_Size size) {

    CookfsLog(printf("enter, want to generate %" TCL_SIZE_MODIFIER "d"
        " bytes", size));

    // Cleanup random destination storage
    memset(buf, 0, size);

#ifdef _WIN32
    // Use BCryptGenRandom() on Windows.
    DWORD status = BCryptGenRandom(NULL, buf, size,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if(NT_SUCCESS(status)) {
        CookfsLog(printf("return result from windows-specific rng"
            " generator"));
        return;
    }
    CookfsLog(printf("WARNING: windows-specific rng generator failed"));
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
            CookfsLog(printf("return result from /dev/urandom"));
            return;
        }
        CookfsLog(printf("WARNING: could not read enough bytes from /dev/urandom,"
            " got only %" TCL_SIZE_MODIFIER "d bytes", read));
    }
    CookfsLog(printf("WARNING: could not open /dev/urandom, error code: %d",
        Tcl_GetErrno()));
    goto fallback_tcl;
#endif /* _WIN32 */

fallback_tcl:

    // Fallback to Tcl random number generator if the platform-specific random
    // number generator did not work.

    if (interp != NULL) {
        CookfsLog(printf("try to use Tcl rng generator"));
        double r;
        for (Tcl_Size i = 0; i < size; i += sizeof(r)) {
            if (Tcl_EvalEx(interp, "::tcl::mathfunc::rand", -1, 0) != TCL_OK) {
                CookfsLog(printf("WARNING: got error from Tcl: %s",
                    Tcl_GetString(Tcl_GetObjResult(interp))));
                goto fallback_c;
            }
            if (Tcl_GetDoubleFromObj(NULL, Tcl_GetObjResult(interp), &r)
                != TCL_OK)
            {
                CookfsLog(printf("WARNING: got something else than double"
                    " from Tcl: %s", Tcl_GetString(Tcl_GetObjResult(interp))));
                goto fallback_c;
            }
            unsigned int want_copy = ((unsigned)i + sizeof(r) > (unsigned)size ?
                (unsigned)size - (unsigned)i : sizeof(r));
            CookfsLog(printf("copy %u bytes from Tcl rng generator",
                want_copy));
            memcpy(&buf[i], (void *)&r, want_copy);
        }
        CookfsLog(printf("return result from Tcl rng generator"));
        return;
    } else {
        CookfsLog(printf("WARNING: could not use Tcl rng generator, interp"
            " is NULL"));
    }

fallback_c:

    // Fallback to C random number generator if enything else did not work.
    CookfsLog(printf("try to use C rng generator"));

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
        unsigned int want_copy = ((unsigned)i + sizeof(r) > (unsigned)size ?
            (unsigned)size - (unsigned)i : sizeof(r));
        CookfsLog(printf("copy %u bytes from C rng generator",
            want_copy));
        memcpy(&buf[i], (void *)&r, want_copy);
    }

    CookfsLog(printf("return result from C rng generator"));
    return;

}

int Cookfs_Sha256Cmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

    UNUSED(clientData);

    Tcl_Obj *obj;
    CSha256 ctx;

    unsigned char *bytes;
    unsigned char sha256[SHA256_DIGEST_SIZE];
    Tcl_Size size;

    if (objc < 2 || objc > 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-bin? data");
        return TCL_ERROR;
    }

    if (objc == 3) {
        const char *opt = Tcl_GetStringFromObj(objv[1], &size);
        if (size < 1 || strncmp(opt, "-bin", size) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "bad option \"%s\": must be -bin", opt));
            Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "option",
                opt, (const char *)NULL);
            return TCL_ERROR;
        }
        obj = objv[2];
    } else {
        obj = objv[1];
    }

    bytes = Tcl_GetByteArrayFromObj(obj, &size);

    Sha256_Init(&ctx);
    Sha256_Update(&ctx, bytes, size);
    Sha256_Final(&ctx, sha256);

    if (objc == 3) {
        obj = Tcl_NewByteArrayObj(sha256, SHA256_DIGEST_SIZE);
    } else {
        char hex[SHA256_DIGEST_SIZE*2+1];
        for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
            sprintf(hex + i*2, "%02X", ((int) sha256[i]));
        }
        hex[SHA256_DIGEST_SIZE*2] = 0;
        obj = Tcl_NewStringObj(hex, SHA256_DIGEST_SIZE*2);
    }

    Tcl_SetObjResult(interp, obj);

    return TCL_OK;
}
