$root = Split-Path $PSScriptRoot -Parent
$page = Join-Path $root 'tools\scorebug-studio\index.html'
Start-Process $page
