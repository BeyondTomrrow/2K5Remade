# Render the NBC SNF package where it sits in reference/reference.png
# (1400x268, preview mode) on the game's own engine (WebView2), then diff it.
param([string]$Out = 'logs\nbc-render.png')
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$exe = Join-Path $root 'tools\presentation-render\presentation-render.exe'
if (-not (Test-Path $exe)) { & (Join-Path $root 'tools\presentation-render\build.ps1') | Out-Null }
& $exe 'https://presentation.local/NBC_SNF/scorebug.html?preview=reference' (Join-Path $root $Out) 1400 268 1500 (Join-Path $root 'mods\presentations') | Out-Null
& python (Join-Path $PSScriptRoot 'diff.py') (Join-Path $root $Out)
