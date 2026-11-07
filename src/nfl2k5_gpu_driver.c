/* Custom Vulkan drivers on Android (Options > GPU Driver).
 *
 * A player can install an Adreno driver (Mesa Turnip or a Qualcomm build) in the
 * zip form emulators use: meta.json + .so, or a bare .so. Android's linker
 * namespaces stop a plain dlopen of a replacement driver, so libadrenotools opens
 * a private libvulkan that loads it; SDL_GPU then uses that handle
 * (SDL_ANDROID_VULKAN_HANDLE, android/patches/sdl-vulkan-pipeline-cache.patch).
 * The System Driver path never touches any of this.
 *
 * A bad driver must not lock anyone out. `trial` is written before the driver is
 * opened and removed once frames have been presented. Still there at the next
 * start means that start never got that far: back to the System Driver, and the
 * player is told why. A failed load or a phone without an Adreno GPU does the same.
 *
 * Drivers live in app-private storage (dlopen needs an exec-allowed path):
 *   <files>/gpu_drivers/selected, trial, last_fallback, <id>/meta.json, <id>/<lib>.so
 * Nothing here limits a setting; a chosen driver only ever falls back for the
 * reasons above, and always says so. Logged as [GPU-DRIVER]. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "miniz.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <android/api-level.h>
#include <SDL3/SDL.h>
#endif
#if defined(NFL2K5_HAVE_ADRENOTOOLS)
#include <adrenotools/driver.h>
#endif

#define GD_MAX 32
#define GD_MAX_FILE (512u << 20)    /* a member larger than any driver: a broken or hostile zip */
#define GD_FRAMES_OK 120

typedef struct {
    char id[64], name[64], version[48], lib[96];
} GdEntry;

static GdEntry s_list[GD_MAX];
static int s_count;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static char s_base[512];
static char s_selected[64];         /* what the next start uses; empty = System Driver */
static char s_running[112] = "System Driver";
static char s_note[128];            /* the last fallback, for the menu */
static int s_remove_armed;
static int s_is_adreno = -1;

extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);

static const char *gd_base(void)
{
    if (!s_base[0]) {
        const char *e = getenv("NFL2K5_GPU_DRIVER_DIR");
#if defined(__ANDROID__)
        if (!e || !*e) {
            const char *in = SDL_GetAndroidInternalStoragePath();
            snprintf(s_base, sizeof s_base, "%s/gpu_drivers", in ? in : ".");
        } else
#endif
        snprintf(s_base, sizeof s_base, "%s", e && *e ? e : "gpu_drivers");
        mkdir(s_base, 0700);
    }
    return s_base;
}

static void gd_path(char *out, size_t n, const char *leaf)
{
    snprintf(out, n, "%s/%s", gd_base(), leaf);
}

static int gd_read_line(const char *leaf, char *out, size_t n)
{
    char p[640];
    FILE *f;
    out[0] = 0;
    gd_path(p, sizeof p, leaf);
    if (!(f = fopen(p, "r"))) return 0;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = 0;
    fclose(f);
    return 1;
}

/* Written through to the disk: the marker has to survive the crash it is for. */
static int gd_write_line(const char *leaf, const char *text)
{
    char p[640];
    FILE *f;
    gd_path(p, sizeof p, leaf);
    if (!(f = fopen(p, "w"))) return 0;
    fputs(text, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return 1;
}

static void gd_unlink(const char *leaf)
{
    char p[640];
    gd_path(p, sizeof p, leaf);
    unlink(p);
}

/* The value of a string key in meta.json. Enough JSON for this flat format,
 * and a value that is not a string (or is missing) gives "". */
static void json_string(const char *json, const char *key, char *out, size_t n)
{
    char pat[64];
    const char *p;
    size_t o = 0;
    out[0] = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    if (!(p = strstr(json, pat))) return;
    p += strlen(pat);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p++ != ':') return;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p++ != '"') return;
    while (*p && *p != '"' && o + 1 < n) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'u') {        /* not worth decoding for a menu label */
                out[o++] = '?';
                for (int i = 0; i < 4 && p[1]; i++) p++;
                p++;
                continue;
            }
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

static int json_int(const char *json, const char *key)
{
    char pat[64];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    if (!(p = strstr(json, pat))) return 0;
    p += strlen(pat);
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;
    return atoi(p);
}

static char *read_file(const char *path, size_t limit)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > limit || !(b = (char *)malloc((size_t)n + 1))) { fclose(f); return NULL; }
    n = (long)fread(b, 1, (size_t)n, f);
    b[n] = 0;
    fclose(f);
    return b;
}

static int plain_name(const char *path, char *out, size_t n);

static void gd_scan(void)
{
    DIR *d = opendir(gd_base());
    struct dirent *e;
    GdEntry list[GD_MAX];
    int n = 0;
    if (d) {
        while ((e = readdir(d)) && n < GD_MAX) {
            char p[900];
            char *meta;
            struct stat st;
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "%s/%s/meta.json", gd_base(), e->d_name);
            if (!(meta = read_file(p, 1u << 16))) continue;
            memset(&list[n], 0, sizeof list[n]);
            snprintf(list[n].id, sizeof list[n].id, "%s", e->d_name);
            json_string(meta, "name", list[n].name, sizeof list[n].name);
            json_string(meta, "driverVersion", list[n].version, sizeof list[n].version);
            json_string(meta, "libraryName", list[n].lib, sizeof list[n].lib);
            free(meta);
            {   /* a name in meta.json never leaves the driver's folder */
                char leaf[sizeof list[n].lib];
                if (!plain_name(list[n].lib, leaf, sizeof leaf)) continue;
                snprintf(list[n].lib, sizeof list[n].lib, "%s", leaf);
            }
            snprintf(p, sizeof p, "%s/%s/%s", gd_base(), e->d_name, list[n].lib);
            if (stat(p, &st) != 0) continue;
            if (!list[n].name[0]) snprintf(list[n].name, sizeof list[n].name, "%s", list[n].id);
            n++;
        }
        closedir(d);
    }
    pthread_mutex_lock(&s_lock);
    memcpy(s_list, list, sizeof(GdEntry) * (size_t)n);
    s_count = n;
    pthread_mutex_unlock(&s_lock);
}

static int gd_find(const char *id)
{
    for (int i = 0; id[0] && i < s_count; i++)
        if (!strcmp(s_list[i].id, id)) return i;
    return -1;
}

static void gd_label(const GdEntry *e, char *out, size_t n)
{
    if (e->version[0]) snprintf(out, n, "%s %s", e->name, e->version);
    else snprintf(out, n, "%s", e->name);
}

static void gd_select(const char *id)
{
    snprintf(s_selected, sizeof s_selected, "%s", id);
    if (id[0]) gd_write_line("selected", id);
    else gd_unlink("selected");
}

int nfl2k5_gpu_driver_is_adreno(void)
{
    if (s_is_adreno < 0) {
#if defined(__ANDROID__)
        char egl[PROP_VALUE_MAX] = { 0 }, vk[PROP_VALUE_MAX] = { 0 };
        __system_property_get("ro.hardware.egl", egl);
        __system_property_get("ro.hardware.vulkan", vk);
        s_is_adreno = strstr(egl, "adreno") || strstr(vk, "adreno") || access("/dev/kgsl-3d0", F_OK) == 0;
        fprintf(stderr, "[GPU-DRIVER] GPU check: ro.hardware.egl=%s ro.hardware.vulkan=%s: %s\n", egl, vk,
                s_is_adreno ? "Adreno" : "not Adreno");
#else
        /* a Linux test build has no Adreno; this lets its menu be driven */
        s_is_adreno = getenv("NFL2K5_GPU_DRIVER_FAKE_ADRENO") != NULL;
#endif
    }
    return s_is_adreno;
}

/* ---- the driver itself ---------------------------------------------------- */
#if defined(GPU_DRIVER_TEST)
extern void *gd_test_open(const GdEntry *e, const char *dir, char *why, size_t n);
#define gd_open gd_test_open
#elif defined(NFL2K5_HAVE_ADRENOTOOLS)
static void *gd_open(const GdEntry *e, const char *dir, char *why, size_t n)
{
    char hooks[512], tmp[640];
    Dl_info info;
    void *h;
    char *slash;
    /* the hook libraries sit next to this one, in nativeLibraryDir
     * (android:extractNativeLibs="true") */
    if (!dladdr((void *)gd_open, &info) || !info.dli_fname) { snprintf(why, n, "no native library folder"); return NULL; }
    snprintf(hooks, sizeof hooks, "%s", info.dli_fname);
    if ((slash = strrchr(hooks, '/'))) slash[1] = 0;
    gd_path(tmp, sizeof tmp, "tmp/");
    mkdir(tmp, 0700);
    fprintf(stderr, "[GPU-DRIVER] opening %s%s (hooks in %s)\n", dir, e->lib, hooks);
    h = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, tmp, hooks, dir, e->lib, NULL, NULL);
    if (!h) {
        const char *err = dlerror();
        snprintf(why, n, "%s", err ? err : "libadrenotools could not open it");
        return NULL;
    }
    if (!dlsym(h, "vkGetInstanceProcAddr")) { snprintf(why, n, "no vkGetInstanceProcAddr in the loader"); return NULL; }
    return h;
}
#else
static void *gd_open(const GdEntry *e, const char *dir, char *why, size_t n)
{
    (void)e; (void)dir;
    snprintf(why, n, "this build cannot load custom drivers");
    return NULL;
}
#endif

static void gd_fallback(const char *what)
{
    snprintf(s_note, sizeof s_note, "%s", what);
    gd_write_line("last_fallback", what);
    gd_select("");
    snprintf(s_running, sizeof s_running, "System Driver");
    fprintf(stderr, "[GPU-DRIVER] fallback to System Driver: %s\n", what);
    xbox_PresentToast("GPU DRIVER", "Using the System Driver:", what, 8000);
}

#if !defined(GPU_DRIVER_TEST)
static void *frames_thread(void *arg);
#endif

/* Before anything loads Vulkan. `cache` gets this driver's pipeline cache file
 * name: another driver's compiled pipelines mean nothing to this one. */
void nfl2k5_gpu_driver_boot(char *cache, size_t cache_n)
{
    char trial[64], dir[640], why[256], line[300];
    void *handle;
    int i;
    GdEntry e;

    snprintf(cache, cache_n, "vk_pipelines.bin");
    gd_scan();
    gd_read_line("selected", s_selected, sizeof s_selected);
    gd_read_line("last_fallback", s_note, sizeof s_note);

    if (gd_read_line("trial", trial, sizeof trial)) {
        gd_unlink("trial");
        if (trial[0] && !strcmp(trial, s_selected)) {
            i = gd_find(trial);
            char label[120];
            if (i >= 0) gd_label(&s_list[i], label, sizeof label);
            else snprintf(label, sizeof label, "%s", trial);
            snprintf(line, sizeof line, "%s did not finish starting last time", label);
            gd_fallback(line);
            return;
        }
    }
    if (!s_selected[0]) {
        fprintf(stderr, "[GPU-DRIVER] chosen: System Driver (%d installed)\n", s_count);
        return;
    }
    if ((i = gd_find(s_selected)) < 0) {
        snprintf(line, sizeof line, "the chosen driver is no longer installed");
        gd_fallback(line);
        return;
    }
    e = s_list[i];
    fprintf(stderr, "[GPU-DRIVER] chosen: %s %s (%s, %s)\n", e.name, e.version, e.id, e.lib);
    if (!nfl2k5_gpu_driver_is_adreno()) {
        gd_fallback("this phone has no Adreno GPU");
        return;
    }
    snprintf(dir, sizeof dir, "%s/%s/", gd_base(), e.id);
    gd_write_line("trial", e.id);
    why[0] = 0;
    handle = gd_open(&e, dir, why, sizeof why);
    if (!handle) {
        gd_unlink("trial");
        snprintf(line, sizeof line, "%s failed to load: %.200s", e.name, why);
        gd_fallback(line);
        return;
    }
    {
        char hex[32];
        snprintf(hex, sizeof hex, "%llx", (unsigned long long)(uintptr_t)handle);
        setenv("SDL_ANDROID_VULKAN_HANDLE", hex, 1);
    }
    gd_label(&e, s_running, sizeof s_running);
    snprintf(cache, cache_n, "vk_pipelines-%s.bin", e.id);
    s_note[0] = 0;
    gd_unlink("last_fallback");
    fprintf(stderr, "[GPU-DRIVER] loaded %s through libadrenotools; pipeline cache %s\n", s_running, cache);
    fprintf(stderr, "[GPU-DRIVER] the Vulkan Driver line below shows which driver really runs\n");
#if !defined(GPU_DRIVER_TEST)
    {
        pthread_t t;
        if (pthread_create(&t, NULL, frames_thread, NULL) == 0) pthread_detach(t);
    }
#endif
}

/* The driver got the game on screen: it keeps its place. */
void nfl2k5_gpu_driver_frames_ok(void)
{
    gd_unlink("trial");
    fprintf(stderr, "[GPU-DRIVER] %s presented %d frames: trial passed\n", s_running, GD_FRAMES_OK);
}

#if !defined(GPU_DRIVER_TEST)
extern volatile long g_gx_presented_frames;     /* gx/gx_sdl.c */
static void *frames_thread(void *arg)
{
    (void)arg;
    while (g_gx_presented_frames < GD_FRAMES_OK) usleep(250000);
    nfl2k5_gpu_driver_frames_ok();
    return NULL;
}
#endif

/* ---- installing ------------------------------------------------------------ */
static int elf_arm64(const unsigned char *b, size_t n)
{
    return n >= 20 && !memcmp(b, "\177ELF", 4) && b[4] == 2 && b[5] == 1 && b[18] == 183 && b[19] == 0;
}

/* The name of a zip member or picked file, with any folders dropped; 0 for one
 * that is not a plain name. Members never choose where they land. */
static int plain_name(const char *path, char *out, size_t n)
{
    const char *p = strrchr(path, '/'), *q = strrchr(path, '\\');
    if (q && (!p || q > p)) p = q;
    p = p ? p + 1 : path;
    if (!*p || !strcmp(p, ".") || !strcmp(p, "..") || strlen(p) >= n) return 0;
    for (const char *c = p; *c; c++)
        if ((unsigned char)*c < 0x20 || *c == ':' ) return 0;
    snprintf(out, n, "%s", p);
    return 1;
}

static void make_id(const char *name, const char *version, char *out, size_t n)
{
    size_t o = 0;
    char both[128];
    snprintf(both, sizeof both, "%s-%s", name, version);
    for (const char *c = both; *c && o + 1 < n; c++)
        out[o++] = isalnum((unsigned char)*c) || *c == '.' || *c == '_' || *c == '-' ? *c : '_';
    while (o && (out[o - 1] == '-' || out[o - 1] == '_')) o--;
    out[o] = 0;
    if (!o || out[0] == '.') snprintf(out, n, "driver");
}

static void remove_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (d) {
        while ((e = readdir(d))) {
            char p[1200];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    rmdir(dir);
}

static int write_bytes(const char *path, const void *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(b, 1, n, f) == n;
    fclose(f);
    if (ok) chmod(path, 0700);
    else unlink(path);
    return ok;
}

static int api_level(void)
{
#if defined(__ANDROID__)
    return android_get_device_api_level();
#else
    const char *e = getenv("NFL2K5_GPU_DRIVER_TEST_API");
    return e ? atoi(e) : 34;
#endif
}

static int install_zip(const char *file, const char *stage, char *name, size_t name_n, char *version, size_t version_n,
                       char *lib, size_t lib_n, char *msg, size_t msg_n)
{
    mz_zip_archive z;
    char *meta = NULL;
    size_t meta_n = 0;
    int ok = 0;
    unsigned count;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, file, 0)) { snprintf(msg, msg_n, "not a readable zip"); return 0; }
    count = mz_zip_reader_get_num_files(&z);
    for (unsigned i = 0; i < count && !meta; i++) {
        mz_zip_archive_file_stat st;
        char leaf[128];
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) continue;
        if (plain_name(st.m_filename, leaf, sizeof leaf) && !strcmp(leaf, "meta.json") && st.m_uncomp_size < (1u << 16))
            meta = (char *)mz_zip_reader_extract_to_heap(&z, i, &meta_n, 0);
    }
    if (!meta) { snprintf(msg, msg_n, "no meta.json in the zip"); goto done; }
    {
        char *text = (char *)malloc(meta_n + 1), path[900];
        int min_api;
        if (!text) goto done;
        memcpy(text, meta, meta_n);
        text[meta_n] = 0;
        json_string(text, "name", name, name_n);
        json_string(text, "driverVersion", version, version_n);
        json_string(text, "libraryName", lib, lib_n);
        min_api = json_int(text, "minApi");
        free(text);
        if (!lib[0]) { snprintf(msg, msg_n, "meta.json names no library"); goto done; }
        {
            char leaf[128];
            if (!plain_name(lib, leaf, sizeof leaf)) { snprintf(msg, msg_n, "bad library name in meta.json"); goto done; }
            snprintf(lib, lib_n, "%s", leaf);
        }
        if (min_api > api_level()) {
            snprintf(msg, msg_n, "needs Android API %d, this phone has %d", min_api, api_level());
            goto done;
        }
        snprintf(path, sizeof path, "%s/meta.json", stage);
        if (!write_bytes(path, meta, meta_n)) { snprintf(msg, msg_n, "could not write to app storage"); goto done; }
    }
    /* every .so, flat: some packages carry helpers next to the driver */
    for (unsigned i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        char leaf[128], path[900];
        size_t n = 0, len;
        void *data;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) continue;
        if (!plain_name(st.m_filename, leaf, sizeof leaf)) continue;
        len = strlen(leaf);
        if (len < 4 || strcmp(leaf + len - 3, ".so")) continue;
        if (st.m_uncomp_size > GD_MAX_FILE) { snprintf(msg, msg_n, "%s is too large to be a driver", leaf); goto done; }
        if (!(data = mz_zip_reader_extract_to_heap(&z, i, &n, 0))) { snprintf(msg, msg_n, "could not unpack %s", leaf); goto done; }
        if (!elf_arm64((const unsigned char *)data, n)) {
            free(data);
            snprintf(msg, msg_n, "%s is not a 64-bit ARM library", leaf);
            goto done;
        }
        snprintf(path, sizeof path, "%s/%s", stage, leaf);
        ok = write_bytes(path, data, n);
        free(data);
        if (!ok) { snprintf(msg, msg_n, "could not write to app storage"); goto done; }
    }
    ok = 0;
    {
        char path[900];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", stage, lib);
        if (stat(path, &st) != 0) { snprintf(msg, msg_n, "%s is not in the zip", lib); goto done; }
    }
    ok = 1;
done:
    if (meta) free(meta);
    mz_zip_reader_end(&z);
    return ok;
}

static int install_so(const char *file, const char *shown, const char *stage, char *name, size_t name_n,
                      char *lib, size_t lib_n, char *msg, size_t msg_n)
{
    char *data, path[900], leaf[96], json[400];
    FILE *f = fopen(file, "rb");
    long n;
    size_t got;
    int ok;
    if (!f) { snprintf(msg, msg_n, "could not read the file"); return 0; }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (unsigned long)n > GD_MAX_FILE || !(data = (char *)malloc((size_t)n))) {
        fclose(f);
        snprintf(msg, msg_n, "not a driver file");
        return 0;
    }
    got = fread(data, 1, (size_t)n, f);
    fclose(f);
    if (!elf_arm64((const unsigned char *)data, got)) {
        free(data);
        snprintf(msg, msg_n, "not a 64-bit ARM library");
        return 0;
    }
    if (!plain_name(shown, leaf, sizeof leaf)) snprintf(leaf, sizeof leaf, "driver.so");
    for (char *c = leaf; *c; c++)       /* it goes into meta.json below */
        if (*c == '"' || *c == '\\') *c = '_';
    snprintf(lib, lib_n, "%s", leaf);
    snprintf(name, name_n, "%s", leaf);
    {
        char *dot = strrchr(name, '.');
        if (dot && dot != name) *dot = 0;
    }
    snprintf(path, sizeof path, "%s/%s", stage, lib);
    ok = write_bytes(path, data, got);
    free(data);
    if (!ok) { snprintf(msg, msg_n, "could not write to app storage"); return 0; }
    snprintf(json, sizeof json, "{\"name\":\"%s\",\"driverVersion\":\"\",\"libraryName\":\"%s\"}", name, lib);
    snprintf(path, sizeof path, "%s/meta.json", stage);
    if (!write_bytes(path, json, strlen(json))) { snprintf(msg, msg_n, "could not write to app storage"); return 0; }
    return 1;
}

/* `file` is a copy the picker made; `shown` the name the player saw. Returns 1
 * and selects nothing: choosing it is the player's next step. */
int nfl2k5_gpu_driver_install(const char *file, const char *shown, char *msg, size_t msg_n)
{
    char stage[640], final[640], id[64], name[64] = { 0 }, version[48] = { 0 }, lib[96] = { 0 };
    char head[4] = { 0 };
    FILE *f = fopen(file, "rb");
    int ok;
    msg[0] = 0;
    if (!f) { snprintf(msg, msg_n, "could not read the file"); return 0; }
    if (fread(head, 1, 4, f) != 4) head[0] = 0;
    fclose(f);
    gd_path(stage, sizeof stage, ".incoming");
    remove_dir(stage);
    mkdir(stage, 0700);
    if (!memcmp(head, "PK", 2))
        ok = install_zip(file, stage, name, sizeof name, version, sizeof version, lib, sizeof lib, msg, msg_n);
    else if (!memcmp(head, "\177ELF", 4))
        ok = install_so(file, shown, stage, name, sizeof name, lib, sizeof lib, msg, msg_n);
    else {
        ok = 0;
        snprintf(msg, msg_n, "pick a driver .zip or .so");
    }
    if (ok) {
        if (!name[0]) snprintf(name, sizeof name, "%s", lib);
        make_id(name, version, id, sizeof id);
        snprintf(final, sizeof final, "%s/%s", gd_base(), id);
        remove_dir(final);          /* the same driver again replaces it */
        ok = rename(stage, final) == 0;
        if (!ok) snprintf(msg, msg_n, "could not write to app storage");
    }
    if (!ok) remove_dir(stage);
    gd_scan();
    fprintf(stderr, "[GPU-DRIVER] install %s: %s%s\n", shown ? shown : file, ok ? "ok " : "refused: ", ok ? name : msg);
    if (ok) snprintf(msg, msg_n, "%s %s", name, version);
    return ok;
}

static void gd_toast(const char *a, const char *b)
{
    xbox_PresentToast("GPU DRIVER", a, b, 6000);
}

/* ---- Options > GPU Driver (src/nfl2k5_video_menu.c) -------------------------
 * Row 0 is the list: System Driver, then each installed driver. */
enum { GD_ROW_CHOICE, GD_ROW_NOW, GD_ROW_ADD, GD_ROW_REMOVE, GD_ROW_NOTE, GD_ROWS };
static const char *const k_gd_labels[GD_ROWS] = { "GPU Driver", "In Use Now", "Add Driver", "Remove Driver", "Note" };

int nfl2k5_gpu_rows(void)
{
    /* the screen is built once at start; on a phone boot has run by now */
    if (!s_count) {
        gd_scan();
        gd_read_line("selected", s_selected, sizeof s_selected);
    }
    fprintf(stderr, "[GPU-DRIVER] menu: %d installed, next start %s\n", s_count, s_selected[0] ? s_selected : "System Driver");
    return GD_ROWS;
}
const char *nfl2k5_gpu_row_label(int r) { return r >= 0 && r < GD_ROWS ? k_gd_labels[r] : ""; }
int nfl2k5_gpu_row_toggle(int r) { (void)r; return 0; }

int nfl2k5_gpu_row_values(int r)
{
    int adreno = nfl2k5_gpu_driver_is_adreno();
    if (r == GD_ROW_CHOICE) return adreno ? s_count + 1 : 1;
    if (r == GD_ROW_ADD) return adreno ? 2 : 1;
    if (r == GD_ROW_REMOVE) return adreno && s_selected[0] ? 2 : 1;
    return 1;
}

int nfl2k5_gpu_row_get(int r)
{
    if (r == GD_ROW_CHOICE) {
        int i = gd_find(s_selected);
        return i < 0 ? 0 : i + 1;
    }
    return 0;
}

#if defined(__ANDROID__)
extern void nfl2k5_android_pick_driver(void);       /* android_settings.c */
#else
static void nfl2k5_android_pick_driver(void)        /* a Linux test build has no picker */
{
    const char *zip = getenv("NFL2K5_GPU_DRIVER_TEST_ZIP");
    char msg[160];
    if (zip) {
        int ok = nfl2k5_gpu_driver_install(zip, zip, msg, sizeof msg);
        gd_toast(ok ? "Added:" : "Not added:", msg);
    }
}
#endif

void nfl2k5_gpu_row_set(int r, int v)
{
    if (r == GD_ROW_CHOICE) {
        s_remove_armed = 0;
        s_note[0] = 0;              /* the old reason has been read: a new choice */
        gd_unlink("last_fallback");
        if (v <= 0 || v > s_count) gd_select("");
        else gd_select(s_list[v - 1].id);
        fprintf(stderr, "[GPU-DRIVER] next start: %s\n", s_selected[0] ? s_selected : "System Driver");
        gd_toast("Close the game and open it again", "to use this driver");
    } else if (r == GD_ROW_ADD) {
        if (nfl2k5_gpu_driver_is_adreno()) nfl2k5_android_pick_driver();
    } else if (r == GD_ROW_REMOVE && s_selected[0]) {
        /* permanent: the first press only asks */
        if (!s_remove_armed) { s_remove_armed = 1; return; }
        {
            char dir[640];
            s_remove_armed = 0;
            snprintf(dir, sizeof dir, "%s/%s", gd_base(), s_selected);
            remove_dir(dir);
            fprintf(stderr, "[GPU-DRIVER] removed %s\n", s_selected);
            gd_select("");
            gd_scan();
            gd_toast("Driver removed", "System Driver from the next start");
        }
    }
}

void nfl2k5_gpu_row_name(int r, int v, char *b, size_t n)
{
    int adreno = nfl2k5_gpu_driver_is_adreno();
    b[0] = 0;
    switch (r) {
    case GD_ROW_CHOICE:
        if (v <= 0 || v > s_count) snprintf(b, n, "System Driver");
        else gd_label(&s_list[v - 1], b, n);
        break;
    case GD_ROW_NOW: snprintf(b, n, "%s", s_running); break;
    case GD_ROW_ADD: snprintf(b, n, "%s", adreno ? "Choose .zip or .so" : "Not available"); break;
    case GD_ROW_REMOVE:
        if (!adreno || !s_selected[0]) snprintf(b, n, "Pick a driver above first");
        else snprintf(b, n, "%s", s_remove_armed ? "Press again to delete it" : "Delete the one above");
        break;
    case GD_ROW_NOTE:
        if (!adreno) snprintf(b, n, "Needs an Adreno GPU: this phone has none");
        else if (s_note[0]) snprintf(b, n, "%s", s_note);
        else snprintf(b, n, "Changes apply the next time you open the game");
        break;
    default: break;
    }
}
