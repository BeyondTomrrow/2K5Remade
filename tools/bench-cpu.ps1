param([string]$Tag = 'cpu', [int]$PlaySeconds = 150, [int]$Scale = 2)
# CPU cost per frame, independent of GPU contention: runs the CPU-vs-CPU quick
# game (tools\drive-quickgame.ps1) and samples NFL2K5.exe's total CPU time
# and the frames it presented ([FPS] lines, every 10 s) over the gameplay
# window. Prints ms of CPU per presented frame and the average FPS.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$env:NFL2K5_REC = ''
$env:RECOMP_GPU_PROFILE = '1'
$env:NFL2K5_RENDER_SCALE = "$Scale"
$env:NFL2K5_PRESENT_SHOT = "$root\logs\$Tag"
$env:NFL2K5_PRESENT_SHOT_SECONDS = '45'
$job = Start-Job -ScriptBlock { param($r, $t, $s) Set-Location $r; & "$r\tools\drive-quickgame.ps1" -Tag $t -PlaySeconds $s -ShotEvery 100000 } -ArgumentList $root, $Tag, $PlaySeconds
# drive-quickgame spends ~140 s in menus before play starts; sample from 60 s into play.
Start-Sleep 200
$p = Get-Process NFL2K5 -ErrorAction SilentlyContinue
if (-not $p) { Write-Output 'NFL2K5 not running at sample start'; Receive-Job $job -Wait; exit 1 }
$log = "$root\logs\$Tag.stderr.log"
$c0 = $p.TotalProcessorTime.TotalMilliseconds
$f0 = (Select-String -Path $log -Pattern '\[FPS\] (\d+)' -AllMatches).Count
Start-Sleep 60
$p.Refresh()
$c1 = $p.TotalProcessorTime.TotalMilliseconds
$fps = (Select-String -Path $log -Pattern '\[FPS\] (\d+)' | Select-Object -Skip $f0 | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$frames = ($fps | Measure-Object -Sum).Sum * 10
$avg = ($fps | Measure-Object -Average).Average
Write-Output ("{0}: CPU {1:N0} ms over 60 s, {2} frames, {3:N1} ms CPU/frame, avg {4:N1} FPS ({5})" -f $Tag, ($c1 - $c0), $frames, (($c1 - $c0) / [math]::Max($frames, 1)), $avg, ($fps -join ' '))
Receive-Job $job -Wait | Select-Object -Last 2
