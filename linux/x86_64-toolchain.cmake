# Cross-compiling the Linux x86-64 release from Windows with the Android NDK's
# clang against an Ubuntu 22.04 sysroot, so the binary needs nothing newer than
# glibc 2.35. linux/README.md has the steps. On a Linux machine with the same
# sysroot layout, set the compilers on the command line instead.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(LINUX_X64_SYSROOT "D:/claude-overflow/linux/sysroot" CACHE PATH "Ubuntu 22.04 amd64 sysroot")
set(_ndk_bin "C:/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin")
set(CMAKE_C_COMPILER "${_ndk_bin}/clang.exe")
set(CMAKE_CXX_COMPILER "${_ndk_bin}/clang++.exe")
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_SYSROOT "${LINUX_X64_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH "${LINUX_X64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
