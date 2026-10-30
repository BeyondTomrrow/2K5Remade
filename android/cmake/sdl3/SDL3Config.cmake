# Android: SDL3 is built from source by android/CMakeLists.txt.
set(SDL3_FOUND TRUE)
if(NOT TARGET SDL3::SDL3 AND TARGET SDL3-shared)
  add_library(SDL3::SDL3 ALIAS SDL3-shared)
endif()
