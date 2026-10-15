param([string[]]$Side = @('left'), [string]$Tag = 'qd', [int]$PlaySeconds = 300, [int]$ShotEvery = 30, [string]$Exe = 'build\Release\NFL2K5.exe', [int]$TapEvery = 0, [string]$TapButton = 'a', [switch]$NoSkip, [string[]]$HomeTeam = @(), [switch]$Retail)
# Drive a CPU-vs-CPU Quick Game: title -> main menu -> Quick Game -> Team
# Select (the controller starts on the HOME side; the default one Left press
# moves it to the middle = CPU vs CPU) -> Coach Matchup -> Start Game ->
# skip the pregame. Presses go through NFL2K5_PRESS_FILE (tools\press.ps1),
# so the window does not need focus, and host pads are ignored so a connected
# controller cannot move the cursor. Screenshots land in logs\$Tag-<s>.png;
# the log is logs\$Tag.stderr.log. Prints how it ended.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$defaultRetail = 'build\Release\NFL2K5.exe'
# The retail executable is a dispatcher: when an active pack has a matching
# native build it starts that build and exits immediately.  Test the selected
# native executable directly so the driver watches the actual game process.
# -Retail: the clean ESPN NFL 2K5 (NFL2K5.exe --retail), whatever pack is active.
if ($Exe -eq $defaultRetail -and -not $Retail) {
    $activePack = Join-Path $root 'mods\active-pack.ini'
    if (Test-Path $activePack) {
        $packName = (Get-Content $activePack -Raw).Trim()
        $candidate = Join-Path $root ("mods\packs\$packName\native\NFL2K5.exe")
        if ($packName -and (Test-Path $candidate -PathType Leaf)) {
            $Exe = $candidate
        }
    }
}
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
# logs\game.lock: whoever runs the game writes it, others wait (two sessions
# share this machine and each start used to kill the other's game). A lock
# older than two hours is stale.
$lock = "$root\logs\game.lock"
function LockLive {
    if (-not (Test-Path $lock)) { return $false }
    $t = Get-Content $lock -Raw
    if ($t -match "drive-quickgame $PID\b") { return $false }
    if (((Get-Date) - (Get-Item $lock).LastWriteTime).TotalHours -ge 2) { return $false }
    # A drive-quickgame lock whose script is gone is stale.
    if ($t -match '^drive-quickgame (\d+)' -and -not (Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue)) { return $false }
    return $true
}
while (LockLive) {
    Start-Sleep 15
}
"drive-quickgame $PID $(Get-Date -Format s) $Tag" | Set-Content $lock -Encoding ascii
# A game already running is someone playing it: wait for them to finish
# (up to 30 minutes) rather than closing it.
$waitStart = Get-Date
while ((Get-Process NFL2K5 -ErrorAction SilentlyContinue) -and ((Get-Date) - $waitStart).TotalMinutes -lt 30) {
    "drive-quickgame $PID $(Get-Date -Format s) $Tag" | Set-Content $lock -Encoding ascii
    Start-Sleep 20
}
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath $env:NFL2K5_PRESS_FILE -ErrorAction SilentlyContinue
$launchArgs = @{}
if ($Retail) { $launchArgs['ArgumentList'] = '--retail' }
$p = Start-Process $Exe @launchArgs -WorkingDirectory $root -PassThru `
     -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
$null = $p.Handle
function Press([string[]]$b, [int]$after) { & "$PSScriptRoot\press.ps1" @b; Start-Sleep $after }
Start-Sleep 90
Press start 8       # title -> "Successfully loaded Settings" popup
Press a 8           # Settings popup -> "Successfully loaded VIP1" popup
Press a 8           # dismiss VIP popup -> title
Press start 8       # title -> main menu
Press a 18          # Quick Game (Team Select needs the time to load before the side press)
foreach ($h in $HomeTeam) { Press $h 2 }   # e.g. -HomeTeam (,'lt'*11) = 49ers (default home is Cowboys)
if ($Side) { Press $Side 3 }   # from home: left = middle (CPU vs CPU), left left = away, -Side @() = play home
Press start 12      # Team Select -> Coach Matchup
Press a 30          # Start Game -> loading -> pregame
if (-not $NoSkip) { Press a 5 }   # skip pregame (-NoSkip watches it)
$t0 = Get-Date
$next = 0
$nextTap = $TapEvery
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $PlaySeconds) {
    $s = [int]((Get-Date) - $t0).TotalSeconds
    if ($s -ge $next) {
        & "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-$s.png" | Out-Null
        $next += $ShotEvery
    }
    # -TapEvery N: press A every N seconds of play (skips pregame segments).
    if ($TapEvery -gt 0 -and $s -ge $nextTap) {
        & "$PSScriptRoot\press.ps1" $TapButton
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
Remove-Item "$root\logs\game.lock" -ErrorAction SilentlyContinue
