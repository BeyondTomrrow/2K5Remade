# NFL2K5 UWP package (Windows + Xbox Dev Mode). Separate CMake project so the
# desktop build (build\Release) and Android (build-android) are untouched.
#
#   tools\uwp\build-uwp.ps1            configure + build into build-uwp
#   tools\uwp\package-uwp.ps1          lay out, pack (makeappx), sign
#
# The exe is a normal x64 PE linked as an AppContainer app against the UWP
# umbrella library (WindowsApp.lib) and the store CRT (vcruntime140_app.dll,
# from the Microsoft.VCLibs.140.00 framework package). Desktop libraries are
# not on the link line, so a call outside the UWP API set fails to link;
# tools/uwp/check-imports.py then checks the result against the App
# Certification Kit's allow-list.
cmake_minimum_required(VERSION 3.20)
project(NFL2K5_UWP LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(FATAL_ERROR "The UWP target is MSVC-only")
endif()

set(NFL2K5_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NFL2K5_UWP_PUBLISHER "CN=NFL2K5 Dev" CACHE STRING "Package publisher (must match the signing certificate subject)")
set(NFL2K5_UWP_PKG_VERSION "0.2.0.0" CACHE STRING "Package version (four parts)")

# MSVC's store CRT import libraries (msvcrt.lib -> vcruntime140_app.dll).
get_filename_component(_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_msvc_root "${_msvc_bin}/../../.." ABSOLUTE)
set(NFL2K5_UWP_CRT_LIB "${_msvc_root}/lib/x64/store")
if(NOT EXISTS "${NFL2K5_UWP_CRT_LIB}/msvcrt.lib")
  message(FATAL_ERROR "Store CRT not found at ${NFL2K5_UWP_CRT_LIB} (MSVC 'C++ Universal Windows Platform' libs)")
endif()

# Only the UWP umbrella library by default: no user32/gdi32/etc.
set(CMAKE_C_STANDARD_LIBRARIES "WindowsApp.lib" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "WindowsApp.lib" CACHE STRING "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

add_executable(NFL2K5_UWP WIN32
  ${NFL2K5_ROOT}/src/platform/uwp/uwp_main.cpp
  ${NFL2K5_ROOT}/src/platform/uwp/uwp_probe.c)
set_target_properties(NFL2K5_UWP PROPERTIES OUTPUT_NAME NFL2K5)
target_include_directories(NFL2K5_UWP PRIVATE ${NFL2K5_ROOT}/src/platform/uwp)
target_compile_definitions(NFL2K5_UWP PRIVATE
  UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS NFL2K5_UWP=1)
target_compile_options(NFL2K5_UWP PRIVATE /W3 /Zi /EHsc $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
target_link_directories(NFL2K5_UWP PRIVATE "${NFL2K5_UWP_CRT_LIB}")
target_link_options(NFL2K5_UWP PRIVATE /APPCONTAINER /DEBUG /OPT:REF /OPT:ICF
  /NODEFAULTLIB:kernel32.lib /NODEFAULTLIB:user32.lib /NODEFAULTLIB:ole32.lib)
target_link_libraries(NFL2K5_UWP PRIVATE d3d12 dxgi)

# Package manifest for the layout step.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Package.appxmanifest.in
               ${CMAKE_BINARY_DIR}/AppxManifest.xml @ONLY)

# ---------------------------------------------------------------------------
# The game itself (stage 2, docs/UWP-PORT.md). Off by default: it compiles the
# ~240 MB of generated code. tools\uwp\build-uwp.ps1 -Game turns it on.
#
# The PC sources are compiled unchanged. Win32 calls outside the UWP API set
# resolve to src/platform/uwp/uwp_win32_shim.c (it defines the __imp_ import
# slots the desktop headers reference), so nothing user32/gdi32/shell32/...
# is imported. SDL3 is the UWP-minimal build (tools/uwp/build-sdl-uwp.ps1):
# core + SDL_GPU (D3D12 on a CoreWindow swap chain) + dummy video + virtual
# joysticks, which uwp_main.cpp feeds from Windows.Gaming.Input.
option(NFL2K5_UWP_GAME "Build the game into the UWP package" OFF)
if(NFL2K5_UWP_GAME)
  set(NFL2K5_GEN_DIR "${NFL2K5_ROOT}/src/recomp/gen" CACHE PATH "Generated code")
  set(NFL2K5_LOCALS_DIR "${NFL2K5_ROOT}/build/gen-locals" CACHE PATH "Register-locals copy of the generated code")
  set(NFL2K5_FUNC_RANGES "${NFL2K5_ROOT}/build/Release/generated/nfl2k5_func_ranges.c" CACHE FILEPATH "tools/gen-func-ranges.py output")
  set(XBOXRECOMP_FFX_SDK "E:/2K5Remade-local/fidelityfx/FidelityFX-SDK-1.1.4/sdk" CACHE PATH "FidelityFX SDK folder")
  if(EXISTS "${NFL2K5_LOCALS_DIR}/recomp_0000.c")
    file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${NFL2K5_LOCALS_DIR}/*.c")
  else()
    file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${NFL2K5_GEN_DIR}/*.c")
  endif()

  find_package(SDL3 REQUIRED CONFIG)
  add_subdirectory(${NFL2K5_ROOT}/external/xboxrecomp ${CMAKE_BINARY_DIR}/xboxrecomp EXCLUDE_FROM_ALL)
  # Desktop-only libraries the xboxrecomp targets ask for (IME): dropped, the
  # shim answers the few calls instead.
  foreach(_t xbox_video xbox_dsound xbox_d3d8 xbox_kernel xbox_apu xbox_nv2a xbox_input xbox_usb platform)
    target_compile_definitions(${_t} PRIVATE NFL2K5_UWP=1)   # the few #ifdef hooks in xboxrecomp
  endforeach()
  foreach(_t xbox_video xbox_dsound xbox_d3d8 xbox_kernel)
    get_target_property(_libs ${_t} INTERFACE_LINK_LIBRARIES)
    if(_libs)
      list(REMOVE_ITEM _libs imm32 d3d11 mf)   # d3d11/mf: WindowsApp.lib has the UWP-safe entry points
      set_target_properties(${_t} PROPERTIES INTERFACE_LINK_LIBRARIES "${_libs}")
    endif()
  endforeach()

  # Generated code on its own, so the rest can be built and linked first.
  add_library(nfl2k5_gen STATIC ${NFL2K5_GENERATED})
  target_include_directories(nfl2k5_gen PRIVATE "${NFL2K5_GEN_DIR}" ${NFL2K5_ROOT}/src)
  target_link_libraries(nfl2k5_gen PRIVATE xboxrecomp)

  set(_g ${NFL2K5_ROOT}/src)
  # The interpreter source; another tree (e.g. a git-HEAD snapshot) can stand
  # in while the working copy is mid-edit.
  set(NFL2K5_UWP_INTERP_SRC "${_g}/interp/x86_interp.c" CACHE FILEPATH "x86_interp.c to compile")
  add_executable(NFL2K5_UWP_Game WIN32
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_main.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_probe.c
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_game.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_win32_shim.c
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_hwnd_test.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_mf_shim.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_selftest.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_xaml_app.cpp
    ${NFL2K5_ROOT}/src/platform/uwp/uwp_webview_host.cpp
    ${_g}/main.c ${_g}/recomp_manual.c ${_g}/nfl2k5_input_hle.c ${_g}/nfl2k5_controls.c
    ${_g}/nfl2k5_controller_setup.c ${_g}/nfl2k5_controller_art.cpp ${_g}/nfl2k5_texture_packs.cpp
    ${_g}/nfl2k5_teams.c ${_g}/nfl2k5_team_files.c ${_g}/nfl2k5_eras_capture.c ${_g}/nfl2k5_eras.c
    ${_g}/nfl2k5_eras_menu.c ${_g}/nfl2k5_eras_launch.c ${_g}/nfl2k5_eras_office.c ${_g}/nfl2k5_eras_draftshow.c
    ${_g}/nfl2k5_eras_playoffs.c ${_g}/nfl2k5_eras_awards.c ${_g}/nfl2k5_eras_rollover.c ${_g}/nfl2k5_eras_depth.c
    ${_g}/nfl2k5_eras_sheets.c ${_g}/nfl2k5_eras_draft.c ${_g}/nfl2k5_eras_draft_sheets.c
    ${_g}/nfl2k5_eras_draft_room.c ${_g}/nfl2k5_eras_module.c ${_g}/nfl2k5_coach_mode.c
    ${_g}/nfl2k5_broadcast_cam.c ${_g}/nfl2k5_ballscan.c ${_g}/nfl2k5_moment_audio.c ${_g}/nfl2k5_plugin_host.c
    ${_g}/nfl2k5_json.c ${_g}/nfl2k5_walk_diag.c ${_g}/nfl2k5_video_menu.c ${_g}/nfl2k5_exit_menu.c
    ${_g}/nfl2k5_mod_packs.c ${_g}/nfl2k5_pack_install.c ${_g}/nfl2k5_presentation.cpp
    ${_g}/nfl2k5_local_music.cpp ${_g}/nfl2k5_save_library.cpp ${_g}/nfl2k5_discord.cpp
    "${NFL2K5_UWP_INTERP_SRC}" ${_g}/interp/interp_setup.c "${NFL2K5_FUNC_RANGES}"
    ${NFL2K5_ROOT}/external/xemu-src/util/miniz/miniz.c ${_g}/nfl2k5_wma_encode.cpp
    ${_g}/presentation/portraits.cpp ${NFL2K5_ROOT}/src/platform/uwp/uwp_webview2_stub.cpp
    ${_g}/nfl2k5_sandbox.cpp ${_g}/nfl2k5_sandbox_menu.c ${_g}/nfl2k5_sandbox_world.c
    ${_g}/nfl2k5_sandbox_actors.c ${_g}/nfl2k5_sandbox_pose.c ${_g}/nfl2k5_sandbox_ui.cpp)
  set_target_properties(NFL2K5_UWP_Game PROPERTIES OUTPUT_NAME NFL2K5 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/game)
  target_include_directories(NFL2K5_UWP_Game PRIVATE "${NFL2K5_GEN_DIR}" ${_g} ${NFL2K5_ROOT}/external/xemu-src/util/miniz
    ${NFL2K5_ROOT}/src/platform/uwp ${NFL2K5_ROOT}/dependencies/webview2/sdk/build/native/include ${_g}/interp)
  # C++/WinRT coroutines (co_await on WinRT async calls) in C++17.
  set_source_files_properties(${NFL2K5_ROOT}/src/platform/uwp/uwp_webview_host.cpp PROPERTIES COMPILE_OPTIONS "/await")
  # main.c unchanged: its main() becomes nfl2k5_real_main(), started on a
  # big-stack thread by uwp_game.cpp; its WinMain is left unused.
  set_source_files_properties(${_g}/main.c PROPERTIES COMPILE_DEFINITIONS
    "SDL_MAIN_HANDLED;main=nfl2k5_real_main;WinMain=nfl2k5_desktop_WinMain")
  target_compile_definitions(NFL2K5_UWP_Game PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS NFL2K5_UWP=1 NFL2K5_UWP_GAME=1
    NFL2K5_PROJECT_ROOT="${NFL2K5_ROOT}" RECOMP_ICALL_FEEDBACK
    NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK NFL2K5_FORCE_UNBLOCK_STATE9_READY
    NFL2K5_FORCE_UNBLOCK_33660_DRAIN NFL2K5_FORCE_UNBLOCK_NETPOLL)
  target_compile_definitions(nfl2k5_gen PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS NFL2K5_PROJECT_ROOT="${NFL2K5_ROOT}" RECOMP_ICALL_FEEDBACK
    NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK NFL2K5_FORCE_UNBLOCK_STATE9_READY
    NFL2K5_FORCE_UNBLOCK_33660_DRAIN NFL2K5_FORCE_UNBLOCK_NETPOLL)
  target_compile_options(nfl2k5_gen PRIVATE /bigobj /Z7)
  target_compile_options(NFL2K5_UWP_Game PRIVATE /bigobj /Zi /FS /EHsc)
  target_link_directories(NFL2K5_UWP_Game PRIVATE "${NFL2K5_UWP_CRT_LIB}")
  target_link_options(NFL2K5_UWP_Game PRIVATE /APPCONTAINER /DEBUG /LARGEADDRESSAWARE /STACK:16777216
    /NODEFAULTLIB:kernel32.lib /NODEFAULTLIB:user32.lib /NODEFAULTLIB:ole32.lib
    /NODEFAULTLIB:pdh.lib /NODEFAULTLIB:d3d11.lib /NODEFAULTLIB:mf.lib /NODEFAULTLIB:winmm.lib)
  target_link_libraries(NFL2K5_UWP_Game PRIVATE nfl2k5_gen xboxrecomp dxgi dxguid d3d12 bcrypt)
endif()
