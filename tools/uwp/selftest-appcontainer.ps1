<#
.SYNOPSIS
    Run the UWP game exe's self-tests (--audio-test, --music-test) inside an
    AppContainer on this PC, without installing the package.
.DESCRIPTION
    Stages build-uwp\game\NFL2K5.exe + store CRT DLLs into a folder that grants
    ALL APPLICATION PACKAGES read/execute (a folder this script owns), and test
    media from -Media (also granted read, so use a test folder, not game data).
#>
param(
    [string]$Stage = 'E:\2K5Remade-local\uwp-stage-selftest',
    [string]$Media = 'E:\2K5Remade-local\uwp-test\media',
    [switch]$Direct     # run without the AppContainer (comparison)
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\uwp-env.ps1" | Out-Null
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$Repo\build-uwp\game\NFL2K5.exe" $Stage -Force
$vclibs = (Get-AppxPackage Microsoft.VCLibs.140.00 | Where-Object Architecture -eq 'X64' |
           Sort-Object Version -Descending | Select-Object -First 1).InstallLocation
Copy-Item "$vclibs\*_app.dll" $Stage -Force
& icacls $Stage /grant '*S-1-15-2-1:(OI)(CI)RX' /T /Q | Out-Null
& icacls $Media /grant '*S-1-15-2-1:(OI)(CI)RX' /T /Q | Out-Null

$runner = "$Repo\build-uwp\appcontainer-run.exe"
Push-Location "$Repo\build-uwp"
try {
    $env:LIB = ($env:LIB -split ';' | Where-Object { $_ -notlike '*\store' }) -join ';'
    & cl.exe /nologo /W3 "$PSScriptRoot\appcontainer-run.c" /Fe:$runner userenv.lib advapi32.lib ole32.lib | Out-Null
    if ($LASTEXITCODE) { throw 'appcontainer-run build failed' }
} finally { Pop-Location }

$files = Get-ChildItem $Media -File | Where-Object { $_.Extension -in '.wav', '.mp3', '.m4a', '.wma', '.aac' -and $_.Name -notlike '*.converted.wma' }
if ($Direct) {
    $out = "$Stage\out"; New-Item -ItemType Directory -Force $out | Out-Null
    Remove-Item "$out\uwp-selftest.txt" -ErrorAction SilentlyContinue
    & "$Stage\NFL2K5.exe" --audio-test $out | Out-Null
    & "$Stage\NFL2K5.exe" --music-test $out @($files.FullName) | Out-Null
    Get-Content "$out\uwp-selftest.txt"
} else {
    & $runner "$Stage\NFL2K5.exe" --audio-test '%AC%'
    & $runner "$Stage\NFL2K5.exe" --music-test '%AC%' @($files.FullName)
}
