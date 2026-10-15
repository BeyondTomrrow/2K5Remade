/*
 * POSIX stand-in for <windows.h>.
 *
 * Only on the include path for non-Windows builds (config/game.cmake). The
 * game's own sources were written against Win32; this routes them to
 * xboxrecomp's portable NT type vocabulary and its POSIX implementations
 * of the Win32 calls (threads, events, SRW locks, timers, files).
 * Anything still missing shows up as a compile error, on purpose.
 */
#ifndef NFL2K5_POSIX_WINDOWS_H
#define NFL2K5_POSIX_WINDOWS_H
#ifdef _WIN32
#error "src/platform/posix must not be on the Windows include path"
#endif
#include <stddef.h>
#include <stdint.h>
#include "platform/xbox_winnt.h"
#include "platform/win32_compat.h"
#include "win32_game_compat.h"
#endif
