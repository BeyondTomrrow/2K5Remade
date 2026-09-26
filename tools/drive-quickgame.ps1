param([string]$Side = '', [string]$Tag = 'qd', [int]$PlaySeconds = 300, [int]$ShotEvery = 30, [string]$Exe = 'build\Release\NFL2K5.exe', [int]$TapEvery = 0, [switch]$NoSkip)
# Drive a CPU-vs-CPU Quick Game: title -> main menu -> Quick Game -> Team
# Select (controller left in the middle) -> Coach Matchup -> Start Game ->
# skip the pregame. Presses go through NFL2K5_PRESS_FILE (tools\press.ps1),
# so the window does not need focus, and host pads are ignored so a connected
# controller cannot move the cursor. Screenshots land in logs\$Tag-<s>.png;
# the log is logs\$Tag.stderr.log. Prints how it ended.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process $Exe -WorkingDirectory $root -PassThru `
     -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
$null = $p.Handle
function Press([string[]]$b, [int]$after) { & "$PSScriptRoot\press.ps1" @b; Start-Sleep $after }
Start-Sleep 35
Press start 10      # title -> main menu
Press a 12          # Quick Game
if ($Side) { Press $Side 3 }   # controller onto a team (left = away, right = home)
Press start 12      # Team Select -> Coach Matchup
Press a 30          # Start Game -> loading -> pregame
if (-not $NoSkip) { Press a 5 }   # skip pregame (-NoSkip watches it)
$t0 = Get-Date
$next = 0
$nextTap = $TapEvery
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $PlaySeconds) {
    $s = [int]((Get-Date) - $t0).TotalSeconds
    if ($s -ge $next) {
        & "$PSScriptRoot\capwin.ps1" -At 0 -Prefix "$Tag-$s" | Out-Null
        $next += $ShotEvery
    }
    # -TapEvery N: press A every N seconds of play (skips pregame segments).
    if ($TapEvery -gt 0 -and $s -ge $nextTap) {
        & "$PSScriptRoot\press.ps1" a
        $nextTap += $TapEvery
    }
    Start-Sleep 2
}
if ($p.HasExited) {
    "EXITED after $([int]((Get-Date) - $t0).TotalSeconds) s of play"
    Select-String -Path "logs\$Tag.stderr.log" -Pattern '^\[CRASH\] (exception|module|function)' | ForEach-Object { $_.Line }
} else {
    "RUNNING after $PlaySeconds s of play"
    Stop-Process -Id $p.Id -Force
}
