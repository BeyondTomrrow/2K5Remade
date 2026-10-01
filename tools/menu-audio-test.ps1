# Menu audio test: boot to the main menu, let the music play, then move the
# menu cursor rapidly (the user's repro for menu music fast-forwarding), then
# idle again. Records the game's audio to logs\$Tag.wav; log in
# logs\$Tag.stderr.log. Extra environment (e.g. RECOMP_APU_VOICE_LOG) is
# inherited.
param([string]$Tag = 'menuaud', [int]$Moves = 30, [int]$MoveMs = 150)
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
$env:RECOMP_AUDIO_WAV = "$root\logs\$Tag.wav"
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process 'build\Release\NFL2K5.exe' -WorkingDirectory $root -PassThru `
     -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
Start-Sleep 35
& "$PSScriptRoot\press.ps1" start; Start-Sleep 12          # title -> main menu
"idle 15 s (music only)"; Start-Sleep 15
"moving $Moves times"
for ($i = 0; $i -lt $Moves; $i++) {
    & "$PSScriptRoot\press.ps1" ($(if ($i % 2) { 'up' } else { 'down' }))
    Start-Sleep -Milliseconds $MoveMs
}
"idle 15 s"; Start-Sleep 15
Stop-Process -Id $p.Id -Force
Remove-Item Env:RECOMP_AUDIO_WAV
"done: logs\$Tag.wav"
