# The My NFL Eras release: the redistributable pack folder and zip (build\eras-pack\), then the
# redistribution check of docs/eras/MOD-PACK-DESIGN.md 8 on the zip (fails the release on any
# retail/NCAA file type, any disc-derived or user image, any file not in pack.json files).
#   tools\eras\pack-release.ps1            build + check
#   tools\eras\pack-release.ps1 -Zip X     check a zip only
param([string]$Zip = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ($Zip) { & python -I "$root\tools\eras\make_pack.py" --check-zip $Zip }
else { & python -I "$root\tools\eras\make_pack.py" --dist }
if ($LASTEXITCODE) { throw 'My NFL Eras pack: redistribution check FAILED (see above).' }
