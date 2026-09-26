param([string[]]$Presses = @(), [string]$Tag = 'menu', [int]$Wait = 4, [string]$Exe = 'build\Release\NFL2K5.exe')
# Boot to the main menu, then send the given presses (one per step, e.g.
# down,down,a), capturing the window after each step as logs\$Tag-<n>.png.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process (Join-Path $root $Exe) -WorkingDirectory $root -PassThru `
     -RedirectStandardError "$root\logs\$Tag.stderr.log" -RedirectStandardOutput "$root\logs\$Tag.stdout.log"
Start-Sleep 35
& "$PSScriptRoot\press.ps1" start; Start-Sleep 8        # title -> main menu
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-00.png" | Out-Null
$n = 1
foreach ($b in $Presses) {
    if ($b -match '^wait(\d+)$') { Start-Sleep ([int]$Matches[1]); continue }
    & "$PSScriptRoot\press.ps1" $b
    Start-Sleep $Wait
    & "$PSScriptRoot\capscreen.ps1" -Out ("logs\{0}-{1:D2}.png" -f $Tag, $n) | Out-Null
    $n++
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Select-String -Path "logs\$Tag.stderr.log" -Pattern '^\[CRASH\] (exception|function)' | ForEach-Object { $_.Line }
