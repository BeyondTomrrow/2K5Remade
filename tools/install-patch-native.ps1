<#
Install a .2k5patch and make its matching native executable in one command.

Example:
  .\tools\install-patch-native.ps1 .\mods\downloads\SOFTDRINK-2K28-v0.7.2k5patch

The game must be closed.  A patch can change default.xbe, so copying an old
native EXE is unsafe: this script verifies the patch, regenerates code for the
installed XBE, builds it, installs it into that pack's native folder and then
makes the pack active.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$Patch
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (Get-Process NFL2K5 -ErrorAction SilentlyContinue) {
    throw 'Close ESPN NFL 2K5 before installing a patch.'
}

$patchPath = (Resolve-Path -LiteralPath $Patch).Path
$python = Join-Path $root 'dependencies\venv\Scripts\python.exe'
if (-not (Test-Path $python)) { $python = 'python' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $root "logs\patch-install-$stamp.log"

Write-Host "Installing and verifying $([IO.Path]::GetFileName($patchPath))..."
& $python (Join-Path $root 'tools\pack-install.py') $patchPath --verify --replace --activate --log $log
if ($LASTEXITCODE) { throw "Patch installation failed. See $log" }

# pack-install writes the source patch path and XBE hash to pack.json.  Locate
# the exact installed folder by those values, rather than assuming a versioned
# folder name chosen by the patch author.
$patchFull = [IO.Path]::GetFullPath($patchPath)
$pack = Get-ChildItem (Join-Path $root 'mods\packs') -Directory | ForEach-Object {
    $manifest = Join-Path $_.FullName 'pack.json'
    if (-not (Test-Path $manifest)) { return }
    try {
        $json = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
        if ($json.source -and ([IO.Path]::GetFullPath([string]$json.source) -eq $patchFull) -and $json.xbe_sha256) {
            [PSCustomObject]@{ Directory = $_; Manifest = $json }
        }
    } catch { }
} | Select-Object -Last 1

if (-not $pack) { throw "The patch installed, but its pack manifest was not found. See $log" }

$shortHash = ([string]$pack.Manifest.xbe_sha256).Substring(0, 12).ToLower()
$buildName = "pack-$shortHash"
$analysis = Join-Path $root "analysis-$buildName"
$gen = Join-Path $root "build\gen-$buildName"
$xbe = Join-Path $pack.Directory.FullName 'default.xbe'

Write-Host "Creating the matching native build for $($pack.Manifest.display_name)..."
& (Join-Path $PSScriptRoot 'analyze.ps1') -Recompile -Xbe $xbe -Analysis $analysis -Gen $gen
if ($LASTEXITCODE) { throw 'Code generation failed; the installed patch is preserved for review.' }

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Game -Pack $buildName
if ($LASTEXITCODE) { throw 'Native build failed; the installed patch is preserved for review.' }

Write-Host "Ready: $($pack.Manifest.display_name) is active with its matching native build."
