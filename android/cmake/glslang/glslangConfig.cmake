# Android: glslang is built from source by android/CMakeLists.txt.
if(TARGET glslang)
  set(glslang_FOUND TRUE)
else()
  set(glslang_FOUND FALSE)
endif()
