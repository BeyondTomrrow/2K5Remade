file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/recomp/gen/*.c")
if(NOT NFL2K5_GENERATED)
  message(FATAL_ERROR "Run tools/analyze.ps1 -Recompile before building the game.")
endif()
add_executable(NFL2K5 WIN32 src/main.c src/recomp_manual.c ${NFL2K5_GENERATED})
target_include_directories(NFL2K5 PRIVATE src/recomp/gen)
option(NFL2K5_ABI_CHECK "Diagnostic: verify ebx/esi/edi preservation across every recompiled call" OFF)
option(NFL2K5_FORCE_UNBLOCK_B09584 "EXPERIMENTAL: force the MEM32(0xB09584) poll in sub_000432C0 to always succeed (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real, not-yet-understood stall instead of fixing it" OFF)
option(NFL2K5_FORCE_UNBLOCK_BDEEF0 "EXPERIMENTAL: force the MEM32(0xBDEEF0) completion-callback wait in sub_00178150 to always succeed (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real, not-yet-understood stall (and a possibly input-gated code path) instead of fixing it" OFF)
option(NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK "EXPERIMENTAL: clear the DirectSound-buffer-style completion counter in sub_0044BB44 immediately instead of waiting for the (stubbed) APU to acknowledge it (see PROJECT_STATUS.md, 2026-09-21) -- bypasses a real deadlock in the audio path instead of fixing the APU stub" OFF)
target_compile_definitions(NFL2K5 PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  NFL2K5_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
  RECOMP_ICALL_FEEDBACK
  $<$<BOOL:${NFL2K5_ABI_CHECK}>:RECOMP_ABI_CHECK>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_B09584}>:NFL2K5_FORCE_UNBLOCK_B09584>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_BDEEF0}>:NFL2K5_FORCE_UNBLOCK_BDEEF0>
  $<$<BOOL:${NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK}>:NFL2K5_FORCE_UNBLOCK_AUDIO_LOCK>)
target_link_libraries(NFL2K5 PRIVATE xboxrecomp d3d11 dxgi dxguid xinput winmm dbghelp bcrypt)
target_compile_options(NFL2K5 PRIVATE /bigobj /Zi)
option(NFL2K5_OPTIMIZE "Optimize generated code after startup is working" OFF)
if(NOT NFL2K5_OPTIMIZE)
  target_compile_options(NFL2K5 PRIVATE /Od)
endif()
target_link_options(NFL2K5 PRIVATE /DEBUG /LARGEADDRESSAWARE /STACK:16777216)
