param([int]$ProcessId, [string]$Out = 'logs\proc.png')
# Capture the main window of a process as it appears on screen (e.g. xemu).
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Dpi2 -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
[Dpi2.U]::SetProcessDPIAware() | Out-Null
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W32P {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$p = Get-Process -Id $ProcessId -ErrorAction Stop
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { 'no main window'; exit 1 }
[W32P]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object W32P+RECT
[W32P]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$b = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $b.Size)
$b.Save((Join-Path (Get-Location) $Out), [System.Drawing.Imaging.ImageFormat]::Png)
"$w x $hh -> $Out  ($($p.MainWindowTitle))"
