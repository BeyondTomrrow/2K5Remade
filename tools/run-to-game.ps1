param([string]$Tag = 'rtg', [int]$Tries = 3, [int]$HoldSeconds = 90, [int[]]$ShotsAfter = @(), [string]$Extra = '')
# Launch the game, drive the menus to Start Game with scripted presses, and
# retry if the run did not reach the Coach Matchup depth (FSM idx >= 5) by 205 s.
# Leaves the game running for $HoldSeconds after Start Game, taking window
# screenshots at the given offsets (seconds after 225), then stops it.
$sp = (Join-Path (Split-Path $PSScriptRoot -Parent) "logs")
$press = 'start@40,start@50,start@60,a@72,a@84,start@96,a@108,a@120,start@132,a@144,a@156,start@168,a@215,a@225'
if ($Extra) { $press = "$press,$Extra" }
for ($k = 1; $k -le $Tries; $k++) {
    $env:NFL2K5_SKIP_INTRO = '1'
    $env:NFL2K5_AUTO_PRESS = $press
    $log = "E:\NFL2K5-PC\logs\$Tag-$k.stderr.log"
    $p = Start-Process -FilePath E:\NFL2K5-PC\build\Release\NFL2K5.exe -WorkingDirectory E:\NFL2K5-PC -PassThru `
         -RedirectStandardError $log -RedirectStandardOutput "E:\NFL2K5-PC\logs\$Tag-$k.stdout.log"
    $null = $p.Handle  # keep a handle so HasExited is truthful
    Start-Sleep 205
    $fsm = Select-String -Path $log -Pattern 'FSM\] A84B18 idx=(\d)' | Select-Object -Last 1
    $idx = if ($fsm) { [int]$fsm.Matches[0].Groups[1].Value } else { 0 }
    if ($p.HasExited -or $idx -lt 5) {
        "try $k : not at Coach Matchup (idx=$idx, exited=$($p.HasExited)); retrying"
        Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction SilentlyContinue
        continue
    }
    "try $k : reached Coach Matchup (idx=$idx); log $log"
    $t0 = Get-Date
    foreach ($s in $ShotsAfter) {
        while (((Get-Date) - $t0).TotalSeconds -lt ($s + 20)) { Start-Sleep -Milliseconds 250 }
        & "$PSScriptRoot\capwin.ps1" -At 0 -Prefix "$Tag-$s" | Out-Null
        "shot $Tag-$s-0.png"
    }
    while (((Get-Date) - $t0).TotalSeconds -lt $HoldSeconds) { Start-Sleep 1 }
    if ($env:RTG_STACKS) {
        $cdb = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
        foreach ($s in 1..2) {
            & $cdb -pv -p $p.Id -y 'E:\NFL2K5-PC\build\Release' -c '~*kc 45;qd' 2>&1 | Out-File -Encoding utf8 "$sp\$Tag-stacks$s.txt"
            Start-Sleep 3
        }
        "stacks $Tag-stacks1.txt $Tag-stacks2.txt"
    }
    Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction SilentlyContinue
    "LOG=$log"
    return
}
"failed to reach the game in $Tries tries"
