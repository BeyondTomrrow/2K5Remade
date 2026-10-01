# Build tools\presentation-render\presentation-render.exe (WebView2 page-to-PNG).
# Sets up MSVC the same way tools\build.ps1 does (no vcvars).
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$vs = $null
foreach ($candidate in @('C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe', 'F:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')) {
    if (Test-Path $candidate) { $vs = & $candidate -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; if ($vs) { break } }
}
if (-not $vs) { $vs = 'F:\Program Files\Microsoft Visual Studio\2022\Community' }
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdk = @('F:\Program Files (x86)\Windows Kits\10', 'C:\Program Files (x86)\Windows Kits\10') | Where-Object { Test-Path "$_\Include" } | Select-Object -First 1
$ver = (Get-ChildItem "$sdk\Include" -Directory | Where-Object { (Test-Path "$($_.FullName)\um\Windows.h") -and (Test-Path "$sdk\Lib\$($_.Name)\um\x64\kernel32.lib") } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$sdk\bin\$ver\x64;" + $env:PATH
$env:INCLUDE = "$($msvc.FullName)\include;$sdk\Include\$ver\ucrt;$sdk\Include\$ver\shared;$sdk\Include\$ver\um;$sdk\Include\$ver\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$sdk\Lib\$ver\ucrt\x64;$sdk\Lib\$ver\um\x64"
$obj = Join-Path $root 'build\temp'
New-Item -ItemType Directory -Force $obj | Out-Null
Push-Location $PSScriptRoot
try {
    & cl.exe /nologo /EHsc /O2 /std:c++17 stackdump.cpp /Festackdump.exe "/Fo$obj\\" dbghelp.lib
    if ($LASTEXITCODE) { throw 'build failed' }
} finally { Pop-Location }
"built $PSScriptRoot\stackdump.exe"
