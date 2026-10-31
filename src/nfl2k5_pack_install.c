/* Built-in mod-pack installer: a 2K5 Mod Studio .2k5patch -> mods/packs/<pack>.
 *
 * This is tools/pack-install.py in C, inside the game, so installing a pack
 * needs no Python, no helper .exe and no PC: the same code runs on Windows,
 * Linux and Android. Drop the release's .2k5patch into <root>/mods/patches
 * (src/nfl2k5_mod_packs.c finds it at start and calls in here on a background
 * thread), or run `NFL2K5 --install-pack <file>`.
 *
 * A .2k5patch (format 3, "source-copy-v1") is a zip: manifest.json lists each
 * disc file with its retail ("before") and patched ("after") size and SHA-256.
 * A changed file has a literal-run member (records: u64 offset, u32 length,
 * 32-byte sha before, 32-byte sha after, then the bytes) and copy members
 * (84-byte records: u64 destination, u64 source, u32 length, two sha256) that
 * copy from the same retail file or another one (cross_copies). Everything
 * else is the retail file at the same offset, truncated or zero-extended.
 *
 * The output is the overlay the game reads (external/xboxrecomp
 * kernel_overlay.c), byte-identical to what pack-install.py writes:
 *   overlay.bin   "2K5OVL1\0", u32 files, u32 changed, the file table
 *                 (u16 name length, name, u64 retail size, u64 new size), then
 *                 per changed file u32 index, u32 extents and the extents
 *                 (u64 dst, u32 len, u8 kind, u16 source file, u64 source)
 *   lit/<n>.bin   file n's literal bytes, concatenated
 *   default.xbe   the patched executable (checked against the manifest hash)
 *   pack.json     name, version, executable hash, file list
 * Nothing on the retail disc is written. Members are inflated with miniz
 * (each one's CRC-32 is checked), the retail files the pack reads are checked
 * against the manifest's SHA-256 (remembered in mods/packs/retail-verified.txt
 * by size and time, so only the first install pays for it), and the pack is
 * built in <pack>.installing and renamed into place only when complete. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include "miniz.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <direct.h>
#include <sys/stat.h>
#pragma comment(lib, "bcrypt.lib")
#define pi_fseek _fseeki64
#define pi_ftell _ftelli64
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <strings.h>
#define _stricmp strcasecmp
#define pi_fseek fseeko
#define pi_ftell ftello
#endif

#define PI_PATH 1024

/* progress for the toasts / menu: 0..100, and what is being done */
volatile int g_pack_install_percent;
char g_pack_install_stage[96];
/* called (on the installing thread) each time the percentage changes */
void (*g_pack_install_on_progress)(int percent, const char *stage);

static FILE *s_log;

static void pi_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[PACKINSTALL] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    if (s_log) {
        va_start(ap, fmt);
        vfprintf(s_log, fmt, ap);
        fputc('\n', s_log);
        fflush(s_log);
        va_end(ap);
    }
}

/* ---- small portable file helpers ------------------------------------------- */

static int pi_mkdir(const char *p)
{
#ifdef _WIN32
    return _mkdir(p) == 0 || GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
#else
    return mkdir(p, 0755) == 0 || errno == EEXIST;
#endif
}

static int pi_stat(const char *p, uint64_t *size, int64_t *mtime, int *is_dir)
{
#ifdef _WIN32
    struct _stat64 st;
    if (_stat64(p, &st) != 0) return 0;
    if (is_dir) *is_dir = (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    if (is_dir) *is_dir = S_ISDIR(st.st_mode);
#endif
    if (size) *size = (uint64_t)st.st_size;
    if (mtime) *mtime = (int64_t)st.st_mtime;
    return 1;
}

static int pi_exists(const char *p) { return pi_stat(p, NULL, NULL, NULL); }

static int pi_rename(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static uint64_t pi_free_bytes(const char *dir)
{
#ifdef _WIN32
    ULARGE_INTEGER avail;
    if (GetDiskFreeSpaceExA(dir, &avail, NULL, NULL)) return avail.QuadPart;
#else
    struct statvfs v;
    if (statvfs(dir, &v) == 0) return (uint64_t)v.f_bavail * (uint64_t)v.f_frsize;
#endif
    return UINT64_MAX;
}

/* Remove a pack folder this installer made: only the files it writes, so a
 * folder holding anything else (an old native build, the player's notes) is
 * left in place rather than wiped. Returns 1 when the folder is gone. */
static int pi_remove_pack_dir(const char *dir)
{
    static const char *const k_files[] = { "overlay.bin", "default.xbe", "pack.json", "install.log" };
    char p[PI_PATH];
    size_t i;
    for (i = 0; i < sizeof k_files / sizeof k_files[0]; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, k_files[i]);
        remove(p);
    }
    snprintf(p, sizeof p, "%s/lit", dir);
#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        char pat[PI_PATH];
        HANDLE h;
        snprintf(pat, sizeof pat, "%s/lit/*.bin", dir);
        h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char f[PI_PATH];
                snprintf(f, sizeof f, "%s/lit/%s", dir, fd.cFileName);
                remove(f);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryA(p);
        return RemoveDirectoryA(dir) != 0 || !pi_exists(dir);
    }
#else
    {
        DIR *d = opendir(p);
        struct dirent *e;
        if (d) {
            while ((e = readdir(d)) != NULL) {
                size_t n = strlen(e->d_name);
                if (n > 4 && !strcmp(e->d_name + n - 4, ".bin")) {
                    char f[PI_PATH];
                    snprintf(f, sizeof f, "%s/%s", p, e->d_name);
                    remove(f);
                }
            }
            closedir(d);
        }
        rmdir(p);
        return rmdir(dir) == 0 || !pi_exists(dir);
    }
#endif
}

/* ---- SHA-256 (BCrypt on Windows; a portable one elsewhere: Android's
 * OpenSSL shim has only the one-shot SHA256) ------------------------------- */

#if defined(_WIN32) && !defined(PI_PORTABLE_SHA)
typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE h;
} PiSha;

static int sha_begin(PiSha *s)
{
    memset(s, 0, sizeof *s);
    if (BCryptOpenAlgorithmProvider(&s->alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) return 0;
    if (BCryptCreateHash(s->alg, &s->h, NULL, 0, NULL, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(s->alg, 0);
        return 0;
    }
    return 1;
}

static void sha_update(PiSha *s, const void *p, size_t n)
{
    BCryptHashData(s->h, (PUCHAR)p, (ULONG)n, 0);
}

static void sha_digest(PiSha *s, unsigned char d[32])
{
    BCryptFinishHash(s->h, d, 32, 0);
    BCryptDestroyHash(s->h);
    BCryptCloseAlgorithmProvider(s->alg, 0);
}
#else
typedef struct {
    uint32_t h[8];
    uint64_t len;
    unsigned char buf[64];
    size_t used;
} PiSha;

static const uint32_t k_sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define PI_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(PiSha *s, const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (; i < 64; i++) {
        uint32_t s0 = PI_ROR(w[i - 15], 7) ^ PI_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = PI_ROR(w[i - 2], 17) ^ PI_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
    e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t t1 = h + (PI_ROR(e, 6) ^ PI_ROR(e, 11) ^ PI_ROR(e, 25)) + ((e & f) ^ (~e & g)) + k_sha_k[i] + w[i];
        uint32_t t2 = (PI_ROR(a, 2) ^ PI_ROR(a, 13) ^ PI_ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static int sha_begin(PiSha *s)
{
    static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(s->h, init, sizeof init);
    s->len = 0;
    s->used = 0;
    return 1;
}

static void sha_update(PiSha *s, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    s->len += n;
    if (s->used) {
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->buf + s->used, p, take);
        s->used += take; p += take; n -= take;
        if (s->used < 64) return;
        sha_block(s, s->buf);
        s->used = 0;
    }
    for (; n >= 64; p += 64, n -= 64) sha_block(s, p);
    memcpy(s->buf, p, n);
    s->used = n;
}

static void sha_digest(PiSha *s, unsigned char d[32])
{
    uint64_t bits = s->len * 8;
    int i;
    s->buf[s->used++] = 0x80;
    if (s->used > 56) {
        memset(s->buf + s->used, 0, 64 - s->used);
        sha_block(s, s->buf);
        s->used = 0;
    }
    memset(s->buf + s->used, 0, 56 - s->used);
    for (i = 0; i < 8; i++) s->buf[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    sha_block(s, s->buf);
    for (i = 0; i < 32; i++) d[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}
#endif

static void sha_end(PiSha *s, char hex[65])
{
    unsigned char d[32];
    int i;
    sha_digest(s, d);
    for (i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    hex[64] = 0;
}

/* ---- a small JSON DOM for manifest.json (a few MB, ~100k nodes) ------------- */

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct J {
    int type;
    char *key, *str;
    double num;
    struct J *child, *next;
} J;

typedef struct { const char *p, *end; int depth, bad; } JP;

static void jp_ws(JP *s) { while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\r' || *s->p == '\n')) s->p++; }

static char *jp_string(JP *s)
{
    const char *q;
    char *out, *o;
    if (s->p >= s->end || *s->p != '"') { s->bad = 1; return NULL; }
    q = ++s->p;
    while (q < s->end && *q != '"') q += (*q == '\\') ? 2 : 1;
    if (q >= s->end) { s->bad = 1; return NULL; }
    out = o = (char *)malloc((size_t)(q - s->p) + 1);
    if (!out) { s->bad = 1; return NULL; }
    while (s->p < q) {
        char c = *s->p++;
        if (c != '\\') { *o++ = c; continue; }
        c = *s->p++;
        switch (c) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned v = 0;
            int k;
            for (k = 0; k < 4 && s->p < q; k++, s->p++)
                v = v * 16 + (unsigned)(isdigit((unsigned char)*s->p) ? *s->p - '0' : (tolower((unsigned char)*s->p) - 'a' + 10));
            if (v < 0x80) *o++ = (char)v;
            else if (v < 0x800) { *o++ = (char)(0xC0 | (v >> 6)); *o++ = (char)(0x80 | (v & 0x3F)); }
            else { *o++ = (char)(0xE0 | (v >> 12)); *o++ = (char)(0x80 | ((v >> 6) & 0x3F)); *o++ = (char)(0x80 | (v & 0x3F)); }
            break;
        }
        default: *o++ = c; break;
        }
    }
    *o = 0;
    s->p = q + 1;
    return out;
}

static J *jp_value(JP *s)
{
    J *v;
    jp_ws(s);
    if (s->p >= s->end || ++s->depth > 64) { s->bad = 1; return NULL; }
    v = (J *)calloc(1, sizeof *v);
    if (!v) { s->bad = 1; return NULL; }
    if (*s->p == '{' || *s->p == '[') {
        int obj = *s->p == '{';
        J **tail = &v->child;
        v->type = obj ? J_OBJ : J_ARR;
        s->p++;
        jp_ws(s);
        if (s->p < s->end && *s->p == (obj ? '}' : ']')) { s->p++; s->depth--; return v; }
        for (;;) {
            char *key = NULL;
            J *c;
            jp_ws(s);
            if (obj) {
                key = jp_string(s);
                jp_ws(s);
                if (s->bad || s->p >= s->end || *s->p != ':') { free(key); s->bad = 1; break; }
                s->p++;
            }
            c = jp_value(s);
            if (!c) { free(key); break; }
            c->key = key;
            *tail = c;
            tail = &c->next;
            jp_ws(s);
            if (s->p < s->end && *s->p == ',') { s->p++; continue; }
            if (s->p < s->end && *s->p == (obj ? '}' : ']')) { s->p++; break; }
            s->bad = 1;
            break;
        }
    } else if (*s->p == '"') {
        v->type = J_STR;
        v->str = jp_string(s);
    } else if (!strncmp(s->p, "true", 4) || !strncmp(s->p, "false", 5)) {
        v->type = J_BOOL;
        v->num = *s->p == 't';
        s->p += *s->p == 't' ? 4 : 5;
    } else if (!strncmp(s->p, "null", 4)) {
        v->type = J_NULL;
        s->p += 4;
    } else {
        char *e;
        v->type = J_NUM;
        v->num = strtod(s->p, &e);
        if (e == s->p) s->bad = 1;
        s->p = e;
    }
    s->depth--;
    return v;
}

static void j_free(J *v)
{
    while (v) {
        J *n = v->next;
        j_free(v->child);
        free(v->key);
        free(v->str);
        free(v);
        v = n;
    }
}

static J *j_get(const J *o, const char *key)
{
    J *c;
    if (!o || o->type != J_OBJ) return NULL;
    for (c = o->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

static const char *j_str(const J *o, const char *key, const char *def)
{
    J *c = j_get(o, key);
    return c && c->type == J_STR && c->str ? c->str : def;
}

static double j_num(const J *o, const char *key, double def)
{
    J *c = j_get(o, key);
    return c && c->type == J_NUM ? c->num : def;
}

static void json_put_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c == '\n') fputs("\\n", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

/* ---- the pack ---------------------------------------------------------------- */

typedef struct {
    uint64_t dst, src;
    uint32_t len;
    uint16_t srcf;                       /* 0xFFFF: literal (src = offset in lit) */
} Ext;

typedef struct {
    const char *path;
    uint64_t before, after;
    const char *sha_before, *sha_after;
    const J *node;
    int changed;                         /* "runs" */
    int needed;                          /* read by the overlay: must be the clean retail file */
} PFile;

typedef struct {
    mz_zip_archive zip;
    char root[PI_PATH], disc[PI_PATH], stage[PI_PATH];
    PFile *files;
    int nfiles;
    uint64_t work_total, work_done;      /* bytes, for the percentage */
    char err[256];
} Pack;

static void pi_progress(Pack *pk, uint64_t add, const char *stage)
{
    pk->work_done += add;
    if (stage) snprintf(g_pack_install_stage, sizeof g_pack_install_stage, "%s", stage);
    {
        int pct = pk->work_total ? (int)(pk->work_done * 100 / pk->work_total) : 0;
        if (pct > 99) pct = 99;
        if (pct != g_pack_install_percent) {
            g_pack_install_percent = pct;
            if (g_pack_install_on_progress) g_pack_install_on_progress(pct, g_pack_install_stage);
        }
    }
}

static int pi_fail(Pack *pk, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pk->err, sizeof pk->err, fmt, ap);
    va_end(ap);
    pi_log("FAILED: %s", pk->err);
    return 0;
}

static void disc_path(const Pack *pk, const char *rel, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s", pk->disc, rel);
#ifdef _WIN32
    for (char *c = out; *c; c++) if (*c == '/') *c = '\\';
#else
    /* The pack names disc files as Windows saw them ("vc_53450030/d"); an
     * extracted disc can spell them otherwise ("vc_53450030/D"), and on
     * Linux/Android those are different names. Match each part's case. */
    if (!pi_exists(out)) {
        char cur[PI_PATH], part[256];
        const char *r = rel;
        snprintf(cur, sizeof cur, "%s", pk->disc);
        while (*r) {
            size_t n = strcspn(r, "/\\");
            DIR *d;
            struct dirent *de;
            int found = 0;
            if (n == 0) { r++; continue; }
            if (n >= sizeof part) return;
            memcpy(part, r, n);
            part[n] = 0;
            r += n;
            if (!(d = opendir(cur))) return;
            while ((de = readdir(d)) != NULL)
                if (!strcasecmp(de->d_name, part)) {
                    size_t l = strlen(cur);
                    snprintf(cur + l, sizeof cur - l, "/%s", de->d_name);
                    found = 1;
                    break;
                }
            closedir(d);
            if (!found) return;          /* leave the plain path: the caller reports it missing */
        }
        snprintf(out, cap, "%s", cur);
    }
#endif
}

static int pfile_index(const Pack *pk, const char *path)
{
    int i;
    for (i = 0; i < pk->nfiles; i++)
        if (!strcmp(pk->files[i].path, path)) return i;
    return -1;
}

/* Every retail file the overlay reads must be the clean USA file the pack was
 * made from (a different disc would give a subtly corrupt game). Results are
 * cached by path, size and time in mods/packs/retail-verified.txt. */
static int verify_retail(Pack *pk)
{
    char cache_path[PI_PATH], line[PI_PATH + 160];
    char (*cached)[PI_PATH + 160] = NULL;
    int ncached = 0, i, ok = 1;
    FILE *cf;
    unsigned char *buf = (unsigned char *)malloc(1 << 22);
    if (!buf) return pi_fail(pk, "out of memory");
    snprintf(cache_path, sizeof cache_path, "%s/mods/packs/retail-verified.txt", pk->root);
    if ((cf = fopen(cache_path, "r")) != NULL) {
        cached = calloc(256, sizeof *cached);
        while (cached && ncached < 256 && fgets(cached[ncached], sizeof cached[0], cf)) {
            cached[ncached][strcspn(cached[ncached], "\r\n")] = 0;
            ncached++;
        }
        fclose(cf);
    }
    for (i = 0; i < pk->nfiles && ok; i++) {
        PFile *f = &pk->files[i];
        char p[PI_PATH], hex[65];
        uint64_t size = 0;
        int64_t mtime = 0;
        int k, hit = 0;
        if (!f->needed) continue;
        disc_path(pk, f->path, p, sizeof p);
        if (!pi_stat(p, &size, &mtime, NULL)) { ok = pi_fail(pk, "retail file %s is missing (%s)", f->path, p); break; }
        if (size != f->before) { ok = pi_fail(pk, "retail file %s is a different size (%llu, the pack needs %llu)", f->path, (unsigned long long)size, (unsigned long long)f->before); break; }
        snprintf(line, sizeof line, "%s %llu %lld %s", f->sha_before, (unsigned long long)size, (long long)mtime, f->path);
        for (k = 0; k < ncached; k++) if (!strcmp(cached[k], line)) hit = 1;
        if (hit) { pi_progress(pk, size, NULL); continue; }
        {
            FILE *rf = fopen(p, "rb");
            PiSha s;
            size_t n;
            if (!rf || !sha_begin(&s)) { if (rf) fclose(rf); ok = pi_fail(pk, "cannot read %s", p); break; }
            snprintf(g_pack_install_stage, sizeof g_pack_install_stage, "Checking %s", f->path);
            while ((n = fread(buf, 1, 1 << 22, rf)) > 0) {
                sha_update(&s, buf, n);
                pi_progress(pk, n, NULL);
            }
            fclose(rf);
            sha_end(&s, hex);
        }
        if (_stricmp(hex, f->sha_before)) { ok = pi_fail(pk, "retail file %s does not match the clean USA retail disc", f->path); break; }
        pi_log("retail %s OK", f->path);
        if ((cf = fopen(cache_path, "a")) != NULL) { fprintf(cf, "%s\n", line); fclose(cf); }
    }
    free(cached);
    free(buf);
    return ok;
}

static int ext_cmp(const void *a, const void *b)
{
    const Ext *x = (const Ext *)a, *y = (const Ext *)b;
    return x->dst < y->dst ? -1 : x->dst > y->dst;
}

static int ext_push(Ext **v, size_t *n, size_t *cap, Ext e)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 4096;
        Ext *nv = (Ext *)realloc(*v, nc * sizeof **v);
        if (!nv) return 0;
        *v = nv;
        *cap = nc;
    }
    (*v)[(*n)++] = e;
    return 1;
}

static int member_index(Pack *pk, const char *name)
{
    return mz_zip_reader_locate_file(&pk->zip, name, NULL, 0);
}

/* A whole (small) member in memory. */
static void *member_heap(Pack *pk, const char *name, size_t *size)
{
    int idx = member_index(pk, name);
    if (idx < 0) return NULL;
    return mz_zip_reader_extract_to_heap(&pk->zip, (mz_uint)idx, size, 0);
}

/* Literal runs: stream-inflate the member, write the bytes to lit, record the
 * extents. Records cross inflate-buffer boundaries, so this is a small state
 * machine: 76 header bytes, then `len` data bytes. */
static int read_literals(Pack *pk, const char *member, FILE *lit, Ext **ext, size_t *n, size_t *cap)
{
    int idx = member_index(pk, member);
    mz_zip_reader_extract_iter_state *it;
    unsigned char *buf, hdr[76];
    size_t hdr_have = 0, got;
    uint64_t left = 0, lit_off = 0;
    int ok = 1;
    if (idx < 0) return pi_fail(pk, "the pack has no member %s", member);
    it = mz_zip_reader_extract_iter_new(&pk->zip, (mz_uint)idx, 0);
    buf = (unsigned char *)malloc(1 << 20);
    if (!it || !buf) { free(buf); if (it) mz_zip_reader_extract_iter_free(it); return pi_fail(pk, "cannot read %s", member); }
    while ((got = mz_zip_reader_extract_iter_read(it, buf, 1 << 20)) > 0 && ok) {
        size_t k = 0;
        pi_progress(pk, got, NULL);
        while (k < got) {
            if (left) {
                size_t take = (size_t)(left < got - k ? left : got - k);
                if (fwrite(buf + k, 1, take, lit) != take) { ok = pi_fail(pk, "writing the pack failed (disk full?)"); break; }
                k += take;
                left -= take;
                continue;
            }
            {
                size_t take = 76 - hdr_have;
                if (take > got - k) take = got - k;
                memcpy(hdr + hdr_have, buf + k, take);
                hdr_have += take;
                k += take;
            }
            if (hdr_have == 76) {
                Ext e;
                uint64_t off;
                uint32_t len;
                memcpy(&off, hdr, 8);
                memcpy(&len, hdr + 8, 4);
                e.dst = off; e.len = len; e.srcf = 0xFFFF; e.src = lit_off;
                if (!ext_push(ext, n, cap, e)) { ok = pi_fail(pk, "out of memory"); break; }
                lit_off += len;
                left = len;
                hdr_have = 0;
            }
        }
    }
    if (!mz_zip_reader_extract_iter_free(it) && ok) ok = pi_fail(pk, "%s is damaged (CRC or inflate error); download the pack again", member);
    if (ok && (left || hdr_have)) ok = pi_fail(pk, "%s ends inside a record", member);
    free(buf);
    return ok;
}

static int read_copies(Pack *pk, const char *member, int srcf, Ext **ext, size_t *n, size_t *cap)
{
    size_t size = 0, k;
    unsigned char *c = (unsigned char *)member_heap(pk, member, &size);
    if (!c) return pi_fail(pk, "cannot read %s (damaged pack?)", member);
    pi_progress(pk, size, NULL);
    for (k = 0; k + 84 <= size; k += 84) {
        Ext e;
        uint32_t len;
        memcpy(&e.dst, c + k, 8);
        memcpy(&e.src, c + k + 8, 8);
        memcpy(&len, c + k + 16, 4);
        e.len = len;
        e.srcf = (uint16_t)srcf;
        if (!ext_push(ext, n, cap, e)) { mz_free(c); return pi_fail(pk, "out of memory"); }
    }
    mz_free(c);
    return 1;
}

static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put64(FILE *f, uint64_t v) { fwrite(&v, 8, 1, f); }

/* The patched default.xbe, built the way the game reads it. */
static unsigned char *rebuild_file(Pack *pk, int fi, const Ext *ext, size_t n, const char *lit_path)
{
    const PFile *f = &pk->files[fi];
    unsigned char *out = (unsigned char *)calloc(1, (size_t)f->after + 1);
    char p[PI_PATH];
    FILE *base, *lit = fopen(lit_path, "rb");
    size_t k;
    if (!out) { if (lit) fclose(lit); return NULL; }
    disc_path(pk, f->path, p, sizeof p);
    if ((base = fopen(p, "rb")) != NULL) {
        size_t want = (size_t)(f->before < f->after ? f->before : f->after);
        if (fread(out, 1, want, base) != want) { fclose(base); free(out); if (lit) fclose(lit); return NULL; }
        fclose(base);
    }
    for (k = 0; k < n; k++) {
        const Ext *e = &ext[k];
        FILE *src;
        uint64_t len = e->len;
        if (e->dst >= f->after) continue;
        if (e->dst + len > f->after) len = f->after - e->dst;
        if (e->srcf == 0xFFFF) {
            src = lit;
        } else {
            disc_path(pk, pk->files[e->srcf].path, p, sizeof p);
            src = fopen(p, "rb");
        }
        if (src) {
            pi_fseek(src, (int64_t)e->src, SEEK_SET);
            if (fread(out + e->dst, 1, (size_t)len, src) != (size_t)len) memset(out + e->dst, 0, (size_t)len);
            if (src != lit) fclose(src);
        }
    }
    if (lit) fclose(lit);
    return out;
}

static void slug(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    int last_us = 1;
    for (; *in && o + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '.' || c == '_' || c == '-') { out[o++] = (char)c; last_us = c == '_'; }
        else if (!last_us) { out[o++] = '_'; last_us = 1; }
    }
    while (o && out[o - 1] == '_') o--;
    out[o] = 0;
    {   /* strip leading '_' like Python's strip("_") */
        size_t s = 0;
        while (out[s] == '_') s++;
        if (s) memmove(out, out + s, strlen(out + s) + 1);
    }
    if (!out[0]) snprintf(out, cap, "pack");
}

/* "v0.7" from "...-v0.7.2k5patch" (tools/pack-install.py release_label). */
static void release_label(const char *path, char *out, size_t cap)
{
    const char *base = path, *p;
    out[0] = 0;
    for (p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    for (p = base; *p; p++) {
        const char *q;
        if ((*p != 'v' && *p != 'V') || (p != base && p[-1] != '-' && p[-1] != '_')) continue;
        const char *best = NULL;
        q = p + 1;
        if (!isdigit((unsigned char)*q)) continue;
        while (isdigit((unsigned char)*q)) q++;
        /* the longest v<n>(.<n>)+ followed by - _ . or the end ("v0.7" in "v0.7.2k5patch") */
        while (*q == '.' && isdigit((unsigned char)q[1])) {
            q++;
            while (isdigit((unsigned char)*q)) q++;
            if (!*q || *q == '-' || *q == '_' || *q == '.') best = q;
        }
        if (!best) continue;
        snprintf(out, cap, "v%.*s", (int)(best - p - 1), p + 1);
        return;
    }
}

static int install(Pack *pk, const char *patch_path, char *folder_out, size_t folder_cap)
{
    size_t mlen = 0;
    char *mtext;
    J *m = NULL, *files, *c;
    JP jp;
    char label[32], version[64], folder[160], name_ver[256], display[256], packs[PI_PATH], final_dir[PI_PATH], p[PI_PATH];
    const char *name;
    uint64_t lit_total = 0, need = 0;
    int i, nchanged = 0, ok = 0, xbe_fi;
    FILE *ov = NULL;
    Ext *xbe_ext = NULL;
    size_t xbe_n = 0;
    char xbe_hash[65] = "";

    if (!mz_zip_reader_init_file(&pk->zip, patch_path, 0))
        return pi_fail(pk, "%s is not a .2k5patch (not a zip, or damaged)", patch_path);
    mtext = (char *)member_heap(pk, "manifest.json", &mlen);
    if (!mtext) { pi_fail(pk, "the pack has no manifest.json"); goto done; }
    jp.p = mtext; jp.end = mtext + mlen; jp.depth = 0; jp.bad = 0;
    m = jp_value(&jp);
    mz_free(mtext);
    if (!m || jp.bad || m->type != J_OBJ) { pi_fail(pk, "manifest.json is not valid JSON"); goto done; }
    if ((int)j_num(m, "format", 0) != 3 || strcmp(j_str(m, "file_contract", ""), "source-copy-v1")) {
        pi_fail(pk, "unsupported pack format %d / %s (needs format 3, source-copy-v1)",
                (int)j_num(m, "format", 0), j_str(m, "file_contract", "?"));
        goto done;
    }
    files = j_get(m, "files");
    if (!files || files->type != J_ARR) { pi_fail(pk, "manifest.json lists no files"); goto done; }
    for (c = files->child; c; c = c->next) pk->nfiles++;
    if (!pk->nfiles || pk->nfiles > 4096) { pi_fail(pk, "manifest.json lists %d files", pk->nfiles); goto done; }
    pk->files = (PFile *)calloc((size_t)pk->nfiles, sizeof *pk->files);
    for (i = 0, c = files->child; c; c = c->next, i++) {
        PFile *f = &pk->files[i];
        J *b = j_get(c, "before"), *a = j_get(c, "after");
        f->node = c;
        f->path = j_str(c, "path", "");
        f->before = (uint64_t)j_num(b, "size", 0);
        f->after = (uint64_t)j_num(a, "size", 0);
        f->sha_before = j_str(b, "sha256", "");
        f->sha_after = j_str(a, "sha256", "");
        if (!*f->path || strstr(f->path, "..") || !b || !a) { pi_fail(pk, "manifest.json has a bad file entry"); goto done; }
        if (!strcmp(j_str(c, "mode", ""), "copy")) {
            if (strcmp(f->sha_before, f->sha_after)) { pi_fail(pk, "%s: a changed file stored whole is not supported yet", f->path); goto done; }
        } else {
            f->changed = 1;
            f->needed = 1;
            nchanged++;
            lit_total += (uint64_t)j_num(c, "literal_bytes", 0);
        }
    }
    for (i = 0; i < pk->nfiles; i++) {
        J *cc = j_get(pk->files[i].node, "cross_copies");
        if (!pk->files[i].changed) continue;
        for (c = cc ? cc->child : NULL; c; c = c->next) {
            int s = pfile_index(pk, j_str(c, "source_path", ""));
            if (s < 0) { pi_fail(pk, "%s copies from %s, which the pack does not list", pk->files[i].path, j_str(c, "source_path", "?")); goto done; }
            pk->files[s].needed = 1;
        }
    }

    name = j_str(m, "name", "Mod Pack");
    release_label(patch_path, label, sizeof label);
    snprintf(version, sizeof version, "%s", label[0] ? label : j_str(m, "version", ""));
    /* folder: the same name tools/pack-install.py gives (an existing install is
     * replaced, and mods/active-pack.ini keeps pointing at it) */
    snprintf(name_ver, sizeof name_ver, "%s-%s", name, version);
    slug(name_ver, folder, sizeof folder);
    {   /* display name: "SOFTDRINK 2K28 v0.7", not "... v0.7 v0.7" */
        size_t nl = strlen(name), vl = strlen(label);
        if (label[0] && !(nl >= vl && !_stricmp(name + nl - vl, label)))
            snprintf(display, sizeof display, "%s %s", name, label);
        else
            snprintf(display, sizeof display, "%s", name);
    }
    pi_log("%s (%s) by %s: %d files, %d changed, %.1f MB of new data",
           display, folder, j_str(m, "author", "?"), pk->nfiles, nchanged, lit_total / 1e6);

    snprintf(packs, sizeof packs, "%s/mods", pk->root);
    pi_mkdir(packs);
    snprintf(packs, sizeof packs, "%s/mods/packs", pk->root);
    pi_mkdir(packs);
    need = lit_total + lit_total / 8 + (64u << 20);
    if (pi_free_bytes(packs) < need) {
        pi_fail(pk, "not enough free space: the pack needs %.1f GB, %.1f GB free",
                need / 1e9, pi_free_bytes(packs) / 1e9);
        goto done;
    }

    /* work: retail check + member bytes */
    for (i = 0; i < pk->nfiles; i++) if (pk->files[i].needed) pk->work_total += pk->files[i].before;
    pk->work_total += lit_total;
    if (!verify_retail(pk)) goto done;

    snprintf(pk->stage, sizeof pk->stage, "%s/%s.installing", packs, folder);
    if (pi_exists(pk->stage)) pi_remove_pack_dir(pk->stage);
    pi_mkdir(pk->stage);
    snprintf(p, sizeof p, "%s/lit", pk->stage);
    if (!pi_mkdir(p)) { pi_fail(pk, "cannot create %s", p); goto done; }

    snprintf(p, sizeof p, "%s/overlay.bin", pk->stage);
    if (!(ov = fopen(p, "wb"))) { pi_fail(pk, "cannot create %s", p); goto done; }
    fwrite("2K5OVL1\0", 1, 8, ov);
    put32(ov, (uint32_t)pk->nfiles);
    put32(ov, (uint32_t)nchanged);
    for (i = 0; i < pk->nfiles; i++) {
        const PFile *f = &pk->files[i];
        uint16_t nl = (uint16_t)strlen(f->path);
        put16(ov, nl);
        fwrite(f->path, 1, nl, ov);
        put64(ov, f->before);
        put64(ov, f->after);
    }

    xbe_fi = pfile_index(pk, "default.xbe");
    for (i = 0; i < pk->nfiles; i++) {
        PFile *f = &pk->files[i];
        Ext *ext = NULL;
        size_t n = 0, cap = 0, k;
        char lit_path[PI_PATH];
        FILE *lit;
        J *pay, *cp, *cc;
        if (!f->changed) continue;
        snprintf(g_pack_install_stage, sizeof g_pack_install_stage, "Unpacking %s", f->path);
        snprintf(lit_path, sizeof lit_path, "%s/lit/%d.bin", pk->stage, i);
        if (!(lit = fopen(lit_path, "wb"))) { pi_fail(pk, "cannot create %s", lit_path); goto done; }
        pay = j_get(f->node, "payload");
        if (pay && !read_literals(pk, j_str(pay, "member", ""), lit, &ext, &n, &cap)) { fclose(lit); free(ext); goto done; }
        if (fclose(lit) != 0) { free(ext); pi_fail(pk, "writing %s failed (disk full?)", lit_path); goto done; }
        if ((cp = j_get(f->node, "copies")) != NULL && !read_copies(pk, j_str(cp, "member", ""), i, &ext, &n, &cap)) { free(ext); goto done; }
        cc = j_get(f->node, "cross_copies");
        for (c = cc ? cc->child : NULL; c; c = c->next)
            if (!read_copies(pk, j_str(c, "member", ""), pfile_index(pk, j_str(c, "source_path", "")), &ext, &n, &cap)) { free(ext); goto done; }
        qsort(ext, n, sizeof *ext, ext_cmp);
        for (k = 1; k < n; k++)
            if (ext[k - 1].dst + ext[k - 1].len > ext[k].dst) { free(ext); pi_fail(pk, "%s: overlapping runs at %llX", f->path, (unsigned long long)ext[k].dst); goto done; }
        put32(ov, (uint32_t)i);
        put32(ov, (uint32_t)n);
        for (k = 0; k < n; k++) {
            uint8_t kind = ext[k].srcf == 0xFFFF ? 0 : 1;
            put64(ov, ext[k].dst);
            put32(ov, ext[k].len);
            fwrite(&kind, 1, 1, ov);
            put16(ov, ext[k].srcf);
            put64(ov, ext[k].src);
        }
        pi_log("  %-16s %8.1f MB -> %8.1f MB, %7zu extents", f->path, f->before / 1e6, f->after / 1e6, n);
        if (i == xbe_fi) { xbe_ext = ext; xbe_n = n; }
        else free(ext);
    }
    if (fclose(ov) != 0) { ov = NULL; pi_fail(pk, "writing overlay.bin failed (disk full?)"); goto done; }
    ov = NULL;

    if (xbe_fi >= 0) {
        const PFile *f = &pk->files[xbe_fi];
        unsigned char *xbe;
        char lit_path[PI_PATH];
        PiSha s;
        FILE *xo;
        snprintf(lit_path, sizeof lit_path, "%s/lit/%d.bin", pk->stage, xbe_fi);
        if (f->changed) xbe = rebuild_file(pk, xbe_fi, xbe_ext, xbe_n, lit_path);
        else xbe = rebuild_file(pk, xbe_fi, NULL, 0, lit_path);
        if (!xbe) { pi_fail(pk, "could not build default.xbe"); goto done; }
        sha_begin(&s);
        sha_update(&s, xbe, (size_t)f->after);
        sha_end(&s, xbe_hash);
        if (_stricmp(xbe_hash, f->sha_after)) { free(xbe); pi_fail(pk, "default.xbe did not verify (got %s)", xbe_hash); goto done; }
        snprintf(p, sizeof p, "%s/default.xbe", pk->stage);
        xo = fopen(p, "wb");
        if (!xo || fwrite(xbe, 1, (size_t)f->after, xo) != (size_t)f->after) { if (xo) fclose(xo); free(xbe); pi_fail(pk, "writing default.xbe failed"); goto done; }
        fclose(xo);
        free(xbe);
        pi_log("default.xbe OK %s", xbe_hash);
    } else {
        pi_fail(pk, "the pack has no default.xbe; it cannot run as a mod pack");
        goto done;
    }

    snprintf(p, sizeof p, "%s/pack.json", pk->stage);
    {
        FILE *pj = fopen(p, "w");
        const char *base = patch_path, *q;
        for (q = patch_path; *q; q++) if (*q == '/' || *q == '\\') base = q + 1;
        if (!pj) { pi_fail(pk, "cannot write pack.json"); goto done; }
        fputs("{\n \"name\": ", pj); json_put_str(pj, name);
        fputs(",\n \"display_name\": ", pj); json_put_str(pj, display);
        fputs(",\n \"version\": ", pj); json_put_str(pj, version);
        fputs(",\n \"manifest_version\": ", pj); json_put_str(pj, j_str(m, "version", ""));
        fputs(",\n \"author\": ", pj); json_put_str(pj, j_str(m, "author", ""));
        fputs(",\n \"description\": ", pj); json_put_str(pj, j_str(m, "description", ""));
        fputs(",\n \"source\": ", pj); json_put_str(pj, base);
        fputs(",\n \"installer\": \"built-in\"", pj);
        fputs(",\n \"xbe_sha256\": ", pj); json_put_str(pj, xbe_hash);
        fputs(",\n \"files\": [", pj);
        for (i = 0; i < pk->nfiles; i++) {
            const PFile *f = &pk->files[i];
            fprintf(pj, "%s\n  {\"path\": ", i ? "," : "");
            json_put_str(pj, f->path);
            fprintf(pj, ", \"before\": {\"size\": %llu, \"sha256\": ", (unsigned long long)f->before);
            json_put_str(pj, f->sha_before);
            fprintf(pj, "}, \"after\": {\"size\": %llu, \"sha256\": ", (unsigned long long)f->after);
            json_put_str(pj, f->sha_after);
            fputs("}}", pj);
        }
        fputs("\n ]\n}\n", pj);
        if (fclose(pj) != 0) { pi_fail(pk, "writing pack.json failed"); goto done; }
    }

    /* into place: the old install (if any) moves aside, the new one in */
    snprintf(final_dir, sizeof final_dir, "%s/%s", packs, folder);
    if (pi_exists(final_dir)) {
        char prev[PI_PATH];
        snprintf(prev, sizeof prev, "%s.previous", final_dir);
        if (pi_exists(prev)) pi_remove_pack_dir(prev);
        if (!pi_rename(final_dir, prev)) { pi_fail(pk, "cannot replace %s (is the game using it?)", final_dir); goto done; }
        if (!pi_rename(pk->stage, final_dir)) { pi_rename(prev, final_dir); pi_fail(pk, "cannot move the pack into place"); goto done; }
        if (!pi_remove_pack_dir(prev)) pi_log("note: %s kept (it holds files this installer did not make)", prev);
    } else if (!pi_rename(pk->stage, final_dir)) {
        pi_fail(pk, "cannot move the pack into place");
        goto done;
    }
    pk->stage[0] = 0;
    snprintf(folder_out, folder_cap, "%s", folder);
    pi_log("installed to %s", final_dir);
    ok = 1;
done:
    if (ov) fclose(ov);
    free(xbe_ext);
    if (pk->stage[0] && pi_exists(pk->stage)) pi_remove_pack_dir(pk->stage);
    j_free(m);
    free(pk->files);
    mz_zip_reader_end(&pk->zip);
    return ok;
}

/* Install `patch` (a .2k5patch) into <root>/mods/packs. disc: the clean
 * retail disc folder (NULL: <root>/original/disc, else <root>/original).
 * Returns 1 on success with the pack folder name in folder_out; on failure 0
 * and a sentence for the player in err. log_path: NULL or a transcript file. */
int nfl2k5_pack_install(const char *root, const char *disc, const char *patch,
                        const char *log_path, char *folder_out, size_t folder_cap,
                        char *err, size_t err_cap)
{
    Pack *pk = (Pack *)calloc(1, sizeof *pk);
    int ok;
    if (!pk) { snprintf(err, err_cap, "out of memory"); return 0; }
    snprintf(pk->root, sizeof pk->root, "%s", root);
    if (disc && *disc) {
        snprintf(pk->disc, sizeof pk->disc, "%s", disc);
    } else {
        char x[PI_PATH];
        snprintf(x, sizeof x, "%s/original/disc/default.xbe", root);
        snprintf(pk->disc, sizeof pk->disc, pi_exists(x) ? "%s/original/disc" : "%s/original", root);
    }
    s_log = log_path && *log_path ? fopen(log_path, "w") : NULL;
    g_pack_install_percent = 0;
    snprintf(g_pack_install_stage, sizeof g_pack_install_stage, "Starting");
    pi_log("installing %s (retail disc %s)", patch, pk->disc);
    {
        time_t t0 = time(NULL);
        ok = install(pk, patch, folder_out, folder_cap);
        pi_log("%s in %d s", ok ? "done" : "failed", (int)(time(NULL) - t0));
    }
    if (!ok) snprintf(err, err_cap, "%s", pk->err[0] ? pk->err : "unknown error");
    g_pack_install_percent = ok ? 100 : 0;
    if (s_log) { fclose(s_log); s_log = NULL; }
    free(pk);
    return ok;
}
