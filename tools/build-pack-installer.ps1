param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$python = Join-Path $root 'dependencies\venv\Scripts\python.exe'
$out = Join-Path $root 'build\tools'
$work = Join-Path $root 'build\pyinstaller-pack-install'

if (-not (Test-Path $python)) {
  throw 'The project Python environment is missing. Run the normal dependency setup first.'
}
if ($Clean -and (Test-Path $work)) {
  Remove-Item -LiteralPath $work -Recurse -Force
}
New-Item -ItemType Directory -Force $out, $work | Out-Null

& $python -m PyInstaller `
  --noconfirm `
  --clean `
  --onefile `
  --console `
  --name NFL2K5-ModPack-Installer `
  --distpath $out `
  --workpath (Join-Path $work 'work') `
  --specpath $work `
  (Join-Path $root 'tools\pack-install.py')
if ($LASTEXITCODE) { throw 'Building the standalone mod-pack installer failed.' }

$exe = Join-Path $out 'NFL2K5-ModPack-Installer.exe'
if (-not (Test-Path $exe)) { throw 'The standalone mod-pack installer was not created.' }
Get-FileHash $exe -Algorithm SHA256
