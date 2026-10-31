<#
.SYNOPSIS
    Run the UWP build of the game on this PC without installing the package:
    build-uwp\game\NFL2K5.exe --hwnd-test (a Win32 window stands in for the
    CoreWindow; everything else is the UWP code path). Takes screenshots.

.DESCRIPTION
    Stage:  E:\2K5Remade-local\uwp-stage-game (exe, dxcompiler/dxil, store CRT
            DLLs from the installed Microsoft.VCLibs.140.00 framework)
    Local:  E:\2K5Remade-local\uwp-local (plays the part of LocalState: log,
            settings); root.txt points at the game root.
    Root:   E:\2K5Remade-local\uwp-root (junctions to E:\2K5Remade\original,
            data, font; portable.txt), so saves/settings never touch the real
            install. Nothing is copied.
    Holds E:\claude-overflow\game-lock while running and stops only its own
    process (by PID).

    -Sandbox runs it inside an AppContainer instead (appcontainer-run.exe);
    that needs the root folder readable by ALL APPLICATION PACKAGES, which
    this script does not grant.
#>
param(
    [int]$Seconds = 90,
    [int[]]$ShotsAt = @(20, 45, 80),
    [string]$ShotDir = 'D:\claude-overflow\uwp-shots',
    [string]$Root = 'E:\2K5Remade-local\uwp-root',
    [string]$Stage = 'E:\2K5Remade-local\uwp-stage-game',
    [string]$Local = 'E:\2K5Remade-local\uwp-local',
    [switch]$Sandbox
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$exe = "$Repo\build-uwp\game\NFL2K5.exe"
if (-not (Test-Path $exe)) { throw 'build-uwp\game\NFL2K5.exe missing: tools\uwp\build-uwp.ps1 -Game' }

New-Item -ItemType Directory -Force $Stage, $Local, $ShotDir | Out-Null
Copy-Item $exe $Stage -Force
if (Test-Path "$Repo\build-uwp\game\NFL2K5.pdb") { Copy-Item "$Repo\build-uwp\game\NFL2K5.pdb" $Stage -Force }
foreach ($d in 'dxcompiler.dll', 'dxil.dll') {
    $src = if (Test-Path "E:\2K5Remade-local\dxc\bin\x64\$d") { "E:\2K5Remade-local\dxc\bin\x64\$d" } else { "$Repo\build\Release\$d" }
    Copy-Item $src $Stage -Force
}
$vclibs = (Get-AppxPackage Microsoft.VCLibs.140.00 | Where-Object Architecture -eq 'X64' |
           Sort-Object Version -Descending | Select-Object -First 1).InstallLocation
Copy-Item "$vclibs\*_app.dll" $Stage -Force
Set-Content -Path "$Local\root.txt" -Value $Root -Encoding ascii

Add-Type @'
using System; using System.Runtime.InteropServices; using System.Drawing;
public static class UwpShot {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  public static bool Save(IntPtr h, string path) {
    RECT r; if (!GetWindowRect(h, out r) || r.R <= r.L) return false;
    using (var bmp = new Bitmap(r.R - r.L, r.B - r.T)) {
      using (var g = Graphics.FromImage(bmp)) {
        /* PW_RENDERFULLCONTENT (2): works for flip-model D3D windows even
         * when another window covers this one. */
        IntPtr dc = g.GetHdc(); bool ok = PrintWindow(h, dc, 2); g.ReleaseHdc(dc);
        if (!ok) g.CopyFromScreen(r.L, r.T, 0, 0, bmp.Size);
      }
      bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }
    return true;
  }
}
'@ -ReferencedAssemblies System.Drawing
[UwpShot]::SetProcessDPIAware() | Out-Null

$lock = 'E:\claude-overflow\game-lock'
while (Test-Path $lock) { Write-Host 'waiting for game-lock...'; Start-Sleep 15 }
New-Item -ItemType Directory $lock | Out-Null
try {
    if ($Sandbox) {
        $p = Start-Process "$Repo\build-uwp\appcontainer-run.exe" -ArgumentList "`"$Stage\NFL2K5.exe`" --hwnd-test `"%AC%`"" -PassThru
    } else {
        $p = Start-Process "$Stage\NFL2K5.exe" -ArgumentList "--hwnd-test `"$Local`"" -WorkingDirectory $Stage -PassThru
    }
    Write-Host "started pid $($p.Id)"
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $t0 = Get-Date
    foreach ($at in ($ShotsAt | Sort-Object)) {
        while (((Get-Date) - $t0).TotalSeconds -lt $at -and -not $p.HasExited) { Start-Sleep -Milliseconds 500 }
        if ($p.HasExited) { break }
        $p.Refresh()
        $h = $p.MainWindowHandle
        if ($h -ne [IntPtr]::Zero) {
            [UwpShot]::SetForegroundWindow($h) | Out-Null
            Start-Sleep -Milliseconds 400
            $f = Join-Path $ShotDir "uwp-game-$stamp-${at}s.png"
            if ([UwpShot]::Save($h, $f)) { Write-Host "shot $f" }
        } else { Write-Host "no window at ${at}s" }
    }
    while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) { Start-Sleep 1 }
    if ($p.HasExited) { Write-Host "exited by itself, code $($p.ExitCode)" }
    else { Stop-Process -Id $p.Id -Force; Write-Host "stopped pid $($p.Id)" }
} finally {
    Remove-Item $lock -Force -Recurse -ErrorAction SilentlyContinue
}
Write-Host "log: $Local\nfl2k5.log"
