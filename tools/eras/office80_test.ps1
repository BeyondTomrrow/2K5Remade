# In-game check of the My NFL Eras office looks (the 1980s office: tools\eras\build-office-ibmpc.py and
# make-office-art.py), background mode (off-screen, never takes focus, muted, presses through its own press
# file, renderer frames). Adapted from tools\eras\stadiums\coliseum_eras_test.ps1: it runs its OWN exe copy
# (build\Release\NFL2K5_office80.exe) with NFL2K5_PRIVATE_PACKS=1 (its own pack G), its own press file and
# frame folder, a sandbox user folder (-AppData, default logs\eras\<tag>-home), and it only ever stops the
# process it started (by PID); it never stops a process named NFL2K5.exe.
#
#   office80_test.ps1 -Do launch -Tag o80a [-SetEnv NFL2K5_ERAS_OFFICE_HOLD=3]   copy the exe, start it, return
#   office80_test.ps1 -Do press  -Tag o80a -Steps down,down:3,a   buttons ('<btn>[:<seconds after>]')
#   office80_test.ps1 -Do shot   -Tag o80a -Name x    newest frame -> logs\eras\<tag>-<name>.png
#   office80_test.ps1 -Do watch  -Tag o80a -Seconds 60 -Every 5   keep a frame every N s (no presses)
#   office80_test.ps1 -Do stop   -Tag o80a            stop that PID, delete the exe copy
param([ValidateSet('launch','press','shot','watch','stop')][string]$Do = 'launch', [string]$Tag = 'office80',
      [string[]]$Steps = @(), [string]$Name = 'now', [int]$Seconds = 120, [int]$Every = 5,
      [int]$QuarterSeconds = 60, [string]$AppData = '', [string[]]$SetEnv = @())
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $root
$out = "$root\logs\eras"
$frames = "$out\$Tag-frames"
$log = "$out\$Tag.stderr.log"
$press = "$out\$Tag-press.txt"
$pidFile = "$out\$Tag.pid"
$exe = "$root\build\Release\NFL2K5_office80.exe"
if (-not $AppData) { $AppData = "$out\$Tag-home" }
New-Item -ItemType Directory -Force $out | Out-Null

function Newest { Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
function Keep([string]$n) {
    $f = Newest
    if ($f) {
        & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$out\$Tag-$n.png"
        "$out\$Tag-$n.png <- $($f.Name)"
    } else { "no frame yet" }
}
function Mine {
    if (-not (Test-Path $pidFile)) { return $null }
    $id = [int](Get-Content $pidFile)
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq 'NFL2K5_office80') { return $p }
    return $null
}

switch ($Do) {
'launch' {
    if (Mine) { "already running: pid $((Mine).Id)"; break }
    Copy-Item "$root\build\Release\NFL2K5.exe" $exe -Force
    Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $frames | Out-Null
    $env:NFL2K5_SKIP_INTRO = '1'
    $env:NFL2K5_PRESS_FILE = $press
    $env:NFL2K5_NO_HOST_PAD = 'always'
    $env:RECOMP_BACKGROUND = '1'
    $env:RECOMP_AUDIO_MUTE = '1'
    $env:RECOMP_SHOT_PREFIX = "$frames\f"
    $env:RECOMP_SHOT_SECONDS = '1'
    Remove-Item Env:NFL2K5_ERAS_CAPTURE -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $AppData | Out-Null
    $env:LOCALAPPDATA = (Resolve-Path $AppData).Path
    $env:NFL2K5_ERAS_LOG = '1'
    $env:NFL2K5_PRIVATE_PACKS = '1'
    $env:NFL2K5_QUARTER_SECONDS = "$QuarterSeconds"
    $env:NFL2K5_RENDER_SCALE = '1'
    Remove-Item Env:NFL2K5_ERAS_STAGE_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }   # e.g. NFL2K5_MEMDUMP=B306D0:40
    Remove-Item -LiteralPath $press -ErrorAction SilentlyContinue
    $p = Start-Process $exe -ArgumentList '--retail' -WorkingDirectory $root -PassThru `
         -RedirectStandardError $log -RedirectStandardOutput "$out\$Tag.stdout.log"
    Set-Content -Path $pidFile -Value $p.Id -Encoding ascii
    "launched pid $($p.Id), log $log, frames $frames, user folder $AppData"
}
'press' {
    foreach ($s in $Steps) {
        $wait = 2; $btn = $s
        if ($btn -match '^(.*):(\d+)$') { $btn = $Matches[1]; $wait = [int]$Matches[2] }
        Set-Content -Path $press -Value $btn -Encoding ascii
        Start-Sleep $wait
    }
    Keep $Name
}
'shot' { Keep $Name }
'watch' {
    $t0 = Get-Date; $k = 0
    while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
        if (-not (Mine)) { "EXITED"; break }
        Start-Sleep $Every
        $k++
        Keep ("{0}{1:d2}" -f $Name, $k) | Out-Null
    }
    "kept $k frames as $Tag-$Name##.png"
}
'stop' {
    $p = Mine
    if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "stopped pid $($p.Id)" } else { "not running" }
    Remove-Item $pidFile -ErrorAction SilentlyContinue
    Remove-Item $exe -ErrorAction SilentlyContinue
    if (Test-Path $exe) { "exe copy still present: $exe" } else { "exe copy deleted" }
}
}
