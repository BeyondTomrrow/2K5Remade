# Linux x86-64 release build of NFL 2K5.
#
# Same layout as android/CMakeLists.txt: the repository's POSIX configuration,
# with the full renderer (gx on SDL_GPU/Vulkan, shaders by glslang at run time)
# instead of the early software path, the OpenSSL shim instead of libcrypto so
# the binary has no OpenSSL version to match, and libstdc++/libgcc linked
# statically. SDL3 is a shared library in lib/ (RUNPATH $ORIGIN/lib). See
# linux/README.md.
cmake_minimum_required(VERSION 3.20)
project(NFL2K5_Linux LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(NFL2K5_SDL3_PREFIX "D:/claude-overflow/linux/sdl3" CACHE PATH "SDL3 install prefix (include/, lib/libSDL3.so.0)")
set(GLSLANG_SRC "C:/Android/src/glslang" CACHE PATH "glslang source tree (runtime shader compiler)")

# ---- glslang (GLSL/HLSL -> SPIR-V at run time) -------------------------------
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory("${GLSLANG_SRC}" glslang EXCLUDE_FROM_ALL)

# ---- OpenSSL: SHA-1, SHA-256, HMAC-SHA1 --------------------------------------
add_library(nfl2k5_crypto STATIC ../android/openssl_shim/crypto.c)
target_include_directories(nfl2k5_crypto PUBLIC ../android/openssl_shim/include)
add_library(OpenSSL::Crypto ALIAS nfl2k5_crypto)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../android/cmake")
set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl3" CACHE PATH "" FORCE)
set(glslang_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../android/cmake/glslang" CACHE PATH "" FORCE)

set(XBOXRECOMP_GX_PORTABLE ON CACHE BOOL "" FORCE)
set(NFL2K5_BUILD_GAME ON CACHE BOOL "" FORCE)
add_subdirectory(.. nfl2k5)

target_compile_definitions(NFL2K5 PRIVATE NFL2K5_GX_PORTABLE=1)
set_target_properties(NFL2K5 PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/lib")
target_link_options(NFL2K5 PRIVATE -static-libstdc++ -static-libgcc -Wl,--enable-new-dtags)
