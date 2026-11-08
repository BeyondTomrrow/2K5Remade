<#
.SYNOPSIS
    Build the Xbox loose-folder install: the UWP app plus YOUR game files in
    one folder (dist\xbox-loose\NFL2K5), ready for deploy-xbox-loose.ps1.

.DESCRIPTION
    Xbox Dev Mode caps an app's LocalState at a few GB (the 6 GB of game files
    hit "out of storage" at about 3.3 GB). A loose-folder install keeps the
    game inside the app's own install folder instead
    (D:\DevelopmentFiles\LooseApps\NFL2K5 on the console); the app finds it
    there by itself (uwp_game.cpp: root.txt, then LocalState\original, then
    <install folder>\original). Saves, settings and caches still go to
    LocalState.

    Takes from the game folder (-GameRoot, default: this checkout):
      original\default.xbe, update.xbe, dashupdate.xbe, original\vc_53450030\*
      mods\presentations, mods\presentation.ini, mods\teams, mods\textures
      assets, data, font, moment-audio (when present)
    Never takes original\disc or original\CACHE (test copies, not game data).
    -IncludeSoftdrink adds mods\packs\SOFTDRINK*; -IncludeMusic adds Music.

    -Pack: instead of the 5.8 GB vc_53450030 folder, one compressed
    original\vc_53450030.xbpak made by tools\xbpack\xbpack.exe (built by
    tools\xbpack\build-xbpack.ps1). Without the movies (default, -Drop movies)
    it is about 2.9 GB; -Drop none keeps everything (about 3.8 GB, lossless).
    The tool decompresses the whole pack and compares it with your files
    before it is used. It keeps the few retail bytes installed mod packs
    (SOFTDRINK) reuse from the movies. -PackFile uses a pack you already made
    instead of making one (about 6 minutes).

    On the same drive files are hard-linked (no 6 GB copy; the originals are
    only ever read). Otherwise robocopy /XJ copies them. Junctions are never
    followed. No game files are part of the GitHub release: every player runs
    this against their own copy.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\uwp\make-xbox-loose.ps1 -GameRoot D:\NFL2K5
#>
[CmdletBinding()]
param(
    [string]$GameRoot,
    [string]$Out,
    [string]$Layout,
    [switch]$IncludeSoftdrink,
    [switch]$IncludeMusic,
    [switch]$Copy,         # always copy instead of hard-linking
    [switch]$Pack,         # the data folder as one compressed .xbpak
    [ValidateSet('movies', 'none')][string]$Drop = 'movies',
    [string]$PackFile      # use this .xbpak instead of making one
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $GameRoot) { $GameRoot = $Root }
if (-not $Out) { $Out = "$Root\dist\xbox-loose\NFL2K5" }
if (-not $Layout) { $Layout = "$Root\build-uwp\layout" }
$GameRoot = (Resolve-Path $GameRoot).Path

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

if (-not (Test-Path "$Layout\AppxManifest.xml")) {
    Fail "$Layout\AppxManifest.xml missing: build the UWP app first (tools\uwp\build-uwp.ps1 -Game, then package-uwp.ps1 -Game)."
}
if (-not (Test-Path "$GameRoot\original\default.xbe") -or -not (Test-Path "$GameRoot\original\vc_53450030")) {
    Fail "$GameRoot\original must hold default.xbe and the vc_53450030 folder from your extracted disc."
}

if (Test-Path $Out) {
    # Only ever remove what this script made: refuse a folder without its marker.
    if (-not (Test-Path "$Out\.made-by-make-xbox-loose")) { Fail "$Out exists and was not made by this script; move it away first." }
    # Hard links: deleting the link never touches the original file. No
    # recursion into junctions (there are none in what this script makes).
    Remove-Item -LiteralPath $Out -Recurse -Force
}
New-Item -ItemType Directory -Force $Out | Out-Null
Set-Content "$Out\.made-by-make-xbox-loose" "made $(Get-Date -Format s) from $GameRoot"

$outDrive = [IO.Path]::GetPathRoot((Resolve-Path $Out).Path).ToUpper()
$script:links = 0; $script:copies = 0

function Add-File([string]$src, [string]$dst) {
    $item = Get-Item -LiteralPath $src -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Host "  skipped (link): $src"; return }
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    $sameDrive = [IO.Path]::GetPathRoot($item.FullName).ToUpper() -eq $outDrive
    if ($sameDrive -and -not $Copy) {
        cmd /c mklink /H "`"$dst`"" "`"$($item.FullName)`"" | Out-Null
        if ($LASTEXITCODE -eq 0) { $script:links++; return }
    }
    Copy-Item -LiteralPath $item.FullName -Destination $dst -Force
    $script:copies++
}

function Add-Dir([string]$src, [string]$dst) {
    if (-not (Test-Path -LiteralPath $src)) { return }
    $d = Get-Item -LiteralPath $src -Force
    if ($d.Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Host "  skipped (junction): $src"; return }
    # Walk by hand so junctions inside are never entered.
    $stack = New-Object System.Collections.Stack
    $stack.Push($d.FullName)
    while ($stack.Count) {
        $dir = $stack.Pop()
        foreach ($e in [IO.Directory]::EnumerateFileSystemEntries($dir)) {
            $attr = [IO.File]::GetAttributes($e)
            if ($attr -band [IO.FileAttributes]::ReparsePoint) { Write-Host "  skipped (link): $e"; continue }
            $rel = $e.Substring($d.FullName.Length).TrimStart('\')
            if ($attr -band [IO.FileAttributes]::Directory) { $stack.Push($e) }
            else { Add-File $e (Join-Path $dst $rel) }
        }
    }
}

Write-Host "App files from $Layout"
foreach ($e in Get-ChildItem -LiteralPath $Layout -Force) {
    if ($e.PSIsContainer) { Add-Dir $e.FullName "$Out\$($e.Name)" }
    else { Copy-Item -LiteralPath $e.FullName "$Out\$($e.Name)" -Force; $script:copies++ }   # small; never linked to the build output
}

Write-Host "Game files from $GameRoot\original (not disc\ or CACHE\)"
foreach ($f in 'default.xbe', 'update.xbe', 'dashupdate.xbe') {
    if (Test-Path "$GameRoot\original\$f") { Add-File "$GameRoot\original\$f" "$Out\original\$f" }
    elseif ($f -eq 'default.xbe') { Fail "original\default.xbe missing" }
}
if ($PackFile) { $Pack = $true }
if ($Pack) {
    $pak = "$Out\original\vc_53450030.xbpak"
    if ($PackFile) {
        Write-Host "Data pack from $PackFile"
        Add-File (Resolve-Path $PackFile).Path $pak
    } else {
        $xbpack = "$Root\tools\xbpack\xbpack.exe"
        if (-not (Test-Path $xbpack)) { Fail "$xbpack missing: run tools\xbpack\build-xbpack.ps1 first." }
        $packArgs = @("$GameRoot\original", $pak, '--drop', $Drop)
        # Every installed mod pack, included or not: each keeps the few
        # retail bytes it reuses from the movies (~110 KB for SOFTDRINK), so
        # it still works if it is copied over later.
        foreach ($p in (Get-ChildItem "$GameRoot\mods\packs" -Directory -ErrorAction SilentlyContinue)) {
            if (Test-Path "$($p.FullName)\overlay.bin") { $packArgs += @('--keep-for', $p.FullName) }
        }
        Write-Host "Packing the data folder (drop: $Drop); this takes a few minutes"
        & $xbpack @packArgs
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $pak)) { Fail "xbpack failed; no pack was made" }
        $script:copies++
    }
} else {
    Add-Dir "$GameRoot\original\vc_53450030" "$Out\original\vc_53450030"
}

Write-Host "Presentations, teams and support folders"
Add-Dir "$GameRoot\mods\presentations" "$Out\mods\presentations"
Add-Dir "$GameRoot\mods\teams" "$Out\mods\teams"
Add-Dir "$GameRoot\mods\textures" "$Out\mods\textures"
if (Test-Path "$GameRoot\mods\presentation.ini") { Add-File "$GameRoot\mods\presentation.ini" "$Out\mods\presentation.ini" }
foreach ($d in 'assets', 'data', 'font', 'moment-audio') { Add-Dir "$GameRoot\$d" "$Out\$d" }
if ($IncludeSoftdrink) {
    $sd = Get-ChildItem "$GameRoot\mods\packs" -Directory -Filter 'SOFTDRINK*' -ErrorAction SilentlyContinue
    if (-not $sd) { Write-Host "  no mods\packs\SOFTDRINK* folder found" -ForegroundColor Yellow }
    foreach ($p in $sd) { Add-Dir $p.FullName "$Out\mods\packs\$($p.Name)" }
}
if ($IncludeMusic) { Add-Dir "$GameRoot\Music" "$Out\Music" }

# Checks: the game data is complete and nothing from disc\ or CACHE\ came along.
if ($Pack) {
    & "$Root\tools\xbpack\xbpack.exe" --info "$Out\original\vc_53450030.xbpak" | Select-Object -First 1
    if ($LASTEXITCODE -ne 0) { Fail "original\vc_53450030.xbpak is not a readable data pack" }
} else {
    $srcVc = Get-ChildItem "$GameRoot\original\vc_53450030" -File -Force | Where-Object { -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) }
    foreach ($f in $srcVc) {
        $o = Get-Item -LiteralPath "$Out\original\vc_53450030\$($f.Name)" -ErrorAction SilentlyContinue
        if (-not $o -or $o.Length -ne $f.Length) { Fail "original\vc_53450030\$($f.Name) did not come across whole" }
    }
}
if ((Test-Path "$Out\original\disc") -or (Test-Path "$Out\original\CACHE")) { Fail "disc\ or CACHE\ ended up in the loose folder" }

if (Test-Path "$PSScriptRoot\README-XBOX.txt") { Copy-Item "$PSScriptRoot\README-XBOX.txt" (Split-Path $Out) -Force }

$all = Get-ChildItem -LiteralPath $Out -Recurse -File -Force
$gb = ($all | Measure-Object Length -Sum).Sum / 1GB
Write-Host ""
Write-Host ("Loose folder ready: {0}" -f $Out) -ForegroundColor Green
Write-Host ("  {0} files, {1:N2} GB ({2} hard links, {3} copies)" -f $all.Count, $gb, $script:links, $script:copies)
Write-Host "Next: tools\uwp\deploy-xbox-loose.cmd  (or see README-XBOX.txt)"
