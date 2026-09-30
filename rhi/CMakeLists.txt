cmake_minimum_required(VERSION 3.25)
project(nfl2k5_rhi LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source grouping
set(RHI_PUBLIC_HEADERS
        include/nfl2k5_rhi.h
)

set(RHI_SOURCES
        src/rhi_common.hpp
        src/rhi_backend.hpp
        src/rhi_entry.cpp
)

# Optional Vulkan Target
find_package(Vulkan QUIET)
if (Vulkan_FOUND)
    message(STATUS "Vulkan SDK discovered. Enabling Vulkan RHI Backend.")
    list(APPEND RHI_SOURCES
            src/vulkan/vk_backend.hpp
            src/vulkan/vk_backend.cpp
    )
    add_compile_definitions(NFL2K5_ENABLE_VULKAN=1)
endif()

# Optional DirectX 12 Target (Windows Only)
if (WIN32)
    message(STATUS "Windows detected. Enabling DirectX 12 RHI Backend.")
    list(APPEND RHI_SOURCES
            src/dx12/dx12_backend.hpp
            src/dx12/dx12_backend.cpp
    )
    add_compile_definitions(NFL2K5_ENABLE_DX12=1)
endif()

# Define the static library
add_library(nfl2k5_rhi STATIC
        ${RHI_PUBLIC_HEADERS}
        ${RHI_SOURCES}
)

# Include path configuration:
# - PUBLIC: the clean C header directory
# - PRIVATE: internal implementation headers
target_include_directories(nfl2k5_rhi
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link platform-specific graphics dependencies privately
if (Vulkan_FOUND)
    target_link_libraries(nfl2k5_rhi PRIVATE Vulkan::Vulkan)
endif()

if (WIN32)
    target_link_libraries(nfl2k5_rhi PRIVATE
            d3d12.lib
            dxgi.lib
            dxguid.lib
    )
endif()

# Compiler warnings and configuration flags
if (MSVC)
    target_compile_options(nfl2k5_rhi PRIVATE /W4 /permissive- /Zc:preprocessor)
else()
    target_compile_options(nfl2k5_rhi PRIVATE -Wall -Wextra -Wpedantic)
endif()