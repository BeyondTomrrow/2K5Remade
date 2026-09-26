param([string]$Out = 'logs\demo.mp4', [int]$PregameSeconds = 75, [int]$AfterKickoff = 15)
# Record a demo video: boot (with intros), Quick Game, the Presentation screen
# (package and themes), back, Start Game, the pregame show with the broadcast
# theme, kickoff, a few plays. Video: the window's client area grabbed from
# the desktop by ffmpeg (a window-DC grab misses the flip-model swap chain).
# Audio: the game's own recording (RECOMP_AUDIO_WAV, game mix + theme), lined
# up with the video by wall clock (<wav>.start). Presses go through
# NFL2K5_PRESS_FILE, so the window needn't have focus; keep it uncovered.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ff) { $ff = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ffmpeg.exe" }
$wav = "$root\logs\demo_audio.wav"
Remove-Item $wav, "$wav.start", "$root\logs\demo_video.mp4" -ErrorAction SilentlyContinue
$env:NFL2K5_SKIP_INTRO = '1'   # the intro movies don't play in real time yet
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
$env:NFL2K5_PRES_LOG = '1'
$env:RECOMP_AUDIO_WAV = $wav
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$log = "$root\logs\demo.stderr.log"
$p = Start-Process "$root\build\Release\NFL2K5.exe" -WorkingDirectory $root -PassThru `
     -RedirectStandardError $log -RedirectStandardOutput "$root\logs\demo.stdout.log"
$env:RECOMP_AUDIO_WAV = $null; $env:NFL2K5_PRES_LOG = $null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class DW {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
[DW]::SetProcessDPIAware() | Out-Null
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep 1; $h = [DW]::FindWindow('ESPN NFL 2K5', [NullString]::Value) }
if ($h -eq [IntPtr]::Zero) { 'no game window'; exit 1 }
Start-Sleep 2
[DW]::SetForegroundWindow($h) | Out-Null
$r = New-Object DW+RECT; [DW]::GetClientRect($h, [ref]$r) | Out-Null
$pt = New-Object DW+POINT; [DW]::ClientToScreen($h, [ref]$pt) | Out-Null
$w = ($r.R - $r.L) - (($r.R - $r.L) % 2); $hh = ($r.B - $r.T) - (($r.B - $r.T) % 2)
"capturing ${w}x${hh} at $($pt.X),$($pt.Y)"

# ffmpeg records until we send 'q' on its stdin.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ff
$psi.Arguments = "-hide_banner -loglevel error -y -f gdigrab -draw_mouse 0 -framerate 30 -offset_x $($pt.X) -offset_y $($pt.Y) -video_size ${w}x${hh} -i desktop -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p `"$root\logs\demo_video.mp4`""
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$videoStart = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
$rec = [System.Diagnostics.Process]::Start($psi)

function Press([string]$b, [int]$after) { & "$PSScriptRoot\press.ps1" $b; Start-Sleep $after }
function LogHas([string]$pat) { (Select-String -Path $log -Pattern $pat -Quiet) }
function WaitLog([string]$pat, [int]$max) {
    for ($i = 0; $i -lt $max; $i++) { if (LogHas $pat) { return $true }; Start-Sleep 1 }
    return $false
}

# Boot: legal screen, loading, then the title screen.
Start-Sleep 33
Press start 8          # title -> main menu
Press a 9              # Quick Game -> team select
Press start 8          # -> Coach Match Up
Press up 3             # wraps to Presentation (under VIP)
Press a 4              # open it: package, themes, animations, volume
Press down 2           # Intro Theme
Press down 2           # Outro Theme
Press down 2           # Scorebug Animations
Press down 3           # Theme Volume
Press b 4              # back to Coach Match Up
Press down 2           # wraps to Start Game
Press a 5              # start the game

# Pregame show with the intro theme, then tap through to the kickoff.
WaitLog 'theme playing' 90 | Out-Null
Start-Sleep $PregameSeconds
for ($i = 0; $i -lt 40 -and -not (LogHas '\] q1 .* phase 4'); $i++) { Press a 6 }
Start-Sleep $AfterKickoff

$rec.StandardInput.Write('q'); $rec.StandardInput.Flush()
$rec.WaitForExit(20000) | Out-Null
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue

# Line the audio up with the video and mux.
$audioStart = [int64](Get-Content "$wav.start")
$skip = ($videoStart - $audioStart) / 1000.0
"video starts $skip s into the audio recording"
$ss = [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0:0.000}', [Math]::Max(0, $skip))
& $ff -hide_banner -loglevel error -y -i "$root\logs\demo_video.mp4" -ss $ss -i $wav -map 0:v -map 1:a `
     -c:v copy -c:a aac -b:a 192k -shortest (Join-Path $root $Out)
Get-Item (Join-Path $root $Out) | Select-Object Name, Length
