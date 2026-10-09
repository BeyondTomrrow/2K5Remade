# Record the game window's client area while a list of button steps runs
# (presses through NFL2K5_PRESS_FILE, so focus is not needed; keep the window
# uncovered -- ffmpeg grabs it from the desktop).
#   tools\record-window.ps1 -Out logs\rosters.mp4 -Steps @('lb:3','rt:2.5', ...)
# A step is '<button>[:<seconds after>]' or 'wait:<seconds>'.
param([string]$Out = 'logs\window.mp4', [string[]]$Steps = @(), [int]$LeadIn = 2)
$root = Split-Path $PSScriptRoot -Parent
$ff = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ff) { $ff = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ffmpeg.exe" }
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RW {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
[RW]::SetProcessDPIAware() | Out-Null
$h = [RW]::FindWindow('ESPN NFL 2K5', [NullString]::Value)
if ($h -eq [IntPtr]::Zero) { 'no game window'; exit 1 }
[RW]::SetForegroundWindow($h) | Out-Null
Start-Sleep 1
$r = New-Object RW+RECT; [RW]::GetClientRect($h, [ref]$r) | Out-Null
$pt = New-Object RW+POINT; [RW]::ClientToScreen($h, [ref]$pt) | Out-Null
$w = ($r.R - $r.L) - (($r.R - $r.L) % 2); $hh = ($r.B - $r.T) - (($r.B - $r.T) % 2)
$outPath = Join-Path $root $Out
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ff
$psi.Arguments = "-hide_banner -loglevel error -y -f gdigrab -draw_mouse 0 -framerate 30 -offset_x $($pt.X) -offset_y $($pt.Y) -video_size ${w}x${hh} -i desktop -vf scale=1920:-2 -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p `"$outPath`""
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$rec = [System.Diagnostics.Process]::Start($psi)
"recording ${w}x${hh} -> $Out"
Start-Sleep $LeadIn
foreach ($s in $Steps) {
    $b, $t = $s.Split(':')
    if (-not $t) { $t = '2' }
    if ($b -ne 'wait') { & "$PSScriptRoot\press.ps1" $b }
    Start-Sleep -Milliseconds ([int]([double]$t * 1000))
}
$rec.StandardInput.Write('q'); $rec.StandardInput.Flush()
$rec.WaitForExit(20000) | Out-Null
Get-Item $outPath | Select-Object Name, Length
