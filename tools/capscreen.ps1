param([string]$Out = 'logs\screen.png', [string]$Title = 'ESPN NFL 2K5')
# Capture the game window as it appears on screen (desktop copy of its rect).
# Works with the D3D swap-chain presenter, which PrintWindow/GDI capture of
# the window DC does not see. The desktop must be unlocked.
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Dpi -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[Dpi.U]::SetProcessDPIAware() | Out-Null
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$h = [W32]::FindWindow('XboxRecompFramebuffer', [NullString]::Value)
if ($h -eq [IntPtr]::Zero) { 'no window'; exit 1 }
[W32]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object W32+RECT
[W32]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$b = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size)
$b.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
"$w x $hh -> $Out"
