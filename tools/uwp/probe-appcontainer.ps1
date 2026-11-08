<#
.SYNOPSIS
    Run the UWP exe's run-time probes inside an AppContainer on this PC,
    without installing the package (works with Developer Mode off).

.DESCRIPTION
    Stages build-uwp\layout plus the store CRT DLLs (from the installed
    Microsoft.VCLibs.140.00 framework) into a staging folder that grants
    ALL APPLICATION PACKAGES read/execute, builds tools\uwp\appcontainer-run.c,
    and runs "NFL2K5.exe --probe <container folder>" in the sandbox.
#>
param([string]$Stage = "$env:TEMP\nfl2k5-uwp-probe")
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\uwp-env.ps1"
$layout = "$Root\build-uwp\layout"
if (-not (Test-Path "$layout\NFL2K5.exe")) { throw 'run package-uwp.ps1 first (needs build-uwp\layout)' }

New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$layout\*" $Stage -Recurse -Force
$vclibs = (Get-AppxPackage Microsoft.VCLibs.140.00 | Where-Object Architecture -eq 'X64' |
           Sort-Object Version -Descending | Select-Object -First 1).InstallLocation
if (-not $vclibs) { throw 'Microsoft.VCLibs.140.00 (x64) framework is not installed' }
Copy-Item "$vclibs\*_app.dll" $Stage -Force
& icacls $Stage /grant '*S-1-15-2-1:(OI)(CI)RX' /T /Q | Out-Null

$runner = "$Root\build-uwp\appcontainer-run.exe"
Push-Location "$Root\build-uwp"
try {
    & cl.exe /nologo /W3 "$PSScriptRoot\appcontainer-run.c" /Fe:$runner userenv.lib advapi32.lib ole32.lib | Out-Null
    if ($LASTEXITCODE) { throw 'appcontainer-run build failed' }
} finally { Pop-Location }
& $runner "$Stage\NFL2K5.exe" --probe '%AC%'
