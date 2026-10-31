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
set(NFL2K5_UWP_PKG_VERSION "0.1.0.0" CACHE STRING "Package version (four parts)")

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
