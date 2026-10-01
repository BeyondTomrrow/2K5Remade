Set-Location E:\NFL2K5-PC
$env:NFL2K5_SKIP_INTRO='1'; $env:NFL2K5_PRESS_FILE="E:\NFL2K5-PC\logs\press.txt"; $env:NFL2K5_NO_HOST_PAD='1'
$env:RECOMP_HW_WATCH='1'; $env:RECOMP_HW_WATCH_ADDR='B04D28'
foreach ($run in 1..6) {
  Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep 2
  $log = "logs\fz6_$run.stderr.log"
  $p = Start-Process 'build\Release\NFL2K5.exe' -WorkingDirectory (Get-Location) -PassThru -RedirectStandardError $log -RedirectStandardOutput "logs\fz6_$run.stdout.log"
  Start-Sleep 45; & .\tools\press.ps1 start; Start-Sleep 6; & .\tools\press.ps1 a; Start-Sleep 3; & .\tools\press.ps1 start
  $t = 0
  while ($t -lt 240 -and -not (Select-String -Path $log -Pattern 'decoder create' -Quiet)) { Start-Sleep 1; $t++ }
  if ($t -ge 240) { "run $run : no custom song"; continue }
  Start-Sleep 6; if ($env:REPRO_SKIP) { & .\tools\press.ps1 r3; Start-Sleep $(if ($env:REPRO_SKIP_WAIT) { [int]$env:REPRO_SKIP_WAIT } else { 8 }) }; & .\tools\press.ps1 a; Start-Sleep 20
  $bad = Select-String -Path $log -Pattern 'SCHED\] bad|CRASH\] function' -Quiet
  "run $run : song after $t s, bad=$bad"
  if ($bad) { break }
}
Get-Process NFL2K5 -ErrorAction SilentlyContinue | Stop-Process -Force


