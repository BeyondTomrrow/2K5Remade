param([string[]]$Keys = @('SPACE'), [int]$HoldMs = 150, [int]$GapMs = 600)
# Press keys in the game window (keyboard input works while it has focus):
# SPACE/Z = A, X = B, RETURN = START, arrows = D-pad. Used to answer prompts
# the scripted NFL2K5_AUTO_PRESS timings miss.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
$vk = @{ SPACE = 0x20; RETURN = 0x0D; Z = 0x5A; X = 0x58; C = 0x43; V = 0x56; LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28 }
$h = [K]::FindWindow('XboxRecompFramebuffer', [NullString]::Value)
if ($h -eq [IntPtr]::Zero) { 'no window'; return }
[void][K]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 200
foreach ($k in $Keys) {
    $code = [byte]$vk[$k.ToUpper()]
    [K]::keybd_event($code, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [K]::keybd_event($code, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $GapMs
    "pressed $k"
}
