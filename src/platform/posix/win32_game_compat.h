/*
 * Win32 calls the NFL 2K5 game sources use that xboxrecomp's
 * platform/win32_compat.h does not provide. POSIX builds only.
 *
 * Paths: the game builds paths with backslashes ("%s\\mods\\packs").
 * Every function here converts '\\' to '/' before touching the filesystem,
 * so call sites stay unchanged.
 */
#ifndef NFL2K5_WIN32_GAME_COMPAT_H
#define NFL2K5_WIN32_GAME_COMPAT_H
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

/* ---- MSVC secure CRT ------------------------------------------------- */
#define _TRUNCATE ((size_t)-1)
int _snprintf_s(char *buf, size_t size, size_t count, const char *fmt, ...);
int sprintf_s(char *buf, size_t size, const char *fmt, ...);
int strcpy_s(char *dst, size_t size, const char *src);
int strncpy_s(char *dst, size_t size, const char *src, size_t count);
int strcat_s(char *dst, size_t size, const char *src);

#define _fseeki64 fseeko
#define _ftelli64 ftello

/* ---- File attributes / directory search -------------------------------- */
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x80u
#endif
#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY 0x01u
#endif

typedef struct _WIN32_FIND_DATAA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    char     cFileName[MAX_PATH];
    char     cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd);
BOOL   FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);
BOOL   FindClose(HANDLE h);
DWORD  GetFileAttributesA(const char *path);
BOOL   CreateDirectoryA(const char *path, void *security);
BOOL   RemoveDirectoryA(const char *path);
BOOL   DeleteFileA(const char *path);
BOOL   CopyFileA(const char *from, const char *to, BOOL fail_if_exists);
#define MOVEFILE_REPLACE_EXISTING 0x1u
#define MOVEFILE_COPY_ALLOWED     0x2u
#define MOVEFILE_WRITE_THROUGH    0x8u
BOOL   MoveFileExA(const char *from, const char *to, DWORD flags);
BOOL   MoveFileA(const char *from, const char *to);
DWORD  GetModuleFileNameA(void *module, char *out, DWORD cap);

/* ---- Extended attributes / times -------------------------------------- */
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard = 0 } GET_FILEEX_INFO_LEVELS;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
BOOL GetFileAttributesExA(const char *path, GET_FILEEX_INFO_LEVELS level, void *out);
BOOL FileTimeToLocalFileTime(const FILETIME *utc, LPFILETIME local);
DWORD GetFullPathNameA(const char *path, DWORD cap, char *out, char **file_part);

/* ---- MSVC intrinsics ---------------------------------------------------- */
static inline LONG InterlockedOr(volatile LONG *p, LONG v) { return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
static inline LONG InterlockedAnd(volatile LONG *p, LONG v) { return __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST); }
static inline unsigned long _byteswap_ulong(unsigned long v) { return __builtin_bswap32((uint32_t)v); }
static inline unsigned char _BitScanForward(unsigned long *idx, unsigned long v)
{ if (!(uint32_t)v) return 0; *idx = (unsigned long)__builtin_ctz((uint32_t)v); return 1; }
static inline unsigned char _BitScanReverse(unsigned long *idx, unsigned long v)
{ if (!(uint32_t)v) return 0; *idx = 31ul - (unsigned long)__builtin_clz((uint32_t)v); return 1; }
DWORD GetCurrentDirectoryA(DWORD cap, char *out);

#ifdef __cplusplus
}
#endif
#endif
