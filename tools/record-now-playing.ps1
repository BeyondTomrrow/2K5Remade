param([string]$Out = 'logs\now-playing-proof.mp4')

# Records the rebuilt main menu, an L3 NOW PLAYING request, its automatic
# return to MAINMENU, and entry into Team Select to prove the marquee does
# not leak into another mode.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ff) { $ff = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ffmpeg.exe" }
if (-not (Test-Path $ff)) { throw 'ffmpeg was not found' }

$press = "$root\logs\press.txt"
$raw = "$root\logs\now-playing-video.mp4"
$wav = "$root\logs\now-playing-audio.wav"
$log = "$root\logs\now-playing.stderr.log"
$final = Join-Path $root $Out
Remove-Item $press, $raw, $wav, "$wav.start", $log, $final -ErrorAction SilentlyContinue

$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = $press
$env:NFL2K5_NO_HOST_PAD = '1'
$env:RECOMP_AUDIO_WAV = $wav
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force
$game = Start-Process "$root\build\Release\NFL2K5.exe" -WorkingDirectory $root -PassThru `
    -RedirectStandardError $log -RedirectStandardOutput "$root\logs\now-playing.stdout.log"
$env:RECOMP_AUDIO_WAV = $null

Add-Type @"
using System; using System.Runtime.InteropServices;
public class NPWindow {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
[NPWindow]::SetProcessDPIAware() | Out-Null
$window = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $window -eq [IntPtr]::Zero; $i++) {
    Start-Sleep 1
    $window = [NPWindow]::FindWindow('ESPN NFL 2K5', [NullString]::Value)
}
if ($window -eq [IntPtr]::Zero) { throw 'NFL 2K5 window did not open' }
[NPWindow]::ShowWindow($window, 3) | Out-Null                 # maximized
[NPWindow]::SetWindowPos($window, [IntPtr](-1), 0, 0, 0, 0, 0x43) | Out-Null # topmost, no move/size
[NPWindow]::SetForegroundWindow($window) | Out-Null
Start-Sleep 1
$rect = New-Object NPWindow+RECT
$point = New-Object NPWindow+POINT
[NPWindow]::GetClientRect($window, [ref]$rect) | Out-Null
[NPWindow]::ClientToScreen($window, [ref]$point) | Out-Null
$width = ($rect.R - $rect.L) - (($rect.R - $rect.L) % 2)
$height = ($rect.B - $rect.T) - (($rect.B - $rect.T) % 2)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ff
$psi.Arguments = "-hide_banner -loglevel error -y -f gdigrab -draw_mouse 0 -framerate 30 -offset_x $($point.X) -offset_y $($point.Y) -video_size ${width}x${height} -i desktop -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p `"$raw`""
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$videoStart = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()
$rec = [Diagnostics.Process]::Start($psi)

function Press([string]$button, [int]$wait) {
    Set-Content -Path $press -Value $button -Encoding ascii
    Start-Sleep $wait
}

Start-Sleep 31
Press start 8        # title screen -> saved settings/profile confirmations
Press a 3            # Settings1 loaded
Press a 3            # CribMax VIP loaded
Press a 8            # VIP1 loaded, then settle on the main menu
# The automatic track-start banner has now restored, so this first frame is
# the untouched MAINMENU heading before the explicit L3 request.
Press l3 8           # NOW PLAYING, scrolling subtitle, then MAINMENU restore
Press a 8            # enter Team Select; no music marquee may remain

$rec.StandardInput.Write('q')
$rec.StandardInput.Flush()
$rec.WaitForExit(20000) | Out-Null
Stop-Process -Id $game.Id -Force -ErrorAction SilentlyContinue

if ((Test-Path $wav) -and (Test-Path "$wav.start")) {
    $audioStart = [int64](Get-Content "$wav.start")
    $skip = [Math]::Max(0, ($videoStart - $audioStart) / 1000.0)
    $ss = [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0:0.000}', $skip)
    & $ff -hide_banner -loglevel error -y -i $raw -ss $ss -i $wav -map 0:v -map 1:a `
        -c:v copy -c:a aac -b:a 192k -shortest $final
} else {
    Move-Item $raw $final -Force
}
Get-Item $final | Select-Object FullName, Length
