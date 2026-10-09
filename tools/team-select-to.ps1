# Step Team Select until the log says the wanted team loaded (scripted tests of
# added teams, src/nfl2k5_teams.c). The game must be running on Team Select with
# NFL2K5_TEAMS_LOG=1 and its stderr in logs\<Tag>.stderr.log.
#   tools\team-select-to.ps1 -Tag zt3 -Pattern 'loads added team' [-Button lt] [-Max 120]
param([string]$Tag, [string]$Pattern = 'loads added team', [string]$Button = 'lt', [int]$Max = 120)
$root = Split-Path $PSScriptRoot -Parent
$log = Join-Path $root "logs\$Tag.stderr.log"
$start = (Select-String -Path $log -Pattern $Pattern -ErrorAction SilentlyContinue | Measure-Object).Count
for ($i = 0; $i -lt $Max; $i++) {
    & "$PSScriptRoot\press.ps1" $Button
    Start-Sleep -Milliseconds 1600
    $now = (Select-String -Path $log -Pattern $Pattern -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($now -gt $start) { "found after $($i + 1) presses"; exit 0 }
}
"not found after $Max presses"
exit 1
