# Package tools\team-creator\team_builder.py as build\tools\NFL2K5-TeamBuilder.exe
# (one file, no Python needed). The installer runs it to build the teams that
# ship as recipes (mods\teams\<team>\creator.json) from the player's own disc.
#   tools\build-team-builder.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root 'tools\team-creator'
$out = Join-Path $root 'build\tools'
$work = Join-Path $root 'build\temp\team-builder'
New-Item -ItemType Directory -Path $out, $work -Force | Out-Null
$ErrorActionPreference = 'Continue'     # PyInstaller logs to stderr
python -m PyInstaller --noconfirm --onefile --console --name NFL2K5-TeamBuilder `
    --distpath $out --workpath $work --specpath $work `
    --paths $src --paths (Join-Path $src 'vendor') `
    --hidden-import nfl_outer --hidden-import nfl_txtr --collect-submodules numpy `
    --exclude-module tkinter `
    (Join-Path $src 'team_builder.py') 2>&1 | Where-Object { $_ -match 'ERROR|WARNING: Hidden import' } | ForEach-Object { "$_" }
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed.' }
Get-Item (Join-Path $out 'NFL2K5-TeamBuilder.exe') | Select-Object Name, Length
