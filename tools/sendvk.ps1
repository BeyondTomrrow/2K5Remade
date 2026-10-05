param([int]$Vk = 0x70)
# Post a key press to the game window (WM_KEYDOWN / WM_KEYUP), no focus needed.
# 0x70 = F1 (video settings), 0x26/0x28 up/down, 0x25/0x27 left/right, 0x0D enter, 0x1B esc.
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W32K {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$h = [W32K]::FindWindow('ESPN NFL 2K5', [NullString]::Value)
if ($h -eq [IntPtr]::Zero) { 'no window'; exit 1 }
[W32K]::PostMessage($h, 0x100, [IntPtr]$Vk, [IntPtr]1) | Out-Null
[W32K]::PostMessage($h, 0x101, [IntPtr]$Vk, [IntPtr]0xC0000001) | Out-Null
