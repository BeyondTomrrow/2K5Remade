# My NFL Eras M10 (the feature pack) in-game tests, background mode only: off-screen, never
# takes focus (RECOMP_BACKGROUND), muted, buttons through its own press file, no host pad,
# renderer frames. The game runs from a SANDBOX root (NFL2K5_ROOT) made of junctions to this
# checkout's original\, data\, font\, cache\ and mods\ sub-folders, with its own copy of saves\
# and its own mods\packs\ (MyNFLEras in or out, per test), mods\active-pack.ini and user folder
# (-AppData). The real saves\ and mods\ are never written. It only ever stops the process it
# started (by PID). Take build\.eras-build.lock first when another agent shares the PC.
#
#   m10_test.ps1 -Do launch -Tag t1 [-Exe build\Release\NFL2K5.exe] [-Pack] [-SoftDrink] [-GameArgs --retail]
#                [-SetEnv K=V,...]
#   m10_test.ps1 -Do press -Tag t1 -Steps start,a:3,down     ('<btn>[:<seconds after>]')
#   m10_test.ps1 -Do shot  -Tag t1 -Name x                  newest frame -> logs\eras\m10-<tag>-<name>.png
#   m10_test.ps1 -Do stop  -Tag t1
param([ValidateSet('launch','press','shot','stop')][string]$Do = 'launch', [string]$Tag = 'm10',
      [string]$Exe = '', [switch]$Pack, [string]$PackDir = '', [switch]$SoftDrink, [string[]]$GameArgs = @('--retail'),
      [string[]]$Steps = @(), [string]$Name = 'now', [int]$QuarterSeconds = 60, [string[]]$SetEnv = @())
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sbx = "E:\eras-sbx\root"
$out = "$root\logs\eras"
$frames = "$out\m10-$Tag-frames"
$log = "$out\m10-$Tag.stderr.log"
$press = "$out\m10-$Tag-press.txt"
$pidFile = "$out\m10-$Tag.pid"
$home_ = "$out\m10-$Tag-home"
New-Item -ItemType Directory -Force $out | Out-Null

function J([string]$link, [string]$target) {
    if (Test-Path $link) { return }
    cmd /c mklink /J "$link" "$target" | Out-Null
}
function Newest { Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
function Keep([string]$n) {
    $f = Newest
    if ($f) { & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$out\m10-$Tag-$n.png"; "$out\m10-$Tag-$n.png" }
    else { "no frame yet" }
}
function Mine {
    if (-not (Test-Path $pidFile)) { return $null }
    $p = Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -like 'NFL2K5_m10*') { return $p }
    return $null
}

switch ($Do) {
'launch' {
    if (Mine) { "already running: pid $((Mine).Id)"; break }
    # the sandbox root
    New-Item -ItemType Directory -Force $sbx, "$sbx\mods", "$sbx\logs" | Out-Null
    foreach ($d in 'original', 'data', 'font', 'cache', 'Music') { if (Test-Path "$root\$d") { J "$sbx\$d" "$root\$d" } }
    foreach ($d in Get-ChildItem "$root\mods" -Directory) {
        if ($d.Name -in @('packs', 'patches', 'downloads')) { continue }
        J "$sbx\mods\$($d.Name)" $d.FullName
    }
    New-Item -ItemType Directory -Force "$sbx\mods\patches" | Out-Null
    foreach ($f in Get-ChildItem "$root\mods" -File) { if ($f.Name -notlike 'active-pack.ini*') { Copy-Item $f.FullName "$sbx\mods\" -Force } }
    foreach ($f in Get-ChildItem $root -File -Filter *.ini) { Copy-Item $f.FullName "$sbx\" -Force }
    if (-not (Test-Path "$sbx\saves")) { Copy-Item "$root\saves" "$sbx\saves" -Recurse }
    # mods\packs: rebuilt for this test (junctions; removed with rmdir, never with a recursive delete)
    if (Test-Path "$sbx\mods\packs") {
        foreach ($l in Get-ChildItem "$sbx\mods\packs" -Force) { cmd /c rmdir "$($l.FullName)" 2>$null | Out-Null; if (Test-Path $l.FullName) { Remove-Item $l.FullName -Force } }
    }
    New-Item -ItemType Directory -Force "$sbx\mods\packs" | Out-Null
    foreach ($d in Get-ChildItem "$root\mods\packs" -Directory) {
        if ($d.Name -eq 'MyNFLEras') { continue }
        J "$sbx\mods\packs\$($d.Name)" $d.FullName
    }
    # -Pack: this checkout's mods\packs\MyNFLEras; -PackDir: another pack folder as MyNFLEras
    if ($PackDir) { J "$sbx\mods\packs\MyNFLEras" (Resolve-Path $PackDir).Path }
    elseif ($Pack) { J "$sbx\mods\packs\MyNFLEras" "$root\mods\packs\MyNFLEras" }
    Remove-Item "$sbx\mods\active-pack.ini" -ErrorAction SilentlyContinue
    if ($SoftDrink) { Set-Content "$sbx\mods\active-pack.ini" 'SOFTDRINK_2K28_v0.6-v0.6' -Encoding ascii }
    # the exe copy (its own name: its own private archive pack G)
    if (-not $Exe) { $Exe = "$root\build\Release\NFL2K5.exe" }
    $copy = "$sbx\NFL2K5_m10$Tag.exe"
    Copy-Item $Exe $copy -Force
    Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $frames, $home_ -Force | Out-Null
    $env:NFL2K5_ROOT = $sbx
    $env:NFL2K5_PORTABLE = '0'
    $env:LOCALAPPDATA = (Resolve-Path $home_).Path
    $env:NFL2K5_SKIP_INTRO = '1'
    $env:NFL2K5_PRESS_FILE = $press
    $env:NFL2K5_NO_HOST_PAD = 'always'
    $env:RECOMP_BACKGROUND = '1'
    $env:RECOMP_AUDIO_MUTE = '1'
    $env:RECOMP_SHOT_PREFIX = "$frames\f"
    $env:RECOMP_SHOT_SECONDS = '1'
    $env:NFL2K5_ERAS_LOG = '1'
    $env:NFL2K5_PRIVATE_PACKS = '1'
    $env:NFL2K5_NO_PACK_DISPATCH = '1'
    $env:NFL2K5_QUARTER_SECONDS = "$QuarterSeconds"
    $env:NFL2K5_RENDER_SCALE = '1'
    foreach ($k in 'NFL2K5_ERAS', 'NFL2K5_ERAS_CAPTURE', 'NFL2K5_ERAS_STAGE_TEST', 'NFL2K5_AUTO_PRESS', 'NFL2K5_RETAIL', 'NFL2K5_VANILLA') { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }
    Remove-Item -LiteralPath $press -ErrorAction SilentlyContinue
    $argList = @($GameArgs | Where-Object { $_ })
    $sp = @{ FilePath = $copy; WorkingDirectory = $sbx; PassThru = $true; RedirectStandardError = $log; RedirectStandardOutput = "$out\m10-$Tag.stdout.log" }
    if ($argList.Count) { $sp.ArgumentList = $argList }
    $p = Start-Process @sp
    Set-Content -Path $pidFile -Value $p.Id -Encoding ascii
    "launched pid $($p.Id) ($copy $($argList -join ' ')), pack $(if ($PackDir) {$PackDir} elseif ($Pack) {'IN'} else {'OUT'}), softdrink $SoftDrink, log $log"
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
'stop' {
    $p = Mine
    if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "stopped pid $($p.Id)" } else { "not running" }
    Remove-Item $pidFile -ErrorAction SilentlyContinue
    Remove-Item "$sbx\NFL2K5_m10$Tag.exe" -ErrorAction SilentlyContinue
}
}
