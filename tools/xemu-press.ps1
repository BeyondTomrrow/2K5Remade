param([string[]]$Keys = @('RETURN'), [int]$HoldMs = 150, [int]$GapMs = 700)
# Press keys in the xemu window. Port 2 of xemu is bound to the keyboard
# (xemu.toml: port2 = 'keyboard'); xemu's keyboard map: A/B/X/Y = the same
# letters, RETURN = START, BACK = BACKSPACE, arrows = D-pad. Scancodes are sent
# (xemu reads SDL scancodes), so the window must have focus.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class XK {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$vk = @{ A = 0x41; B = 0x42; X = 0x58; Y = 0x59; RETURN = 0x0D; BACK = 0x08;
         LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28 }
$ext = @('LEFT', 'UP', 'RIGHT', 'DOWN')
$p = Get-Process xemu -ErrorAction SilentlyContinue | Select -First 1
if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { 'no xemu window'; return }
[void][XK]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 200
foreach ($k in $Keys) {
    $code = [byte]$vk[$k.ToUpper()]
    $scan = [byte][XK]::MapVirtualKey($code, 0)
    $e = if ($ext -contains $k.ToUpper()) { 1 } else { 0 }
    [XK]::keybd_event($code, $scan, $e, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [XK]::keybd_event($code, $scan, $e -bor 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $GapMs
    "pressed $k"
}
