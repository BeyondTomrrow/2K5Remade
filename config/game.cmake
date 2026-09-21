file(GLOB NFL2K5_GENERATED CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/recomp/gen/*.c")
if(NOT NFL2K5_GENERATED)
  message(FATAL_ERROR "Run tools/analyze.ps1 -Recompile before building the game.")
endif()
add_executable(NFL2K5 WIN32 src/main.c src/recomp_manual.c ${NFL2K5_GENERATED})
target_include_directories(NFL2K5 PRIVATE src/recomp/gen)
target_compile_definitions(NFL2K5 PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  NFL2K5_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
  RECOMP_ICALL_FEEDBACK)
target_link_libraries(NFL2K5 PRIVATE xboxrecomp d3d11 dxgi dxguid xinput winmm dbghelp bcrypt)
target_compile_options(NFL2K5 PRIVATE /bigobj /Zi)
option(NFL2K5_OPTIMIZE "Optimize generated code after startup is working" OFF)
if(NOT NFL2K5_OPTIMIZE)
  target_compile_options(NFL2K5 PRIVATE /Od)
endif()
target_link_options(NFL2K5 PRIVATE /DEBUG /LARGEADDRESSAWARE /STACK:16777216)
