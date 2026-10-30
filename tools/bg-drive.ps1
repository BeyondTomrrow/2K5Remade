param([string[]]$Steps = @(), [string]$Tag = 'bg', [int]$BootSeconds = 50, [switch]$KeepRunning, [switch]$Pack)
# Drive the menus unattended while the PC is in use: the clean game (--retail
# unless -Pack) in an off-screen window that never takes focus (RECOMP_BACKGROUND),
# muted (RECOMP_AUDIO_MUTE), buttons through the game's press file (never the
# real keyboard or pad). Frames come from the renderer itself (RECOMP_SHOT_PREFIX,
# every second); after each step the newest is kept as logs\<tag>-<n>.png.
# A step is '<button>[x<count>][:<seconds>]' or 'shot' (as tools\drive-menu.ps1).
# Starts from the title: Start, A, A, Start reaches the main menu on an existing profile.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = 'always'
$env:RECOMP_BACKGROUND = '1'
$env:RECOMP_AUDIO_MUTE = '1'
$frames = "$root\logs\bgframes"
Remove-Item $frames -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $frames | Out-Null
$env:RECOMP_SHOT_PREFIX = "$frames\f"
$env:RECOMP_SHOT_SECONDS = '1'
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$launch = @{ FilePath = 'build\Release\NFL2K5.exe'; WorkingDirectory = $root; PassThru = $true
             RedirectStandardError = "logs\$Tag.stderr.log"; RedirectStandardOutput = "logs\$Tag.stdout.log" }
if (-not $Pack) { $launch['ArgumentList'] = '--retail' }
$p = Start-Process @launch
function Keep([int]$n) {
    Start-Sleep -Milliseconds 1500
    $f = Get-ChildItem $frames -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($f) {
        & python -c "from PIL import Image; import sys; Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" $f.FullName "$root\logs\$Tag-$n.png"
    }
}
Start-Sleep $BootSeconds
foreach ($b in 'start', 'a', 'a', 'start') { & "$PSScriptRoot\press.ps1" $b; Start-Sleep 8 }
$n = 0
Keep $n
foreach ($s in $Steps) {
    if ($p.HasExited) { "EXITED at step $n ($s)"; break }
    $wait = 3; $count = 1; $btn = $s
    if ($btn -match '^(.*):(\d+)$') { $btn = $Matches[1]; $wait = [int]$Matches[2] }
    if ($btn -match '^(\S+) x(\d+)$') { $btn = $Matches[1]; $count = [int]$Matches[2] }
    if ($btn -ne 'shot') { for ($i = 0; $i -lt $count; $i++) { & "$PSScriptRoot\press.ps1" $btn; Start-Sleep -Milliseconds 400 } }
    Start-Sleep $wait
    $n++
    Keep $n
}
if (-not $KeepRunning -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
if ($p.HasExited) { Select-String -Path "logs\$Tag.stderr.log" -Pattern '^\[CRASH\] (exception|function)' | ForEach-Object Line }
"done: $n steps, frames logs\$Tag-0..$n.png"
