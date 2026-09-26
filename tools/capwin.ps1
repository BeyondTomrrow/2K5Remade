param([int[]]$At = @(20, 30, 40, 50, 60, 70), [string]$Prefix = 'win')
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  public struct RECT { public int L, T, R, B; }
}
"@
$sp = (Join-Path (Split-Path $PSScriptRoot -Parent) "logs")
$t0 = Get-Date
foreach ($t in $At) {
    while (((Get-Date) - $t0).TotalSeconds -lt $t) { Start-Sleep -Milliseconds 200 }
    $h = [W]::FindWindow('ESPN NFL 2K5', [NullString]::Value)
    if ($h -eq [IntPtr]::Zero) { "t=$t no window"; continue }
    $r = New-Object W+RECT
    [void][W]::GetClientRect($h, [ref]$r)
    $w = [Math]::Max(1, $r.R - $r.L); $hh = [Math]::Max(1, $r.B - $r.T)
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][W]::PrintWindow($h, $hdc, 3)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save("$sp\$Prefix-$t.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "t=$t saved $Prefix-$t.png"
}
