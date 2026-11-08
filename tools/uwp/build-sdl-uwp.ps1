<#
.SYNOPSIS
    Build the UWP-minimal SDL3 (static) used by the UWP game target.
.DESCRIPTION
    Source: E:\2K5Remade-local\sdl-src-uwp (a copy of the desktop build's SDL
    source, commit 8bf3b7215 + the local NFL2K5 patches, with the UWP patches
    committed in its own local git). Install: E:\2K5Remade-local\sdl3-uwp.
    SDL_UWP_MINIMAL keeps only core + GPU (D3D12/D3D11 backends) + dummy video
    + virtual joysticks + dummy audio; see docs/UWP-PORT.md.
#>
param(
    [string]$Source = 'E:\2K5Remade-local\sdl-src-uwp',
    [string]$Build = 'E:\2K5Remade-local\sdl-build-uwp',
    [string]$Install = 'E:\2K5Remade-local\sdl3-uwp'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\uwp-env.ps1"
$opts = @(
    '-DSDL_UWP_MINIMAL=ON', '-DSDL_SHARED=OFF', '-DSDL_STATIC=ON', '-DSDL_TEST_LIBRARY=OFF',
    '-DSDL_OPENGL=OFF', '-DSDL_OPENGLES=OFF', '-DSDL_VULKAN=OFF', '-DSDL_RENDER=OFF', '-DSDL_GPU_OPENXR=OFF',
    '-DSDL_HIDAPI=OFF', '-DSDL_HAPTIC=OFF', '-DSDL_SENSOR=OFF', '-DSDL_POWER=OFF', '-DSDL_CAMERA=OFF',
    '-DSDL_DIALOG=OFF', '-DSDL_TRAY=OFF', '-DSDL_XINPUT=OFF', '-DSDL_WASAPI=OFF', '-DSDL_DISKAUDIO=OFF',
    '-DSDL_DUMMYVIDEO=ON', '-DSDL_OFFSCREEN=OFF', '-DSDL_VIRTUAL_JOYSTICK=ON', '-DSDL_DUMMYAUDIO=ON',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY'
)
& $UwpCMake -S $Source -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DCMAKE_INSTALL_PREFIX=$Install" @opts
if ($LASTEXITCODE) { throw 'SDL configure failed' }
& $UwpCMake --build $Build --parallel
if ($LASTEXITCODE) { throw 'SDL build failed' }
& $UwpCMake --install $Build | Out-Null
Write-Host "SDL3 (UWP minimal) installed to $Install"
