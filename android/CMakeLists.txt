# Android build of NFL 2K5 (arm64-v8a; x86_64 for emulators).
#
# Wraps the repository's own CMake (the POSIX/Linux configuration) with what
# Android does not have: SDL3 is built from source (SDL3_SRC), OpenSSL is a
# three-function shim (openssl_shim), and the game is a shared library,
# libmain.so, that SDL's Java activity loads (android/app). See
# android/README.md for building the APK.
cmake_minimum_required(VERSION 3.20)
project(NFL2K5_Android LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

set(SDL3_SRC "C:/Android/src/SDL" CACHE PATH "SDL3 source tree")
set(GLSLANG_SRC "C:/Android/src/glslang" CACHE PATH "glslang source tree (runtime shader compiler)")

# ---- SDL3, shared (SDLActivity loads libSDL3.so) ----------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL3_SRC}" SDL3 EXCLUDE_FROM_ALL)

# ---- glslang (GLSL/HLSL -> SPIR-V at run time) -------------------------------
if(EXISTS "${GLSLANG_SRC}/CMakeLists.txt")
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
  set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory("${GLSLANG_SRC}" glslang EXCLUDE_FROM_ALL)
endif()

# ---- OpenSSL: SHA-1, SHA-256, HMAC-SHA1 --------------------------------------
add_library(nfl2k5_crypto STATIC openssl_shim/crypto.c)
target_include_directories(nfl2k5_crypto PUBLIC openssl_shim/include)
set_target_properties(nfl2k5_crypto PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(OpenSSL::Crypto ALIAS nfl2k5_crypto)

# find_package(SDL3/OpenSSL/glslang) in the game's CMake find these targets.
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl3" CACHE PATH "" FORCE)
set(glslang_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/glslang" CACHE PATH "" FORCE)

# bionic has pthreads in libc: "pthread" in link lists resolves to nothing
add_library(pthread INTERFACE)

set(NFL2K5_BUILD_GAME ON CACHE BOOL "" FORCE)
add_subdirectory(.. nfl2k5)
