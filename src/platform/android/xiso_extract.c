/* ── First start on Android: unpack the player's NFL 2K5 disc image ──────
 *
 * The game reads its disc as files (original/default.xbe, vc_53450030/...).
 * A player who has only the .iso puts it in Internal storage/NFL2K5 or
 * Download; the first start finds it (an XDVDFS image holding default.xbe)
 * and unpacks it into <root>/original, with a progress bar. It unpacks into
 * original.part and renames at the end, so an interrupted start begins again.
 * The reader is tools/xiso_read.py's, in C. */
#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <openssl/sha.h>
#include "xbe_identify.h"

#define XISO_SECTOR 2048ull
static const char k_magic[] = "MICROSOFT*XBOX*MEDIA";

typedef struct {
    FILE *f;
    uint64_t base;                     /* offset of the XDVDFS partition */
    uint64_t total, done;              /* bytes of file data, for progress */
    SDL_Window *win;
    SDL_Renderer *ren;
    Uint64 last_draw;
    char status[256];
    int failed;
} Xiso;

static int xiso_open(Xiso *x, const char *path)
{
    static const uint64_t bases[] = { 0, 0x18300000ull, 0xFD90000ull, 0x2090000ull, 0x89D80000ull, 0x2080000ull };
    char m[20];
    size_t i;
    memset(x, 0, sizeof *x);
    x->f = fopen(path, "rb");
    if (!x->f) return 0;
    for (i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        if (fseeko(x->f, (off_t)(bases[i] + 32 * XISO_SECTOR), SEEK_SET) == 0 &&
                fread(m, 1, 20, x->f) == 20 && !memcmp(m, k_magic, 20)) {
            x->base = bases[i];
            return 1;
        }
    }
    fclose(x->f);
    x->f = NULL;
    return 0;
}

static uint8_t *xiso_read_dir(Xiso *x, uint32_t sector, uint32_t size)
{
    uint8_t *d = (uint8_t *)malloc(size ? size : 1);
    if (!d) return NULL;
    if (fseeko(x->f, (off_t)(x->base + sector * XISO_SECTOR), SEEK_SET) || fread(d, 1, size, x->f) != size) {
        free(d);
        return NULL;
    }
    return d;
}

static void draw_progress(Xiso *x, int force)
{
    Uint64 now = SDL_GetTicks();
    SDL_Event e;
    int w = 0, h = 0;
    float frac;
    while (SDL_PollEvent(&e)) { }
    if (!x->ren || (!force && now - x->last_draw < 100)) return;
    x->last_draw = now;
    SDL_GetRenderOutputSize(x->ren, &w, &h);
    SDL_SetRenderScale(x->ren, 1.0f, 1.0f);
    SDL_SetRenderDrawColor(x->ren, 8, 16, 32, 255);
    SDL_RenderClear(x->ren);
    frac = x->total ? (float)((double)x->done / (double)x->total) : 0.0f;
    {
        SDL_FRect bar = { w * 0.1f, h * 0.55f, w * 0.8f, h * 0.05f }, fill = bar;
        fill.w = bar.w * frac;
        SDL_SetRenderDrawColor(x->ren, 60, 60, 70, 255);
        SDL_RenderFillRect(x->ren, &bar);
        SDL_SetRenderDrawColor(x->ren, 220, 40, 40, 255);
        SDL_RenderFillRect(x->ren, &fill);
    }
    SDL_SetRenderScale(x->ren, 3.0f, 3.0f);
    SDL_SetRenderDrawColor(x->ren, 255, 255, 255, 255);
    SDL_RenderDebugText(x->ren, w * 0.1f / 3.0f, h * 0.38f / 3.0f, "NFL 2K5: unpacking your disc image (first start only)");
    {
        char line[96];
        snprintf(line, sizeof line, "%.0f%%  (%.1f of %.1f GB)", frac * 100.0, x->done / 1e9, x->total / 1e9);
        SDL_RenderDebugText(x->ren, w * 0.1f / 3.0f, h * 0.45f / 3.0f, line);
    }
    SDL_RenderDebugText(x->ren, w * 0.1f / 3.0f, h * 0.65f / 3.0f, x->status);
    SDL_RenderPresent(x->ren);
}

/* walk == 0: count bytes; 1: write files under `out` */
static void xiso_walk(Xiso *x, uint32_t sector, uint32_t size, const char *out, int write, int depth)
{
    uint8_t *d;
    uint32_t stack[512];
    int sp = 0;
    uint8_t *seen;
    if (depth > 16 || !size || x->failed) return;
    d = xiso_read_dir(x, sector, size);
    if (!d) { x->failed = 1; return; }
    seen = (uint8_t *)calloc(size / 4 + 1, 1);
    stack[sp++] = 0;
    while (sp > 0 && !x->failed) {
        uint32_t off = stack[--sp];
        uint16_t left, right;
        uint32_t start, fsize;
        uint8_t attr, nlen;
        char name[256], path[1024];
        if (off + 14 > size || seen[off / 4]) continue;
        seen[off / 4] = 1;
        memcpy(&left, d + off, 2); memcpy(&right, d + off + 2, 2);
        memcpy(&start, d + off + 4, 4); memcpy(&fsize, d + off + 8, 4);
        attr = d[off + 12]; nlen = d[off + 13];
        if (left == 0xFFFF && right == 0xFFFF && start == 0xFFFFFFFFu) continue;
        if (off + 14 + nlen > size) continue;
        memcpy(name, d + off + 14, nlen);
        name[nlen] = 0;
        snprintf(path, sizeof path, "%s/%s", out, name);
        if (attr & 0x10) {
            if (write) mkdir(path, 0755);
            if (fsize) xiso_walk(x, start, fsize, path, write, depth + 1);
        } else if (!write) {
            x->total += fsize;
        } else {
            FILE *o = fopen(path, "wb");
            static uint8_t buf[1 << 20];
            uint64_t left_bytes = fsize;
            if (!o) { snprintf(x->status, sizeof x->status, "could not write %s (%s)", path, strerror(errno)); x->failed = 1; break; }
            snprintf(x->status, sizeof x->status, "%s", name);
            if (fseeko(x->f, (off_t)(x->base + start * XISO_SECTOR), SEEK_SET)) x->failed = 1;
            while (left_bytes && !x->failed) {
                size_t n = left_bytes > sizeof buf ? sizeof buf : (size_t)left_bytes;
                if (fread(buf, 1, n, x->f) != n || fwrite(buf, 1, n, o) != n) {
                    snprintf(x->status, sizeof x->status, "read/write failed at %s (%s)", name, strerror(errno));
                    x->failed = 1;
                    break;
                }
                left_bytes -= n;
                x->done += n;
                draw_progress(x, 0);
            }
            if (fclose(o)) x->failed = 1;
        }
        if (left && sp < 510) stack[sp++] = (uint32_t)left * 4;
        if (right && sp < 510) stack[sp++] = (uint32_t)right * 4;
    }
    free(seen);
    free(d);
}

static int xiso_root(Xiso *x, uint32_t *sector, uint32_t *size)
{
    if (fseeko(x->f, (off_t)(x->base + 32 * XISO_SECTOR + 20), SEEK_SET)) return 0;
    return fread(sector, 4, 1, x->f) == 1 && fread(size, 4, 1, x->f) == 1;
}

/* A message the player must see before the game's window exists: the
 * system's dialog, or drawn in a window of its own when that cannot show;
 * either way also in the log. Waits for a tap (or 60 s). */
void nfl2k5_android_notice(const char *title, const char *msg)
{
    SDL_Window *w = NULL;
    SDL_Renderer *r = NULL;
    Uint64 t0;
    fprintf(stderr, "[NOTICE] %s: %s\n", title, msg);
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL)) return;
    fprintf(stderr, "[NOTICE] dialog failed (%s): drawing it\n", SDL_GetError());
    if (!SDL_CreateWindowAndRenderer("NFL 2K5", 1280, 720, SDL_WINDOW_FULLSCREEN, &w, &r)) return;
    t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 60000) {
        SDL_Event e;
        int ww = 0, hh = 0, line = 0, done = 0;
        const char *p = msg;
        while (SDL_PollEvent(&e))
            if ((e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_KEY_UP ||
                 e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) && SDL_GetTicks() - t0 > 1500)
                done = 1;
        if (done) break;
        SDL_GetRenderOutputSize(r, &ww, &hh);
        SDL_SetRenderScale(r, 1, 1);
        SDL_SetRenderDrawColor(r, 10, 14, 30, 255);
        SDL_RenderClear(r);
        SDL_SetRenderScale(r, 2.5f, 2.5f);
        SDL_SetRenderDrawColor(r, 255, 200, 60, 255);
        SDL_RenderDebugText(r, 20, 20, title);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        /* wrapped to the window's width */
        while (*p) {
            char buf[256];
            int cols = (int)(ww / 2.5f / 8) - 6, n = 0, last_space = -1;
            if (cols < 20) cols = 20;
            if (cols > 250) cols = 250;
            while (p[n] && p[n] != '\n' && n < cols) { if (p[n] == ' ') last_space = n; n++; }
            if (p[n] && p[n] != '\n' && last_space > 0) n = last_space;
            memcpy(buf, p, (size_t)n); buf[n] = 0;
            SDL_RenderDebugText(r, 20, 50 + line++ * 12, buf);
            p += n;
            if (*p == '\n' || *p == ' ') p++;
        }
        SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
        SDL_RenderDebugText(r, 20, hh / 2.5f - 30, "Tap to close.");
        SDL_RenderPresent(r);
        SDL_Delay(50);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
}

static char s_ps2_iso[256];            /* a PlayStation 2 image seen while looking */
static char s_wrong_iso[256];          /* an Xbox image of another version of the game */
static char s_modded_iso[256];         /* a Mod Studio disc (SOFTDRINK): the USA game, patched */

/* The executables the recompiled code was made from (src/main.c): the
 * retail USA default.xbe, and the same with the built-in widescreen patch. */
static int xbe_digest_ok(const unsigned char d[32])
{
    static const char *const ok[] = {
        "73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9",
        "81d6e16c25ca7ea1014d0ced915f910f5d354eaeabeb9adfcbb6956e2a0b54f8" };
    char hex[65];
    int i;
    for (i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return !strcmp(hex, ok[0]) || !strcmp(hex, ok[1]);
}

/* 1: the file at `path` is one of those executables. */
int nfl2k5_android_xbe_file_ok(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf, d[32];
    long n;
    int ok;
    if (!f) return 0;
    fseeko(f, 0, SEEK_END); n = (long)ftello(f); fseeko(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    ok = buf && n > 0 && fread(buf, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (ok) { SHA256(buf, (size_t)n, d); ok = xbe_digest_ok(d); }
    free(buf);
    return ok;
}

/* 1: the file at `path` is a modded copy of the USA executable. */
static int xbe_file_modded(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t head[4096];
    size_t n;
    if (!f) return 0;
    n = fread(head, 1, sizeof head, f);       /* the headers come first */
    fclose(f);
    return nfl2k5_xbe_is_modded(head, n);
}

enum { XBE_OTHER = 0, XBE_RETAIL = 1, XBE_MODDED = 2 };

/* What the image's default.xbe (in its root directory) is. */
static int xiso_xbe_kind(Xiso *x)
{
    uint32_t rs, rz, off = 0;
    uint8_t *d;
    int ok = XBE_OTHER, guard = 0;
    if (!xiso_root(x, &rs, &rz) || !(d = xiso_read_dir(x, rs, rz))) return 0;
    while (off + 14 <= rz && guard++ < 4096) {
        uint16_t left, right;
        uint32_t start, fsize;
        uint8_t nlen = d[off + 13];
        int c;
        memcpy(&left, d + off, 2); memcpy(&right, d + off + 2, 2);
        memcpy(&start, d + off + 4, 4); memcpy(&fsize, d + off + 8, 4);
        if (off + 14 + nlen > rz) break;
        /* the directory is a binary tree ordered by case-insensitive name */
        c = strncasecmp("default.xbe", (const char *)d + off + 14, nlen);
        if (c == 0 && nlen != 11) c = nlen < 11 ? 1 : -1;
        if (c == 0) {
            unsigned char *buf = (unsigned char *)malloc(fsize ? fsize : 1), dg[32];
            if (buf && fseeko(x->f, (off_t)(x->base + start * XISO_SECTOR), SEEK_SET) == 0 &&
                    fread(buf, 1, fsize, x->f) == fsize) {
                SHA256(buf, fsize, dg);
                ok = xbe_digest_ok(dg) ? XBE_RETAIL : nfl2k5_xbe_is_modded(buf, fsize) ? XBE_MODDED : XBE_OTHER;
            }
            free(buf);
            break;
        }
        if (c < 0) { if (!left) break; off = (uint32_t)left * 4; }
        else { if (!right) break; off = (uint32_t)right * 4; }
    }
    free(d);
    return ok;
}

static int ends_iso(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && (!strcasecmp(n + l - 4, ".iso") || (l > 5 && !strcasecmp(n + l - 5, ".xiso")));
}

/* The best disc image in the usual folders: an XDVDFS image, "2K5" in the
 * name preferred. */
static int find_iso(char *best, size_t n)
{
    const char *dirs[] = { "/sdcard/NFL2K5", "/sdcard/Download", "/sdcard/Downloads", "/sdcard/Documents", "/sdcard",
                           "/storage/emulated/0/Download", NULL };
    int score = -1, i;
    best[0] = 0;
    for (i = 0; dirs[i]; i++) {
        DIR *d = opendir(dirs[i]);
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char p[1024], up[256];
            Xiso x;
            int s = 0, k;
            if (!ends_iso(e->d_name)) continue;
            snprintf(p, sizeof p, "%s/%s", dirs[i], e->d_name);
            if (!xiso_open(&x, p)) {
                /* a PlayStation 2 disc (ISO 9660, "PLAYSTATION" system id) */
                FILE *f = fopen(p, "rb");
                char id[16];
                if (f && fseeko(f, 0x8000, SEEK_SET) == 0 && fread(id, 1, 16, f) == 16 &&
                        !memcmp(id + 1, "CD001", 5) && !memcmp(id + 8, "PLAYSTAT", 8))
                    snprintf(s_ps2_iso, sizeof s_ps2_iso, "%s", e->d_name);
                if (f) fclose(f);
                continue;
            }
            k = xiso_xbe_kind(&x);
            fclose(x.f);
            if (k != XBE_RETAIL) {           /* the code would not match it */
                if (k == XBE_MODDED) snprintf(s_modded_iso, sizeof s_modded_iso, "%s", e->d_name);
                else snprintf(s_wrong_iso, sizeof s_wrong_iso, "%s", e->d_name);
                fprintf(stderr, "[ISO] %s: not the retail USA disc (%s), skipped\n", p,
                        k == XBE_MODDED ? "modded" : "another version");
                continue;
            }
            for (k = 0; e->d_name[k] && k < 255; k++) up[k] = (char)toupper((unsigned char)e->d_name[k]);
            up[k] = 0;
            if (strstr(up, "2K5")) s += 2;
            if (strstr(up, "NFL")) s += 1;
            if (strstr(up, "PATCHED") || strstr(up, "FIXED")) s -= 1;   /* modified images: the plain disc first */
            if (s > score) { score = s; snprintf(best, n, "%s", p); }
        }
        closedir(d);
    }
    return best[0] != 0;
}

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) { unlink(path); return; }
    while ((e = readdir(d)) != NULL) {
        char p[1024];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", path, e->d_name);
        rm_tree(p);
    }
    closedir(d);
    rmdir(path);
}

/* 1: <root>/original now holds the disc. 0: no image found. -1: failed (message shown). */
int nfl2k5_android_unpack_iso(const char *root)
{
    char iso[1024], part[1024], dest[1024], msg[1536];
    Xiso x;
    uint32_t rs, rz;
    struct statvfs vf;
    if (!find_iso(iso, sizeof iso)) {
        /* Unpacked by hand counts too: main.c passed over it for its hash. */
        snprintf(part, sizeof part, "%s/original/default.xbe", root);
        if (!s_modded_iso[0] && xbe_file_modded(part))
            snprintf(s_modded_iso, sizeof s_modded_iso, "The default.xbe in NFL2K5/original");
        if (s_modded_iso[0]) {
            /* Not a pack we can run alone: the converted code is matched against
             * the retail executable to find what a mod changed, and a patched
             * disc has no retail executable left to compare with. */
            snprintf(msg, sizeof msg,
                     "%s is a modded copy of ESPN NFL 2K5 (a SOFTDRINK or 2K5 Mod Studio disc). "
                     "NFL 2K5 cannot start from a modded disc: it needs the original one, and it "
                     "applies the mod itself.\n\n"
                     "1. Put your original USA Xbox disc image in Download. That is the dump you made "
                     "the modded disc from. Then open NFL 2K5 again.\n"
                     "2. To play the mod, also put its .2k5patch file in Download. The game installs "
                     "it, then turn it on under Extras > Mods.", s_modded_iso);
            nfl2k5_android_notice("NFL 2K5: modded disc image", msg);
            return -1;
        }
        if (s_wrong_iso[0]) {
            snprintf(msg, sizeof msg,
                     "%s is a different version of ESPN NFL 2K5 (its default.xbe does not match). "
                     "This port needs the original USA Xbox disc: on your PC that is "
                     "\"ESPN NFL 2K5 (USA).xiso.iso\". Put it in Download and open NFL 2K5 again.", s_wrong_iso);
            nfl2k5_android_notice("NFL 2K5: different version of the game", msg);
            return -1;
        }
        if (s_ps2_iso[0]) {
            snprintf(msg, sizeof msg,
                     "%s is the PlayStation 2 version of ESPN NFL 2K5. This port runs the original Xbox "
                     "game: it needs the Xbox disc image (it holds default.xbe).", s_ps2_iso);
            nfl2k5_android_notice("NFL 2K5: wrong disc image", msg);
            return -1;
        }
        return 0;
    }
    if (!xiso_open(&x, iso) || !xiso_root(&x, &rs, &rz)) return 0;
    mkdir(root, 0755);
    snprintf(part, sizeof part, "%s/original.part", root);
    snprintf(dest, sizeof dest, "%s/original", root);
    rm_tree(part);
    mkdir(part, 0755);
    xiso_walk(&x, rs, rz, part, 0, 0);
    if (statvfs(root, &vf) == 0 && (uint64_t)vf.f_bavail * vf.f_frsize < x.total + (64ull << 20)) {
        snprintf(msg, sizeof msg,
                 "Found %s, but unpacking it needs %.1f GB free and there is %.1f GB.\n\n"
                 "Free some space and open NFL 2K5 again. (After it unpacks, the .iso can be deleted.)",
                 iso, x.total / 1e9, (double)vf.f_bavail * vf.f_frsize / 1e9);
        nfl2k5_android_notice("NFL 2K5: not enough space", msg);
        fclose(x.f);
        rm_tree(part);
        return -1;
    }
    fprintf(stderr, "[ISO] unpacking %s (%.2f GB) into %s\n", iso, x.total / 1e9, dest);
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) &&
            SDL_CreateWindowAndRenderer("NFL 2K5", 1280, 720, SDL_WINDOW_FULLSCREEN, &x.win, &x.ren))
        draw_progress(&x, 1);
    xiso_walk(&x, rs, rz, part, 1, 0);
    fclose(x.f);
    if (x.ren) SDL_DestroyRenderer(x.ren);
    if (x.win) SDL_DestroyWindow(x.win);
    if (x.failed) {
        snprintf(msg, sizeof msg, "Unpacking %s failed: %s", iso, x.status);
        nfl2k5_android_notice("NFL 2K5", msg);
        rm_tree(part);
        return -1;
    }
    if (rmdir(dest) != 0 && errno != ENOENT) {
        /* not empty (a copy that was started by hand): kept aside, never deleted */
        char aside[1024];
        snprintf(aside, sizeof aside, "%s/original.before-iso", root);
        rename(dest, aside);
    }
    if (rename(part, dest) != 0) {
        snprintf(msg, sizeof msg, "Could not finish unpacking (%s).", strerror(errno));
        nfl2k5_android_notice("NFL 2K5", msg);
        return -1;
    }
    fprintf(stderr, "[ISO] done: %s\n", dest);
    return 1;
}
#endif
