param([string[]]$Steps = @(), [string]$Tag = 'menu', [string]$Exe = 'build\Release\NFL2K5.exe', [int]$BootSeconds = 50,
      [switch]$KeepRunning)
# Drive the menus with a list of steps and screenshot after each one.
# A step is '<button>[x<count>][:<seconds to wait>]', e.g. 'down x3:2', 'a:6',
# or 'shot' (screenshot only). Buttons are tools\press.ps1's names (a, b, x,
# y, start, back, up, down, left, right, lt, rt ...). Starts from the title:
# Settings and VIP each acknowledge separately, so Start, A, A, Start reaches
# the main menu on an existing profile.
# Screenshots: logs\<tag>-<n>.png. Uses logs\game.lock like drive-quickgame.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$defaultRetail = 'build\Release\NFL2K5.exe'
# Native packs are launched by the retail dispatcher in a child process.  Use
# the selected native executable directly so this driver follows the game,
# rather than treating a successful dispatcher handoff as an early exit.
if ($Exe -eq $defaultRetail) {
    $activePack = Join-Path $root 'mods\active-pack.ini'
    if (Test-Path $activePack) {
        $packName = (Get-Content $activePack -Raw).Trim()
        $candidate = Join-Path $root ("mods\packs\$packName\native\NFL2K5.exe")
        if ($packName -and (Test-Path $candidate -PathType Leaf)) {
            $Exe = $candidate
        }
    }
}
$env:NFL2K5_SKIP_INTRO = '1'
$env:NFL2K5_PRESS_FILE = "$root\logs\press.txt"
$env:NFL2K5_NO_HOST_PAD = '1'
$lock = "$root\logs\game.lock"
while ((Test-Path $lock) -and ((Get-Date) - (Get-Item $lock).LastWriteTime).TotalHours -lt 2) {
    $t = Get-Content $lock -Raw
    if ($t -match '^\S+ (\d+)' -and -not (Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue)) { break }
    Start-Sleep 15
}
"drive-menu $PID $(Get-Date -Format s) $Tag" | Set-Content $lock -Encoding ascii
while (Get-Process NFL2K5 -ErrorAction SilentlyContinue) { Start-Sleep 20 }
$p = Start-Process $Exe -WorkingDirectory $root -PassThru -RedirectStandardError "logs\$Tag.stderr.log" -RedirectStandardOutput "logs\$Tag.stdout.log"
Start-Sleep $BootSeconds
& "$PSScriptRoot\press.ps1" start; Start-Sleep 8
& "$PSScriptRoot\press.ps1" a; Start-Sleep 8
& "$PSScriptRoot\press.ps1" a; Start-Sleep 8
& "$PSScriptRoot\press.ps1" start; Start-Sleep 8
$n = 0
& "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-$n.png" | Out-Null
foreach ($s in $Steps) {
    if ($p.HasExited) { "EXITED at step $n ($s)"; break }
    $wait = 3; $count = 1; $btn = $s
    if ($btn -match '^(.*):(\d+)$') { $btn = $Matches[1]; $wait = [int]$Matches[2] }
    if ($btn -match '^(\S+) x(\d+)$') { $btn = $Matches[1]; $count = [int]$Matches[2] }
    if ($btn -ne 'shot') { for ($i = 0; $i -lt $count; $i++) { & "$PSScriptRoot\press.ps1" $btn; Start-Sleep -Milliseconds 400 } }
    Start-Sleep $wait
    $n++
    & "$PSScriptRoot\capscreen.ps1" -Out "logs\$Tag-$n.png" | Out-Null
}
if (-not $KeepRunning -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
if ($p.HasExited) { Select-String -Path "logs\$Tag.stderr.log" -Pattern '^\[CRASH\] (exception|function)' | ForEach-Object Line }
Remove-Item $lock -ErrorAction SilentlyContinue
"done: $n steps, screenshots logs\$Tag-0..$n.png"
