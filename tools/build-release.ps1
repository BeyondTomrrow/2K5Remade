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

$required = @(
    (Join-Path $root 'build\Release\NFL2K5.exe'),
    (Join-Path $root 'tools\extract-xiso.exe'),
    (Join-Path $root 'nfl2k5_video.ini'),
    $iscc
)
foreach ($path in $required) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required release input is missing: $path"
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
