# Record the game's speaker output during a CPU-vs-CPU Quick Game, then check it.
#   tools\audio-record.ps1 -Tag aud-retail -Retail          (clean ESPN NFL 2K5)
#   tools\audio-record.ps1 -Tag aud-sd                      (active mod pack)
# Writes logs\<tag>.wav (RECOMP_AUDIO_WAV) and runs tools\audio-check.py on it.
param([string]$Tag = 'aud', [switch]$Retail, [int]$PlaySeconds = 150)
$root = Split-Path $PSScriptRoot -Parent
$env:RECOMP_AUDIO_WAV = "$root\logs\$Tag.wav"
$env:RECOMP_APU_VOICE_LOG = '1'
if ($Retail) { & "$PSScriptRoot\drive-quickgame.ps1" -Retail -Tag $Tag -PlaySeconds $PlaySeconds -ShotEvery 30 -TapEvery 5 }
else { & "$PSScriptRoot\drive-quickgame.ps1" -Tag $Tag -PlaySeconds $PlaySeconds -ShotEvery 30 -TapEvery 5 }
Remove-Item Env:RECOMP_AUDIO_WAV, Env:RECOMP_APU_VOICE_LOG -ErrorAction SilentlyContinue
python "$PSScriptRoot\audio-check.py" "$root\logs\$Tag.wav"
