/* Native mod-pack selection and restart handoff.
 *
 * Installed packs live in mods\packs\<folder>.  A playable pack contains
 * pack.json, default.xbe, and native\NFL2K5.exe (the native build compiled
 * from that patched XBE).  The in-game Features > Mod Packs screen writes
 * mods\active-pack.ini and restarts into the selected executable.  The same
 * file is also honored at boot, so launching the normal desktop shortcut
 * always resumes the player's selected pack. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MP_MAX_PACKS 16
#define MP_NAME_CHARS 64

typedef struct {
    char folder[MAX_PATH];
    char name[MP_NAME_CHARS];
    char exe[MAX_PATH];
} ModPack;

static ModPack s_packs[MP_MAX_PACKS];
static int s_pack_count;
static int s_active = -1;
static char s_root[MAX_PATH];

static int mp_file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void mp_read_json_name(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    char buf[8192], *p, *q;
    size_t n;
    out[0] = 0;
    if (!f) return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    p = strstr(buf, "\"name\"");
    if (!p || !(p = strchr(p, ':')) || !(p = strchr(p, '"'))) return;
    p++;
    q = strchr(p, '"');
    if (!q) return;
    n = (size_t)(q - p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static void mp_config_path(char *out, size_t cap)
{
    snprintf(out, cap, "%s\\mods\\active-pack.ini", s_root);
}

static void mp_read_active(char *out, size_t cap)
{
    char path[MAX_PATH];
    FILE *f;
    size_t n;
    out[0] = 0;
    mp_config_path(path, sizeof(path));
    f = fopen(path, "rb");
    if (!f) return;
    if (!fgets(out, (int)cap, f)) out[0] = 0;
    fclose(f);
    n = strlen(out);
    while (n && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ' || out[n - 1] == '\t'))
        out[--n] = 0;
}

static int mp_safe_folder(const char *s)
{
    if (!s || !*s || strstr(s, "..") || strchr(s, '/') || strchr(s, '\\') || strchr(s, ':')) return 0;
    return 1;
}

static int mp_write_active(const char *folder)
{
    char path[MAX_PATH], tmp[MAX_PATH];
    FILE *f;
    mp_config_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    if (folder && *folder) fprintf(f, "%s\n", folder);
    if (fclose(f) != 0) return 0;
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return 0;
    }
    return 1;
}

static int mp_current_exe(char *out, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    return n > 0 && n < cap;
}

static int mp_same_path(const char *a, const char *b)
{
    char aa[MAX_PATH], bb[MAX_PATH];
    size_t i;
    snprintf(aa, sizeof(aa), "%s", a ? a : "");
    snprintf(bb, sizeof(bb), "%s", b ? b : "");
    for (i = 0; aa[i]; i++) if (aa[i] == '/') aa[i] = '\\';
    for (i = 0; bb[i]; i++) if (bb[i] == '/') bb[i] = '\\';
    return !_stricmp(aa, bb);
}

static void mp_retail_exe(char *out, size_t cap)
{
    snprintf(out, cap, "%s\\NFL2K5.exe", s_root);
    if (!mp_file_exists(out))
        snprintf(out, cap, "%s\\build\\Release\\NFL2K5.exe", s_root);
}

static int mp_launch(const char *exe)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH * 2];
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    snprintf(cmd, sizeof(cmd), "\"%s\" --run", exe);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, s_root, &si, &pi)) {
        fprintf(stderr, "[MODPACK] Could not launch %s (error %lu)\n", exe, (unsigned long)GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

/* Called before Xbox state is initialized. Returns nonzero when this process
 * handed off to the selected build and the caller should exit immediately. */
int nfl2k5_modpacks_boot_dispatch(const char *root)
{
    char folder[MAX_PATH], target[MAX_PATH], current[MAX_PATH];
    snprintf(s_root, sizeof(s_root), "%s", root);
    mp_read_active(folder, sizeof(folder));
    if (*folder && mp_safe_folder(folder))
        snprintf(target, sizeof(target), "%s\\mods\\packs\\%s\\native\\NFL2K5.exe", root, folder);
    else
        mp_retail_exe(target, sizeof(target));
    if (!mp_file_exists(target)) {
        if (*folder)
            fprintf(stderr, "[MODPACK] Selected pack '%s' has no native build; using retail.\n", folder);
        mp_write_active("");
        mp_retail_exe(target, sizeof(target));
    }
    if (!mp_current_exe(current, sizeof(current)) || mp_same_path(current, target)) return 0;
    fprintf(stderr, "[MODPACK] Handing off to %s\n", target);
    return mp_launch(target);
}

void nfl2k5_modpacks_init(const char *root)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH], active[MAX_PATH];
    snprintf(s_root, sizeof(s_root), "%s", root);
    s_pack_count = 0;
    s_active = -1;
    mp_read_active(active, sizeof(active));
    snprintf(pattern, sizeof(pattern), "%s\\mods\\packs\\*", root);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        ModPack *p;
        char json[MAX_PATH], xbe[MAX_PATH];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' ||
            s_pack_count >= MP_MAX_PACKS || !mp_safe_folder(fd.cFileName)) continue;
        p = &s_packs[s_pack_count];
        snprintf(p->folder, sizeof(p->folder), "%s", fd.cFileName);
        snprintf(json, sizeof(json), "%s\\mods\\packs\\%s\\pack.json", root, p->folder);
        snprintf(xbe, sizeof(xbe), "%s\\mods\\packs\\%s\\default.xbe", root, p->folder);
        snprintf(p->exe, sizeof(p->exe), "%s\\mods\\packs\\%s\\native\\NFL2K5.exe", root, p->folder);
        if (!mp_file_exists(json) || !mp_file_exists(xbe) || !mp_file_exists(p->exe)) continue;
        mp_read_json_name(json, p->name, sizeof(p->name));
        if (!p->name[0]) snprintf(p->name, sizeof(p->name), "%s", p->folder);
        if (!_stricmp(active, p->folder)) s_active = s_pack_count;
        s_pack_count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    fprintf(stderr, "[MODPACK] %d playable pack(s), active=%s\n", s_pack_count,
            s_active >= 0 ? s_packs[s_active].name : "Original ESPN NFL 2K5");
}

int nfl2k5_modpacks_count(void) { return s_pack_count; }
const char *nfl2k5_modpacks_name(int row)
{
    return row >= 0 && row < s_pack_count ? s_packs[row].name : "Mod Pack";
}
int nfl2k5_modpacks_get(int row) { return row == s_active; }

typedef struct {
    char exe[MAX_PATH];
    char rollback_folder[MAX_PATH];
    int rollback_active;
} RestartArgs;
static DWORD WINAPI mp_restart_thread(void *arg)
{
    RestartArgs *a = (RestartArgs *)arg;
    Sleep(350);
    if (mp_launch(a->exe)) ExitProcess(0);
    mp_write_active(a->rollback_folder);
    s_active = a->rollback_active;
    free(a);
    return 0;
}

void nfl2k5_modpacks_set(int row, int on)
{
    const char *folder = "";
    char exe[MAX_PATH];
    int previous = s_active;
    RestartArgs *a;
    HANDLE th;
    if (row < 0 || row >= s_pack_count) return;
    if (on) {
        if (s_active == row) return;
        folder = s_packs[row].folder;
        snprintf(exe, sizeof(exe), "%s", s_packs[row].exe);
        s_active = row;
    } else {
        if (s_active != row) return;
        mp_retail_exe(exe, sizeof(exe));
        s_active = -1;
    }
    if (!mp_write_active(folder)) {
        s_active = previous;
        fprintf(stderr, "[MODPACK] Could not save selected pack.\n");
        return;
    }
    a = (RestartArgs *)malloc(sizeof(*a));
    if (!a) {
        s_active = previous;
        mp_write_active(previous >= 0 ? s_packs[previous].folder : "");
        return;
    }
    snprintf(a->exe, sizeof(a->exe), "%s", exe);
    snprintf(a->rollback_folder, sizeof(a->rollback_folder), "%s",
             previous >= 0 ? s_packs[previous].folder : "");
    a->rollback_active = previous;
    th = CreateThread(NULL, 0, mp_restart_thread, a, 0, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        s_active = previous;
        mp_write_active(previous >= 0 ? s_packs[previous].folder : "");
        free(a);
    }
}
