param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$IsoPath,
    [string]$OutputDirectory = 'E:\NFL2K5-PC\original'
)

$ErrorActionPreference = 'Stop'
$extractor = 'E:\NFL2K5-PC\tools\extract-xiso.exe'
$resolvedIso = (Resolve-Path -LiteralPath $IsoPath).Path

if ([IO.Path]::GetExtension($resolvedIso) -notin '.iso', '.xiso') {
    throw 'The input must be an .iso or .xiso file.'
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& $extractor -x -d $OutputDirectory $resolvedIso
if ($LASTEXITCODE -ne 0) {
    throw "extract-xiso failed with exit code $LASTEXITCODE."
}

$xbe = Join-Path $OutputDirectory 'default.xbe'
if (Test-Path -LiteralPath $xbe) {
    Write-Host "Extraction complete. Found: $xbe"
} else {
    Write-Warning "Extraction finished, but default.xbe was not found directly in $OutputDirectory."
}
