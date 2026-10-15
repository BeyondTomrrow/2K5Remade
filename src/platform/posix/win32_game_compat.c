/*
 * POSIX implementations for win32_game_compat.h. See that header.
 *
 * Paths arrive Windows-style ("C:\\x" never, but "mods\\packs\\X" always);
 * fix_path() turns '\\' into '/'. Matching in FindFirstFileA is
 * case-insensitive, like NTFS, because the game's own data and the
 * folders users create rarely agree on case.
 */
#define _GNU_SOURCE
#include <windows.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void fix_path(const char *in, char *out, size_t cap)
{
    size_t i;
    if (!cap) return;
    for (i = 0; in && in[i] && i + 1 < cap; i++) out[i] = in[i] == '\\' ? '/' : in[i];
    out[i] = 0;
}

/* ---- secure CRT -------------------------------------------------------- */

int _snprintf_s(char *buf, size_t size, size_t count, const char *fmt, ...)
{
    va_list ap;
    int n;
    size_t cap = size;
    if (!buf || !size) return -1;
    if (count != _TRUNCATE && count + 1 < cap) cap = count + 1;
    va_start(ap, fmt);
    n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0) { buf[0] = 0; return -1; }
    return (size_t)n >= cap ? -1 : n;          /* MSVC returns -1 on truncation */
}

int sprintf_s(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!buf || !size) return -1;
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size) { buf[0] = 0; return -1; }
    return n;
}

int strcpy_s(char *dst, size_t size, const char *src)
{
    size_t n;
    if (!dst || !size) return EINVAL;
    if (!src) { dst[0] = 0; return EINVAL; }
    n = strlen(src);
    if (n >= size) { dst[0] = 0; return ERANGE; }
    memcpy(dst, src, n + 1);
    return 0;
}

int strncpy_s(char *dst, size_t size, const char *src, size_t count)
{
    size_t n;
    if (!dst || !size) return EINVAL;
    if (!src) { dst[0] = 0; return EINVAL; }
    n = strnlen(src, count == _TRUNCATE ? size - 1 : count);
    if (n >= size) {
        if (count == _TRUNCATE) n = size - 1;
        else { dst[0] = 0; return ERANGE; }
    }
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

int strcat_s(char *dst, size_t size, const char *src)
{
    size_t have;
    if (!dst || !size || !src) return EINVAL;
    have = strnlen(dst, size);
    if (have == size) return EINVAL;
    return strcpy_s(dst + have, size - have, src);
}

/* ---- time conversion --------------------------------------------------- */

/* FILETIME counts 100 ns ticks since 1601-01-01 UTC. */
#define EPOCH_DIFF_100NS 116444736000000000ULL

static FILETIME ft_from_timespec(struct timespec ts)
{
    unsigned long long v = (unsigned long long)ts.tv_sec * 10000000ULL +
                           (unsigned long long)(ts.tv_nsec / 100) + EPOCH_DIFF_100NS;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)v;
    ft.dwHighDateTime = (DWORD)(v >> 32);
    return ft;
}

BOOL FileTimeToLocalFileTime(const FILETIME *utc, LPFILETIME local)
{
    unsigned long long v;
    time_t t;
    struct tm lt;
    if (!utc || !local) return FALSE;
    v = ((unsigned long long)utc->dwHighDateTime << 32) | utc->dwLowDateTime;
    t = (time_t)((v - EPOCH_DIFF_100NS) / 10000000ULL);
    if (!localtime_r(&t, &lt)) return FALSE;
    v += (unsigned long long)((long long)lt.tm_gmtoff * 10000000LL);
    local->dwLowDateTime = (DWORD)v;
    local->dwHighDateTime = (DWORD)(v >> 32);
    return TRUE;
}

/* ---- attributes -------------------------------------------------------- */

static DWORD attrs_from_stat(const struct stat *st)
{
    DWORD a = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (!(st->st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
    return a;
}

DWORD GetFileAttributesA(const char *path)
{
    char p[PATH_MAX];
    struct stat st;
    fix_path(path, p, sizeof p);
    if (stat(p, &st) != 0) return INVALID_FILE_ATTRIBUTES;
    return attrs_from_stat(&st);
}

BOOL GetFileAttributesExA(const char *path, GET_FILEEX_INFO_LEVELS level, void *out)
{
    char p[PATH_MAX];
    struct stat st;
    WIN32_FILE_ATTRIBUTE_DATA *d = (WIN32_FILE_ATTRIBUTE_DATA *)out;
    if (level != GetFileExInfoStandard || !d) return FALSE;
    fix_path(path, p, sizeof p);
    if (stat(p, &st) != 0) return FALSE;
    d->dwFileAttributes = attrs_from_stat(&st);
    d->ftCreationTime = ft_from_timespec(st.st_ctim);
    d->ftLastAccessTime = ft_from_timespec(st.st_atim);
    d->ftLastWriteTime = ft_from_timespec(st.st_mtim);
    d->nFileSizeHigh = (DWORD)((unsigned long long)st.st_size >> 32);
    d->nFileSizeLow = (DWORD)st.st_size;
    return TRUE;
}

DWORD GetFullPathNameA(const char *path, DWORD cap, char *out, char **file_part)
{
    char p[PATH_MAX], full[PATH_MAX];
    size_t n;
    fix_path(path, p, sizeof p);
    if (!realpath(p, full)) {
        /* Like Win32, a path that does not exist yet still resolves. */
        if (p[0] == '/') snprintf(full, sizeof full, "%s", p);
        else {
            char cwd[PATH_MAX];
            if (!getcwd(cwd, sizeof cwd)) return 0;
            snprintf(full, sizeof full, "%s/%s", cwd, p);
        }
    }
    n = strlen(full);
    if (n + 1 > cap) return (DWORD)(n + 1);
    memcpy(out, full, n + 1);
    if (file_part) {
        char *slash = strrchr(out, '/');
        *file_part = slash ? slash + 1 : out;
    }
    return (DWORD)n;
}

/* ---- directories and files -------------------------------------------- */

BOOL CreateDirectoryA(const char *path, void *security)
{
    char p[PATH_MAX];
    (void)security;
    fix_path(path, p, sizeof p);
    if (mkdir(p, 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND);
    return FALSE;
}

BOOL RemoveDirectoryA(const char *path)
{
    char p[PATH_MAX];
    fix_path(path, p, sizeof p);
    return rmdir(p) == 0;
}

BOOL DeleteFileA(const char *path)
{
    char p[PATH_MAX];
    fix_path(path, p, sizeof p);
    if (unlink(p) == 0) return TRUE;
    SetLastError(errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL CopyFileA(const char *from, const char *to, BOOL fail_if_exists)
{
    char a[PATH_MAX], b[PATH_MAX], buf[65536];
    int in, out;
    ssize_t n;
    BOOL ok = TRUE;
    fix_path(from, a, sizeof a);
    fix_path(to, b, sizeof b);
    in = open(a, O_RDONLY | O_CLOEXEC);
    if (in < 0) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    out = open(b, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
    if (out < 0) { close(in); SetLastError(errno == EEXIST ? ERROR_FILE_EXISTS : ERROR_ACCESS_DENIED); return FALSE; }
    while ((n = read(in, buf, sizeof buf)) > 0) {
        char *q = buf;
        while (n > 0) {
            ssize_t w = write(out, q, (size_t)n);
            if (w <= 0) { ok = FALSE; break; }
            q += w; n -= w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = FALSE;
    close(in);
    if (close(out) != 0) ok = FALSE;
    if (!ok) unlink(b);
    return ok;
}

BOOL MoveFileExA(const char *from, const char *to, DWORD flags)
{
    char a[PATH_MAX], b[PATH_MAX];
    struct stat st;
    fix_path(from, a, sizeof a);
    fix_path(to, b, sizeof b);
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(b, &st) == 0) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if (rename(a, b) == 0) {
        if (flags & MOVEFILE_WRITE_THROUGH) sync();
        return TRUE;
    }
    if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED) && CopyFileA(from, to, FALSE))
        return unlink(a) == 0;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL MoveFileA(const char *from, const char *to) { return MoveFileExA(from, to, 0); }

DWORD GetModuleFileNameA(void *module, char *out, DWORD cap)
{
    ssize_t n;
    (void)module;
    if (!out || !cap) return 0;
    n = readlink("/proc/self/exe", out, cap - 1);
    if (n < 0) { out[0] = 0; return 0; }
    out[n] = 0;
    return (DWORD)n;
}

/* ---- FindFirstFileA ---------------------------------------------------- */

typedef struct find_state {
    DIR *dir;
    char dir_path[PATH_MAX];
    char pattern[PATH_MAX];
} find_state;

static BOOL find_fill(find_state *fs, WIN32_FIND_DATAA *fd)
{
    struct dirent *e;
    while ((e = readdir(fs->dir)) != NULL) {
        char full[PATH_MAX * 2];
        struct stat st;
        if (fnmatch(fs->pattern, e->d_name, FNM_CASEFOLD) != 0) continue;
        memset(fd, 0, sizeof *fd);
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
        snprintf(full, sizeof full, "%s/%s", fs->dir_path, e->d_name);
        if (stat(full, &st) == 0) {
            fd->dwFileAttributes = attrs_from_stat(&st);
            fd->ftCreationTime = ft_from_timespec(st.st_ctim);
            fd->ftLastAccessTime = ft_from_timespec(st.st_atim);
            fd->ftLastWriteTime = ft_from_timespec(st.st_mtim);
            fd->nFileSizeHigh = (DWORD)((unsigned long long)st.st_size >> 32);
            fd->nFileSizeLow = (DWORD)st.st_size;
        } else {
            fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        }
        return TRUE;
    }
    return FALSE;
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd)
{
    char p[PATH_MAX];
    char *slash;
    find_state *fs;
    fix_path(pattern, p, sizeof p);
    fs = (find_state *)calloc(1, sizeof *fs);
    if (!fs) return INVALID_HANDLE_VALUE;
    slash = strrchr(p, '/');
    if (slash) {
        *slash = 0;
        snprintf(fs->dir_path, sizeof fs->dir_path, "%s", *p ? p : "/");
        snprintf(fs->pattern, sizeof fs->pattern, "%s", slash + 1);
    } else {
        snprintf(fs->dir_path, sizeof fs->dir_path, ".");
        snprintf(fs->pattern, sizeof fs->pattern, "%s", p);
    }
    if (!strcmp(fs->pattern, "*.*")) snprintf(fs->pattern, sizeof fs->pattern, "*");
    fs->dir = opendir(fs->dir_path);
    if (!fs->dir || !find_fill(fs, fd)) {
        if (fs->dir) closedir(fs->dir);
        free(fs);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)fs;
}

BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd)
{
    if (!h || h == INVALID_HANDLE_VALUE) return FALSE;
    if (find_fill((find_state *)h, fd)) return TRUE;
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL FindClose(HANDLE h)
{
    find_state *fs = (find_state *)h;
    if (!fs || h == INVALID_HANDLE_VALUE) return FALSE;
    if (fs->dir) closedir(fs->dir);
    free(fs);
    return TRUE;
}

/* ---- MSVC CRT internals used by tools/apply-gen-patches.py ------------- */
/* The generated-code patches log through fprintf(__acrt_iob_func(2), ...),
 * MSVC's spelling of stderr. (__cdecl is defined empty for POSIX builds in
 * config/game.cmake.) */
void *__acrt_iob_func(unsigned index)
{
    return index == 0 ? (void *)stdin : index == 1 ? (void *)stdout : (void *)stderr;
}

DWORD GetCurrentDirectoryA(DWORD cap, char *out)
{
    char cwd[PATH_MAX];
    size_t n;
    if (!getcwd(cwd, sizeof cwd)) return 0;
    n = strlen(cwd);
    if (n + 1 > cap) return (DWORD)(n + 1);
    memcpy(out, cwd, n + 1);
    return (DWORD)n;
}
