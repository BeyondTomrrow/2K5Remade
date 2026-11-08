# The release links the SDL3 shared library built on the baseline distribution
# (linux/README.md) and ships it in lib/ next to the game.
set(SDL3_FOUND TRUE)
if(NOT TARGET SDL3::SDL3)
  add_library(SDL3::SDL3 SHARED IMPORTED)
  set_target_properties(SDL3::SDL3 PROPERTIES
    IMPORTED_LOCATION "${NFL2K5_SDL3_PREFIX}/lib/libSDL3.so.0"
    IMPORTED_NO_SONAME FALSE
    INTERFACE_INCLUDE_DIRECTORIES "${NFL2K5_SDL3_PREFIX}/include")
endif()
