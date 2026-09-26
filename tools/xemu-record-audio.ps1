param([int]$BootSeconds = 70, [int]$MenuSeconds = 60, [string]$Out = 'logs\xemu_audio.wav')
# Record what xemu plays (the reference for our APU) through the speakers'
# loopback (tools/loopback_rec.exe; xemu has no file audio output). Boots the
# configured disc, presses Start (keyboard Enter) at the title, records the
# main menu, then closes xemu gracefully (a forced kill has corrupted its
# state before). Keep other audio quiet while it runs.
$root = Split-Path $PSScriptRoot -Parent
if (Get-Process xemu -ErrorAction SilentlyContinue) { 'xemu already running'; exit 1 }
$p = Start-Process 'E:\xemu-0.8.136-windows-x86_64\xemu.exe' -PassThru
$rec = Start-Process "$PSScriptRoot\loopback_rec.exe" -ArgumentList @((Join-Path $root $Out), ($BootSeconds + $MenuSeconds + 8)) -PassThru -NoNewWindow
Add-Type @"
using System; using System.Runtime.InteropServices;
public class XK {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
Start-Sleep $BootSeconds
$p.Refresh()
[XK]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 500
foreach ($k in 1..2) {
    [XK]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150
    [XK]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero); Start-Sleep 3
}
$t = Get-Date
$rec.WaitForExit(($MenuSeconds + 20) * 1000) | Out-Null
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force }
Get-Item (Join-Path $root $Out) | Select-Object Name, Length
"menu recording started at $($t.ToString('HH:mm:ss')) (about $BootSeconds s into the file)"
