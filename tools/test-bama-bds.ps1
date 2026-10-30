# Drive a CPU-vs-CPU Quick Game with Alabama as the HOME team and confirm it
# plays in Bryant-Denny Stadium (the custom-team stadium binding in
# nfl2k5_team_files.c / nfl2k5_teams.c). Forces Eras OFF so the mods\eras\stadiums
# fallback path is exercised. Presses go through NFL2K5_PRESS_FILE so the window
# needs no focus. Screenshots + log go to logs\$Tag*.
param([string]$Tag = 'bamabds', [int]$PlaySeconds = 100, [int]$ShotEvery = 15, [int]$MaxTeamSteps = 140)
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$Exe = 'build\Release\NFL2K5.exe'
$log = "logs\$Tag.stderr.log"

$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = 'always'
$env:NFL2K5_TEAMS_LOG = '1'
$env:NFL2K5_ERAS = '0'      # force Eras off -> test the Quick-Game stadium fallback
$env:RECOMP_BACKGROUND = '1'   # windowed, no focus steal (user is on the PC)
$env:RECOMP_AUDIO_MUTE = '1'
Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue

Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath $env:NFL2K5_PRESS_FILE -ErrorAction SilentlyContinue
$p = Start-Process $Exe -ArgumentList '--retail' -WorkingDirectory $root -PassThru `
     -RedirectStandardError $log -RedirectStandardOutput "logs\$Tag.stdout.log"
$null = $p.Handle
function Press([string[]]$b, [int]$after) { & "$PSScriptRoot\press.ps1" @b; Start-Sleep $after }

Start-Sleep 90
Press start 8       # title -> Settings popup
Press a 8           # -> VIP popup
Press a 8           # -> title
Press start 8       # -> main menu
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-nav1-mainmenu.png" | Out-Null
Press a 18          # Quick Game -> Team Select (home cursor on default team)
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-nav2-afterA.png" | Out-Null

# Step the HOME team with LT until the log says Alabama loaded.
$pat = 'AlabamaCrimsonTide'
$before = (Select-String -Path $log -Pattern $pat -ErrorAction SilentlyContinue | Measure-Object).Count
$found = $false
$anyLoad = 'loads added team'
for ($i = 0; $i -lt $MaxTeamSteps; $i++) {
    & "$PSScriptRoot\press.ps1" lt
    Start-Sleep -Milliseconds 1500
    if ($i -eq 4) { & "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-nav3-teamselect.png" | Out-Null }
    $now = (Select-String -Path $log -Pattern $pat -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($now -gt $before) { "Alabama reached after $($i + 1) LT presses"; $found = $true; break }
}
"total 'loads added team' events seen: $((Select-String -Path $log -Pattern $anyLoad -ErrorAction SilentlyContinue | Measure-Object).Count)"
if (-not $found) { "Alabama NOT reached in $MaxTeamSteps steps"; Stop-Process -Id $p.Id -Force; exit 1 }

Press left 3        # from home: one Left = middle = CPU vs CPU (home stays Alabama)
Press start 12      # Team Select -> Coach Matchup
Press a 30          # Start Game -> loading -> pregame
Press a 6           # skip pregame

$t0 = Get-Date; $next = 0
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $PlaySeconds) {
    $s = [int]((Get-Date) - $t0).TotalSeconds
    if ($s -ge $next) { & "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-$s.png" | Out-Null; $next += $ShotEvery }
    Start-Sleep 2
}
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
"--- stadium binding log ---"
Select-String -Path $log -Pattern '\[TEAMFILES\].*stadium' | ForEach-Object { $_.Line }
"--- any Eras stadium pack log ---"
Select-String -Path $log -Pattern '\[TEAMS\].*stadium pack' | ForEach-Object { $_.Line }
"--- crashes ---"
Select-String -Path $log -Pattern '^\[CRASH\]' | ForEach-Object { $_.Line }
"done"
