# In-game check of the NCAA 07 stadium packs (tools/eras/stadiums/ncaa_stadiums.py) through My NFL Eras, background
# mode only (RECOMP_BACKGROUND=1, RECOMP_AUDIO_MUTE=1, NFL2K5_NO_HOST_PAD=always, NFL2K5_PRIVATE_PACKS=1), so it can
# run beside another workflow: its OWN exe copy (build\Release\NFL2K5_stadiums.exe), its own press file and frame
# folder, a sandbox NFL2K5_ROOT and a sandbox user folder (-AppData). It only ever stops the PID it started; it
# never stops a process named NFL2K5.exe. Adapted from coliseum_eras_test.ps1 / tools\eras\sched_test.ps1.
#
# The sandbox root (default logs\eras\stadsbx-root) holds directory junctions to the real original, mods, cache,
# Music, font, assets, config, tools ...; a COPIED saves\ (without the Partition*.img images and backups\); and a
# data\ overlay whose eras\runtime\1980\ is a copy, so the teams.csv edit never touches the real file.
#
#   ncaa_eras_test.ps1 -Do setup                                     build the sandbox root
#   ncaa_eras_test.ps1 -Do stadium -Team vikings -Stadium 'MIAMI ORANGE BOWL'   sandbox teams.csv: that team's stadium
#   ncaa_eras_test.ps1 -Do launch -Tag ob1                           copy the exe, start it (--retail), return
#   ncaa_eras_test.ps1 -Do press  -Tag ob1 -Steps down,down:3,a      buttons ('<btn>[:<seconds after>]')
#   ncaa_eras_test.ps1 -Do shot   -Tag ob1 -Name x                   newest frame -> logs\eras\<tag>-<name>.png
#   ncaa_eras_test.ps1 -Do watch  -Tag ob1 -Seconds 120 -Every 5     keep a frame every N s (no presses)
#   ncaa_eras_test.ps1 -Do stop   -Tag ob1                           stop that PID, delete the exe copy
#   ncaa_eras_test.ps1 -Do teardown                                  remove the junctions (cmd /c rmdir) and the root
param([ValidateSet('setup','stadium','launch','press','shot','watch','stop','teardown')][string]$Do = 'launch',
      [string]$Tag = 'ob1', [string[]]$Steps = @(), [string]$Name = 'now', [int]$Seconds = 120, [int]$Every = 5,
      [int]$QuarterSeconds = 60, [string]$AppData = '', [string[]]$SetEnv = @(), [string]$SandboxRoot = '',
      [string]$Team = 'vikings', [string]$Stadium = '')
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
Set-Location $root
$out = "$root\logs\eras"
$frames = "$out\$Tag-frames"
$log = "$out\$Tag.stderr.log"
$press = "$out\$Tag-press.txt"
$pidFile = "$out\$Tag.pid"
$exe = "$root\build\Release\NFL2K5_stadiums.exe"
if (-not $AppData) { $AppData = "$out\$Tag-home" }
if (-not $SandboxRoot) { $SandboxRoot = "$out\stadsbx-root" }
New-Item -ItemType Directory -Force $out | Out-Null

function Junction([string]$link, [string]$target) { cmd /c mklink /J "$link" "$target" | Out-Null }
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
    if ($p -and $p.ProcessName -eq 'NFL2K5_stadiums') { return $p }
    return $null
}
# every directory that is a junction under the sandbox root, deepest first
# (never recursive: a recursive listing would walk into the junctions' real targets)
function Junctions([string]$dir) {
    foreach ($d in "$dir\data\eras\runtime", "$dir\data\eras", "$dir\data", $dir) {
        if (Test-Path -LiteralPath $d) {
            Get-ChildItem -LiteralPath $d -Force -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
        }
    }
}

switch ($Do) {
'setup' {
    if (Test-Path $SandboxRoot) { "sandbox exists: $SandboxRoot (teardown first)"; break }
    New-Item -ItemType Directory $SandboxRoot | Out-Null
    foreach ($d in 'original','mods','cache','Music','font','assets','config','tools','dependencies','external') {
        if (Test-Path "$root\$d") { Junction "$SandboxRoot\$d" "$root\$d" }
    }
    # data\ overlay: junctions everywhere except data\eras\runtime\1980 (copied)
    New-Item -ItemType Directory "$SandboxRoot\data\eras\runtime" -Force | Out-Null
    Get-ChildItem "$root\data" -Force | Where-Object { $_.Name -ne 'eras' } | ForEach-Object { Junction "$SandboxRoot\data\$($_.Name)" $_.FullName }
    Get-ChildItem "$root\data\eras" -Force | Where-Object { $_.Name -ne 'runtime' } | ForEach-Object {
        if ($_.PSIsContainer) { Junction "$SandboxRoot\data\eras\$($_.Name)" $_.FullName } else { Copy-Item $_.FullName "$SandboxRoot\data\eras\" }
    }
    Get-ChildItem "$root\data\eras\runtime" -Force | Where-Object { $_.Name -ne '1980' } | ForEach-Object { Junction "$SandboxRoot\data\eras\runtime\$($_.Name)" $_.FullName }
    Copy-Item "$root\data\eras\runtime\1980" "$SandboxRoot\data\eras\runtime\1980" -Recurse
    # saves\ copy (the Partition*.img are the old xemu HDD images, unused by this build; backups\ not needed)
    New-Item -ItemType Directory "$SandboxRoot\saves" | Out-Null
    Get-ChildItem "$root\saves" -Force | Where-Object { $_.Name -notlike 'Partition*.img' -and $_.Name -ne 'backups' } |
        ForEach-Object { Copy-Item $_.FullName "$SandboxRoot\saves\" -Recurse }
    "sandbox: $SandboxRoot"
    Junctions $SandboxRoot | ForEach-Object { "  junction $($_.FullName)" }
}
'stadium' {
    $csv = "$SandboxRoot\data\eras\runtime\1980\teams.csv"
    & python -c @"
import csv, io, sys
p, team, stad = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in raw else '\n'
lines = raw.split(nl)
head = next(csv.reader([lines[0]]))
col = head.index('stadium')
for i, line in enumerate(lines[1:], 1):
    if not line: continue
    row = next(csv.reader([line]))
    if row[0] == team:
        print('was', row[col]); row[col] = stad
        b = io.StringIO(); csv.writer(b, lineterminator='').writerow(row); lines[i] = b.getvalue()
open(p, 'wb').write(nl.join(lines).encode('utf-8'))
print('now', stad)
"@ $csv $Team $Stadium
}
'launch' {
    if (Mine) { "already running: pid $((Mine).Id)"; break }
    if (-not (Test-Path "$SandboxRoot\saves")) { "no sandbox: run -Do setup first"; break }
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
    $env:NFL2K5_TEAMS_LOG = '1'
    $env:NFL2K5_PRIVATE_PACKS = '1'
    $env:NFL2K5_ROOT = (Resolve-Path $SandboxRoot).Path
    $env:NFL2K5_QUARTER_SECONDS = "$QuarterSeconds"
    $env:NFL2K5_RENDER_SCALE = '1'
    Remove-Item Env:NFL2K5_ERAS_STAGE_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }
    Remove-Item -LiteralPath $press -ErrorAction SilentlyContinue
    $p = Start-Process $exe -ArgumentList '--retail' -WorkingDirectory $env:NFL2K5_ROOT -PassThru `
         -RedirectStandardError $log -RedirectStandardOutput "$out\$Tag.stdout.log"
    Set-Content -Path $pidFile -Value $p.Id -Encoding ascii
    "launched pid $($p.Id), log $log, frames $frames, user folder $AppData, root $env:NFL2K5_ROOT"
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
'teardown' {
    if (Mine) { "still running: stop it first"; break }
    if (-not (Test-Path $SandboxRoot)) { "no sandbox"; break }
    foreach ($j in (Junctions $SandboxRoot)) { cmd /c rmdir "$($j.FullName)" }
    $left = Junctions $SandboxRoot
    if ($left) { "junctions left, not deleting:"; $left | ForEach-Object { "  $($_.FullName)" }; break }
    Remove-Item $SandboxRoot -Recurse -Force
    "removed $SandboxRoot"
}
}
