# Package tools\eras\build_assets.py as build\tools\NFL2K5-ErasBuilder.exe (one file, no
# Python needed), the My NFL Eras first-run asset builder (docs/eras/MOD-PACK-DESIGN.md 7).
# The game release ships it in tools\ beside NFL2K5-TeamBuilder.exe; the game's hub starts
# it ("Build My Assets"), or players run it from the game folder.
#   tools\build-eras-builder.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root 'tools\eras'
$vendor = Join-Path $root 'tools\team-creator\vendor'
$out = Join-Path $root 'build\tools'
$work = Join-Path $root 'build\temp\eras-builder'
New-Item -ItemType Directory -Path $out, $work -Force | Out-Null
# The tools the builder drives, bundled as data under eras_tools\ (it loads them by path).
$data = @(
  "$src\build-office-pack.py;eras_tools", "$src\build-office-ibmpc.py;eras_tools",
  "$src\extract_era_logos.py;eras_tools", "$src\build_era_team_art.py;eras_tools",
  "$src\eras_settings.py;eras_tools", "$src\stadiums;eras_tools\stadiums",
  "$root\tools\ncaa07-extractor;eras_tools\ncaa07-extractor",
  "$vendor\nfl_txtr.py;eras_tools\vendor", "$vendor\nfl_outer.py;eras_tools\vendor",
  "$vendor\LICENSE-2k-football-mod-tools.txt;licenses"
) | ForEach-Object { @('--add-data', $_) }
$ErrorActionPreference = 'Continue'     # PyInstaller logs to stderr
python -m PyInstaller --noconfirm --onefile --console --name NFL2K5-ErasBuilder `
    --distpath $out --workpath $work --specpath $work `
    --paths $src --paths $vendor --paths (Join-Path $src 'stadiums') `
    --hidden-import nfl_txtr --hidden-import nfl_outer --collect-submodules numpy `
    --hidden-import PIL.Image --exclude-module tkinter @data `
    (Join-Path $src 'build_assets.py') 2>&1 | Where-Object { $_ -match 'ERROR|WARNING: Hidden import' } | ForEach-Object { "$_" }
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed.' }
Get-Item (Join-Path $out 'NFL2K5-ErasBuilder.exe') | Select-Object Name, Length
