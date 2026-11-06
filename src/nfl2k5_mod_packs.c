/* Mod-pack selection and restart handoff.
 *
 * Installed packs live in mods\packs\<folder>.  A playable pack contains
 * pack.json and default.xbe.  The in-game Mods screen writes
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
    p = strstr(buf, "\"display_name\"");
    if (!p) p = strstr(buf, "\"name\"");
    if (!p || !(p = strchr(p, ':')) || !(p = strchr(p, '"'))) return;
    p++;
    q = strchr(p, '"');
    if (!q) return;
    n = (size_t)(q - p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
    /* A name too wide for its column blanks the whole Mod Packs list, so keep it
     * short: "SOFTDRINK 2K28 v0.9 64 MB (experimental) v0.9" -> "... v0.9 64 MB". */
    p = strstr(out, " (");
    if (p && (q = strchr(p, ')')) != NULL) memmove(p, q + 1, strlen(q + 1) + 1);
    /* "SOFTDRINK 2K28 v0.7 v0.7" (installed packs get the version added twice) */
    q = strrchr(out, ' ');
    if (q && q > out && strstr(out, q) < q) *q = 0;
}

#ifdef NFL2K5_UWP
char *xbox_user_path(const char *rel, char *out, size_t n);
#endif

static void mp_config_path(char *out, size_t cap)
{
#ifdef NFL2K5_UWP
    /* the game folder is read-only to a UWP app: the choice is the app's own */
    xbox_user_path("mods\\active-pack.ini", out, cap);
#else
    snprintf(out, cap, "%s/mods/active-pack.ini", s_root);
#endif
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
    /* The old Widescreen pack: widescreen is built in now (Video Settings), and
     * its own native build would never get later fixes. */
    if (!_stricmp(s, "Widescreen")) return 0;
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

#ifdef _WIN32
static void mp_retail_exe(char *out, size_t cap)
{
    /* every pack runs on the main executable: the one that is running */
    if (mp_current_exe(out, (size_t)cap)) return;
    snprintf(out, cap, "%s\\NFL2K5.exe", s_root);
    if (!mp_file_exists(out))
        snprintf(out, cap, "%s\\build\\Release\\NFL2K5.exe", s_root);
}
#else
/* POSIX: every pack runs on the main executable, so the retail target is
 * whichever NFL2K5 is running (no build/Release layout, no .exe). */
static int mp_current_exe(char *out, size_t cap);
static void mp_retail_exe(char *out, size_t cap)
{
    if (!mp_current_exe(out, cap)) snprintf(out, cap, "%s/NFL2K5", s_root);
}
#endif

#ifdef _WIN32
static int mp_launch(const char *exe)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH * 2], work[MAX_PATH], *slash;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    /* this run's own pack choice must not reach the new game: it reads
     * active-pack.ini. (NFL2K5_PACK left SOFTDRINK's overlay on after turning
     * the pack off; NFL2K5_RETAIL would keep --retail after turning one on.) */
    SetEnvironmentVariableA("NFL2K5_PACK", NULL);
    SetEnvironmentVariableA("NFL2K5_RETAIL", NULL);
    snprintf(cmd, sizeof(cmd), "\"%s\" --run", exe);
    snprintf(work, sizeof(work), "%s", exe);
    slash = strrchr(work, '\\');
    if (slash) *slash = 0;
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, slash ? work : s_root, &si, &pi)) {
        fprintf(stderr, "[MODPACK] Could not launch %s (error %lu)\n", exe, (unsigned long)GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
#else
/* POSIX: run `cmd` through /bin/sh in `cwd`. wait=0 detaches; wait=1 blocks
 * and stores the exit status in *code. Returns nonzero if it started. */
#include <sys/wait.h>
#include <unistd.h>
static int mp_spawn_sh(const char *cmd, const char *cwd, int wait, DWORD *code)
{
    char dir[MAX_PATH];
    pid_t pid;
    snprintf(dir, sizeof dir, "%s", cwd);
    xbox_path_normalize(dir);
    pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        if (!wait) setsid();
        if (chdir(dir) != 0) _exit(127);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (wait) {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {}
        if (code) *code = WIFEXITED(status) ? (DWORD)WEXITSTATUS(status) : 1u;
    }
    return 1;
}

static int mp_launch(const char *exe)
{
    char cmd[MAX_PATH * 2 + 16], work[MAX_PATH], *slash;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s", exe);
    xbox_path_normalize(path);
    snprintf(cmd, sizeof(cmd), "\"%s\" --run", path);
    snprintf(work, sizeof(work), "%s", path);
    slash = strrchr(work, '/');
    if (slash) *slash = 0;
    if (!mp_spawn_sh(cmd, slash ? work : s_root, 0, NULL)) {
        fprintf(stderr, "[MODPACK] Could not launch %s\n", path);
        return 0;
    }
    return 1;
}
#endif

/* Called before Xbox state is initialized. Returns nonzero when this process
 * handed off to the selected build and the caller should exit immediately. */
int nfl2k5_modpacks_boot_dispatch(const char *root)
{
    char folder[MAX_PATH], target[MAX_PATH], current[MAX_PATH];
    snprintf(s_root, sizeof(s_root), "%s", root);
    /* NFL2K5_INTERP_PACK (testing): run that pack interpreted in this process. */
    if (getenv("NFL2K5_INTERP_PACK") && *getenv("NFL2K5_INTERP_PACK")) return 0;
    mp_read_active(folder, sizeof(folder));
    /* Every pack runs on the installed main executable.  Native pack EXEs
     * were tied to a single patched-XBE hash and could contain the original
     * developer's build path, causing error 267 on another PC. */
    if (*folder && mp_safe_folder(folder)) {
        char xbe[MAX_PATH];
        snprintf(xbe, sizeof xbe, "%s/mods/packs/%s/default.xbe", root, folder);
        if (!mp_file_exists(xbe)) {
            fprintf(stderr, "[MODPACK] Selected pack '%s' is not installed; using retail.\n", folder);
            mp_write_active("");
        }
    }
#if defined(__ANDROID__) || defined(NFL2K5_UWP)
    /* one app, no other executable to hand off to: main.c loads the pack
     * (UWP: a packaged app cannot start another exe anyway) */
    (void)target; (void)current;
    return 0;
#else
    mp_retail_exe(target, sizeof(target));
    if (!mp_current_exe(current, sizeof(current)) || mp_same_path(current, target)) return 0;
    if (!mp_file_exists(target)) return 0;      /* nothing to hand off to: keep running */
    fprintf(stderr, "[MODPACK] Handing off to %s\n", target);
    return mp_launch(target);
#endif
}

/* mods/patches drop folder: a .2k5patch placed there is installed into
 * mods/packs in the background by the game itself (src/nfl2k5_pack_install.c:
 * no Python, no helper program, the same on PC and Android), then offered
 * under Extras > Mods. On Android a .2k5patch in the phone's Download folder
 * is picked up too, so a player never has to move a 1 GB file by hand.
 * A patched .iso/.xiso still goes through the PC tool (tools/pack-install.py
 * or the bundled installer .exe) on Windows. mods/patches/<file>.installed /
 * .failed mark files already handled; delete the marker to install again. */
extern void xbox_PresentToast(const char *header, const char *line1, const char *line2, unsigned ms);
extern int nfl2k5_pack_install(const char *root, const char *disc, const char *patch,
                               const char *log_path, char *folder_out, size_t folder_cap,
                               char *err, size_t err_cap);
extern void (*g_pack_install_on_progress)(int percent, const char *stage);

static char s_installing[MAX_PATH];
static void mp_install_progress(int percent, const char *stage)
{
    static int last = -100;
    char line[128];
    if (percent < last) last = -100;
    if (percent - last < 10) return;                /* a toast every 10% */
    last = percent;
    snprintf(line, sizeof line, "%s - %d%%", stage && *stage ? stage : "Installing", percent);
    xbox_PresentToast("MOD PACKS", s_installing, line, 6000);
}

/* "SOFTDRINK_2K28" from "SOFTDRINK_2K28_v0.7-v0.7": the pack's name without
 * its version, so a new release can take over from the one it replaces. */
static void mp_pack_stem(const char *folder, char *out, size_t cap)
{
    size_t i, n = strlen(folder);
    for (i = 1; i + 1 < n; i++)
        if ((folder[i] == 'v' || folder[i] == 'V') && (folder[i - 1] == '_' || folder[i - 1] == '-') &&
                folder[i + 1] >= '0' && folder[i + 1] <= '9') { n = i - 1; break; }
    if (n >= cap) n = cap - 1;
    memcpy(out, folder, n);
    out[n] = 0;
}

static int mp_ends_with(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !_stricmp(s + a - b, ext);
}

static void mp_install_one(const char *dir, const char *name)
{
    char file[MAX_PATH], done[MAX_PATH], failed[MAX_PATH], logp[MAX_PATH], logdir[MAX_PATH];
    char folder[MAX_PATH] = "", err[512] = "";
    int ok = 0;
    snprintf(file, sizeof file, "%s/%s", dir, name);
    snprintf(done, sizeof done, "%s/mods/patches/%s.installed", s_root, name);
    snprintf(failed, sizeof failed, "%s/mods/patches/%s.failed", s_root, name);
    if (mp_file_exists(done) || mp_file_exists(failed)) return;
    snprintf(logdir, sizeof logdir, "%s/logs", s_root);
    CreateDirectoryA(logdir, NULL);
    snprintf(logp, sizeof logp, "%s/logs/patch-install-%s.log", s_root, name);
    fprintf(stderr, "[MODPACK] installing %s\n", file);
    if (mp_ends_with(name, ".2k5patch")) {
        snprintf(s_installing, sizeof s_installing, "Installing %s", name);
        xbox_PresentToast("MOD PACKS", s_installing, "This takes a few minutes; keep playing", 10000);
        g_pack_install_on_progress = mp_install_progress;
        ok = nfl2k5_pack_install(s_root, NULL, file, logp, folder, sizeof folder, err, sizeof err);
        g_pack_install_on_progress = NULL;
    } else {
#ifdef _WIN32
        /* a finished, patched disc image: the PC tool compares it with the retail disc */
        char tool[MAX_PATH], py[MAX_PATH], cmd[4 * MAX_PATH];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        snprintf(tool, sizeof tool, "%s\\tools\\NFL2K5-ModPack-Installer.exe", s_root);
        snprintf(py, sizeof py, "%s\\tools\\pack-install.py", s_root);
        if (mp_file_exists(tool))
            snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --root \"%s\" --replace --log \"%s\"", tool, file, s_root, logp);
        else if (mp_file_exists(py))
            snprintf(cmd, sizeof cmd, "python \"%s\" \"%s\" --root \"%s\" --replace --log \"%s\"", py, file, s_root, logp);
        else
            return;
        xbox_PresentToast("MOD PACKS", "Installing", name, 8000);
        memset(&si, 0, sizeof si);
        memset(&pi, 0, sizeof pi);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                           NULL, s_root, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            ok = code == 0;
            if (!ok) snprintf(err, sizeof err, "the disc-image installer failed (exit %lu); see %s", (unsigned long)code, logp);
        } else {
            /* e.g. 2: python not on PATH */
            snprintf(err, sizeof err, "the disc-image installer could not start (Windows error %lu): %s",
                     (unsigned long)GetLastError(), cmd);
        }
#else
        snprintf(err, sizeof err, "a patched disc image can only be installed on the PC; use the pack's .2k5patch here");
#endif
    }
    {
        FILE *m = fopen(ok ? done : failed, "w");
        if (m) {
            fprintf(m, "%s\nlog: %s\n", ok ? (folder[0] ? folder : "installed") : err, logp);
            fclose(m);
        }
    }
    fprintf(stderr, "[MODPACK] %s: %s\n", name, ok ? "installed" : err);
    if (!ok) {
        xbox_PresentToast("MOD PACKS", "Install failed", err, 20000);
        return;
    }
    if (folder[0]) {
        /* A new release of the pack the player already uses (or the first
         * pack at all) becomes the selected one, so the next start plays it. */
        char active[MAX_PATH], a_stem[MAX_PATH], n_stem[MAX_PATH];
        mp_read_active(active, sizeof active);
        mp_pack_stem(active, a_stem, sizeof a_stem);
        mp_pack_stem(folder, n_stem, sizeof n_stem);
        /* An experimental cut of a release (SOFTDRINK's 64 MB one gives up most of
         * the pack) is the player's choice: Extras > Mods, not this. */
        if ((!active[0] || !_stricmp(a_stem, n_stem)) && !getenv("NFL2K5_NO_PACK_AUTOSELECT") &&
                !strstr(folder, "experimental") && mp_write_active(folder)) {
            fprintf(stderr, "[MODPACK] %s is now the selected pack (was '%s')\n", folder, active);
            xbox_PresentToast("MOD PACKS", "Installed and selected:", folder,
                              20000);
            xbox_PresentToast("MOD PACKS", "Restart the game to play it", "(Extras > Mods switches packs)", 20000);
            return;
        }
    }
    xbox_PresentToast("MOD PACKS", "Installed - restart the game, then", "Extras > Mods to turn it on", 20000);
}

static void mp_scan_dir(const char *dir, int iso_too)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%s/*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (mp_ends_with(fd.cFileName, ".2k5patch") ||
                (iso_too && (mp_ends_with(fd.cFileName, ".iso") || mp_ends_with(fd.cFileName, ".xiso"))))
            mp_install_one(dir, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static DWORD WINAPI mp_patch_thread(void *arg)
{
    char dir[MAX_PATH];
    (void)arg;
    Sleep(8000);                                  /* let the game get to its menus first */
    snprintf(dir, sizeof dir, "%s/mods/patches", s_root);
    mp_scan_dir(dir, 1);
#ifdef __ANDROID__
    mp_scan_dir("/sdcard/Download", 0);
#endif
    return 0;
}

void nfl2k5_modpacks_init(const char *root)
{
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[MAX_PATH], active[MAX_PATH];
    snprintf(s_root, sizeof(s_root), "%s", root);
    s_pack_count = 0;
    s_active = -1;
    {
        char patches[MAX_PATH];
        HANDLE th;
        snprintf(patches, sizeof patches, "%s/mods/patches", root);
        CreateDirectoryA(patches, NULL);
        th = CreateThread(NULL, 0, mp_patch_thread, NULL, 0, NULL);
        if (th) CloseHandle(th);
    }
    mp_read_active(active, sizeof(active));
    snprintf(pattern, sizeof(pattern), "%s/mods/packs/*", root);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        ModPack *p;
        char json[MAX_PATH], xbe[MAX_PATH];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' ||
            s_pack_count >= MP_MAX_PACKS || !mp_safe_folder(fd.cFileName)) continue;
        p = &s_packs[s_pack_count];
        snprintf(p->folder, sizeof(p->folder), "%s", fd.cFileName);
        snprintf(json, sizeof(json), "%s/mods/packs/%s/pack.json", root, p->folder);
        snprintf(xbe, sizeof(xbe), "%s/mods/packs/%s/default.xbe", root, p->folder);
        /* Do not launch a hash-specific code pack.  The base executable
         * interprets the selected patch and remains portable across releases. */
        mp_retail_exe(p->exe, sizeof(p->exe));
        if (!mp_file_exists(json) || !mp_file_exists(xbe)) continue;
        {   /* "kind": "feature" (My NFL Eras): an additive feature pack, never an XBE pack
             * to select here (src/nfl2k5_plugin_host.c turns it on) */
            FILE *jf = fopen(json, "rb");
            char head[4096];
            size_t n = jf ? fread(head, 1, sizeof head - 1, jf) : 0;
            if (jf) fclose(jf);
            head[n] = 0;
            if (strstr(head, "\"kind\"") && strstr(head, "\"feature\"")) continue;
        }
        mp_read_json_name(json, p->name, sizeof(p->name));
        if (!p->name[0]) snprintf(p->name, sizeof(p->name), "%s", p->folder);
        if (!_stricmp(active, p->folder)) s_active = s_pack_count;
        s_pack_count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    fprintf(stderr, "[MODPACK] %d playable pack(s), active=%s\n", s_pack_count,
            s_active >= 0 ? s_packs[s_active].name : "Original ESPN NFL 2K5");
}

/* Feature packs (docs/eras/MOD-PACK-DESIGN.md 3, 9 step 8): additive, each On/Off on its own,
 * after the XBE packs in the same list. The switch is mods\features.ini "<id>=0|1", read at
 * boot (src/nfl2k5_plugin_host.c), so a change applies at the next start. */
static const struct { const char *id, *folder, *label; } k_features[] = {
    { "my-nfl-eras", "MyNFLEras", "My NFL Eras (feature pack)" },
};
#define MP_FEATURES ((int)(sizeof k_features / sizeof k_features[0]))

static int mp_feature_installed(int f)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s/mods/packs/%s/pack.json", s_root[0] ? s_root : ".", k_features[f].folder);
    return mp_file_exists(p);
}

/* the f-th installed feature pack behind list row `row`, or -1 */
static int mp_feature_row(int row)
{
    int f, k = s_pack_count;
    for (f = 0; f < MP_FEATURES; f++)
        if (mp_feature_installed(f) && k++ == row) return f;
    return -1;
}

static int mp_feature_on(int f)
{
    char p[MAX_PATH], line[256];
    FILE *fp;
    int on = 1;
    snprintf(p, sizeof p, "%s/mods/features.ini", s_root[0] ? s_root : ".");
    if (!(fp = fopen(p, "r"))) return 1;
    while (fgets(line, sizeof line, fp)) {
        char key[96], val[32];
        if (sscanf(line, " %95[^= \t] = %31s", key, val) == 2 && !_stricmp(key, k_features[f].id))
            on = !(!strcmp(val, "0") || !_stricmp(val, "off"));
    }
    fclose(fp);
    return on;
}

static void mp_feature_set(int f, int on)
{
    char p[MAX_PATH], tmp[MAX_PATH], line[256];
    FILE *in, *out;
    int done = 0;
    snprintf(p, sizeof p, "%s/mods/features.ini", s_root[0] ? s_root : ".");
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    if (!(out = fopen(tmp, "w"))) { fprintf(stderr, "[MODPACK] Could not write %s.\n", p); return; }
    if ((in = fopen(p, "r")) != NULL) {
        while (fgets(line, sizeof line, in)) {
            char key[96];
            if (sscanf(line, " %95[^= \t]", key) == 1 && !_stricmp(key, k_features[f].id)) {
                if (!done) fprintf(out, "%s=%d\n", k_features[f].id, on ? 1 : 0);
                done = 1;
                continue;
            }
            fputs(line, out);
        }
        fclose(in);
    } else fprintf(out, "; Feature packs: <id>=1 on, <id>=0 off (Features > Mod Packs; applies at the next start)\n");
    if (!done) fprintf(out, "%s=%d\n", k_features[f].id, on ? 1 : 0);
    fclose(out);
    if (!MoveFileExA(tmp, p, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); return; }
    fprintf(stderr, "[MODPACK] %s %s (applies at the next start)\n", k_features[f].label, on ? "on" : "off");
    xbox_PresentToast(k_features[f].label, on ? "On" : "Off", "Restart the game to apply", 4000);
}

int nfl2k5_modpacks_count(void)
{
    int f, n = s_pack_count;
    for (f = 0; f < MP_FEATURES; f++) n += mp_feature_installed(f);
    return n;
}
const char *nfl2k5_modpacks_name(int row)
{
    int f = mp_feature_row(row);
    if (f >= 0) return k_features[f].label;
    return row >= 0 && row < s_pack_count ? s_packs[row].name : "Mod Pack";
}
/* 1: list row `row` is an XBE pack (switching it restarts the game), not a feature pack */
int nfl2k5_modpacks_is_pack(int row)
{
    return row >= 0 && row < s_pack_count;
}
int nfl2k5_modpacks_get(int row)
{
    int f = mp_feature_row(row);
    return f >= 0 ? mp_feature_on(f) : row == s_active;
}

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
    if (mp_feature_row(row) >= 0) {
        int f = mp_feature_row(row);
        if ((mp_feature_on(f) != 0) != (on != 0)) mp_feature_set(f, on);
        return;
    }
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
#if defined(__ANDROID__) || defined(NFL2K5_UWP)
    /* An app cannot start a second copy of itself: the choice is saved and
     * applies when the game is opened again. */
    (void)a; (void)th; (void)exe;
    fprintf(stderr, "[MODPACK] selected '%s'; applies at the next start\n", folder);
    xbox_PresentToast("MOD PACKS", on ? "Selected - close and reopen the game" : "Off - close and reopen the game",
                      "to apply", 8000);
    return;
#endif
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
