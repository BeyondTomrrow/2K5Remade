# In-game check of custom soundtracks (Crib Jukebox / Music folder; docs/MUSIC-FIX-20261021.md), background
# mode only: RECOMP_BACKGROUND=1 (off-screen, never takes focus), RECOMP_AUDIO_MUTE=1 (speakers silent; the
# RECOMP_AUDIO_WAV recording taps the mix before the mute), NFL2K5_NO_HOST_PAD=always, its own press file,
# NFL2K5_PRIVATE_PACKS=1, its OWN exe copy (build\Release\NFL2K5_music.exe), a sandbox NFL2K5_ROOT and a
# sandbox user folder (-AppData). It only ever stops the PID it started; it never stops NFL2K5.exe.
# Adapted from tools\eras\draftshow\draftshow_test.ps1.
#
# The sandbox root (default logs\music\sbx-root) holds directory junctions to the real original, mods, cache,
# Music, font, assets, config, tools, data ...; and a COPIED saves\ (Soundtracks included; without the
# Partition*.img images and backups\). Junctions are removed with `cmd /c rmdir` only.
#
#   music_test.ps1 -Do setup
#   music_test.ps1 -Do launch -Tag m1 [-Pack SOFTDRINK_2K28_v0.6-v0.6] [-SetEnv K=V,...]   (no -Pack: --retail)
#   music_test.ps1 -Do press  -Tag m1 -Steps down,down:3,a    buttons ('<btn>[:<seconds after>]')
#   music_test.ps1 -Do shot   -Tag m1 -Name x                 newest presented frame -> logs\music\<tag>-<name>.png
#   music_test.ps1 -Do stop   -Tag m1                         stop that PID, delete the exe copy
#   music_test.ps1 -Do teardown                               remove the junctions and the root
param([ValidateSet('setup','launch','press','shot','watch','stop','teardown')][string]$Do = 'launch',
      [string]$Tag = 'ds1', [string[]]$Steps = @(), [string]$Name = 'now', [int]$Seconds = 60, [int]$Every = 2,
      [string]$AppData = '', [string[]]$SetEnv = @(), [string]$SandboxRoot = '',
      [string]$HomeFrom = 'logs\eras\office80-home', [string]$Pack = '', [switch]$Foreground)
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $root
$out = "$root\logs\music"
$frames = "$out\$Tag-frames"
$log = "$out\$Tag.stderr.log"
$press = "$out\$Tag-press.txt"
$pidFile = "$out\$Tag.pid"
$exe = "$root\build\Release\NFL2K5_music.exe"
if (-not $AppData) { $AppData = "$out\home" }
if (-not $SandboxRoot) { $SandboxRoot = "$out\sbx-root" }
New-Item -ItemType Directory -Force $out | Out-Null

function Junction([string]$link, [string]$target) { cmd /c mklink /J "$link" "$target" | Out-Null }
function Newest { Get-ChildItem $frames -Filter 'p*.bmp' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
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
    if ($p -and $p.ProcessName -eq 'NFL2K5_music') { return $p }
    return $null
}
function Junctions([string]$dir) {
    if (Test-Path -LiteralPath $dir) {
        Get-ChildItem -LiteralPath $dir -Force -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
    }
}

switch ($Do) {
'setup' {
    if (-not (Test-Path $SandboxRoot)) {
        New-Item -ItemType Directory $SandboxRoot | Out-Null
        foreach ($d in 'original','mods','cache','Music','font','assets','config','tools','dependencies','external','data') {
            if (Test-Path "$root\$d") { Junction "$SandboxRoot\$d" "$root\$d" }
        }
        New-Item -ItemType Directory "$SandboxRoot\saves" | Out-Null
        Get-ChildItem "$root\saves" -Force | Where-Object { $_.Name -notlike 'Partition*.img' -and $_.Name -ne 'backups' } |
            ForEach-Object { Copy-Item $_.FullName "$SandboxRoot\saves\" -Recurse }
        "sandbox: $SandboxRoot"
        Junctions $SandboxRoot | ForEach-Object { "  junction $($_.FullName)" }
    } else { "sandbox exists: $SandboxRoot" }
    if (-not (Test-Path $AppData)) {
        Copy-Item "$root\$HomeFrom" $AppData -Recurse
        "user folder: $AppData (copied from $HomeFrom)"
    } else { "user folder exists: $AppData" }
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
    $env:NFL2K5_PRESENT_SHOT = "$frames\p"
    $env:NFL2K5_PRESENT_SHOT_SECONDS = '1'
    $env:NFL2K5_REC = "$frames\rec"
    $env:RECOMP_BACKGROUND_WINDOW = '1280x720'
    $ff = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($ff) { $t = (Get-Item $ff.Source).Target; $env:NFL2K5_FFMPEG = if ($t) { [string]$t } else { $ff.Source } }
    Remove-Item Env:RECOMP_SHOT_PREFIX -ErrorAction SilentlyContinue
    Remove-Item Env:NFL2K5_ERAS_CAPTURE -ErrorAction SilentlyContinue
    $env:LOCALAPPDATA = (Resolve-Path $AppData).Path
    $env:NFL2K5_ERAS_LOG = '1'
    $env:NFL2K5_PRES_LOG = '1'
    $env:NFL2K5_PRIVATE_PACKS = '1'
    $env:NFL2K5_ROOT = (Resolve-Path $SandboxRoot).Path
    $env:NFL2K5_RENDER_SCALE = '1'
    Remove-Item Env:NFL2K5_ERAS_STAGE_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:NFL2K5_AUTO_PRESS -ErrorAction SilentlyContinue
    if ($Foreground) {   # the PC is free: a normal window, speakers on, the player's own video settings
        foreach ($v in 'RECOMP_BACKGROUND','RECOMP_AUDIO_MUTE','RECOMP_BACKGROUND_WINDOW','NFL2K5_RENDER_SCALE') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
    }
    foreach ($kv in ($SetEnv | ForEach-Object { $_ -split ',' })) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; "  env $k=$v" }
    Remove-Item -LiteralPath $press -ErrorAction SilentlyContinue
    $args0 = if ($Pack) { @('--run') } else { @('--retail') }
    if ($Pack) { $env:NFL2K5_INTERP_PACK = $Pack } else { Remove-Item Env:NFL2K5_INTERP_PACK -ErrorAction SilentlyContinue }
    $env:RECOMP_AUDIO_WAV = "$out\$Tag.wav"
    $p = Start-Process $exe -ArgumentList $args0 -WorkingDirectory $env:NFL2K5_ROOT -PassThru `
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
