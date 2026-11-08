# Cross-compiling the Android configuration for ARM64 Linux (glibc), to test
# the Android code paths -- ARM64 MMIO traps, gx on Vulkan, glslang -- in an
# emulated ARM64 machine (android/README.md, "Testing without a device").
# The sysroot is copied from that machine (Debian 13 arm64).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(LINUX_ARM64_SYSROOT "C:/Android/vm/sysroot" CACHE PATH "Debian arm64 sysroot")
set(_ndk_bin "C:/Android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin")
set(CMAKE_C_COMPILER "${_ndk_bin}/clang.exe")
set(CMAKE_CXX_COMPILER "${_ndk_bin}/clang++.exe")
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${LINUX_ARM64_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH "${LINUX_ARM64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
