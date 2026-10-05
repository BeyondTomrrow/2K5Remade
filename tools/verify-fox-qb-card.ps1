param(
    [int]$WaitSeconds = 230,
    [string]$Tag = 'fox-qb-overnight'
)

# This runs beside drive-quickgame.ps1.  The quick-game driver deliberately
# leaves both teams CPU-controlled, then this script opens Gamecast after two
# minutes of live play to make a real passing row available to the FOX bridge.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
Start-Sleep -Seconds $WaitSeconds

# Gamecast lives in the in-game pause menu, which is opened by START.
& "$PSScriptRoot\press.ps1" start
Start-Sleep -Seconds 3
& "$PSScriptRoot\press.ps1" down down down down
Start-Sleep -Seconds 3
& "$PSScriptRoot\press.ps1" a
Start-Sleep -Seconds 5

# Passing starts on the first column.  Move through the remaining columns so
# the live bridge observes yards and, where present, touchdown/interception.
& "$PSScriptRoot\press.ps1" right right right right
Start-Sleep -Seconds 5

& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-card.png" | Out-Null
Select-String -Path "logs\$Tag.stderr.log" -Pattern '^\[GAMECAST-QB\]' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Line } | Set-Content -Path "logs\$Tag-result.txt" -Encoding utf8
