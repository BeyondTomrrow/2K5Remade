# My NFL Eras M1 tests (docs/eras/PHASE2-PLAN.md T1.1-T1.4): the clean game
# (--retail) in background mode -- off-screen, never takes focus, muted -- with
# the result capture armed for every game (NFL2K5_ERAS_CAPTURE=1), [ERAS] log
# lines on, and one-minute quarters. Buttons go through the press file only.
# Frames come from the renderer (RECOMP_SHOT_PREFIX, one per second).
#
#   tools\eras-capture-test.ps1 -Do launch -Tag t11 [-Stage colts,patriots]   start the game, return
#   tools\eras-capture-test.ps1 -Do press  -Tag t11 -Steps a,down:2   buttons (each '<btn>[:<s>]')
#   tools\eras-capture-test.ps1 -Do shot   -Tag t11 -Name ts          newest frame -> logs\eras\<tag>-<name>.png
#   tools\eras-capture-test.ps1 -Do play   -Tag t11 [-Until '\[ERAS\] final'] [-TapEvery 10] [-Minutes 30]
#        press A every TapEvery s (keeps host pads ignored, skips cut scenes) until the
#        stderr log matches -Until, keeping one frame every 15 s and pruning the rest
#   tools\eras-capture-test.ps1 -Do stop   -Tag t11
# M3 menu tests: -NoCapture (no NFL2K5_ERAS_CAPTURE), -AppData <dir> (LOCALAPPDATA for
# the game, so the user folder is <dir>\NFL2K5-PC: a sandbox profile for league
# saves), -SetEnv 'NAME=value',... (more variables, e.g. NFL2K5_ERAS_MENU=modes).
param([ValidateSet('launch','press','shot','play','stop')][string]$Do = 'launch', [string]$Tag = 'eras',
      [string[]]$Steps = @(), [string]$Name = 'now', [string]$Until = '\[ERAS\] final ', [int]$TapEvery = 10,
      [int]$Minutes = 30, [int]$QuarterSeconds = 60, [string]$Stage = '',
      [switch]$NoCapture, [string]$AppData = '', [string[]]$SetEnv = @())
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$out = "$root\logs\eras"
$frames = "$out\$Tag-frames"
$log = "$out\$Tag.stderr.log"
New-Item -ItemType Directory -Force $out | Out-Null

function Newest { Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
function Keep([string]$n) {
    $f = Newest
    if ($f) {
        & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$out\$Tag-$n.png"
        "$out\$Tag-$n.png <- $($f.Name)"
    } else { "no frame yet" }
}

switch ($Do) {
'launch' {
    Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 1
    Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $frames | Out-Null
    $env:NFL2K5_SKIP_INTRO = '1'
    $env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
    $env:NFL2K5_NO_HOST_PAD = 'always'
    $env:RECOMP_BACKGROUND = '1'
    $env:RECOMP_AUDIO_MUTE = '1'
    $env:RECOMP_SHOT_PREFIX = "$frames\f"
    $env:RECOMP_SHOT_SECONDS = '1'
    if ($NoCapture) { Remove-Item Env:NFL2K5_ERAS_CAPTURE -ErrorAction SilentlyContinue }
    else { $env:NFL2K5_ERAS_CAPTURE = '1' }
    if ($AppData) { New-Item -ItemType Directory -Force $AppData | Out-Null; $env:LOCALAPPDATA = (Resolve-Path $AppData).Path }
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }
    $env:NFL2K5_ERAS_LOG = '1'
    $env:NFL2K5_QUARTER_SECONDS = "$QuarterSeconds"
    $env:NFL2K5_RENDER_SCALE = '1'   # the saved scale (up to 9x) runs at 2 fps beside another game
    # M2: NFL2K5_ERAS_STAGE_TEST=<home>,<away> stages two 1980 franchises at the first Team Select step
    if ($Stage) { $env:NFL2K5_ERAS_STAGE_TEST = $Stage; $env:NFL2K5_TEAMS_LOG = '1' }
    else { Remove-Item Env:NFL2K5_ERAS_STAGE_TEST -ErrorAction SilentlyContinue }
    Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $env:NFL2K5_PRESS_FILE -ErrorAction SilentlyContinue
    $p = Start-Process 'build\Release\NFL2K5.exe' -ArgumentList '--retail' -WorkingDirectory $root -PassThru `
         -RedirectStandardError $log -RedirectStandardOutput "$out\$Tag.stdout.log"
    "launched pid $($p.Id), log $log, frames $frames"
}
'press' {
    foreach ($s in $Steps) {
        $wait = 2; $btn = $s
        if ($btn -match '^(.*):(\d+)$') { $btn = $Matches[1]; $wait = [int]$Matches[2] }
        & "$PSScriptRoot\press.ps1" $btn
        Start-Sleep $wait
    }
    Keep $Name
}
'shot' { Keep $Name }
'play' {
    $t0 = Get-Date; $nextTap = 0; $nextKeep = 0; $kept = @{}
    while (((Get-Date) - $t0).TotalMinutes -lt $Minutes) {
        if (-not (Get-Process NFL2K5 -ErrorAction SilentlyContinue)) { "EXITED"; break }
        if (Select-String -Path $log -Pattern $Until -ErrorAction SilentlyContinue) { "MATCHED '$Until' after $([int]((Get-Date) - $t0).TotalSeconds) s"; break }
        $s = [int]((Get-Date) - $t0).TotalSeconds
        if ($TapEvery -gt 0 -and $s -ge $nextTap) { & "$PSScriptRoot\press.ps1" a; $nextTap = $s + $TapEvery }
        if ($s -ge $nextKeep) { $f = Newest; if ($f) { $kept[$f.Name] = 1 }; $nextKeep = $s + 15 }
        # prune: keep every 15th-second frame and the last 30
        $all = Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime
        if ($all.Count -gt 40) { $all | Select-Object -First ($all.Count - 30) | Where-Object { -not $kept[$_.Name] } | Remove-Item -Force -ErrorAction SilentlyContinue }
        Start-Sleep 2
    }
    Keep $Name
}
'stop' {
    Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
    "stopped"
}
}
