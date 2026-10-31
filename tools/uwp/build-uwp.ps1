<#
.SYNOPSIS
    Configure and build the UWP package's exe into build-uwp (see docs/UWP-PORT.md).
.PARAMETER Configuration
    Release (default) or Debug.
.PARAMETER Publisher
    Package publisher; must equal the signing certificate's subject.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [string]$Publisher = 'CN=NFL2K5 Dev'
)
$ErrorActionPreference = 'Stop'
$Root = Resolve-Path "$PSScriptRoot\..\.."
. "$PSScriptRoot\uwp-env.ps1"
$build = "$Root\build-uwp"
& $UwpCMake -S "$Root\uwp" -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" `
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DNFL2K5_UWP_PUBLISHER=$Publisher" `
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
if ($LASTEXITCODE) { throw 'configure failed' }
& $UwpCMake --build $build --parallel
if ($LASTEXITCODE) { throw 'build failed' }
& $UwpPython -I "$PSScriptRoot\check-imports.py" "$build\NFL2K5.exe"
if ($LASTEXITCODE) { Write-Warning 'imports outside the UWP API set (see above): the exe would not load on Xbox' }
