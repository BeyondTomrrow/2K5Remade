/* xbpack: pack the title's data folder (vc_53450030) into one .xbpak.
 *
 *   xbpack <game folder> <out.xbpak> [--drop movies,crib] [--level N]
 *          [--block KB] [--sub KB] [--threads N] [--all] [--keep-for <mod pack>]
 *   xbpack --verify <pak> <game folder>
 *   xbpack --info <pak>
 *
 * <game folder> holds vc_53450030 (normally <root>\original). The original
 * files are only read. The pack is written to <out>.tmp, decompressed in full
 * and compared with the originals (every byte outside a dropped range must be
 * identical, dropped ranges must hold exactly the stub/zeros the pack says);
 * only then is it renamed to <out>. Format: external/xboxrecomp/src/kernel/
 * xbpak_format.h.
 *
 * --drop movies: every archive entry that is an MPEG program stream (the boot
 *   intro, the Crib TV videos, three more) keeps its first ~64 KB up to a pack
 *   boundary, then an MPEG program end code, then zeros.
 * --drop crib: the Crib's own assets (crib*.iff except the trophy case) are
 *   zeroed. Only ~13 MB; the Crib's videos already go with "movies".
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#define XXH_STATIC_LINKING_ONLY
#include "common/xxhash.h"
#include "xbpak_format.h"

typedef struct { uint64_t off, len; uint32_t kind; } Drop;
/* kind | DROP_CONT: the rest of a dropped range that was cut (at a bank
 * boundary, or around bytes a mod pack's overlay reads); no end code there. */
#define DROP_CONT 0x80000000u

typedef struct {
    char name[64];                  /* "vc_53450030/3" */
    wchar_t path[MAX_PATH];
    uint64_t size;
    Drop *drops;
    uint32_t ndrops, cap;
    uint32_t first_block, nblocks;
    uint64_t hash;
} PFile;

static PFile s_files[64];
static uint32_t s_nfiles;
static uint32_t s_block = 256 * 1024;
static int s_level = 19;
static int s_threads;

static void die(const char *m) { fprintf(stderr, "xbpack: %s\n", m); exit(1); }

static void add_drop(PFile *f, uint64_t off, uint64_t len, uint32_t kind)
{
    if (!len) return;
    if (f->ndrops == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 16;
        f->drops = (Drop *)realloc(f->drops, f->cap * sizeof(Drop));
    }
    f->drops[f->ndrops].off = off; f->drops[f->ndrops].len = len; f->drops[f->ndrops].kind = kind;
    f->ndrops++;
}

static int cmp_drop(const void *a, const void *b)
{
    const Drop *x = (const Drop *)a, *y = (const Drop *)b;
    return x->off < y->off ? -1 : x->off > y->off;
}

static int pread_h(HANDLE h, void *buf, uint32_t n, uint64_t off)
{
    uint8_t *p = (uint8_t *)buf;
    while (n) {
        OVERLAPPED ov; DWORD got = 0;
        memset(&ov, 0, sizeof ov);
        ov.Offset = (DWORD)off; ov.OffsetHigh = (DWORD)(off >> 32);
        if (!ReadFile(h, p, n, &got, &ov) || !got) return 0;
        p += got; n -= got; off += got;
    }
    return 1;
}

static HANDLE open_r(const wchar_t *p)
{
    return CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}

/* The archive's name hash: CRC-32 of the upper-cased name as UTF-16LE. */
static uint32_t name_crc(const char *s)
{
    uint32_t c = 0xFFFFFFFFu;
    for (; *s; s++) {
        uint8_t b[2] = { (uint8_t)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s), 0 };
        for (int k = 0; k < 2; k++) {
            c ^= b[k];
            for (int i = 0; i < 8; i++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

static const char *const k_crib_assets[] = {
    "crib_loading.iff", "crib.iff", "cribah.iff", "cribpf.iff", "crib_16k.iff", "crib_32k.iff",
    "crib_64k.iff", "crib_av.iff", "crib_bar_light.iff", "crib_bar_stool.iff", "crib_bench.iff",
    "crib_bobblehead.iff", "crib_ceiling_fan.iff", "crib_coffee_table.iff", "crib_console_table.iff",
    "crib_couch.iff", "crib_credenza.iff", "crib_darts.iff", "crib_end_table.iff", "crib_intro.iff",
    "crib_jukebox.iff", "crib_recliner.iff", "crib_sofa_chair.iff", "crib_theater_seating.iff",
    "crib_triviagame.iff", "crib_wall_clock.iff",   /* crib_trophy.iff stays: the trophy case */
};

/* Mark what --drop removes. The archive spans the banks 0..F as one byte
 * range; its index is at the start of bank 0. */
static void plan_drops(uint32_t flags, uint64_t *movie_bytes, uint64_t *crib_bytes)
{
    PFile *bank[16];
    uint64_t start[17];
    HANDLE h0;
    uint32_t count, i, k;
    uint8_t head[0x9C];
    uint8_t *table;
    *movie_bytes = *crib_bytes = 0;
    if (!flags) return;
    for (i = 0; i < 16; i++) {
        char want[32];
        snprintf(want, sizeof want, "vc_53450030/%X", i);
        bank[i] = NULL;
        for (k = 0; k < s_nfiles; k++) if (!_stricmp(s_files[k].name, want)) bank[i] = &s_files[k];
        if (!bank[i]) die("a data bank (0..F) is missing; cannot drop entries");
    }
    start[0] = 0;
    for (i = 0; i < 16; i++) start[i + 1] = start[i] + bank[i]->size;
    h0 = open_r(bank[0]->path);
    if (h0 == INVALID_HANDLE_VALUE || !pread_h(h0, head, sizeof head, 0)) die("cannot read the archive index");
    count = *(uint32_t *)head;
    table = (uint8_t *)malloc((size_t)count * 12);
    if (!pread_h(h0, table, count * 12, sizeof head)) die("cannot read the archive index");
    CloseHandle(h0);
    for (i = 0; i < count; i++) {
        uint32_t hash = *(uint32_t *)(table + 12 * i);
        uint64_t size = *(uint32_t *)(table + 12 * i + 4);   /* all 32 bits: the speech pool is 1.4 GB */
        uint64_t off = (uint64_t)*(uint32_t *)(table + 12 * i + 8) * 2048;
        uint64_t end = off + size;
        int b = 0, is_crib = 0;
        uint8_t magic[4];
        while (b < 15 && off >= start[b + 1]) b++;
        if (end > start[16]) continue;
        {
            HANDLE h = open_r(bank[b]->path);
            int ok = h != INVALID_HANDLE_VALUE && pread_h(h, magic, 4, off - start[b]);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (!ok) continue;
        }
        for (k = 0; k < sizeof k_crib_assets / sizeof *k_crib_assets; k++)
            if (hash == name_crc(k_crib_assets[k])) is_crib = 1;
        if ((flags & XBPAK_DROP_MOVIES) && !memcmp(magic, "\0\0\x01\xBA", 4) && size > 256 * 1024) {
            /* Keep the start up to the first pack header at or after 64 KB,
             * where an end code goes; the rest becomes zeros. */
            uint64_t cut = 0, p;
            uint8_t buf[65536 + 4];
            for (p = off + 65536; p + 4 <= end && !cut; p += 65536) {
                int bb = 0;
                uint32_t n = (uint32_t)((end - p) < 65536 + 4 ? (end - p) : 65536 + 4), j;
                HANDLE h;
                while (bb < 15 && p >= start[bb + 1]) bb++;
                if (p + n > start[bb + 1]) n = (uint32_t)(start[bb + 1] - p);
                h = open_r(bank[bb]->path);
                if (h == INVALID_HANDLE_VALUE || !pread_h(h, buf, n, p - start[bb])) die("read failed");
                CloseHandle(h);
                for (j = 0; j + 4 <= n; j++)
                    if (buf[j] == 0 && buf[j + 1] == 0 && buf[j + 2] == 1 && buf[j + 3] == 0xBA) { cut = p + j; break; }
            }
            if (!cut) continue;
            /* The dropped range starts at the end code; it may span banks. */
            for (p = cut; p < end; ) {
                int bb = 0;
                uint64_t stop;
                while (bb < 15 && p >= start[bb + 1]) bb++;
                stop = end < start[bb + 1] ? end : start[bb + 1];
                add_drop(bank[bb], p - start[bb], stop - p, XBPAK_DROP_MOVIES | (p == cut ? 0 : DROP_CONT));
                p = stop;
            }
            *movie_bytes += end - cut;
        } else if ((flags & XBPAK_DROP_CRIB) && is_crib) {
            uint64_t p;
            for (p = off; p < end; ) {
                int bb = 0;
                uint64_t stop;
                while (bb < 15 && p >= start[bb + 1]) bb++;
                stop = end < start[bb + 1] ? end : start[bb + 1];
                add_drop(bank[bb], p - start[bb], stop - p, XBPAK_DROP_CRIB);
                p = stop;
            }
            *crib_bytes += size;
        }
    }
    free(table);
    for (i = 0; i < s_nfiles; i++)
        if (s_files[i].ndrops) qsort(s_files[i].drops, s_files[i].ndrops, sizeof(Drop), cmp_drop);
}

/* Read [off, off+n) of a file's packed content: the original bytes, with each
 * dropped range replaced (a movie's range starts with 00 00 01 B9, the
 * program end code; everything else is zeros). */
static void logical_read(const PFile *f, HANDLE h, uint8_t *buf, uint32_t n, uint64_t off)
{
    uint32_t i;
    if (!pread_h(h, buf, n, off)) die("read failed");
    for (i = 0; i < f->ndrops; i++) {
        const Drop *d = &f->drops[i];
        uint64_t a = d->off > off ? d->off : off;
        uint64_t b = d->off + d->len < off + n ? d->off + d->len : off + n;
        uint64_t p;
        if (a >= b) continue;
        memset(buf + (a - off), 0, (size_t)(b - a));
        if (d->kind == XBPAK_DROP_MOVIES)          /* not a continuation: the end code */
            for (p = d->off; p < d->off + 4 && p < d->off + d->len; p++)
                if (p >= off && p < off + n) buf[p - off] = (uint8_t)"\0\0\x01\xB9"[p - d->off];
    }
}

/* ---- compressing regions on all cores ----
 * A region is s_block bytes. Compressed whole when that saves at least a
 * fifth (the data repeats at MB distances, so a big window pays); otherwise
 * it is cut into s_sub blocks, each stored zstd, raw or as zeros, so audio
 * and other dense data stay cheap to read at random. */
typedef struct {
    uint8_t *out;                   /* the region's stored bytes, back to back */
    uint32_t outlen;
    uint32_t nb;                    /* blocks it became */
    XbpakBlock b[256];              /* offset = position within out; loff relative to the region */
} RegionOut;

typedef struct {
    uint8_t *in;
    uint32_t nreg, lastlen;
    RegionOut *ro;
    volatile LONG next;
} Job;

static uint32_t s_sub = 256 * 1024;

static int all_zero(const uint8_t *p, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) if (p[k]) return 0;
    return 1;
}

static DWORD WINAPI worker(LPVOID arg)
{
    Job *j = (Job *)arg;
    ZSTD_CCtx *big = ZSTD_createCCtx(), *small = ZSTD_createCCtx();
    size_t cap = ZSTD_compressBound(s_block);
    uint8_t *tmp = (uint8_t *)malloc(cap);
    int wlog = 10;
    while ((1u << wlog) < s_block && wlog < 27) wlog++;
    ZSTD_CCtx_setParameter(big, ZSTD_c_compressionLevel, s_level);
    ZSTD_CCtx_setParameter(big, ZSTD_c_windowLog, wlog);
    ZSTD_CCtx_setParameter(small, ZSTD_c_compressionLevel, s_level);
    for (;;) {
        LONG i = InterlockedIncrement(&j->next) - 1;
        RegionOut *r;
        uint8_t *src;
        uint32_t len;
        size_t c;
        if (i >= (LONG)j->nreg) break;
        r = &j->ro[i];
        src = j->in + (size_t)i * s_block;
        len = (uint32_t)i == j->nreg - 1 ? j->lastlen : s_block;
        if (!r->out) r->out = (uint8_t *)malloc(cap + 64 * 1024);
        r->outlen = 0; r->nb = 0;
        memset(r->b, 0, sizeof r->b);
        if (all_zero(src, len)) {
            r->b[0].method = XBPAK_BLOCK_ZERO; r->b[0].ulen = len; r->nb = 1;
            continue;
        }
        c = ZSTD_compress2(big, tmp, cap, src, len);
        if (!ZSTD_isError(c) && c * 5 <= (size_t)len * 4) {
            memcpy(r->out, tmp, c);
            r->b[0].method = XBPAK_BLOCK_ZSTD; r->b[0].csize = (uint32_t)c; r->b[0].ulen = len;
            r->outlen = (uint32_t)c; r->nb = 1;
            continue;
        }
        {
            uint32_t o;
            for (o = 0; o < len; o += s_sub) {
                uint32_t n = len - o < s_sub ? len - o : s_sub;
                XbpakBlock *b = &r->b[r->nb++];
                b->loff = o; b->ulen = n; b->offset = r->outlen;
                if (all_zero(src + o, n)) { b->method = XBPAK_BLOCK_ZERO; continue; }
                c = ZSTD_compressCCtx(small, r->out + r->outlen, cap + 64 * 1024 - r->outlen, src + o, n, s_level);
                if (ZSTD_isError(c) || c >= n) {
                    memcpy(r->out + r->outlen, src + o, n);
                    b->method = XBPAK_BLOCK_RAW; b->csize = n;
                } else {
                    b->method = XBPAK_BLOCK_ZSTD; b->csize = (uint32_t)c;
                }
                r->outlen += b->csize;
            }
        }
    }
    free(tmp);
    ZSTD_freeCCtx(big);
    ZSTD_freeCCtx(small);
    return 0;
}

static void wr(HANDLE h, const void *p, uint32_t n)
{
    DWORD put;
    if (!WriteFile(h, p, n, &put, NULL) || put != n) die("write failed (disk full?)");
}

/* The banks 0..F only: pack G (and G.list) is the custom-teams pack the game
 * rewrites at boot from mods\teams; --all takes every file. */
static void scan_folder(const wchar_t *game, int all)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE f;
    _snwprintf(pat, MAX_PATH, L"%s\\vc_53450030\\*", game);
    f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) die("no vc_53450030 folder in the game folder");
    do {
        PFile *p;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!all && !(fd.cFileName[1] == 0 && iswxdigit(fd.cFileName[0]))) continue;
        if (s_nfiles == 64) die("too many files");
        p = &s_files[s_nfiles++];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "vc_53450030/%ls", fd.cFileName);
        _snwprintf(p->path, MAX_PATH, L"%s\\vc_53450030\\%s", game, fd.cFileName);
        p->size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static int verify(const wchar_t *pak, const wchar_t *game);

/* --keep-for <mod pack folder or overlay.bin>: a mod pack's overlay
 * (kernel_overlay.c) builds its files partly from copies of retail bytes.
 * Its big copies (up to 1 MB each) are retail movies it moves elsewhere;
 * those stay dropped and the moved movie keeps the stub's end code. Its
 * small copies are byte runs the patch converter found anywhere in the
 * retail files, some inside movies: those bytes are kept, or the mod would
 * read zeros (SOFTDRINK v0.7 crashed at kickoff, 2026-11-01). */
#define KEEP_MAX_COPY 65536u
static const wchar_t *s_keep[16];
static int s_nkeep;
static uint32_t s_keep_clamped;

static void keep_range(PFile *f, uint64_t a, uint64_t b)
{
    uint32_t i, n0 = f->ndrops;
    for (i = 0; i < n0; i++) {
        Drop d = f->drops[i];
        uint64_t s = a > d.off ? a : d.off, e = b < d.off + d.len ? b : d.off + d.len;
        /* a movie's end code stays: it is what ends the stub */
        if (d.kind == XBPAK_DROP_MOVIES && s < d.off + 4) {
            s_keep_clamped++;
            s = d.off + 4;
        }
        if (s >= e) continue;
        /* cut [s, e) out of drop i: keep the head in place, append the tail */
        f->drops[i].len = s - d.off;
        if (d.off + d.len > e) add_drop(f, e, d.off + d.len - e, d.kind | DROP_CONT);
    }
    /* drop the now-empty heads */
    {
        uint32_t w = 0;
        for (i = 0; i < f->ndrops; i++) if (f->drops[i].len) f->drops[w++] = f->drops[i];
        f->ndrops = w;
    }
}

static void keep_for_overlay(const wchar_t *arg)
{
    wchar_t path[MAX_PATH];
    FILE *fp;
    char magic[8];
    uint32_t nfiles, nchanged, i, k;
    PFile **map;
    uint64_t kept = 0, nkept = 0;
    DWORD a = GetFileAttributesW(arg);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) _snwprintf(path, MAX_PATH, L"%s\\overlay.bin", arg);
    else _snwprintf(path, MAX_PATH, L"%s", arg);
    fp = _wfopen(path, L"rb");
    if (!fp || fread(magic, 1, 8, fp) != 8 || memcmp(magic, "2K5OVL1\0", 8) || fread(&nfiles, 4, 1, fp) != 1
            || fread(&nchanged, 4, 1, fp) != 1 || nfiles > 4096)
        die("--keep-for: not a mod pack overlay (overlay.bin)");
    map = (PFile **)calloc(nfiles, sizeof *map);
    for (i = 0; i < nfiles; i++) {
        uint16_t n;
        char name[200];
        uint64_t sizes[2];
        if (fread(&n, 2, 1, fp) != 1 || n >= sizeof name || fread(name, 1, n, fp) != n || fread(sizes, 8, 2, fp) != 2)
            die("--keep-for: overlay index truncated");
        name[n] = 0;
        for (k = 0; k < s_nfiles; k++) if (!_stricmp(s_files[k].name, name)) map[i] = &s_files[k];
    }
    for (i = 0; i < nchanged; i++) {
        uint32_t idx, n;
        if (fread(&idx, 4, 1, fp) != 1 || fread(&n, 4, 1, fp) != 1) die("--keep-for: overlay truncated");
        for (k = 0; k < n; k++) {
            uint8_t e[23];
            uint32_t len;
            uint16_t srcf;
            uint64_t src;
            PFile *f;
            uint32_t j;
            if (fread(e, 1, 23, fp) != 23) die("--keep-for: overlay truncated");
            if (e[12] != 1) continue;                                   /* literal */
            memcpy(&len, e + 8, 4); memcpy(&srcf, e + 13, 2); memcpy(&src, e + 15, 8);
            if (len > KEEP_MAX_COPY || srcf >= nfiles || !(f = map[srcf]) || !f->ndrops) continue;
            for (j = 0; j < f->ndrops; j++)
                if (src < f->drops[j].off + f->drops[j].len && src + len > f->drops[j].off) {
                    keep_range(f, src, src + len);
                    kept += len; nkept++;
                    break;
                }
        }
    }
    fclose(fp);
    free(map);
    for (i = 0; i < s_nfiles; i++)
        if (s_files[i].ndrops) qsort(s_files[i].drops, s_files[i].ndrops, sizeof(Drop), cmp_drop);
    printf("%ls: kept %llu small runs (%.1f KB) the mod reads from dropped data%s\n", path,
           (unsigned long long)nkept, kept / 1024.0,
           s_keep_clamped ? " (some overlap a movie's end code, which stays)" : "");
}

static int build(const wchar_t *game, const wchar_t *out, uint32_t flags, int all)
{
    wchar_t tmp[MAX_PATH];
    HANDLE o, th[64];
    XbpakHeader hdr;
    XbpakBlock *blocks = NULL;
    uint32_t nblocks = 0, bcap = 0, i, run, max_ulen = 0;
    uint64_t pos, movie_bytes, crib_bytes, raw_total = 0;
    uint8_t *in;
    RegionOut *ro;
    DWORD t0 = GetTickCount();

    if (s_block / s_sub > 256) die("--block / --sub must be at most 256");
    run = (uint32_t)s_threads * 2;
    in = (uint8_t *)malloc((size_t)run * s_block);
    ro = (RegionOut *)calloc(run, sizeof *ro);
    if (!in || !ro) die("out of memory (try a smaller --block or fewer --threads)");
    scan_folder(game, all);
    plan_drops(flags, &movie_bytes, &crib_bytes);
    for (i = 0; i < (uint32_t)s_nkeep; i++) keep_for_overlay(s_keep[i]);
    for (i = 0; i < s_nfiles; i++) raw_total += s_files[i].size;
    printf("%u files, %.2f GB; dropping %.1f MB of movies, %.1f MB of Crib assets\n", s_nfiles,
           raw_total / 1073741824.0, movie_bytes / 1048576.0, crib_bytes / 1048576.0);

    _snwprintf(tmp, MAX_PATH, L"%s.tmp", out);
    o = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (o == INVALID_HANDLE_VALUE) die("cannot create the output file");
    memset(&hdr, 0, sizeof hdr);
    wr(o, &hdr, sizeof hdr);
    pos = sizeof hdr;

    for (i = 0; i < s_nfiles; i++) {
        PFile *f = &s_files[i];
        HANDLE h = open_r(f->path);
        XXH64_state_t *xs = XXH64_createState();
        uint64_t off = 0;
        if (h == INVALID_HANDLE_VALUE) die("cannot open a data file");
        XXH64_reset(xs, 0);
        f->first_block = nblocks;
        while (off < f->size) {
            Job j;
            uint64_t len = f->size - off < (uint64_t)run * s_block ? f->size - off : (uint64_t)run * s_block;
            uint32_t n = (uint32_t)((len + s_block - 1) / s_block), k, q, nt;
            logical_read(f, h, in, (uint32_t)len, off);
            XXH64_update(xs, in, (size_t)len);
            memset(&j, 0, sizeof j);
            j.in = in; j.nreg = n; j.ro = ro;
            j.lastlen = (uint32_t)(len - (uint64_t)(n - 1) * s_block);
            nt = (uint32_t)s_threads < n ? (uint32_t)s_threads : n;
            for (k = 0; k < nt; k++) th[k] = CreateThread(NULL, 0, worker, &j, 0, NULL);
            WaitForMultipleObjects(nt, th, TRUE, INFINITE);
            for (k = 0; k < nt; k++) CloseHandle(th[k]);
            for (k = 0; k < n; k++) {
                RegionOut *r = &ro[k];
                uint64_t rbase = off + (uint64_t)k * s_block;
                if (r->outlen) wr(o, r->out, r->outlen);
                for (q = 0; q < r->nb; q++) {
                    XbpakBlock b = r->b[q];
                    if (nblocks == bcap) {
                        bcap = bcap ? bcap * 2 : 4096;
                        blocks = (XbpakBlock *)realloc(blocks, bcap * sizeof *blocks);
                    }
                    b.loff += rbase;
                    b.offset = b.method == XBPAK_BLOCK_ZERO ? 0 : pos + b.offset;
                    if (b.ulen > max_ulen) max_ulen = b.ulen;
                    blocks[nblocks++] = b;
                }
                pos += r->outlen;
            }
            off += len;
            printf("\r  %-16s %5.1f%%  pack so far %.2f GB  ", f->name, 100.0 * off / f->size, pos / 1073741824.0);
            fflush(stdout);
        }
        f->nblocks = nblocks - f->first_block;
        f->hash = XXH64_digest(xs);
        XXH64_freeState(xs);
        CloseHandle(h);
        printf("\n");
    }

    hdr.index_off = pos;
    for (i = 0; i < s_nfiles; i++) {
        PFile *f = &s_files[i];
        uint16_t nl = (uint16_t)strlen(f->name);
        uint32_t k;
        wr(o, &nl, 2); wr(o, f->name, nl);
        wr(o, &f->size, 8); wr(o, &f->hash, 8);
        wr(o, &f->first_block, 4); wr(o, &f->nblocks, 4); wr(o, &f->ndrops, 4);
        for (k = 0; k < f->ndrops; k++) {
            wr(o, &f->drops[k].off, 8); wr(o, &f->drops[k].len, 8); wr(o, &f->drops[k].kind, 4);
        }
        pos += 2 + nl + 8 + 8 + 12 + (uint64_t)f->ndrops * 20;
    }
    wr(o, blocks, nblocks * (uint32_t)sizeof *blocks);
    pos += (uint64_t)nblocks * sizeof *blocks;
    memcpy(hdr.magic, XBPAK_MAGIC, 8);
    hdr.version = XBPAK_VERSION;
    hdr.block_size = s_block;
    hdr.nfiles = s_nfiles;
    hdr.drop_flags = flags;
    hdr.index_size = pos - hdr.index_off;
    hdr.nblocks = nblocks;
    hdr.max_ulen = max_ulen;
    SetFilePointer(o, 0, NULL, FILE_BEGIN);
    wr(o, &hdr, sizeof hdr);
    CloseHandle(o);
    printf("packed %.2f GB -> %.3f GB (%.1f%%) in %lu s\n", raw_total / 1073741824.0, pos / 1073741824.0,
           100.0 * pos / raw_total, (unsigned long)((GetTickCount() - t0) / 1000));

    printf("verifying...\n");
    if (verify(tmp, game) != 0) {
        DeleteFileW(tmp);
        die("verification FAILED; the pack was deleted");
    }
    if (!MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING)) die("cannot rename the finished pack");
    printf("OK: %ls\n", out);
    return 0;
}

/* ---- reading a pack back (--verify, --info) ---- */
typedef struct {
    HANDLE h;
    XbpakHeader hdr;
    PFile files[64];
    XbpakBlock *blocks;
} Pak;

static int pak_open(Pak *p, const wchar_t *path)
{
    uint8_t *ix, *q;
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (p->h == INVALID_HANDLE_VALUE) return 0;
    if (!pread_h(p->h, &p->hdr, sizeof p->hdr, 0) || memcmp(p->hdr.magic, XBPAK_MAGIC, 8)
            || p->hdr.version != XBPAK_VERSION || p->hdr.nfiles > 64) return 0;
    ix = (uint8_t *)malloc((size_t)p->hdr.index_size);
    if (!pread_h(p->h, ix, (uint32_t)p->hdr.index_size, p->hdr.index_off)) return 0;
    q = ix;
    for (i = 0; i < p->hdr.nfiles; i++) {
        PFile *f = &p->files[i];
        uint16_t nl = *(uint16_t *)q; q += 2;
        uint32_t k;
        memcpy(f->name, q, nl < 63 ? nl : 63); q += nl;
        f->size = *(uint64_t *)q; q += 8;
        f->hash = *(uint64_t *)q; q += 8;
        f->first_block = *(uint32_t *)q; q += 4;
        f->nblocks = *(uint32_t *)q; q += 4;
        f->ndrops = *(uint32_t *)q; q += 4;
        f->drops = (Drop *)calloc(f->ndrops ? f->ndrops : 1, sizeof(Drop));
        for (k = 0; k < f->ndrops; k++) {
            f->drops[k].off = *(uint64_t *)q; q += 8;
            f->drops[k].len = *(uint64_t *)q; q += 8;
            f->drops[k].kind = *(uint32_t *)q; q += 4;
        }
    }
    p->blocks = (XbpakBlock *)q;
    return 1;
}

static int pak_block(Pak *p, ZSTD_DCtx *dc, const XbpakBlock *b, uint8_t *dst, uint8_t *scratch)
{
    if (b->method == XBPAK_BLOCK_ZERO) { memset(dst, 0, b->ulen); return 1; }
    if (!pread_h(p->h, scratch, b->csize, b->offset)) return 0;
    if (b->method == XBPAK_BLOCK_RAW) { if (b->csize != b->ulen) return 0; memcpy(dst, scratch, b->ulen); return 1; }
    return ZSTD_decompressDCtx(dc, dst, b->ulen, scratch, b->csize) == b->ulen;
}

static int verify(const wchar_t *pak, const wchar_t *game)
{
    Pak p;
    uint32_t i, bad = 0;
    ZSTD_DCtx *dc = ZSTD_createDCtx();
    uint8_t *a, *b, *scratch;
    uint64_t total = 0, zbytes = 0;
    LARGE_INTEGER fq, d0, d1;
    double dsec = 0;
    QueryPerformanceFrequency(&fq);
    if (!pak_open(&p, pak)) { fprintf(stderr, "not a readable .xbpak\n"); return 1; }
    a = (uint8_t *)malloc(p.hdr.max_ulen);
    b = (uint8_t *)malloc(p.hdr.max_ulen);
    scratch = (uint8_t *)malloc(ZSTD_compressBound(p.hdr.max_ulen));
    for (i = 0; i < p.hdr.nfiles; i++) {
        PFile *f = &p.files[i];
        wchar_t path[MAX_PATH];
        HANDLE h;
        uint32_t k;
        uint64_t expect = 0;
        XXH64_state_t *xs = XXH64_createState();
        XXH64_reset(xs, 0);
        _snwprintf(path, MAX_PATH, L"%s\\%hs", game, f->name);
        for (wchar_t *c = path; *c; c++) if (*c == L'/') *c = L'\\';
        h = open_r(path);
        if (h == INVALID_HANDLE_VALUE) { fprintf(stderr, "missing original %s\n", f->name); bad++; continue; }
        {
            LARGE_INTEGER sz;
            GetFileSizeEx(h, &sz);
            if ((uint64_t)sz.QuadPart != f->size) { fprintf(stderr, "%s: size differs\n", f->name); bad++; }
        }
        for (k = 0; k < f->nblocks; k++) {
            const XbpakBlock *bl = &p.blocks[f->first_block + k];
            if (bl->loff != expect) { fprintf(stderr, "%s: block %u out of place\n", f->name, k); bad++; break; }
            QueryPerformanceCounter(&d0);
            if (!pak_block(&p, dc, bl, a, scratch)) {
                fprintf(stderr, "%s: block %u does not decompress\n", f->name, k); bad++; break;
            }
            QueryPerformanceCounter(&d1);
            if (bl->method == XBPAK_BLOCK_ZSTD) { dsec += (double)(d1.QuadPart - d0.QuadPart) / fq.QuadPart; zbytes += bl->ulen; }
            logical_read(f, h, b, bl->ulen, bl->loff);   /* the original, with the declared drops applied */
            if (memcmp(a, b, bl->ulen)) { fprintf(stderr, "%s: block %u differs\n", f->name, k); bad++; break; }
            XXH64_update(xs, a, bl->ulen);
            total += bl->ulen;
            expect += bl->ulen;
        }
        if (expect != f->size) { fprintf(stderr, "%s: blocks cover %llu of %llu bytes\n", f->name,
                                         (unsigned long long)expect, (unsigned long long)f->size); bad++; }
        if (XXH64_digest(xs) != f->hash) { fprintf(stderr, "%s: hash differs\n", f->name); bad++; }
        XXH64_freeState(xs);
        CloseHandle(h);
    }
    printf("verified %.2f GB: %s (zstd decompression %.0f MB/s on one core)\n", total / 1073741824.0,
           bad ? "MISMATCH" : "every kept byte identical to the original",
           dsec > 0 ? zbytes / 1048576.0 / dsec : 0.0);
    CloseHandle(p.h);
    ZSTD_freeDCtx(dc);
    return bad ? 1 : 0;
}

static int info(const wchar_t *pak)
{
    Pak p;
    uint32_t i;
    if (!pak_open(&p, pak)) { fprintf(stderr, "not a readable .xbpak\n"); return 1; }
    printf("region %u KB, %u files, %u blocks (largest %u KB), drop flags %u\n", p.hdr.block_size / 1024,
           p.hdr.nfiles, p.hdr.nblocks, p.hdr.max_ulen / 1024, p.hdr.drop_flags);
    for (i = 0; i < p.hdr.nfiles; i++) {
        PFile *f = &p.files[i];
        uint64_t stored = 0, dropped = 0, bigb = 0;
        uint32_t k;
        for (k = 0; k < f->nblocks; k++) {
            stored += p.blocks[f->first_block + k].csize;
            if (p.blocks[f->first_block + k].ulen > 1024 * 1024) bigb += p.blocks[f->first_block + k].ulen;
        }
        for (k = 0; k < f->ndrops; k++) dropped += f->drops[k].len;
        printf("  %-16s %8.1f MB -> %8.1f MB  (%.0f%% in big blocks; %u dropped ranges, %.1f MB)\n", f->name,
               f->size / 1048576.0, stored / 1048576.0, f->size ? 100.0 * bigb / f->size : 0.0, f->ndrops,
               dropped / 1048576.0);
    }
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    uint32_t flags = 0;
    int i, all = 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    s_threads = (int)si.dwNumberOfProcessors;
    s_block = 16 * 1024 * 1024;
    if (argc >= 3 && !wcscmp(argv[1], L"--info")) return info(argv[2]);
    if (argc >= 4 && !wcscmp(argv[1], L"--verify")) return verify(argv[2], argv[3]);
    if (argc < 3) {
        fprintf(stderr, "usage: xbpack <game folder> <out.xbpak> [--drop movies,crib|none] [--level N]\n"
                        "              [--block KB] [--sub KB] [--threads N] [--all] [--keep-for <mod pack>]\n"
                        "       xbpack --verify <pak> <game folder>\n"
                        "       xbpack --info <pak>\n");
        return 2;
    }
    for (i = 3; i < argc; i++) {
        if (!wcscmp(argv[i], L"--drop") && i + 1 < argc) {
            const wchar_t *v = argv[++i];
            if (wcsstr(v, L"movies")) flags |= XBPAK_DROP_MOVIES;
            if (wcsstr(v, L"crib")) flags |= XBPAK_DROP_CRIB;
        } else if (!wcscmp(argv[i], L"--level") && i + 1 < argc) {
            s_level = _wtoi(argv[++i]);
        } else if (!wcscmp(argv[i], L"--block") && i + 1 < argc) {
            s_block = (uint32_t)_wtoi(argv[++i]) * 1024;
        } else if (!wcscmp(argv[i], L"--sub") && i + 1 < argc) {
            s_sub = (uint32_t)_wtoi(argv[++i]) * 1024;
        } else if (!wcscmp(argv[i], L"--threads") && i + 1 < argc) {
            s_threads = _wtoi(argv[++i]);
        } else if (!wcscmp(argv[i], L"--keep-for") && i + 1 < argc) {
            if (s_nkeep < 16) s_keep[s_nkeep++] = argv[++i];
        } else if (!wcscmp(argv[i], L"--all")) {
            all = 1;
        }
    }
    if (s_threads < 1) s_threads = 1;
    if (s_threads > 32) s_threads = 32;
    if (s_block < 65536 || s_block > 64 * 1024 * 1024) die("--block must be 64..65536 KB");
    if (s_sub < 16384 || s_sub > s_block) die("--sub must be 16 KB..--block");
    return build(argv[1], argv[2], flags, all);
}
