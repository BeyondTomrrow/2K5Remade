param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$Game)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = $null
foreach ($candidate in @('C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe','F:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')) {
  if (Test-Path $candidate) { $vs = & $candidate -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; if ($vs) { break } }
}
if (-not $vs) { $vs = 'F:\Program Files\Microsoft Visual Studio\2022\Community' }
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdk = @('F:\Program Files (x86)\Windows Kits\10','C:\Program Files (x86)\Windows Kits\10') | Where-Object { Test-Path "$_\Include" } | Select-Object -First 1
if (-not $msvc -or -not $sdk) { throw 'MSVC or Windows SDK files missing.' }
$sdkver = Get-ChildItem "$sdk\Include" -Directory | Where-Object { (Test-Path "$($_.FullName)\um\Windows.h") -and (Test-Path "$sdk\Lib\$($_.Name)\um\x64\kernel32.lib") } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdkver) { throw 'No complete Windows SDK detected.' }
$ver = $sdkver.Name
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$sdk\bin\$ver\x64;$ninja;" + $env:PATH
$env:INCLUDE = "$($msvc.FullName)\include;$sdk\Include\$ver\ucrt;$sdk\Include\$ver\shared;$sdk\Include\$ver\um;$sdk\Include\$ver\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$sdk\Lib\$ver\ucrt\x64;$sdk\Lib\$ver\um\x64"
$env:WindowsSdkDir = "$sdk\"
$env:WindowsSDKVersion = "$ver\"
$env:TEMP = "$root\build\temp"
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
# Fail explicitly if regeneration drops the title's reset completion hook.
if ($Game) {
  $audioSource = Get-Content -Raw -LiteralPath "$root/src/recomp/gen/recomp_0030.c"
  if (-not $audioSource.Contains('nfl2k5_ack_ac97_reset(eax + 0xFEC0010Bu)')) {
    throw 'AC97 reset hook missing after regeneration. Restore the verified reset-write patch described in docs/PROGRESS.md.'
  }
}
$gameOption = if ($Game) { 'ON' } else { 'OFF' }
& $cmake -S $root -B "$root\build\$Configuration" -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DNFL2K5_BUILD_GAME=$gameOption" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl 2>&1 | Tee-Object "$root\logs\configure-$Configuration.log"
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
if ($Game) { $buildTargets = @('--target','NFL2K5','NFL2K5_toolchain_check') }
else { $buildTargets = @() }
& $cmake --build "$root\build\$Configuration" @buildTargets --parallel 4 2>&1 | Tee-Object "$root\logs\build-$Configuration.log"
if ($LASTEXITCODE) { throw 'Build failed; see logs.' }
& "$root\build\$Configuration\NFL2K5_toolchain_check.exe"
if ($LASTEXITCODE) { throw 'Native toolchain check failed.' }
if (-not $Game -and -not (Test-Path "$root\build\$Configuration\NFL2K5_Rebuild.exe")) { throw 'Native reconstruction build missing.' }
