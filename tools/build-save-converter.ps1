param(
    [string]$SourceDir = (Join-Path $PSScriptRoot '..\external\nfl2k5tool_dart'),
    [string]$Output = (Join-Path $PSScriptRoot 'nfl2k5-save-converter.exe')
)

$ErrorActionPreference = 'Stop'
$upstream = 'https://github.com/BAD-AL/nfl2k5tool_dart.git'
$verifiedCommit = 'e3afeece61896380a95e506ab43ae1abda0ae8a5'
$patchFile = Join-Path $PSScriptRoot 'nfl2k5tool-name-pool.patch'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'Git is required to obtain nfl2k5tool_dart.'
}
if (-not (Get-Command dart -ErrorAction SilentlyContinue)) {
    throw 'Dart SDK 3.10.8 or newer is required. Install Dart, then run this script again.'
}

$SourceDir = [IO.Path]::GetFullPath($SourceDir)
$Output = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $SourceDir)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SourceDir) | Out-Null
    & git clone $upstream $SourceDir
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone nfl2k5tool_dart.' }
}

Push-Location $SourceDir
try {
    $head = (& git rev-parse HEAD).Trim()
    if ($head -ne $verifiedCommit) {
        Write-Warning "Building upstream revision $head; the locally verified revision is $verifiedCommit."
    }

    & git apply --reverse --check $patchFile 2>$null
    $alreadyPatched = $LASTEXITCODE -eq 0
    if (-not $alreadyPatched) {
        & git apply --check $patchFile
        if ($LASTEXITCODE -ne 0) { throw 'The player-name-pool patch does not apply to this source revision.' }
        & git apply $patchFile
        if ($LASTEXITCODE -ne 0) { throw 'Could not apply the player-name-pool patch.' }
    }

    & dart pub get
    if ($LASTEXITCODE -ne 0) { throw 'dart pub get failed.' }
    & dart compile exe 'bin\nfl2k5tool_dart.dart' -o $Output
    if ($LASTEXITCODE -ne 0) { throw 'Dart converter compilation failed.' }
}
finally {
    Pop-Location
}

Write-Host "Built $Output"
Write-Warning 'Do not redistribute this binary until BAD-AL confirms the upstream license/redistribution terms.'
