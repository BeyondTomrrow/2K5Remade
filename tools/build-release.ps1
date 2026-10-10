param(
    [string]$Version = '0.1.0-preview',
    [switch]$SkipGameBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$iscc = 'C:\Program Files\Inno Setup 7\ISCC.exe'
$sourceZip = Join-Path $dist ("NFL2K5-PC-src-{0}.zip" -f (Get-Date -Format 'yyyy-MM-dd'))

if (-not $SkipGameBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Game
    if ($LASTEXITCODE -ne 0) { throw 'Release game build failed.' }
}

& (Join-Path $PSScriptRoot 'build-pack-installer.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Standalone mod-pack installer build failed.' }

# SOFTDRINK 2K28 v0.4 disc image (Mod Studio output) and the retail+widescreen executable.
$softDrinkV04Hash = '2b0fbbbb89b5c72aaed7c454bf6e78dd2c97f1e417e2d761e515907ed81471ab'
$widescreenHash = '81d6e16c25ca7ea1014d0ced915f910f5d354eaeabeb9adfcbb6956e2a0b54f8'

$required = @(
    (Join-Path $root 'build\Release\NFL2K5.exe'),
    (Join-Path $root 'build\tools\NFL2K5-ModPack-Installer.exe'),
    (Join-Path $root 'tools\extract-xiso.exe'),
    (Join-Path $root 'tools\nfl2k5-save-converter.exe'),
    (Join-Path $root 'installer\nfl2k5_video.default.ini'),
    (Join-Path $root "release\code-packs\$softDrinkV04Hash\NFL2K5.exe"),
    $iscc
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required release input is missing: $path"
    }
}

# Teams that ship (installer: release\teams -> mods\teams): every tracked file
# under mods\teams (the clubs' presentation files) and each release team's own
# files. Never the files built from the player's disc (files\, cache\,
# team.rost), and never a team that is not listed here (test teams).
$releaseTeams = @('Claude')
$teamStage = Join-Path $root 'release\teams'
if (Test-Path -LiteralPath $teamStage) { Remove-Item -LiteralPath $teamStage -Recurse -Force }
foreach ($rel in (& git -C $root ls-files mods/teams)) {
    $dst = Join-Path $teamStage ($rel.Substring('mods/teams/'.Length))
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $rel) -Destination $dst
}
foreach ($team in $releaseTeams) {
    foreach ($rel in @('team.json', 'roster.csv', 'logos\logo.png', 'logos\scorebug.png')) {
        $src = Join-Path $root "mods\teams\$team\$rel"
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "Release team file is missing: $src" }
        $dst = Join-Path $teamStage "$team\$rel"
        New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst
    }
}

New-Item -ItemType Directory -Path $dist -Force | Out-Null
& $iscc (Join-Path $root 'installer\NFL2K5-PC.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }

if (Test-Path -LiteralPath $sourceZip) { Remove-Item -LiteralPath $sourceZip -Force }
Push-Location $root
try {
    & tar -a -cf $sourceZip src
    if ($LASTEXITCODE -ne 0) { throw 'Source ZIP creation failed.' }
} finally {
    Pop-Location
}

$setup = Join-Path $dist 'NFL2K5-PC-Setup.exe'
$hashes = Get-FileHash $setup, $sourceZip -Algorithm SHA256
$hashLines = foreach ($hash in $hashes) {
    '{0} *{1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path $hash.Path -Leaf)
}
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Value $hashLines -Encoding ascii

Write-Host "Release $Version is ready:"
Get-Item $setup, $sourceZip | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
