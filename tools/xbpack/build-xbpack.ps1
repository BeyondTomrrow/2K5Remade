<# Build tools\xbpack\xbpack.exe (the .xbpak maker) with MSVC and the vendored zstd. #>
param([string]$Out = (Join-Path $PSScriptRoot 'xbpack.exe'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\uwp\uwp-env.ps1')
# A desktop tool: no Store libraries.
$env:LIB = ($env:LIB -split ';' | Where-Object { $_ -notmatch '\\store$' }) -join ';'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$z = Join-Path $root 'external\zstd'
$obj = Join-Path $env:TEMP 'xbpack-obj'
New-Item -ItemType Directory -Force $obj | Out-Null
$src = @(Join-Path $PSScriptRoot 'xbpack.c') + (Get-ChildItem "$z\common\*.c", "$z\compress\*.c", "$z\decompress\*.c").FullName
& cl /nologo /O2 /MT /W3 /DZSTD_DISABLE_ASM /DZSTD_MULTITHREAD=0 /D_CRT_SECURE_NO_WARNINGS `
    /I"$z" /I"$z\common" /I"$root\external\xboxrecomp\src\kernel" /Fo"$obj\\" /Fe"$Out" $src
if ($LASTEXITCODE -ne 0) { throw 'xbpack build failed' }
Write-Host "built $Out"
