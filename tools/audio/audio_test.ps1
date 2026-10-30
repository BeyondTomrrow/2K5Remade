# In-game audio captures (docs/AUDIO-FIX-20261021.md), background mode only: RECOMP_BACKGROUND=1
# (off-screen, never takes focus), RECOMP_AUDIO_MUTE=1 (speakers silent; RECOMP_AUDIO_WAV records the
# mix before the mute), NFL2K5_NO_HOST_PAD=always, its own press file, NFL2K5_PRIVATE_PACKS=1, its own
# exe copy build\Release\NFL2K5_audio.exe, a sandbox NFL2K5_ROOT (E:\eras-sbx\audio-root: junctions to
# this checkout's folders, mods\<each sub-folder> and mods\packs\<each pack>, its own mods\active-pack.ini,
# a SPARSE copy of saves\) and a sandbox user folder (logs\audio\<tag>-home, copied from the player's
# own %LOCALAPPDATA%\NFL2K5-PC settings so the presentation/ini settings match). It only ever stops the
# PID it started; it never touches NFL2K5.exe. Junctions are removed only with `cmd /c rmdir`.
#
#   audio_test.ps1 -Do launch -Tag b1 [-SoftDrink] [-SetEnv K=V,...]   (no -SoftDrink: --retail)
#   audio_test.ps1 -Do quickgame -Tag b1                               title -> CPU vs CPU Quick Game kickoff
#   audio_test.ps1 -Do play -Tag b1 -Seconds 420                       A every 6 s (coin toss, play calls)
#   audio_test.ps1 -Do press -Tag b1 -Steps start,a:3                  ('<btn>[:<seconds after>]')
#   audio_test.ps1 -Do shot  -Tag b1 -Name x                           newest frame -> logs\audio\<tag>-<name>.png
#   audio_test.ps1 -Do stop  -Tag b1
#   audio_test.ps1 -Do clean                                           remove the sandbox's junctions
# -ExeName / -Sbx / -OutDir: another exe-copy name, sandbox root and output folder, so a second agent's runs
# never share the first one's (AUDIO-REPORT-HOTKEY-20261022 used NFL2K5_audio2, E:\eras-sbxudio2-root, logsudio2).
param([ValidateSet('launch','quickgame','play','press','shot','stop','clean')][string]$Do = 'launch', [string]$Tag = 'a1', [int]$Seconds = 420,
      [switch]$SoftDrink, [string]$SoftDrinkPack = 'SOFTDRINK_2K28_v0.6.1-v0.6.1',
      [string[]]$Steps = @(), [string]$Name = 'now', [string[]]$SetEnv = @(), [int]$WavSeconds = 1200,
      [string]$SourceExe = '', [string]$ExeName = 'NFL2K5_audio', [string]$Sbx = 'E:\eras-sbx\audio-root',
      [string]$OutDir = '')
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sbx = $Sbx
$out = if ($OutDir) { $OutDir } else { "$root\logs\audio" }
$frames = "$out\$Tag-frames"
$log = "$out\$Tag.stderr.log"
$press = "$out\$Tag-press.txt"
$pidFile = "$out\$Tag.pid"
$home_ = "$out\$Tag-home"
$exe = "$root\build\Release\$ExeName.exe"
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
    if ($f) { & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$out\$Tag-$n.png"; "$out\$Tag-$n.png" }
    else { "no frame yet" }
    Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -SkipLast 5 | Remove-Item -ErrorAction SilentlyContinue
}
function Mine {
    if (-not (Test-Path $pidFile)) { return $null }
    $p = Get-Process -Id ([int](Get-Content $pidFile)) -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq $ExeName) { return $p }
    return $null
}
function Press([string[]]$list) {
    foreach ($s in $list) {
        $wait = 2; $btn = $s
        if ($btn -match '^(.*):(\d+)$') { $btn = $Matches[1]; $wait = [int]$Matches[2] }
        Set-Content -Path $press -Value $btn -Encoding ascii
        "$(Get-Date -Format HH:mm:ss) press $btn" | Add-Content "$out\$Tag-presses.log"
        Start-Sleep $wait
    }
}

switch ($Do) {
'launch' {
    if (Mine) { "already running: pid $((Mine).Id)"; break }
    $free = (Get-PSDrive E).Free / 1GB
    if ($free -lt 25) { "E: has only $([int]$free) GB free; not launching"; break }
    New-Item -ItemType Directory -Force $sbx, "$sbx\mods", "$sbx\mods\packs", "$sbx\logs" | Out-Null
    foreach ($d in 'original', 'data', 'assets', 'cache', 'config', 'dependencies', 'external', 'font', 'Music', 'tools') {
        if (Test-Path "$root\$d") { J "$sbx\$d" "$root\$d" }
    }
    foreach ($d in Get-ChildItem "$root\mods" -Directory) {
        if ($d.Name -in @('packs', 'patches', 'downloads')) { continue }
        J "$sbx\mods\$($d.Name)" $d.FullName
    }
    foreach ($d in Get-ChildItem "$root\mods\packs" -Directory) { J "$sbx\mods\packs\$($d.Name)" $d.FullName }
    foreach ($f in Get-ChildItem "$root\mods\packs" -File) { if ($f.Extension -ne '.zip') { Copy-Item $f.FullName "$sbx\mods\packs\" -Force } }
    New-Item -ItemType Directory -Force "$sbx\mods\patches" | Out-Null
    foreach ($f in Get-ChildItem "$root\mods" -File) { if ($f.Name -notlike 'active-pack.ini*') { Copy-Item $f.FullName "$sbx\mods\" -Force } }
    foreach ($f in Get-ChildItem $root -File -Filter *.ini) { Copy-Item $f.FullName "$sbx\" -Force }
    # saves\Partition*.img are sparse (5.4 GB apparent, ~0.5 GB used): Git's cp keeps them sparse
    if (-not (Test-Path "$sbx\saves")) {
        New-Item -ItemType Directory -Force "$sbx\saves" | Out-Null
        & 'C:\Program Files\Git\usr\bin\cp.exe' -r --sparse=always "$root/saves/." "$sbx/saves/"
        if ($LASTEXITCODE) { throw "copying saves failed" }
        Remove-Item "$sbx\saves\backups" -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item "$sbx\mods\active-pack.ini" -ErrorAction SilentlyContinue
    if ($SoftDrink) { Set-Content "$sbx\mods\active-pack.ini" $SoftDrinkPack -Encoding ascii }
    if (-not $SourceExe) { $SourceExe = "$root\build\Release\NFL2K5.exe" }   # -SourceExe: another build (a pack's native one)
    Copy-Item $SourceExe $exe -Force
    Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory $frames | Out-Null
    if (-not (Test-Path $home_)) {
        # the player's own settings (presentation package, game ini, controls), not their caches
        $src = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NFL2K5-PC'
        New-Item -ItemType Directory -Force "$home_\NFL2K5-PC" | Out-Null
        foreach ($f in Get-ChildItem $src -File -Filter *.ini) { Copy-Item $f.FullName "$home_\NFL2K5-PC\" }
        if (Test-Path "$src\mods") { Copy-Item "$src\mods" "$home_\NFL2K5-PC\mods" -Recurse }
    }
    $env:NFL2K5_ROOT = $sbx
    $env:NFL2K5_PORTABLE = '0'
    $env:LOCALAPPDATA = (Resolve-Path $home_).Path
    $env:NFL2K5_SKIP_INTRO = '1'
    $env:NFL2K5_PRESS_FILE = $press
    $env:NFL2K5_NO_HOST_PAD = 'always'
    $env:RECOMP_BACKGROUND = '1'
    $env:RECOMP_AUDIO_MUTE = '1'
    $env:RECOMP_AUDIO_WAV = "$out\$Tag.wav"
    $env:RECOMP_AUDIO_WAV_SECONDS = "$WavSeconds"
    $env:RECOMP_APU_PITCH_LOG = '1'
    $env:NFL2K5_FRAMETIME_CSV = "$out\$Tag-frametime.csv"
    $env:RECOMP_SHOT_PREFIX = "$frames\f"
    $env:RECOMP_SHOT_SECONDS = '2'
    $env:NFL2K5_PRES_LOG = '1'
    $env:NFL2K5_PRIVATE_PACKS = '1'
    $env:NFL2K5_NO_PACK_DISPATCH = '1'
    foreach ($k in 'NFL2K5_ERAS', 'NFL2K5_ERAS_MENU', 'NFL2K5_ERAS_CAPTURE', 'NFL2K5_ERAS_STAGE_TEST', 'NFL2K5_AUTO_PRESS', 'NFL2K5_RETAIL', 'NFL2K5_VANILLA', 'NFL2K5_QUARTER_SECONDS', 'NFL2K5_INTERP_PACK', 'NFL2K5_RENDER_SCALE', 'RECOMP_BACKGROUND_WINDOW') { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    foreach ($kv in $SetEnv) { $k, $v = $kv -split '=', 2; Set-Item "Env:$k" $v; "  env $k=$v" }
    Remove-Item -LiteralPath $press, "$out\$Tag-presses.log" -ErrorAction SilentlyContinue
    $sp = @{ FilePath = $exe; WorkingDirectory = $sbx; PassThru = $true; RedirectStandardError = $log; RedirectStandardOutput = "$out\$Tag.stdout.log" }
    if (-not $SoftDrink) { $sp.ArgumentList = @('--retail') }
    $p = Start-Process @sp
    Set-Content -Path $pidFile -Value $p.Id -Encoding ascii
    # -SetEnv is for this launch only: a second launch from the same session must not inherit it
    foreach ($kv in $SetEnv) { $k = ($kv -split '=', 2)[0]; Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
    "launched pid $($p.Id) ($exe), softdrink $SoftDrink, log $log, wav $out\$Tag.wav"
}
'quickgame' {
    # tools\drive-quickgame.ps1's sequence: title, Settings / VIP popups, Main Menu, Quick Game,
    # one Left = CPU vs CPU, Coach Matchup, Start Game, skip the pregame
    Press @('start:8', 'a:8', 'a:8', 'start:8', 'a:18', 'left:3', 'start:12', 'a:30', 'a:5')
    Keep 'kickoff'
}
'play' {
    # the coin toss and the play calls wait for a press (the first controller ends up on a team):
    # A every 6 s for -Seconds (as tools\audio-record.ps1's -TapEvery), a frame every 30 s
    $t0 = Get-Date; $k = 0
    while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
        if (-not (Mine)) { "EXITED"; break }
        Set-Content -Path $press -Value 'a' -Encoding ascii
        Start-Sleep 6
        if ((++$k % 5) -eq 0) { Keep ("play{0:d3}" -f [int]((Get-Date) - $t0).TotalSeconds) | Out-Null }
    }
    "played $([int]((Get-Date) - $t0).TotalSeconds) s"
}
'press' { Press $Steps; Keep $Name }
'shot' { Keep $Name }
'stop' {
    $p = Mine
    if ($p) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; "stopped pid $($p.Id)" } else { "not running (exited on its own?)" }
    Remove-Item $pidFile -ErrorAction SilentlyContinue
    Remove-Item $exe -ErrorAction SilentlyContinue
}
'clean' {
    if (Mine) { "still running"; break }
    Unjunction "$sbx\mods\packs"
    Unjunction "$sbx\mods"
    Unjunction $sbx
    $left = @($sbx, "$sbx\mods", "$sbx\mods\packs") | ForEach-Object { Get-ChildItem $_ -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue }
    "junctions left: $(@($left).Count)"
    if (-not @($left).Count) { Remove-Item $sbx -Recurse -Force; "removed $sbx" }
    Get-ChildItem "$root\original\disc" -Directory -Filter 'VC_53450030.NFL2K5_audio*' -ErrorAction SilentlyContinue | ForEach-Object { Remove-Item $_.FullName -Recurse -Force; "removed $($_.FullName)" }
}
}
