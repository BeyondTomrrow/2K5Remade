param([ValidateRange(1,600)][int]$Seconds = 120, [switch]$ValidateOnly, [switch]$Optimized)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = if ($Optimized) { "$root\build\Release-opt\NFL2K5.exe" } else { "$root\build\Release\NFL2K5.exe" }
if (-not (Test-Path $exe)) { throw 'Build the game first with tools/build.ps1 -Game.' }
$label = if ($ValidateOnly) { 'validate' } else { 'startup' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdout = "$root\logs\$label-$stamp.stdout.log"
$stderr = "$root\logs\$label-$stamp.stderr.log"
# Let the launcher own the bounded shutdown.  Keeping the in-game watchdog
# beyond this timeout avoids its large stack dump cutting off the GPU method
# ranking emitted at the ten-second pushbuffer report.
$env:RECOMP_WATCHDOG_SECS = [string]($Seconds + 5)
$env:RECOMP_AC97_READY = '1'
# 2026-09-23 defaults (set the variable beforehand to override): run guest
# threads one at a time like the single-core Xbox (xbox_ggl.h), drive the vblank
# ISR, and skip the intro movies the way a button press would.
if (-not $env:RECOMP_GGL) { $env:RECOMP_GGL = '1' }
if (-not $env:RECOMP_VBLANK) { $env:RECOMP_VBLANK = '1' }
if (-not $env:NFL2K5_SKIP_INTRO) { $env:NFL2K5_SKIP_INTRO = '1' }
# The first game worker now runs: it is required to advance the title's
# front-end state once audio and callback compatibility are in place.
Remove-Item Env:RECOMP_SKIP_NFL2K5_BOOT_WORKER -ErrorAction SilentlyContinue
$env:RECOMP_SKIP_THREAD_PRIORITY = '1'
Remove-Item Env:RECOMP_CS_MODE -ErrorAction SilentlyContinue
$env:XBOX_LOG_LEVEL = '0'
$env:RECOMP_KERNEL_LOG_BUDGET = '0'
$env:RECOMP_FB_WINDOW = '1'
$env:RECOMP_FB_DUMP = "$root\logs\framebuffer-latest.bmp"
$env:RECOMP_NV2A_TRACE = if ($env:RECOMP_FULL_PB_TRACE -eq '1') { '1' } else { '0' }
$env:RECOMP_PB_EXEC = '1'
$env:RECOMP_PB_SCAN = if ($env:RECOMP_FULL_PB_TRACE -eq '1') { '1' } else { '0' }
$argument = if ($ValidateOnly) { '--validate' } else { '--run' }
$process = Start-Process -FilePath $exe -ArgumentList $argument -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$processHandle = $process.Handle
$finished = $process.WaitForExit($Seconds * 1000)
if (-not $finished) {
    Stop-Process -Id $process.Id
    $process.WaitForExit()
    Write-Output "Stopped diagnostic run after $Seconds seconds."
} else {
    $process.WaitForExit()
    Write-Output "Exit code: $($process.ExitCode)"
}
Write-Output "Output: $stdout"
Write-Output "Diagnostics: $stderr"
Get-Content -LiteralPath $stdout -Tail 12
Get-Content -LiteralPath $stderr -Tail 30
if (-not $finished) { exit 124 }
exit $process.ExitCode
