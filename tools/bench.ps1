param([int[]]$Scales = @(2, 4, 8, 12), [int]$PlaySeconds = 120, [string]$Tag = 'bench', [string]$Exe = 'build\Release\NFL2K5.exe',
      [string]$Out = 'docs\BENCHMARKS.md', [string]$Title = '')
# Repeatable gameplay benchmark. For each internal resolution scale it plays
# the CPU-vs-CPU quick game (tools\drive-quickgame.ps1: same menus, same
# teams, kickoff after the coin toss) and then summarises the renderer's
# [BENCH] lines (one per 10 s: average FPS, 1%/0.1% lows, frame time, GPU ms,
# process CPU ms per frame, working set, private bytes, VRAM) with
# tools\bench-report.py, appending the table to $Out.
# The game clock is never changed: the game simulates 60 frames a second at
# most, so 60 is the ceiling at every scale.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
foreach ($s in $Scales) {
    $env:NFL2K5_RENDER_SCALE = "$s"
    & "$PSScriptRoot\drive-quickgame.ps1" -Tag "$Tag-$s" -PlaySeconds $PlaySeconds -ShotEvery 100000 -Exe $Exe | Out-Null
    Remove-Item Env:NFL2K5_RENDER_SCALE
}
$keep = [math]::Max(1, [int]($PlaySeconds / 10) - 3)
& python "$PSScriptRoot\bench-report.py" $Tag @Scales --keep $keep --out $Out --title $(if ($Title) { $Title } else { $Tag })
