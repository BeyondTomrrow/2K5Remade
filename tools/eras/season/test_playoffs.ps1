# Offline seeding check (M14): builds tools\eras\season\test_playoffs.c with src\nfl2k5_eras_playoffs.c (the
# toolchain environment of tools\build.ps1) and runs it on a league folder (default logs\season\home-real,
# whose results.csv holds the real 1980 scores).
#   tools\eras\season\test_playoffs.ps1 [-Package data\eras\runtime\1980] [-League logs\season\home-real\NFL2K5-PC\eras\league1]
param([string]$Package = 'data\eras\runtime\1980', [string]$League = 'logs\season\home-real\NFL2K5-PC\eras\league1')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$vs = 'F:\Program Files\Microsoft Visual Studio\2022\Community'
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdk = @('F:\Program Files (x86)\Windows Kits\10','C:\Program Files (x86)\Windows Kits\10') | Where-Object { Test-Path "$_\Include" } | Select-Object -First 1
$sdkver = Get-ChildItem "$sdk\Include" -Directory | Where-Object { (Test-Path "$($_.FullName)\um\Windows.h") -and (Test-Path "$sdk\Lib\$($_.Name)\um\x64\kernel32.lib") } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$ver = $sdkver.Name
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$sdk\bin\$ver\x64;" + $env:PATH
$env:INCLUDE = "$($msvc.FullName)\include;$sdk\Include\$ver\ucrt;$sdk\Include\$ver\shared;$sdk\Include\$ver\um;$sdk\Include\$ver\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$sdk\Lib\$ver\ucrt\x64;$sdk\Lib\$ver\um\x64"
Set-Location $root
New-Item -ItemType Directory -Force build\temp\season | Out-Null
& cl /nologo /W3 /O1 /I src "/Fobuild\temp\season\\" /Febuild\temp\season\test_playoffs.exe tools\eras\season\test_playoffs.c src\nfl2k5_eras_playoffs.c
if ($LASTEXITCODE -ne 0) { throw "cl failed" }
& build\temp\season\test_playoffs.exe $Package $League
