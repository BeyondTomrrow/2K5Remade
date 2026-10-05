param([string]$Tag = 'team-select')

# Stops at Team Select rather than advancing to Coach Matchup.  This is used
# to verify controller ownership before an unattended CPU-versus-CPU run.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
$p = Start-Process 'build\Release\NFL2K5.exe' -WorkingDirectory $root -PassThru `
    -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
Start-Sleep -Seconds 33
& "$PSScriptRoot\press.ps1" start
Start-Sleep -Seconds 8
& "$PSScriptRoot\press.ps1" a
Start-Sleep -Seconds 10
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag.png" | Out-Null
