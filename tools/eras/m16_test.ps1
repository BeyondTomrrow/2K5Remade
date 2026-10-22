# M16 (menus fix) in-game tests, background mode only: off-screen, never takes focus
# (RECOMP_BACKGROUND), muted, buttons through its own press file, no host pad, renderer frames.
# The game runs from a SANDBOX root (NFL2K5_ROOT, E:\eras-sbx\m16-root) of junctions to this
# checkout's folders (original, data, assets, cache, config, dependencies, external, font, Music,
# tools; mods\<each sub-folder> and mods\packs\<each pack>) with its own COPY of saves\ and of the
# files at mods\ (features.ini, ...), its own mods\active-pack.ini, and a sandbox user folder
# (LOCALAPPDATA). The exe is a copy named NFL2K5_menus.exe (its own private archive folder, so a
# rerun does not write another 330 MB pack). Junctions are removed only with `cmd /c rmdir`
# (-Do clean). It only ever stops the process it started (by PID).
#
#   m16_test.ps1 -Do launch -Tag t1 [-SoftDrink] [-GameArgs --retail] [-UserLeague] [-SetEnv K=V,...]
#   m16_test.ps1 -Do press -Tag t1 -Steps start,a:3,down     ('<btn>[:<seconds after>]')
#   m16_test.ps1 -Do shot  -Tag t1 -Name x                  newest frame -> logs\eras\m16-<tag>-<name>.png
#   m16_test.ps1 -Do stop  -Tag t1
#   m16_test.ps1 -Do clean                                  remove the sandbox's junctions (rmdir only)
param([ValidateSet('launch','press','shot','stop','clean')][string]$Do = 'launch', [string]$Tag = 'm16',
      [string]$Exe = '', [switch]$SoftDrink, [string]$SoftDrinkPack = 'SOFTDRINK_2K28_v0.6-v0.6',
      [string[]]$GameArgs = @(), [switch]$UserLeague,
      [string[]]$Steps = @(), [string]$Name = 'now', [int]$QuarterSeconds = 60, [string[]]$SetEnv = @())
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sbx = "E:\eras-sbx\m16-root"
$out = "$root\logs\eras"
$frames = "$out\m16-$Tag-frames"
$log = "$out\m16-$Tag.stderr.log"
$press = "$out\m16-$Tag-press.txt"
$pidFile = "$out\m16-$Tag.pid"
$home_ = "$out\m16-$Tag-home"
New-Item -ItemType Directory -Force $out | Out-Null

function J([string]$link, [string]$target) {
    if (Test-Path $link) { return }
    cmd /c mklink /J "$link" "$target" | Out-Null
}
function Unjunction([string]$dir) {
    if (-not (Test-Path $dir)) { return }
    foreach ($l in Get-ChildItem $dir -Force -ErrorAction SilentlyContinue) {
        if ($l.Attributes -band [IO.FileAttributes]::ReparsePoint) { cmd /c rmdir "$($l.FullName)" | Out-Null }
    }
}
function Newest { Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
function Keep([string]$n) {
    $f = Newest
    if ($f) { & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$out\m16-$Tag-$n.png"; "$out\m16-$Tag-$n.png" }
    else { "no frame yet" }
    # one 1 MB frame a second: keep only the newest few (the drive is nearly full)
    Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -SkipLast 5 | Remove-Item -ErrorAction SilentlyContinue
}
function Mine {
    if (-not (Test-Path $pidFile)) { return $null }
    $p = Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq 'NFL2K5_menus') { return $p }
    return $null
}

switch ($Do) {
'launch' {
    if (Mine) { "already running: pid $((Mine).Id)"; break }
    New-Item -ItemType Directory -Force $sbx, "$sbx\mods", "$sbx\mods\packs", "$sbx\logs" | Out-Null
    foreach ($d in 'original', 'data', 'assets', 'cache', 'config', 'dependencies', 'external', 'font', 'Music', 'tools') {
        if (Test-Path "$root\$d") { J "$sbx\$d" "$root\$d" }
    }
    foreach ($d in Get-ChildItem "$root\mods" -Directory) {
        if ($d.Name -in @('packs', 'patches', 'downloads')) { continue }
        J "$sbx\mods\$($d.Name)" $d.FullName
    }
    foreach ($d in Get-ChildItem "$root\mods\packs" -Directory) { J "$sbx\mods\packs\$($d.Name)" $d.FullName }
    foreach ($f in Get-ChildItem "$root\mods\packs" -File) { Copy-Item $f.FullName "$sbx\mods\packs\" -Force }
    New-Item -ItemType Directory -Force "$sbx\mods\patches" | Out-Null
    foreach ($f in Get-ChildItem "$root\mods" -File) { if ($f.Name -notlike 'active-pack.ini*') { Copy-Item $f.FullName "$sbx\mods\" -Force } }
    foreach ($f in Get-ChildItem $root -File -Filter *.ini) { Copy-Item $f.FullName "$sbx\" -Force }
    # saves\Partition*.img are sparse (5.4 GB apparent, ~0.5 GB used): Copy-Item would fill them in
    # and fill the drive; Git's cp keeps them sparse
    if (-not (Test-Path "$sbx\saves")) {
        New-Item -ItemType Directory -Force "$sbx\saves" | Out-Null
        & 'C:\Program Files\Git\usr\bin\cp.exe' -r --sparse=always "$root/saves/." "$sbx/saves/"
        if ($LASTEXITCODE) { throw "copying saves failed" }
    }
    Remove-Item "$sbx\mods\active-pack.ini" -ErrorAction SilentlyContinue
    if (Test-Path "$root\mods\active-pack.ini") { Copy-Item "$root\mods\active-pack.ini" "$sbx\mods\active-pack.ini" }
    if ($SoftDrink) { Set-Content "$sbx\mods\active-pack.ini" $SoftDrinkPack -Encoding ascii }
    if (-not $Exe) { $Exe = "$root\build\Release\NFL2K5_menus.exe" }
    $copy = "$sbx\NFL2K5_menus.exe"
    Copy-Item $Exe $copy -Force
    Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $frames, $home_ -Force | Out-Null
    # -UserLeague: a COPY of the user's own league (read only) in the sandbox user folder
    if ($UserLeague) {
        $ul = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NFL2K5-PC\eras\league1'
        New-Item -ItemType Directory -Force "$home_\NFL2K5-PC\eras" | Out-Null
        if (Test-Path $ul) { Copy-Item $ul "$home_\NFL2K5-PC\eras\league1" -Recurse -Force }
    }
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
    foreach ($k in 'NFL2K5_ERAS', 'NFL2K5_ERAS_MENU', 'NFL2K5_ERAS_CAPTURE', 'NFL2K5_ERAS_STAGE_TEST', 'NFL2K5_AUTO_PRESS', 'NFL2K5_RETAIL', 'NFL2K5_VANILLA', 'NFL2K5_TEST_FREE_MB', 'NFL2K5_ERAS_HUB_TEST') { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v }
    Remove-Item -LiteralPath $press -ErrorAction SilentlyContinue
    $argList = @($GameArgs | Where-Object { $_ })
    $sp = @{ FilePath = $copy; WorkingDirectory = $sbx; PassThru = $true; RedirectStandardError = $log; RedirectStandardOutput = "$out\m16-$Tag.stdout.log" }
    if ($argList.Count) { $sp.ArgumentList = $argList }
    $p = Start-Process @sp
    Set-Content -Path $pidFile -Value $p.Id -Encoding ascii
    "launched pid $($p.Id) ($copy $($argList -join ' ')), softdrink $SoftDrink, user league $UserLeague, log $log"
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
    if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "stopped pid $($p.Id)" } else { "not running (exited on its own?)" }
    Remove-Item $pidFile -ErrorAction SilentlyContinue
}
'clean' {
    if (Mine) { "still running"; break }
    Unjunction "$sbx\mods\packs"
    Unjunction "$sbx\mods"
    Unjunction $sbx
    Remove-Item "$sbx\NFL2K5_menus.exe" -ErrorAction SilentlyContinue
    $left = @($sbx, "$sbx\mods", "$sbx\mods\packs") | ForEach-Object { Get-ChildItem $_ -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue }
    "junctions left: $(@($left).Count)"
}
}
