param([int]$MaxSeconds = 240, [string]$Tag = 'freeze')
$sp = (Join-Path (Split-Path $PSScriptRoot -Parent) "logs")
$log = "E:\NFL2K5-PC\logs\$Tag.stderr.log"
$cdb = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2606.22001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
$p = Start-Process -FilePath E:\NFL2K5-PC\build\Release\NFL2K5.exe -WorkingDirectory E:\NFL2K5-PC -PassThru `
     -RedirectStandardError $log -RedirectStandardOutput "E:\NFL2K5-PC\logs\$Tag.stdout.log"
$null = $p.Handle  # keep a handle so HasExited is truthful
"started pid $($p.Id)"
$t0 = Get-Date; $last = -1; $same = 0
while (((Get-Date) - $t0).TotalSeconds -lt $MaxSeconds -and -not $p.HasExited) {
    Start-Sleep -Seconds 5
    $line = Select-String -Path $log -Pattern 'VBLANKPEEK' | Select-Object -Last 1
    if (-not $line) { continue }
    $v = [int]([regex]::Match($line.Line, 'vblanks=(\d+)').Groups[1].Value)
    if ($v -eq $last -and $v -gt 50) { $same++ } else { $same = 0 }
    $last = $v
    if ($same -ge 3) {
        "vblanks frozen at $v after $([int]((Get-Date) - $t0).TotalSeconds)s -- capturing stacks"
        & $cdb -pv -p $p.Id -y 'E:\NFL2K5-PC\build\Release' -c '.lines -e;~*kc 30;qd' 2>&1 |
            Out-File -Encoding utf8 "$sp\$Tag-stacks.txt"
        "stacks saved to $Tag-stacks.txt; game left running (pid $($p.Id))"
        return
    }
}
if ($p.HasExited) { "game exited (code $($p.ExitCode))" } else { "no freeze within $MaxSeconds s; last vblanks=$last; game left running" }
