/* Android: the subset of OpenSSL the game uses (android/openssl_shim/crypto.c). */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SHA_DIGEST_LENGTH 20
#define SHA256_DIGEST_LENGTH 32
unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md);
unsigned char *SHA256(const unsigned char *d, size_t n, unsigned char *md);
#ifdef __cplusplus
}
#endif
