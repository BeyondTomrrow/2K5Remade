<# Dot-source: MSVC + Windows SDK environment for the UWP build (this process only).
   Mirrors tools/build.ps1's Enable-MsvcEnvironment; sets $UwpCMake, $UwpSdkBin. #>
$vs = if ($env:NFL2K5_VS_PATH) { $env:NFL2K5_VS_PATH } else { 'F:\Program Files\Microsoft Visual Studio\2022\Community' }
if (-not (Test-Path $vs)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) { $vs = & $vswhere -latest -products '*' -property installationPath }
}
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdkVer = (Get-ChildItem "$sdkRoot\Include" -Directory | Where-Object { Test-Path "$($_.FullName)\cppwinrt\winrt\base.h" } |
           Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
if (-not $msvc -or -not $sdkVer) { throw 'MSVC or a Windows SDK with C++/WinRT headers was not found' }
$inc = "$sdkRoot\Include\$sdkVer"; $lib = "$sdkRoot\Lib\$sdkVer"
$cmakeDir = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$UwpSdkBin = "$sdkRoot\bin\$sdkVer\x64"
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$UwpSdkBin;$cmakeDir\Ninja;$env:PATH"
$env:INCLUDE = "$($msvc.FullName)\include;$inc\ucrt;$inc\shared;$inc\um;$inc\winrt;$inc\cppwinrt"
$env:LIB = "$($msvc.FullName)\lib\x64\store;$($msvc.FullName)\lib\x64;$lib\ucrt\x64;$lib\um\x64"
$env:WindowsSdkDir = "$sdkRoot\"; $env:WindowsSDKVersion = "$sdkVer\"
$UwpCMake = "$cmakeDir\CMake\bin\cmake.exe"
$UwpPython = (Get-Command python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1).Source
if (-not $UwpPython) { $UwpPython = "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe" }
Write-Host "UWP toolchain: MSVC $($msvc.Name), Windows SDK $sdkVer"
