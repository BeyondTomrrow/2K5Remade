/* The few OpenSSL calls the game makes (SHA-1, SHA-256, HMAC-SHA1), for
 * Android builds, where OpenSSL is not part of the platform. Straight from
 * FIPS 180-4 and RFC 2104. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "openssl/sha.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void sha1_block(uint32_t h[5], const unsigned char *p)
{
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], t;
    int i;
    for (i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static const uint32_t k256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static void sha256_block(uint32_t h[8], const unsigned char *p)
{
    uint32_t w[64], s[8], t1, t2;
    int i;
    for (i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
    for (; i < 64; i++)
        w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
               (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
    memcpy(s, h, sizeof s);
    for (i = 0; i < 64; i++) {
        t1 = s[7] + (ROR(s[4], 6) ^ ROR(s[4], 11) ^ ROR(s[4], 25)) + ((s[4] & s[5]) ^ (~s[4] & s[6])) + k256[i] + w[i];
        t2 = (ROR(s[0], 2) ^ ROR(s[0], 13) ^ ROR(s[0], 22)) + ((s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]));
        s[7] = s[6]; s[6] = s[5]; s[5] = s[4]; s[4] = s[3] + t1;
        s[3] = s[2]; s[2] = s[1]; s[1] = s[0]; s[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) h[i] += s[i];
}

/* Merkle-Damgard padding shared by both: blocks of 64 bytes, big-endian length. */
static void md_run(uint32_t *h, int nh, void (*blk)(uint32_t *, const unsigned char *),
                   const unsigned char *d, size_t n, unsigned char *out)
{
    unsigned char tail[128];
    size_t full = n & ~(size_t)63, rest = n - full, tl;
    uint64_t bits = (uint64_t)n * 8;
    int i;
    for (size_t o = 0; o < full; o += 64) blk(h, d + o);
    memset(tail, 0, sizeof tail);
    memcpy(tail, d + full, rest);
    tail[rest] = 0x80;
    tl = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++) tail[tl - 1 - i] = (unsigned char)(bits >> (8 * i));
    blk(h, tail);
    if (tl == 128) blk(h, tail + 64);
    for (i = 0; i < nh; i++) {
        out[4 * i] = (unsigned char)(h[i] >> 24); out[4 * i + 1] = (unsigned char)(h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h[i] >> 8); out[4 * i + 3] = (unsigned char)h[i];
    }
}

static void sha1_blk(uint32_t *h, const unsigned char *p) { sha1_block(h, p); }
static void sha256_blk(uint32_t *h, const unsigned char *p) { sha256_block(h, p); }

unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md)
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    md_run(h, 5, sha1_blk, d, n, md);
    return md;
}

unsigned char *SHA256(const unsigned char *d, size_t n, unsigned char *md)
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    md_run(h, 8, sha256_blk, d, n, md);
    return md;
}

static const EVP_MD s_sha1 = { 20 };
const EVP_MD *EVP_sha1(void) { return &s_sha1; }

/* HMAC-SHA1 only (all the game asks for). */
unsigned char *HMAC(const EVP_MD *evp, const void *key, int key_len, const unsigned char *d, size_t n,
                    unsigned char *md, unsigned int *md_len)
{
    unsigned char k[64], buf[64 + 20], *inner;
    size_t i;
    (void)evp;
    memset(k, 0, sizeof k);
    if (key_len > 64) SHA1((const unsigned char *)key, (size_t)key_len, k);
    else if (key_len > 0) memcpy(k, key, (size_t)key_len);
    inner = (unsigned char *)malloc(64 + n);
    if (!inner) return NULL;
    for (i = 0; i < 64; i++) inner[i] = k[i] ^ 0x36;
    if (n) memcpy(inner + 64, d, n);
    SHA1(inner, 64 + n, buf + 64);
    free(inner);
    for (i = 0; i < 64; i++) buf[i] = k[i] ^ 0x5c;
    SHA1(buf, sizeof buf, md);
    if (md_len) *md_len = 20;
    return md;
}
