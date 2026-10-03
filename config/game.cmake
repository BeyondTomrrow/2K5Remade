# NFL2K5_GEN_DIR / NFL2K5_LOCALS_DIR: the generated code to build. Defaults
# are the retail game's; a mod pack build (tools/build-pack.ps1) points them
# at its own tree (build/gen-<pack>, build/gen-<pack>-locals).
set(NFL2K5_GEN_DIR "${PROJECT_SOURCE_DIR}/src/recomp/gen" CACHE PATH "Generated code (tools/analyze.ps1 -Recompile)")
set(NFL2K5_LOCALS_DIR "${PROJECT_SOURCE_DIR}/build/gen-locals" CACHE PATH "Register-locals copy (tools/gen-reg-locals.py)")
# NFL2K5_REG_LOCALS: compile tools/gen-reg-locals.py's copy of the generated
# code, in which every function keeps the guest registers it uses in locals
# instead of thread-local globals. tools/build.ps1 writes it.
option(NFL2K5_REG_LOCALS "Compile the generated code with guest registers in locals" ON)
if(NFL2K5_REG_LOCALS AND EXISTS "${NFL2K5_LOCALS_DIR}/recomp_0000.c")
  file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${NFL2K5_LOCALS_DIR}/*.c")
else()
  file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${NFL2K5_GEN_DIR}/*.c")
endif()
if(NOT NFL2K5_GENERATED)
  message(FATAL_ERROR "Run tools/analyze.ps1 -Recompile before building the game.")
endif()
add_executable(NFL2K5 WIN32 src/main.c src/recomp_manual.c src/nfl2k5_input_hle.c src/nfl2k5_video_menu.c src/nfl2k5_mod_packs.c src/nfl2k5_presentation.cpp src/nfl2k5_local_music.cpp src/nfl2k5_wma_encode.cpp src/presentation/webview2_host.cpp src/presentation/portraits.cpp src/nfl2k5.rc ${NFL2K5_GENERATED})
target_include_directories(NFL2K5 PRIVATE "${NFL2K5_GEN_DIR}" src)
# The XBE the generated code came from (empty: retail). A pack build runs that
# XBE and finds its installed pack by this hash (src/main.c).
set(NFL2K5_XBE_SHA256 "" CACHE STRING "SHA-256 of the XBE the generated code was built from (empty = retail)")
if(NFL2K5_XBE_SHA256)
  set_source_files_properties(src/main.c PROPERTIES COMPILE_DEFINITIONS "NFL2K5_XBE_SHA256=\"${NFL2K5_XBE_SHA256}\"")
endif()
# HTML presentation host (src/presentation): WebView2 SDK headers and static
# loader. The engine itself is the WebView2 runtime that ships with Windows.
set(NFL2K5_WEBVIEW2_SDK "${PROJECT_SOURCE_DIR}/dependencies/webview2/sdk/build/native")
if(NOT EXISTS "${NFL2K5_WEBVIEW2_SDK}/include/WebView2.h")
  message(FATAL_ERROR "WebView2 SDK missing: run tools/get-webview2-sdk.ps1")
endif()
target_include_directories(NFL2K5 PRIVATE "${NFL2K5_WEBVIEW2_SDK}/include")
# C++/WinRT (Windows.Graphics.Capture, Windows.UI.Composition) from the
# Windows SDK the developer prompt selected.
if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
  string(REPLACE "\\" "/" _nfl2k5_sdk "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt")
  target_include_directories(NFL2K5 PRIVATE "${_nfl2k5_sdk}")
endif()
target_link_libraries(NFL2K5 PRIVATE "${NFL2K5_WEBVIEW2_SDK}/x64/WebView2LoaderStatic.lib" version)
option(NFL2K5_ABI_CHECK "Diagnostic: verify ebx/esi/edi preservation across every recompiled call" OFF)
option(NFL2K5_COVERAGE "Diagnostic: record which recompiled functions are ever entered via a direct call, to measure what fraction of the program actually runs (see PROJECT_STATUS.md, 2026-09-22)" OFF)
option(NFL2K5_FORCE_UNBLOCK_B09584 "EXPERIMENTAL: force the MEM32(0xB09584) poll in sub_000432C0 to always succeed (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real, not-yet-understood stall instead of fixing it" OFF)
option(NFL2K5_FORCE_UNBLOCK_BDEEF0 "EXPERIMENTAL: force the MEM32(0xBDEEF0) completion-callback wait in sub_00178150 to always succeed (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real, not-yet-understood stall (and a possibly input-gated code path) instead of fixing it" OFF)
option(NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK "EXPERIMENTAL: clear the DirectSound-buffer-style completion counter in sub_0044BB44 immediately instead of waiting for the (stubbed) APU to acknowledge it (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real deadlock in the audio path instead of fixing the APU stub" OFF)
option(NFL2K5_FORCE_UNBLOCK_TASK42440 "EXPERIMENTAL: force the async boot task (type 0x42440) that gates archive/font registration in sub_00042820 to always report complete immediately (see PROJECT_STATUS.md, 2026-09-21) -- likely the same missing-vblank-signal root cause documented 2026-09-17, bypassed here instead of fixed" OFF)
option(NFL2K5_FORCE_UNBLOCK_TASK42200 "EXPERIMENTAL: force the async task submitted in sub_00042210 (nested one level inside the TASK42440 chain) to always report complete immediately (see PROJECT_STATUS.md, 2026-09-21) -- same missing-vblank-signal root cause, a second nested instance" OFF)
option(NFL2K5_FORCE_UNBLOCK_STATE9_READY "EXPERIMENTAL: force the frontend state-9 archive/font readiness bit to appear set once its tick-based wait times out (see PROJECT_STATUS.md, 2026-09-21) -- the actual disc-read submission (sub_000439B8) is never reached by anything, root cause not found this session; this only lets the state machine proceed, the underlying data still never loads" OFF)
option(NFL2K5_FORCE_UNBLOCK_33660_DRAIN "EXPERIMENTAL: force sub_00033660's MEM32(esi+8) drain-wait (called from inside the GPU wait, sub_00028DE0) to succeed after 200 retries (see PROJECT_STATUS.md, 2026-09-22) -- live cdb attaches show this field frozen at 1 across tens of thousands of retries; the real clearing site was not found this session" OFF)
option(NFL2K5_FORCE_UNBLOCK_NETPOLL "EXPERIMENTAL: force sub_00045E60's network hardware/socket readiness poll (sub_00484EAB/B6, called from XNetStartup) to a nonzero status after 50 retries (see PROJECT_STATUS.md, 2026-09-22) -- there is no real network hardware behind this poll in this recompiler, so it never resolves on its own; a new, consistently-reproducing stall discovered after updating the xboxrecomp toolchain" OFF)
target_compile_definitions(NFL2K5 PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  NFL2K5_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
  RECOMP_ICALL_FEEDBACK
  $<$<BOOL:${NFL2K5_ABI_CHECK}>:RECOMP_ABI_CHECK>
  $<$<BOOL:${NFL2K5_COVERAGE}>:RECOMP_COVERAGE>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_B09584}>:NFL2K5_FORCE_UNBLOCK_B09584>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_BDEEF0}>:NFL2K5_FORCE_UNBLOCK_BDEEF0>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK}>:NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_TASK42440}>:NFL2K5_FORCE_UNBLOCK_TASK42440>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_TASK42200}>:NFL2K5_FORCE_UNBLOCK_TASK42200>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_STATE9_READY}>:NFL2K5_FORCE_UNBLOCK_STATE9_READY>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_33660_DRAIN}>:NFL2K5_FORCE_UNBLOCK_33660_DRAIN>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_NETPOLL}>:NFL2K5_FORCE_UNBLOCK_NETPOLL>)
target_link_libraries(NFL2K5 PRIVATE xboxrecomp d3d11 dxgi dxguid xinput winmm dbghelp bcrypt)
target_compile_options(NFL2K5 PRIVATE /bigobj /Zi /FS)
option(NFL2K5_OPTIMIZE "Optimize generated code after startup is working" OFF)
if(NOT NFL2K5_OPTIMIZE)
  target_compile_options(NFL2K5 PRIVATE /Od)
endif()
target_link_options(NFL2K5 PRIVATE /DEBUG /LARGEADDRESSAWARE /STACK:16777216)
