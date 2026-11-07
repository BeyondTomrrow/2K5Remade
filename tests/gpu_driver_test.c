/* Custom GPU driver logic without a GPU: install, refuse, select, remove, and the
 * trial marker's fallback (src/nfl2k5_gpu_driver.c). Each boot() stands for one
 * start of the game. The real load through libadrenotools needs an Adreno phone
 * and is not covered here. Run with an empty folder as argv[1]. */
#define GPU_DRIVER_TEST 1
#include "../src/nfl2k5_gpu_driver.c"

static int s_fail, s_pass;
static char s_toast[512];
static int s_open_calls, s_open_ok = 1;

#define CHECK(c) do { if (c) s_pass++; else { s_fail++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

void xbox_PresentToast(const char *h, const char *a, const char *b, unsigned ms)
{
    (void)ms;
    snprintf(s_toast, sizeof s_toast, "%s|%s|%s", h, a, b);
}

void *gd_test_open(const GdEntry *e, const char *dir, char *why, size_t n)
{
    char t[64];
    (void)e; (void)dir;
    s_open_calls++;
    /* the marker must be on disk before the driver is touched */
    CHECK(gd_read_line("trial", t, sizeof t) && !strcmp(t, e->id));
    if (!s_open_ok) { snprintf(why, n, "test: dlopen failed"); return NULL; }
    return (void *)0x1234;
}

static const unsigned char k_arm64[64] = { 0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0,0,0,0,0,0,0,0, 3, 0, 183, 0 };
static const unsigned char k_x86[64]   = { 0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0,0,0,0,0,0,0,0, 3, 0, 62, 0 };

static void zip(const char *path, const char *meta, const char *member, const void *data, size_t n)
{
    mz_zip_archive z;
    memset(&z, 0, sizeof z);
    unlink(path);
    if (!mz_zip_writer_init_file(&z, path, 0)) { printf("no zip writer\n"); exit(2); }
    if (meta) mz_zip_writer_add_mem(&z, "meta.json", meta, strlen(meta), MZ_DEFAULT_COMPRESSION);
    if (member) mz_zip_writer_add_mem(&z, member, data, n, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
}

static int exists(const char *a, const char *b)
{
    char p[900];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", a, b);
    return stat(p, &st) == 0;
}

static char s_cache[128];
static void boot(void)
{
    unsetenv("SDL_ANDROID_VULKAN_HANDLE");
    s_toast[0] = 0;
    s_note[0] = 0;
    snprintf(s_running, sizeof s_running, "System Driver");
    nfl2k5_gpu_driver_boot(s_cache, sizeof s_cache);
}

int main(int argc, char **argv)
{
    char tmp[600], msg[200], name[128], base[512];
    const char *good = "{\"schemaVersion\":1,\"name\":\"Turnip \\\"Test\\\"\",\"description\":\"x\",\"author\":\"y\","
                       "\"packageVersion\":\"1\",\"vendor\":\"Mesa\",\"driverVersion\":\"Vulkan 1.3.2\","
                       "\"minApi\":28,\"libraryName\":\"vulkan.ad07XX.so\"}";
    if (argc < 2) return 2;
    snprintf(base, sizeof base, "%s/gd", argv[1]);
    setenv("NFL2K5_GPU_DRIVER_DIR", base, 1);
    snprintf(tmp, sizeof tmp, "%s/x.zip", argv[1]);
    s_is_adreno = 1;

    /* nothing chosen: the unchanged path */
    boot();
    CHECK(!strcmp(s_cache, "vk_pipelines.bin"));
    CHECK(!getenv("SDL_ANDROID_VULKAN_HANDLE"));
    CHECK(s_open_calls == 0);

    /* a good zip, in a folder as some packages are */
    zip(tmp, good, "sub/vulkan.ad07XX.so", k_arm64, sizeof k_arm64);
    CHECK(nfl2k5_gpu_driver_install(tmp, "t.zip", msg, sizeof msg) == 1);
    CHECK(s_count == 1);
    CHECK(!strcmp(s_list[0].name, "Turnip \"Test\""));
    CHECK(!strcmp(s_list[0].version, "Vulkan 1.3.2"));
    CHECK(!strcmp(s_list[0].lib, "vulkan.ad07XX.so"));
    CHECK(s_selected[0] == 0);
    CHECK(nfl2k5_gpu_row_values(0) == 2);
    nfl2k5_gpu_row_name(0, 1, name, sizeof name);
    CHECK(!strcmp(name, "Turnip \"Test\" Vulkan 1.3.2"));
    nfl2k5_gpu_row_name(0, 0, name, sizeof name);
    CHECK(!strcmp(name, "System Driver"));
    /* every value stays narrow enough for the screen to draw it */
    for (int r = 0; r < GD_ROWS; r++) {
        char v[200];
        nfl2k5_gpu_row_name(r, 1, v, sizeof v);
        CHECK(strlen(v) <= GD_VALUE_CHARS);
    }

    /* the same driver again replaces it */
    CHECK(nfl2k5_gpu_driver_install(tmp, "t.zip", msg, sizeof msg) == 1);
    CHECK(s_count == 1);

    /* refused, leaving nothing behind */
    zip(tmp, NULL, "vulkan.so", k_arm64, sizeof k_arm64);
    CHECK(nfl2k5_gpu_driver_install(tmp, "nometa.zip", msg, sizeof msg) == 0 && strstr(msg, "meta.json"));
    zip(tmp, good, "vulkan.ad07XX.so", k_x86, sizeof k_x86);
    CHECK(nfl2k5_gpu_driver_install(tmp, "x86.zip", msg, sizeof msg) == 0 && strstr(msg, "64-bit ARM"));
    zip(tmp, good, "vulkan.ad07XX.so", "not a library, longer than twenty bytes", 39);
    CHECK(nfl2k5_gpu_driver_install(tmp, "text.zip", msg, sizeof msg) == 0);
    zip(tmp, good, "other.so", k_arm64, sizeof k_arm64);
    CHECK(nfl2k5_gpu_driver_install(tmp, "wrong.zip", msg, sizeof msg) == 0 && strstr(msg, "not in the zip"));
    zip(tmp, "{\"name\":\"n\",\"minApi\":99,\"libraryName\":\"a.so\"}", "a.so", k_arm64, sizeof k_arm64);
    CHECK(nfl2k5_gpu_driver_install(tmp, "api.zip", msg, sizeof msg) == 0 && strstr(msg, "API 99"));
    {
        FILE *f = fopen(tmp, "wb");
        fputs("this is not a driver at all", f);
        fclose(f);
    }
    CHECK(nfl2k5_gpu_driver_install(tmp, "note.txt", msg, sizeof msg) == 0);
    CHECK(s_count == 1);
    CHECK(!exists(base, ".incoming"));

    /* a member or library name cannot climb out of the driver's folder */
    zip(tmp, "{\"name\":\"Climb\",\"libraryName\":\"../../climb.so\"}", "../../climb.so", k_arm64, sizeof k_arm64);
    CHECK(nfl2k5_gpu_driver_install(tmp, "climb.zip", msg, sizeof msg) == 1);
    CHECK(!exists(argv[1], "climb.so"));
    CHECK(!exists(base, "climb.so"));
    CHECK(exists(base, "Climb/climb.so"));
    CHECK(s_count == 2);

    /* a bare .so, whose file name goes into its meta.json */
    {
        FILE *f = fopen(tmp, "wb");
        fwrite(k_arm64, 1, sizeof k_arm64, f);
        fclose(f);
    }
    CHECK(nfl2k5_gpu_driver_install(tmp, "my\"drv.so", msg, sizeof msg) == 1);
    CHECK(s_count == 3);
    gd_scan();
    CHECK(s_count == 3);

    /* choose it: loads, marker up, its own pipeline cache */
    {
        int t = -1;
        for (int i = 0; i < s_count; i++) if (!strncmp(s_list[i].name, "Turnip", 6)) t = i;
        CHECK(t >= 0);
        nfl2k5_gpu_row_set(0, t + 1);
        CHECK(strstr(s_toast, "open it again") != NULL);
        boot();
        CHECK(s_open_calls == 1);
        CHECK(getenv("SDL_ANDROID_VULKAN_HANDLE") && !strcmp(getenv("SDL_ANDROID_VULKAN_HANDLE"), "1234"));
        CHECK(strstr(s_cache, "vk_pipelines-Turnip") && strstr(s_cache, ".bin"));
        CHECK(exists(base, "trial"));
        char running[120];
        nfl2k5_gpu_row_name(1, 0, running, sizeof running);
        CHECK(strstr(running, "Turnip") != NULL);

        /* killed before it presented: the next start falls back and says why */
        boot();
        CHECK(s_open_calls == 1);
        CHECK(!strcmp(s_cache, "vk_pipelines.bin"));
        CHECK(!getenv("SDL_ANDROID_VULKAN_HANDLE"));
        CHECK(!exists(base, "trial"));
        CHECK(s_selected[0] == 0);
        CHECK(strstr(s_toast, "did not finish") != NULL);
        nfl2k5_gpu_row_name(4, 0, name, sizeof name);
        CHECK(strstr(name, "Last driver failed") != NULL);
        /* and stays on the System Driver, with the reason still in the menu */
        boot();
        CHECK(s_open_calls == 1 && s_selected[0] == 0);
        nfl2k5_gpu_row_name(4, 0, name, sizeof name);
        CHECK(strstr(name, "Last driver failed") != NULL);

        /* presented: it keeps its place across starts */
        nfl2k5_gpu_row_set(0, t + 1);
        boot();
        CHECK(s_open_calls == 2);
        nfl2k5_gpu_driver_frames_ok();
        CHECK(!exists(base, "trial"));
        boot();
        CHECK(s_open_calls == 3 && s_selected[0] != 0);
        nfl2k5_gpu_driver_frames_ok();

        /* will not load: System Driver in the same start, no marker left */
        s_open_ok = 0;
        boot();
        CHECK(s_open_calls == 4);
        CHECK(!strcmp(s_cache, "vk_pipelines.bin"));
        CHECK(!getenv("SDL_ANDROID_VULKAN_HANDLE"));
        CHECK(!exists(base, "trial"));
        CHECK(strstr(s_toast, "failed to load") != NULL);
        CHECK(s_selected[0] == 0);
        s_open_ok = 1;

        /* no Adreno: never opened, and the rows say so */
        nfl2k5_gpu_row_set(0, t + 1);
        s_is_adreno = 0;
        boot();
        CHECK(s_open_calls == 4);
        CHECK(strstr(s_toast, "no Adreno") != NULL);
        CHECK(!strcmp(s_cache, "vk_pipelines.bin"));
        CHECK(nfl2k5_gpu_row_values(0) == 1 && nfl2k5_gpu_row_values(2) == 1);
        nfl2k5_gpu_row_name(4, 0, name, sizeof name);
        CHECK(strstr(name, "Needs an Adreno") != NULL);
        nfl2k5_gpu_row_set(2, 1);          /* Add Driver does nothing */
        s_is_adreno = 1;

        /* removing asks first, then the files go and the System Driver returns */
        nfl2k5_gpu_row_set(0, t + 1);
        char id[64];
        snprintf(id, sizeof id, "%s", s_selected);
        nfl2k5_gpu_row_set(3, 1);
        CHECK(exists(base, id));
        nfl2k5_gpu_row_name(3, 1, name, sizeof name);
        CHECK(strstr(name, "again") != NULL);
        nfl2k5_gpu_row_set(3, 0);
        CHECK(!exists(base, id));
        CHECK(s_selected[0] == 0 && s_count == 2);

        /* a chosen driver that vanished */
        gd_write_line("selected", "gone");
        boot();
        CHECK(strstr(s_toast, "no longer installed") != NULL);
        CHECK(s_open_calls == 4);
    }
    printf("%d passed, %d failed\n", s_pass, s_fail);
    return s_fail != 0;
}
