<#
.SYNOPSIS
    Configure and build the UWP package's exe into build-uwp (see docs/UWP-PORT.md).
.PARAMETER Configuration
    Release (default) or Debug.
.PARAMETER Publisher
    Package publisher; must equal the signing certificate's subject.
.PARAMETER Game
    Also build the game (stage 2) into build-uwp\game\NFL2K5.exe. Compiles the
    ~240 MB of generated code the first time. Needs the UWP-minimal SDL3
    (tools\uwp\build-sdl-uwp.ps1).
.PARAMETER Target
    Build only this ninja target (e.g. nfl2k5_gen, xbox_kernel).
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Publisher = 'CN=NFL2K5 Dev',
    [switch]$Game,
    [string]$Target = '',
    [int]$Jobs = 0
)
$ErrorActionPreference = 'Stop'
$Root = Resolve-Path "$PSScriptRoot\..\.."
. "$PSScriptRoot\uwp-env.ps1"
$build = "$Root\build-uwp"
& $UwpCMake -S "$Root\uwp" -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DNFL2K5_UWP_PUBLISHER=$Publisher" `
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY `
    "-DNFL2K5_UWP_GAME=$(if ($Game) { 'ON' } else { 'OFF' })" `
    -DSDL3_DIR=E:/2K5Remade-local/sdl3-uwp/cmake
if ($LASTEXITCODE) { throw 'configure failed' }
$buildArgs = @('--build', $build)
if ($Jobs -gt 0) { $buildArgs += @('--parallel', $Jobs) } else { $buildArgs += '--parallel' }
if ($Target) { $buildArgs += @('--target', $Target) }
& $UwpCMake @buildArgs
if ($LASTEXITCODE) { throw 'build failed' }
if ($Target) { return }
foreach ($exe in @("$build\NFL2K5.exe") + $(if ($Game) { @("$build\game\NFL2K5.exe") } else { @() })) {
    Write-Host "Import check: $exe"
    & $UwpPython -I "$PSScriptRoot\check-imports.py" $exe
    if ($LASTEXITCODE) { Write-Warning 'imports outside the UWP API set (see above): the exe would not load on Xbox' }
}
