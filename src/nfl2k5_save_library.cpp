#include <windows.h>
#ifdef _WIN32
#include <bcrypt.h>
#include <shellapi.h>   /* ShellExecuteA: F1 > System > Open the saves folder */
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sys/random.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "nfl2k5_save_library.h"
#include "miniz.h"

namespace {

static std::string g_save_root;
static HANDLE g_worker;

static const unsigned char kNfl2k5SaveKey[16] = {
    0x72, 0x2E, 0x75, 0x65, 0xFB, 0x84, 0x1B, 0x09,
    0xE9, 0x38, 0xDA, 0x75, 0x63, 0x93, 0xFF, 0x80
};

struct SaveKind {
    const char *code;
    const char *folder;
    const char *fallback;
};

static const SaveKind kKinds[] = {
    {"FXG", "franchise", "Franchise"},
    {"TMM", "rosters",   "Roster"},
    {"STG", "settings",  "Settings"},
    {"USR", "vip",       "VIP"},
};

static std::string join(const std::string &a, const std::string &b)
{
    if (a.empty()) return b;
    if (a.back() == '\\' || a.back() == '/') return a + b;
    return a + "\\" + b;
}

static bool is_dir(const std::string &path)
{
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool is_file(const std::string &path)
{
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void make_dirs(const std::string &path)
{
    if (path.empty() || is_dir(path)) return;
    size_t p = path.find_last_of("\\/");
    if (p != std::string::npos) make_dirs(path.substr(0, p));
    CreateDirectoryA(path.c_str(), NULL);
}

static std::vector<std::string> dirs(const std::string &path)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(path, "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, ".."))
            out.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

static bool valid_container_id(const std::string &s)
{
    if (s.size() != 12) return false;
    for (char c : s) if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

static std::vector<unsigned char> read_bytes(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamoff n = f.tellg();
    if (n <= 0) return {};
    std::vector<unsigned char> v((size_t)n);
    f.seekg(0);
    if (!f.read((char *)v.data(), n)) return {};
    return v;
}

#ifdef _WIN32
static bool hmac_sha1(const std::vector<unsigned char> &data,
                      const unsigned char key[16], unsigned char out[20])
{
    BCRYPT_ALG_HANDLE alg = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM,
                                                   NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0)
        status = BCryptHash(alg, const_cast<PUCHAR>(key), 16,
                            const_cast<PUCHAR>(data.data()), (ULONG)data.size(),
                            out, 20);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return status >= 0;
}
#else
static bool hmac_sha1(const std::vector<unsigned char> &data,
                      const unsigned char key[16], unsigned char out[20])
{
    unsigned int len = 0;
    return HMAC(EVP_sha1(), key, 16, data.data(), data.size(), out, &len) && len == 20;
}
#endif

static bool write_atomic_bytes(const std::string &path,
                               const unsigned char *data, size_t size)
{
    std::string tmp = path + ".tmp";
    DeleteFileA(tmp.c_str());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write((const char *)data, (std::streamsize)size)) {
            DeleteFileA(tmp.c_str());
            return false;
        }
    }
    if (!MoveFileExA(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

static void migrate_udata_signatures(const std::string &udata)
{
    /* The recompiled PC runtime exposes an all-zero XboxSignatureKey to the
     * title.  Console/community saves use the retail title key.  Normalize
     * valid retail signatures to the runtime key when installing them so
     * native PC saves and imported saves can be loaded in the same session. */
    static const unsigned char runtime_zero_key[16] = {};
    for (const std::string &id : dirs(udata)) {
        if (!valid_container_id(id)) continue;
        std::string dir = join(udata, id);
        std::string dat_path = join(dir, "SAVEGAME.DAT");
        std::string extra_path = join(dir, "EXTRA");
        std::vector<unsigned char> dat = read_bytes(dat_path);
        std::vector<unsigned char> extra = read_bytes(extra_path);
        unsigned char retail[20], runtime[20];
        if (dat.empty() || extra.size() != 20 ||
            !hmac_sha1(dat, runtime_zero_key, runtime)) continue;
        if (!memcmp(extra.data(), runtime, sizeof runtime)) continue;
        if (!hmac_sha1(dat, kNfl2k5SaveKey, retail) ||
            memcmp(extra.data(), retail, sizeof retail)) {
            fprintf(stderr, "[SAVE-LIBRARY] signature does not match NFL 2K5 or the PC runtime key: %s\n",
                    id.c_str());
            continue;
        }
        if (write_atomic_bytes(extra_path, runtime, sizeof runtime))
            fprintf(stderr, "[SAVE-LIBRARY] converted retail signature for PC runtime: %s\n", id.c_str());
    }
}

static bool valid_xbox_extra(const std::string &path)
{
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return false;
    return d.nFileSizeHigh == 0 && d.nFileSizeLow == 20;
}

static std::string read_utf16_value(const std::string &path, const char *prefix)
{
    std::vector<unsigned char> b = read_bytes(path);
    std::string s;
    size_t first = b.size() >= 2 && b[0] == 0xff && b[1] == 0xfe ? 2 : 0;
    for (size_t i = first; i + 1 < b.size(); i += 2) {
        unsigned ch = b[i] | (unsigned(b[i + 1]) << 8);
        if (!ch) break;
        s.push_back(ch < 128 ? char(ch) : '_');
    }
    if (prefix && s.rfind(prefix, 0) == 0) s.erase(0, strlen(prefix));
    return s;
}

static bool write_utf16(const std::string &path, const std::string &text, bool bom)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (bom) { f.put((char)0xff); f.put((char)0xfe); }
    for (unsigned char c : text) { f.put((char)c); f.put(0); }
    f.put(0); f.put(0);
    return !!f;
}

static std::string trim(std::string s)
{
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

static std::string safe_name(std::string s, const char *fallback)
{
    s = trim(s);
    static const char *bad = "<>:\"/\\|?*";
    for (char &c : s) if ((unsigned char)c < 32 || strchr(bad, c)) c = '_';
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = fallback;
    if (s.size() > 80) s.resize(80);
    return s;
}

static const SaveKind *kind_for_code(const std::string &code)
{
    for (const auto &k : kKinds) if (code == k.code) return &k;
    /* Community roster converters use ROS; NFL 2K5's own Team Management
     * saves use TMM.  Both belong in the rosters library. */
    if (code == "ROS") return &kKinds[1];
    return nullptr;
}

static std::map<std::string, std::string> read_manifest(const std::string &dir)
{
    std::map<std::string, std::string> m;
    std::ifstream f(join(dir, ".nfl2k5-save.ini"));
    for (std::string line; std::getline(f, line);) {
        size_t p = line.find('=');
        if (p != std::string::npos) m[trim(line.substr(0, p))] = trim(line.substr(p + 1));
    }
    return m;
}

static bool write_manifest(const std::string &dir, const std::string &id,
                           const std::string &type, const std::string &name)
{
    {   /* unchanged: leave the file alone (this ran every 2.5 s for every
         * save, each time a write-through replace -- disk traffic during play) */
        auto m = read_manifest(dir);
        if (m["format"] == "1" && m["container"] == id && m["type"] == trim(type) && m["name"] == trim(name))
            return true;
    }
    std::string tmp = join(dir, ".nfl2k5-save.ini.tmp");
    std::ofstream f(tmp, std::ios::trunc);
    if (!f) return false;
    f << "format=1\ncontainer=" << id << "\ntype=" << type
      << "\nname=" << name << "\n";
    f.close();
    std::string dst = join(dir, ".nfl2k5-save.ini");
    if (!MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

static bool same_file(const std::string &a, const std::string &b)
{
    WIN32_FILE_ATTRIBUTE_DATA x{}, y{};
    if (!GetFileAttributesExA(a.c_str(), GetFileExInfoStandard, &x) ||
        !GetFileAttributesExA(b.c_str(), GetFileExInfoStandard, &y)) return false;
    return x.nFileSizeHigh == y.nFileSizeHigh && x.nFileSizeLow == y.nFileSizeLow &&
           x.ftLastWriteTime.dwHighDateTime == y.ftLastWriteTime.dwHighDateTime &&
           x.ftLastWriteTime.dwLowDateTime == y.ftLastWriteTime.dwLowDateTime;
}

static unsigned long long mtime(const std::string &path)
{
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32) |
           d.ftLastWriteTime.dwLowDateTime;
}

static bool copy_atomic(const std::string &src, const std::string &dst)
{
    if (!is_file(src) || same_file(src, dst)) return true;
    make_dirs(dst.substr(0, dst.find_last_of("\\/")));
    std::string tmp = dst + ".tmp";
    DeleteFileA(tmp.c_str());
    if (!CopyFileA(src.c_str(), tmp.c_str(), FALSE)) return false;
    if (!MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        return false;
    }
    return true;
}

static void sync_files(const std::string &a, const std::string &b)
{
    static const char *files[] = {"SAVEGAME.DAT", "EXTRA", "TYPE", "SaveMeta.xbx"};
    for (const char *file : files) {
        std::string ap = join(a, file), bp = join(b, file);
        bool af = is_file(ap), bf = is_file(bp);
        if (!af && !bf) continue;
        if (af && (!bf || mtime(ap) >= mtime(bp))) copy_atomic(ap, bp);
        else if (bf) copy_atomic(bp, ap);
    }
}

static std::string lower(std::string s)
{
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string stem(const std::string &path)
{
    size_t slash = path.find_last_of("\\/");
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = s.find_last_of('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

static bool supported_import(const std::string &name)
{
    std::string x = lower(name);
    static const char *exts[] = {".dat", ".zip", ".max", ".psu", ".ps2", ".bin", ".img"};
    for (const char *e : exts)
        if (x.size() >= strlen(e) && x.compare(x.size() - strlen(e), strlen(e), e) == 0) return true;
    return false;
}

static bool extract_save_zip(const std::string &zip_path, const std::string &out,
                             std::string &type)
{
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zip_path.c_str(), 0)) return false;
    make_dirs(out);
    bool got_dat = false, got_extra = false;
    mz_uint64 dat_size = 0, extra_size = 0;
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
        std::string name = st.m_filename ? st.m_filename : "";
        size_t slash = name.find_last_of("\\/");
        name = slash == std::string::npos ? name : name.substr(slash + 1);
        std::string key = lower(name), dst;
        if (key == "savegame.dat") {
            if (st.m_uncomp_size < dat_size) continue;
            dst = join(out, "SAVEGAME.DAT"); got_dat = true; dat_size = st.m_uncomp_size;
        }
        else if (key == "extra") {
            /* PS2 bundles also contain a four-byte PS2 checksum named EXTRA.
             * Prefer the 20-byte Xbox HMAC emitted in the UDATA subtree. */
            if (st.m_uncomp_size < extra_size) continue;
            dst = join(out, "EXTRA"); got_extra = true; extra_size = st.m_uncomp_size;
        }
        else if (key == "type") dst = join(out, "TYPE");
        else if (key == "savemeta.xbx") dst = join(out, "SaveMeta.xbx");
        else continue;
        if (!mz_zip_reader_extract_to_file(&zip, i, dst.c_str(), 0)) {
            mz_zip_reader_end(&zip);
            return false;
        }
    }
    mz_zip_reader_end(&zip);
    if (is_file(join(out, "TYPE"))) type = read_utf16_value(join(out, "TYPE"), nullptr);
    /* NFL 2K5 Xbox saves use a 20-byte HMAC. A PS2 bundle may contain a
     * four-byte checksum with the same EXTRA filename; never install it. */
    return got_dat && got_extra && extra_size == 20;
}

#ifdef _WIN32
static bool run_converter(const std::string &converter, const std::string &input,
                          const std::string &zip_out)
{
    std::string cmd = "\"" + converter + "\" \"" + input + "\" \"-out:" + zip_out + "\"";
    std::vector<char> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(0);
    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(converter.c_str(), mutable_cmd.data(), NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    DWORD wait = WaitForSingleObject(pi.hProcess, 120000);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exit_code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return wait == WAIT_OBJECT_0 && exit_code == 0 && is_file(zip_out);
}
#else
/* POSIX: run the converter directly (no shell), give it 120 s like Windows. */
static bool run_converter(const std::string &converter, const std::string &input,
                          const std::string &zip_out)
{
    std::string exe = converter, out_arg = "-out:" + zip_out, in = input;
    for (std::string *p : {&exe, &out_arg, &in}) for (char &c : *p) if (c == '\\') c = '/';
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execl(exe.c_str(), exe.c_str(), in.c_str(), out_arg.c_str(), (char *)nullptr);
        _exit(127);
    }
    int status = 0;
    for (int waited_ms = 0;; waited_ms += 50) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r < 0) return false;
        if (waited_ms >= 120000) { kill(pid, SIGKILL); waitpid(pid, &status, 0); return false; }
        usleep(50 * 1000);
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && is_file(zip_out);
}
#endif

static std::vector<std::string> files(const std::string &path)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(path, "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
            out.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

static void remove_import_work_dir(const std::string &work)
{
    /* Work directories are created by this importer and may only contain
     * these staging files.  Never recurse through an arbitrary path here. */
    static const char *staged[] = {
        "SAVEGAME.DAT", "EXTRA", "TYPE", "SaveMeta.xbx",
        ".nfl2k5-save.ini", ".nfl2k5-save.ini.tmp"
    };
    for (const char *name : staged) DeleteFileA(join(work, name).c_str());
    RemoveDirectoryA(work.c_str());
}

static void clean_stale_import_work_dirs(const std::string &inbox)
{
    for (const std::string &name : dirs(inbox)) {
        if (name.rfind(".working-", 0) == 0)
            remove_import_work_dir(join(inbox, name));
    }
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(join(inbox, ".working-*.zip").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                DeleteFileA(join(inbox, fd.cFileName).c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

static bool archive_import(const std::string &input, const std::string &archive)
{
    if (MoveFileExA(input.c_str(), archive.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    if (!copy_atomic(input, archive)) return false;
    return DeleteFileA(input.c_str()) != FALSE;
}

static void process_import_inbox(const std::string &base)
{
    std::string inbox = join(base, "import");
    std::string root = base.substr(0, base.find_last_of("\\/"));
    std::string converter = join(join(root, "tools"), "nfl2k5-save-converter.exe");
    make_dirs(join(inbox, "processed"));
    make_dirs(join(inbox, "failed"));
    clean_stale_import_work_dirs(inbox);
    for (const std::string &file : files(inbox)) {
        if (!supported_import(file)) continue;
        std::string input = join(inbox, file);
        std::string work = join(inbox, ".working-" + std::to_string(GetTickCount64()));
        std::string zip_out = work + ".zip";
        make_dirs(work);
        std::string type;
        bool ok = false;
        if (lower(file).size() >= 4 && lower(file).compare(lower(file).size() - 4, 4, ".zip") == 0)
            ok = extract_save_zip(input, work, type);
        if (!ok && !is_file(converter))
            fprintf(stderr, "[SAVE-IMPORT] %s needs tools\\nfl2k5-save-converter.exe, which is not installed\n",
                    file.c_str());
        else if (!ok && run_converter(converter, input, zip_out))
            ok = extract_save_zip(zip_out, work, type);
        DeleteFileA(zip_out.c_str());
        if (ok) {
            std::vector<unsigned char> dat = read_bytes(join(work, "SAVEGAME.DAT"));
            if (type != "FXG" && type != "ROS" && type != "TMM")
                type = dat.size() >= 4 && !memcmp(dat.data(), "ROST", 4) ? "ROS" : "FXG";
            const SaveKind *kind = kind_for_code(type);
            if (kind) {
                std::string name = safe_name(stem(file), kind->fallback);
                std::string target = join(join(base, kind->folder), name);
                if (is_dir(target)) target += " (Imported " + std::to_string(GetTickCount64()) + ")";
                make_dirs(target);
                ok = copy_atomic(join(work, "SAVEGAME.DAT"), join(target, "SAVEGAME.DAT")) &&
                     copy_atomic(join(work, "EXTRA"), join(target, "EXTRA")) &&
                     valid_xbox_extra(join(target, "EXTRA")) &&
                     write_utf16(join(target, "TYPE"), type, false) &&
                     write_utf16(join(target, "SaveMeta.xbx"), "Name=" + name + "\r\n", true);
                if (!ok) {
                    fprintf(stderr, "[SAVE-IMPORT] staging failed for %s; original will be preserved\n",
                            file.c_str());
                    remove_import_work_dir(target);
                }
                if (ok) {
                    std::string archived = join(join(inbox, "processed"), file);
                    if (archive_import(input, archived))
                        fprintf(stderr, "[SAVE-IMPORT] converted %s -> %s/%s\n", file.c_str(), kind->folder, name.c_str());
                    else
                        fprintf(stderr, "[SAVE-IMPORT] converted %s, but could not archive the original; it will be retried\n",
                                file.c_str());
                }
            } else ok = false;
        }
        if (!ok) {
            std::string failed = join(join(inbox, "failed"), file);
            if (archive_import(input, failed))
                fprintf(stderr, "[SAVE-IMPORT] could not convert %s; preserved in import/failed\n", file.c_str());
            else
                fprintf(stderr, "[SAVE-IMPORT] could not convert or archive %s; original remains in import\n", file.c_str());
        }
        remove_import_work_dir(work);
    }
}

static std::string new_container_id(const std::string &udata)
{
    unsigned char random[6];
    for (;;) {
#ifdef _WIN32
        if (BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
#else
        if (getrandom(random, sizeof random, 0) != (ssize_t)sizeof random) {
#endif
            unsigned long long n = GetTickCount64() ^ (unsigned long long)GetCurrentProcessId() << 32;
            memcpy(random, &n, sizeof random);
        }
        char id[13];
        for (unsigned i = 0; i < 6; ++i) sprintf(id + i * 2, "%02X", random[i]);
        id[12] = 0;
        if (!is_dir(join(udata, id))) return id;
    }
}

static std::string library_dir_for(const std::string &base, const SaveKind &kind,
                                   const std::string &name, const std::string &id)
{
    std::string parent = join(base, kind.folder);
    std::string wanted = join(parent, safe_name(name, kind.fallback));
    if (!is_dir(wanted)) return wanted;
    auto m = read_manifest(wanted);
    if (m["container"] == id) return wanted;
    return wanted + " [" + id + "]";
}

/* ---- save backups and game options (2026-10-09) --------------------------
 * nfl2k5_game.ini (per-user folder): save_backups = how many earlier versions
 * of each save to keep (0 = none; default 3). Before the library copy of a
 * save is replaced by a newer one the game wrote, the old one goes to
 * saves\backups\<kind>\<name>\<date-time>\, and the oldest beyond the limit
 * are deleted. */
extern "C" char *xbox_user_path(const char *rel, char *out, size_t n);

static int game_ini_get(const char *key, int def)
{
    char p[MAX_PATH], line[256];
    FILE *f = fopen(xbox_user_path("nfl2k5_game.ini", p, sizeof p), "r");
    int v = def;
    if (!f) return def;
    while (fgets(line, sizeof line, f)) {
        char k[64]; int n;
        if (line[0] != ';' && sscanf(line, " %63[a-z_] = %d", k, &n) == 2 && !strcmp(k, key)) v = n;
    }
    fclose(f);
    return v;
}

static void game_ini_set(const char *key, int value)
{
    char p[MAX_PATH], line[256];
    std::vector<std::string> lines;
    bool found = false;
    FILE *f = fopen(xbox_user_path("nfl2k5_game.ini", p, sizeof p), "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char k[64]; int n;
            if (line[0] != ';' && sscanf(line, " %63[a-z_] = %d", k, &n) == 2 && !strcmp(k, key)) {
                snprintf(line, sizeof line, "%s = %d\n", key, value);
                found = true;
            }
            lines.push_back(line);
        }
        fclose(f);
    }
    if (!found) { snprintf(line, sizeof line, "%s = %d\n", key, value); lines.push_back(line); }
    if ((f = fopen(p, "w")) != NULL) {
        for (auto &l : lines) fputs(l.c_str(), f);
        fclose(f);
    }
}

static void remove_tree(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(dir, "*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            std::string p = join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else DeleteFileA(p.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

static void backup_previous(const std::string &lib_dir, const char *kind_folder, const std::string &name)
{
    int keep = game_ini_get("save_backups", 3);
    std::string dat = join(lib_dir, "SAVEGAME.DAT");
    if (keep <= 0 || !is_file(dat)) return;
    std::string root = join(join(join(g_save_root, "backups"), kind_folder), safe_name(name, "Save"));
    WIN32_FILE_ATTRIBUTE_DATA d{};
    SYSTEMTIME st{};
    FILETIME local{};
    char stamp[32];
    if (!GetFileAttributesExA(dat.c_str(), GetFileExInfoStandard, &d)) return;
    FileTimeToLocalFileTime(&d.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);
    snprintf(stamp, sizeof stamp, "%04u-%02u-%02u %02u.%02u.%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::string dst = join(root, stamp);
    if (is_dir(dst)) return;                         /* this version is already kept */
    make_dirs(dst);
    for (const char *file : {"SAVEGAME.DAT", "EXTRA", "TYPE", "SaveMeta.xbx", ".nfl2k5-save.ini"})
        if (is_file(join(lib_dir, file))) CopyFileA(join(lib_dir, file).c_str(), join(dst, file).c_str(), FALSE);
    fprintf(stderr, "[SAVE-LIBRARY] backed up %s (%s)\n", name.c_str(), stamp);
    std::vector<std::string> versions = dirs(root);  /* names sort by date */
    std::sort(versions.begin(), versions.end());
    while ((int)versions.size() > keep) { remove_tree(join(root, versions.front())); versions.erase(versions.begin()); }
}

/* Before anything is copied (import_library copies whichever side is newer,
 * in both directions): every save the game rewrote keeps the library version
 * it is about to replace. */
static void backup_changed(const std::string &base, const std::string &udata)
{
    for (const std::string &id : dirs(udata)) {
        if (!valid_container_id(id)) continue;
        std::string src = join(udata, id);
        if (!is_file(join(src, "SAVEGAME.DAT"))) continue;
        const SaveKind *kind = kind_for_code(trim(read_utf16_value(join(src, "TYPE"), nullptr)));
        if (!kind) continue;
        std::string name = read_utf16_value(join(src, "SaveMeta.xbx"), "Name=");
        if (name.empty()) name = std::string(kind->fallback) + " " + id;
        std::string dst = library_dir_for(base, *kind, name, id);
        if (is_file(join(dst, "SAVEGAME.DAT")) &&
            mtime(join(src, "SAVEGAME.DAT")) > mtime(join(dst, "SAVEGAME.DAT")))
            backup_previous(dst, kind->folder, name);
    }
}

static void mirror_udata(const std::string &base, const std::string &udata)
{
    for (const std::string &id : dirs(udata)) {
        if (!valid_container_id(id)) continue;
        std::string src = join(udata, id);
        if (!is_file(join(src, "SAVEGAME.DAT"))) continue;
        std::string type = read_utf16_value(join(src, "TYPE"), nullptr);
        const SaveKind *kind = kind_for_code(type);
        if (!kind) continue;
        std::string name = read_utf16_value(join(src, "SaveMeta.xbx"), "Name=");
        if (name.empty()) name = std::string(kind->fallback) + " " + id;
        std::string dst = library_dir_for(base, *kind, name, id);
        make_dirs(dst);
        /* The game saved over it: keep the version being replaced. */
        if (is_file(join(dst, "SAVEGAME.DAT")) &&
            mtime(join(src, "SAVEGAME.DAT")) > mtime(join(dst, "SAVEGAME.DAT")))
            backup_previous(dst, kind->folder, name);
        sync_files(src, dst);
        write_manifest(dst, id, type, name);
    }
}

static void import_library(const std::string &base, const std::string &udata)
{
    for (const auto &kind : kKinds) {
        std::string parent = join(base, kind.folder);
        for (const std::string &folder : dirs(parent)) {
            std::string src = join(parent, folder);
            if (!is_file(join(src, "SAVEGAME.DAT"))) continue;
            auto m = read_manifest(src);
            /* A raw payload still needs NFL 2K5's Xbox HMAC in EXTRA.  The
             * cross-platform importer creates it; never install an unsigned
             * payload here because the game would show a damaged save. */
            if (m.empty() && !valid_xbox_extra(join(src, "EXTRA"))) {
                fprintf(stderr, "[SAVE-LIBRARY] pending import (needs conversion/signing): %s\n", src.c_str());
                continue;
            }
            if (!valid_xbox_extra(join(src, "EXTRA"))) {
                fprintf(stderr, "[SAVE-LIBRARY] rejected invalid Xbox signature: %s\n", src.c_str());
                continue;
            }
            std::string id = m["container"];
            if (!valid_container_id(id)) id = new_container_id(udata);
            std::string dst = join(udata, id);
            make_dirs(dst);
            std::string effective_type = read_utf16_value(join(src, "TYPE"), nullptr);
            const SaveKind *existing_kind = kind_for_code(effective_type);
            if (!existing_kind || strcmp(existing_kind->folder, kind.folder)) effective_type = kind.code;
            if (!is_file(join(src, "TYPE"))) write_utf16(join(src, "TYPE"), effective_type, false);
            if (!is_file(join(src, "SaveMeta.xbx"))) write_utf16(join(src, "SaveMeta.xbx"), "Name=" + folder + "\r\n", true);
            sync_files(src, dst);
            write_manifest(src, id, effective_type, folder);
        }
    }
}

static void sync_once()
{
    std::string title = join(join(g_save_root, "UDATA"), "53450030");
    make_dirs(title);
    for (const auto &k : kKinds) make_dirs(join(g_save_root, k.folder));
    make_dirs(join(g_save_root, "import"));
    process_import_inbox(g_save_root);
    backup_changed(g_save_root, title);
    import_library(g_save_root, title);
    migrate_udata_signatures(title);
    mirror_udata(g_save_root, title);
}

/* Names, sizes and modification times of everything the sync looks at --
 * metadata only, no file is opened. The sync runs again only when this
 * changes (it used to re-read and re-sign every save every 2.5 s, which kept
 * a core and the disk busy during play: 2026-10-09 profile, [HITCH] reads). */
static unsigned long long fingerprint_dir(const std::string &dir, int depth)
{
    unsigned long long h = 1469598103934665603ULL;
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA(join(dir, "*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return h;
    do {
        if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
        for (const char *p = fd.cFileName; *p; ++p) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
        h = (h ^ fd.nFileSizeLow) * 1099511628211ULL;
        h = (h ^ fd.ftLastWriteTime.dwLowDateTime) * 1099511628211ULL;
        h = (h ^ fd.ftLastWriteTime.dwHighDateTime) * 1099511628211ULL;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && depth > 0)
            h ^= fingerprint_dir(join(dir, fd.cFileName), depth - 1) * 31;
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    return h;
}

static unsigned long long fingerprint()
{
    unsigned long long h = fingerprint_dir(join(join(g_save_root, "UDATA"), "53450030"), 1);
    for (const auto &k : kKinds) h = h * 31 + fingerprint_dir(join(g_save_root, k.folder), 1);
    return h * 31 + fingerprint_dir(join(g_save_root, "import"), 1);
}

static DWORD WINAPI worker(void *)
{
    unsigned long long seen = fingerprint();
    for (;;) {
        Sleep(2500);
        unsigned long long now = fingerprint();
        if (now == seen) continue;
        sync_once();
        seen = fingerprint();             /* including what the sync itself wrote */
    }
}

} // namespace

extern "C" void nfl2k5_save_library_start(const char *save_root)
{
    if (!save_root || !*save_root || g_worker) return;
    g_save_root = save_root;
    sync_once();
    g_worker = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (g_worker) CloseHandle(g_worker);
}

/* ---- F1 > System page ------------------------------------------------------ */
extern "C" void xbox_MenuInvalidate(void);
namespace {
struct XboxMenuPage {
    const char *title;
    int (*count)(void);
    void (*label)(int row, char *name, char *val, size_t n);
    void (*change)(int row, int dir);
    void (*activate)(int row);
    int (*busy)(void);
};
enum { S_BACKUPS, S_OPEN, S_AUTOSAVE, S_LIMITS, S_COUNT };
const int kBackupChoices[] = { 0, 1, 3, 5, 10 };

int sys_count() { return S_COUNT; }
void sys_label(int r, char *name, char *val, size_t n)
{
    switch (r) {
    case S_BACKUPS: {
        int k = game_ini_get("save_backups", 3);
        strcpy(name, "Save backups");
        if (k) snprintf(val, n, "keep %d per save", k); else snprintf(val, n, "Off");
        break;
    }
    case S_OPEN:     strcpy(name, "Open the saves folder"); val[0] = 0; break;
    case S_AUTOSAVE: strcpy(name, "Franchise autosave"); snprintf(val, n, "Unavailable"); break;
    case S_LIMITS:   strcpy(name, "Expanded limits (next start)");
                     snprintf(val, n, "%s", game_ini_get("expanded_limits", 1) ? "On" : "Off"); break;
    }
}
void sys_change(int r, int dir)
{
    if (r == S_BACKUPS) {
        int k = game_ini_get("save_backups", 3), i = 0;
        for (; i < 5 && kBackupChoices[i] != k; i++) {}
        i = i >= 5 ? 2 : (i + 5 + dir) % 5;
        game_ini_set("save_backups", kBackupChoices[i]);
    } else if (r == S_LIMITS) {
        game_ini_set("expanded_limits", !game_ini_get("expanded_limits", 1));
    } else return;
    xbox_MenuInvalidate();
}
void sys_activate(int r)
{
    if (r == S_OPEN) {
        char full[MAX_PATH];
        if (GetFullPathNameA(g_save_root.c_str(), MAX_PATH, full, nullptr))
#ifdef _WIN32
            ShellExecuteA(nullptr, "open", full, nullptr, nullptr, SW_SHOWNORMAL);
#else
        {
            pid_t pid = fork();     /* desktop file manager via xdg-open */
            if (pid == 0) {
                setsid();
                if (fork() == 0) { execlp("xdg-open", "xdg-open", full, (char *)nullptr); _exit(127); }
                _exit(0);
            }
            if (pid > 0) waitpid(pid, nullptr, 0);
        }
#endif
    } else if (r >= 0) sys_change(r, 1);
}
const XboxMenuPage kSystemPage = { "SYSTEM", sys_count, sys_label, sys_change, sys_activate, nullptr };
} // namespace

extern "C" void xbox_MenuAddPage(const void *p);
extern "C" void nfl2k5_save_library_menu_init(void) { xbox_MenuAddPage(&kSystemPage); }
