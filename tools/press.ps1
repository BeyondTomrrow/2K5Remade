# Queue button presses for a game started with NFL2K5_PRESS_FILE pointing at
# logs\press.txt (see file_press in src/nfl2k5_input_hle.c): a b x y start back
# up down left right. Works without the game window having focus.
#   tools\press.ps1 a
#   tools\press.ps1 down down a
$file = Join-Path (Split-Path $PSScriptRoot -Parent) 'logs\press.txt'
Set-Content -Path $file -Value ($args -join ' ') -Encoding ascii
