# Situation Mode to the first play call, with default settings and teams.
#   tools\drive-situation.ps1 [-Tag sit] [-Mode native|interp|retail] [-Seconds 40]
#   native : the active pack's native executable (mods\packs\<pack>\native)
#   interp : NFL2K5.exe with the active pack, its changed code interpreted
#   retail : NFL2K5.exe --retail (clean ESPN NFL 2K5)
# Path (one Settings popup at boot): Start, A -> Main Menu; Down, A -> Game
# Modes; Down x4, A -> Situation Settings; Start -> Team Select; Start -> game.
# Screenshots logs\<tag>-<step>.png, log logs\<tag>.stderr.log. Leaves the game running.
param([string]$Tag = 'sit', [ValidateSet('native', 'interp', 'retail')][string]$Mode = 'native', [int]$Seconds = 40)
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$exe = 'build\Release\NFL2K5.exe'
$argsList = @{}
Remove-Item Env:NFL2K5_NO_PACK_DISPATCH -ErrorAction SilentlyContinue
if ($Mode -eq 'native') {
    $pack = ((Get-Content 'mods\active-pack.ini' -Raw) -join '').Trim()
    $exe = "mods\packs\$pack\native\NFL2K5.exe"
} elseif ($Mode -eq 'interp') {
    $env:NFL2K5_NO_PACK_DISPATCH = '1'
} else {
    $argsList['ArgumentList'] = '--retail'
}
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath $env:NFL2K5_PRESS_FILE -ErrorAction SilentlyContinue
$p = Start-Process $exe @argsList -WorkingDirectory $root -PassThru -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
function Step([string]$b, [int]$wait, [string]$shot) {
    & "$PSScriptRoot\press.ps1" $b; Start-Sleep $wait
    if ($shot) { & "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-$shot.png" | Out-Null }
}
Start-Sleep 55
Step start 8 ''
Step a 7 'mainmenu'
Step down 3 ''
Step a 7 'gamemodes'
foreach ($k in 1..4) { Step down 2 '' }
Step a 8 'settings'
Step start 10 'teamselect'
Step start 14 'game'
Start-Sleep $Seconds
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-later.png" | Out-Null
"pid $($p.Id) exited=$($p.HasExited)"
