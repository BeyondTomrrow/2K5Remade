param([string]$Config = "$env:APPDATA\xemu\xemu\xemu-audio3.toml", [string]$OutDir = 'E:\NFL2K5-PC\logs\audio3', [string]$SdlDisk = '')
# Oracle launch (docs/AUDIO-USER-REPORTS-20261022.md): private config (keyboard on port 1, retail
# ISO), QEMU monitor 4444 / gdb 1234, window restored but parked off-screen without activation
# (minimized xemu froze twice), its Windows audio session muted (QEMU's wavcapture still records).
$x = 'E:\xemu-0.8.136-windows-x86_64\xemu.exe'
$old = Get-Process xemu -ErrorAction SilentlyContinue
if ($old) { [void]$old.CloseMainWindow(); if (-not $old.WaitForExit(8000)) { Stop-Process -Id $old.Id -Force; "killed old xemu $($old.Id)" } else { "closed old xemu" } ; Start-Sleep 2 }
Get-Process vsjitdebugger -ErrorAction SilentlyContinue | Stop-Process -Force
# xemu rewrites its config on exit (it turned volume_limit into 0 and bound the player's pad to
# port 2 on 2026-10-22), so the private config is written fresh for every launch. It is the player's
# own config plus the keyboard on port 1: a minimal config without the [input] gamepad_mappings
# entry crashed xemu at start (access violation at xemu+0x509315, every launch) once the player's
# pad was connected.
@'
[general]
show_welcome = false
games_dir = 'E:\'
last_viewed_menu_index = 6

[input]
gamepad_mappings = [
    { gamepad_id = '0300fa675e040000ff02000000007801'}
    ]

[input.bindings]
port1 = 'keyboard'

[audio]
volume_limit = 1.0

[sys.files]
bootrom_path = 'E:\Xbox-Emulator-Files\mcpx\mcpx_1.0.bin'
flashrom_path = 'E:\Xbox-Emulator-Files\bios\Complex_4627.bin'
eeprom_path = 'C:\Users\MJBAMA2010\AppData\Roaming\xemu\xemu\eeprom.bin'
hdd_path = 'E:\Xbox-Emulator-Files\hdd\xbox_hdd.qcow2'
dvd_path = 'E:\NFL2K5-PC\original\NFL2K5.xiso.iso'
'@ | Set-Content $Config -Encoding ascii
# -SdlDisk <file>: xemu's APU output goes straight to SDL audio (not through QEMU's audiodev, so
# the monitor's wavcapture records silence); SDL's "disk" driver writes that raw PCM to a file and
# nothing to the speakers. Format: what xemu asks of SDL (48 kHz stereo s16).
if ($SdlDisk) { $env:SDL_AUDIODRIVER = 'disk'; $env:SDL_DISKAUDIOFILE = $SdlDisk; $env:SDL_DISKAUDIODELAY = '0'; Remove-Item $SdlDisk -ErrorAction SilentlyContinue }
$p = Start-Process -FilePath $x -WorkingDirectory (Split-Path $x) -WindowStyle Minimized -PassThru -RedirectStandardOutput "$OutDir\xemu.stdout.log" -RedirectStandardError "$OutDir\xemu.stderr.log" -ArgumentList @('-config_path', "`"$Config`"", '-monitor', 'tcp:127.0.0.1:4444,server=on,wait=off', '-gdb', 'tcp:127.0.0.1:1234')
if ($SdlDisk) { Remove-Item Env:SDL_AUDIODRIVER, Env:SDL_DISKAUDIOFILE, Env:SDL_DISKAUDIODELAY -ErrorAction SilentlyContinue }
Set-Content "$OutDir\xemu.pid" $p.Id
Start-Process powershell -ArgumentList '-NoProfile', '-File', "E:\NFL2K5-PC\tools\audio\app_mute.ps1", '-ProcId', $p.Id, '-Mute', '0', '-Volume', '0.001', '-RetrySeconds', '120' -WindowStyle Hidden -RedirectStandardOutput "$OutDir\xemu-mute.out"
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class XW {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
}
"@
$deadline = (Get-Date).AddSeconds(20); $h = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) { $p.Refresh(); $h = $p.MainWindowHandle; if ($h -ne [IntPtr]::Zero) { break }; Start-Sleep -Milliseconds 250 }
if ($h -ne [IntPtr]::Zero) {
    [void][XW]::ShowWindow($h, 4)                                        # SW_SHOWNOACTIVATE: restore, no focus
    [void][XW]::SetWindowPos($h, [IntPtr]::Zero, -4000, -4000, 0, 0, 0x0001 -bor 0x0010 -bor 0x0004)  # NOSIZE|NOACTIVATE|NOZORDER
    "xemu pid $($p.Id) window parked off-screen"
} else { "xemu pid $($p.Id): no window handle yet" }
