# In-game test of the converted Coliseum, background mode (off-screen window that never takes focus, muted,
# presses through a press file, never the real keyboard or pad).
# Installs the test team (tools\eras\stadiums\coliseum_test_team.py), drives Quick Game with it at home,
# keeps frames of the pregame flyover and the first plays, then removes the test team again.
#   powershell -File tools\eras\stadiums\coliseum_ingame_test.ps1 [-Tag coliseum3] [-Back 12]
# It runs its own copy of the game (build\Release\NFL2K5_coliseum.exe) with its own press file and frame folder,
# so it neither stops nor is stopped by another workflow's NFL2K5.exe tests and builds.
# -Back: Team Select steps back from the default home team (the Cowboys) to wrap into the outside list's last
# entry (the zz folder). Measured 2026-10-16: the 10th LT press (2 s apart) loads it.
param([string]$Tag = 'coliseum3', [int]$Back = 9, [int]$Flyover = 15, [int]$BootSeconds = 100)
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
Set-Location $root
$exe = "$root\build\Release\NFL2K5_coliseum.exe"
Get-Process NFL2K5_coliseum -ErrorAction SilentlyContinue | Stop-Process -Force
Copy-Item "$root\build\Release\NFL2K5.exe" $exe -Force
$press = "$root\logs\coliseum-press.txt"
$frames = "$root\logs\coliseum-frames"
Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $frames | Out-Null
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = $press
$env:NFL2K5_NO_HOST_PAD = 'always'
$env:RECOMP_BACKGROUND = '1'
$env:RECOMP_AUDIO_MUTE = '1'
$env:RECOMP_SHOT_PREFIX = "$frames\f"
$env:RECOMP_SHOT_SECONDS = '1'
$env:NFL2K5_TEAMS_LOG = '1'
$env:NFL2K5_ASSET_LOG = '1'
function Press([string]$b) { Set-Content -Path $press -Value $b -Encoding ascii }
function Keep([string]$name) {
    Start-Sleep -Milliseconds 1500
    $f = Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($f) { & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$root\logs\$Tag-$name.png" }
}
& python tools\eras\stadiums\coliseum_test_team.py install
try {
    $p = $null
    $p = Start-Process -FilePath $exe -ArgumentList '--retail' -WorkingDirectory $root -PassThru `
        -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
    Start-Sleep $BootSeconds
    foreach ($b in 'start', 'a', 'a', 'start') { Press $b; Start-Sleep 12 }
    Keep 'menu'
    $plan = @(,@('a', 18, 'teamselect'))
    for ($i = 0; $i -lt $Back; $i++) { $plan += ,@('lt', 2, $null) }
    $plan += ,@('shot', 3, 'home')
    $plan += ,@('left', 3, 'cpu')
    $plan += ,@('start', 12, 'matchup')
    $plan += ,@('a', 20, 'loading')
    for ($i = 0; $i -lt $Flyover; $i++) { $plan += ,@('shot', 4, "pregame$i") }
    $plan += ,@('a', 6, 'skip')
    for ($i = 0; $i -lt 8; $i++) { $plan += ,@('shot', 5, "play$i") }
    foreach ($s in $plan) {
        if ($p.HasExited) { "EXITED before $($s[2])"; break }
        if ($s[0] -eq 'seek') {   # one LT at a time, each waiting for its Team Select step in the log
            $log = "logs\$Tag.stderr.log"
            $pat = '\[TEAMS\] side \d (step|loads)'
            for ($i = 0; $i -lt 40; $i++) {
                $n0 = (Select-String -Path $log -Pattern $pat | Measure-Object).Count
                Press 'lt'
                for ($w = 0; $w -lt 24; $w++) {
                    Start-Sleep -Milliseconds 500
                    if ((Select-String -Path $log -Pattern $pat | Measure-Object).Count -gt $n0) { break }
                }
                Start-Sleep 1
                $last = Select-String -Path $log -Pattern $pat | Select-Object -Last 1
                if ($last -and $last.Line -match 'zzColiseumTest') { "home: the test team after $($i + 1) LT"; break }
            }
        } elseif ($s[0] -ne 'shot') { Press $s[0] }
        Start-Sleep $s[1]
        if ($s[2]) { Keep $s[2] }
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force } else { "exit code $($p.ExitCode)" }
} finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    & python tools\eras\stadiums\coliseum_test_team.py remove
    Remove-Item $exe -ErrorAction SilentlyContinue
}
Select-String -Path "logs\$Tag.stderr.log" -Pattern 'TEAMFILES|loads added|s60|CRASH|stadium' | Select-Object -First 40 | ForEach-Object Line







