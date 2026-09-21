cmake_minimum_required(VERSION 3.20)
project(NFL2K5_PC LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(external/xboxrecomp)
add_executable(NFL2K5_toolchain_check tools/toolchain_check.cpp)
target_link_libraries(NFL2K5_toolchain_check PRIVATE d3d12 dxgi)
add_executable(NFL2K5_Rebuild WIN32 src/rebuild/native_main.c src/rebuild/legal_texture.c)
add_executable(NFL2K5_AssetIndex tools/asset_index.c)
target_compile_definitions(NFL2K5_AssetIndex PRIVATE _CRT_SECURE_NO_WARNINGS NFL2K5_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(NFL2K5_Rebuild PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  NFL2K5_PROJECT_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\")
target_link_libraries(NFL2K5_Rebuild PRIVATE user32 gdi32 shell32)
set_target_properties(NFL2K5_Rebuild PROPERTIES
  C_STANDARD 11
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# The game target is deliberately gated until the XBE entry point and
# generated code have been reviewed. A toolchain check is not the game.
option(NFL2K5_BUILD_GAME "Build the reviewed game integration" OFF)
if(NFL2K5_BUILD_GAME)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/game.cmake")
    message(FATAL_ERROR "Analyze original/default.xbe and supply reviewed config/game.cmake first.")
  endif()
  include(config/game.cmake)
endif()
