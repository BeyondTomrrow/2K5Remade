# Boot xemu under gdb, press Start at the title, and let tools\xemu-music-ring.gdb
# dump the left music ring (0x83B60C20) every 100 music copies.
$root = Split-Path $PSScriptRoot -Parent
if (Get-Process xemu -ErrorAction SilentlyContinue) { 'xemu already running'; exit 1 }
& "$PSScriptRoot\start-xemu-gdb.ps1" -PauseAtBoot | Out-Null
Start-Sleep 3
$gdb = Start-Process "$root\dependencies\msys2\ucrt64\bin\gdb.exe" -ArgumentList @('--batch', '-x', "$root\tools\xemu-music-ring.gdb") `
       -PassThru -NoNewWindow -RedirectStandardOutput "$root\logs\xemu_ring_gdb.txt"
Add-Type @"
using System; using System.Runtime.InteropServices;
public class XK2 {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$x = Get-Process xemu
foreach ($t in 80, 95, 110) {
    Start-Sleep ($t - ($(if ($t -eq 80) { 0 } else { $t - 15 })))
    $x.Refresh()
    [XK2]::SetForegroundWindow($x.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 300
    [XK2]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150
    [XK2]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
}
$gdb.WaitForExit(600000) | Out-Null
$x.Refresh()
if (-not $x.HasExited) { $x.CloseMainWindow() | Out-Null; if (-not $x.WaitForExit(15000)) { Stop-Process -Id $x.Id -Force } }
Get-Content "$root\logs\xemu_ring_gdb.txt" | Select-String 'copy'
