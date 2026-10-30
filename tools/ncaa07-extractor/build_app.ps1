# Build the NCAA 07 to Blender app (one folder, no Python needed) into
# H:\NCAA07_Extractor_Work\app_v3\NCAA07_Blender_v3\NCAA07_Blender_v3.exe
#   tools\ncaa07-extractor\build_app.ps1 [-Out <folder>]
param([string]$Out = 'H:\NCAA07_Extractor_Work\app_v3')
$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$work = Join-Path $env:TEMP 'ncaa07_app_build'
if (-not (Test-Path (Join-Path $src 'tools\quickbms.exe'))) { throw 'tools\quickbms.exe is missing (it is not kept in git).' }
New-Item -ItemType Directory -Force -Path $Out, $work | Out-Null
$ErrorActionPreference = 'Continue'     # PyInstaller logs to stderr
python -m PyInstaller --noconfirm --windowed --name NCAA07_Blender_v3 `
    --distpath $Out --workpath $work --specpath $work `
    --paths $src `
    --add-data "$src\tools;tools" `
    --add-data "$src\blender_addon;blender_addon" `
    --add-data "$src\ncaa_blender_build.py;." `
    --hidden-import ncaa_player --hidden-import ncaa_anim --hidden-import ncaa_bridge_client `
    --hidden-import ncaa_assemble --hidden-import ncaa_rig --hidden-import ncaa_rig_analysis `
    --collect-submodules numpy `
    (Join-Path $src 'ncaa_app.py') 2>&1 | Where-Object { $_ -match 'ERROR' } | ForEach-Object { "$_" }
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed.' }
$exe = Join-Path $Out 'NCAA07_Blender_v3\NCAA07_Blender_v3.exe'
Set-Content -Path (Join-Path (Split-Path $Out -Parent) 'Open_NCAA07_Tool_v3.cmd') -Encoding ascii -Value @(
    '@echo off', "start """" ""$exe""")
Get-Item $exe | Select-Object FullName, Length
