param([string[]]$Keys = @('RETURN'), [int]$HoldMs = 120, [int]$GapMs = 600)
# Keys into the xemu window WITHOUT focus: PostMessage WM_KEYDOWN/WM_KEYUP with the scancode in
# lParam (SDL turns them into its keyboard state; xemu's keyboard controller reads that state).
# xemu keyboard map: A/B/X/Y letters, RETURN = START, BACK (backspace) = BACK, arrows = D-pad.
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class XP {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
}
"@
$vk = @{ A = 0x41; B = 0x42; X = 0x58; Y = 0x59; RETURN = 0x0D; BACK = 0x08; LEFT = 0x25; UP = 0x26; RIGHT = 0x27; DOWN = 0x28 }
$ext = @('LEFT', 'UP', 'RIGHT', 'DOWN')
$p = Get-Process xemu -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { 'no xemu window'; return }
$h = $p.MainWindowHandle
foreach ($k in $Keys) {
    $code = [uint32]$vk[$k.ToUpper()]
    $scan = [XP]::MapVirtualKey($code, 0)
    $e = if ($ext -contains $k.ToUpper()) { 1 } else { 0 }
    $down = [IntPtr](1 -bor ($scan -shl 16) -bor ($e -shl 24))
    $up = [IntPtr]([int64](1 -bor ($scan -shl 16) -bor ($e -shl 24) -bor 0xC0000000))
    [void][XP]::PostMessage($h, 0x100, [IntPtr]$code, $down)
    Start-Sleep -Milliseconds $HoldMs
    [void][XP]::PostMessage($h, 0x101, [IntPtr]$code, $up)
    Start-Sleep -Milliseconds $GapMs
    "posted $k"
}
