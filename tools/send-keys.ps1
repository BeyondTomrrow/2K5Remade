param([string[]]$Keys = @('F1'), [int]$GapMs = 600, [string]$Process = 'NFL2K5')
# Press keys in the game window (it is brought to the front first). Names:
# F1..F12, ESC, RETURN, UP, DOWN, LEFT, RIGHT, PGUP, PGDN, SPACE, or a single
# letter/digit. Used to test the F1 settings pages without a person at the
# keyboard (tools\drive-menu.ps1 drives the game's own menus through its pad).
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class SK {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$vk = @{ ESC = 0x1B; RETURN = 0x0D; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; PGUP = 0x21; PGDN = 0x22; SPACE = 0x20 }
for ($i = 1; $i -le 12; $i++) { $vk["F$i"] = 0x6F + $i }
$ext = @('UP', 'DOWN', 'LEFT', 'RIGHT', 'PGUP', 'PGDN')
$p = Get-Process $Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { 'no game window'; return }
[void][SK]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 250
foreach ($k in $Keys) {
    $name = $k.ToUpper()
    $code = if ($vk.ContainsKey($name)) { [byte]$vk[$name] } else { [byte][char]$name }
    $scan = [byte][SK]::MapVirtualKey($code, 0)
    $e = if ($ext -contains $name) { 1 } else { 0 }
    [SK]::keybd_event($code, $scan, $e, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [SK]::keybd_event($code, $scan, $e -bor 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $GapMs
}
