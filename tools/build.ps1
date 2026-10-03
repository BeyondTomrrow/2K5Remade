# -Pack <name>: build a mod pack's code (tools/analyze.ps1 -Xbe <its default.xbe>
# -Analysis analysis-<name> -Gen build\gen-<name>) into build\<Configuration>-<name>.
param([ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$Game, [switch]$Optimize, [switch]$DebugGen, [string]$Pack = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vs = $null
foreach ($candidate in @('C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe','F:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')) {
  if (Test-Path $candidate) { $vs = & $candidate -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; if ($vs) { break } }
}
if (-not $vs) { $vs = 'F:\Program Files\Microsoft Visual Studio\2022\Community' }
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdk = @('F:\Program Files (x86)\Windows Kits\10','C:\Program Files (x86)\Windows Kits\10') | Where-Object { Test-Path "$_\Include" } | Select-Object -First 1
if (-not $msvc -or -not $sdk) { throw 'MSVC or Windows SDK files missing.' }
$sdkver = Get-ChildItem "$sdk\Include" -Directory | Where-Object { (Test-Path "$($_.FullName)\um\Windows.h") -and (Test-Path "$sdk\Lib\$($_.Name)\um\x64\kernel32.lib") } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdkver) { throw 'No complete Windows SDK detected.' }
$ver = $sdkver.Name
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$sdk\bin\$ver\x64;$ninja;" + $env:PATH
$env:INCLUDE = "$($msvc.FullName)\include;$sdk\Include\$ver\ucrt;$sdk\Include\$ver\shared;$sdk\Include\$ver\um;$sdk\Include\$ver\winrt"
$env:LIB = "$($msvc.FullName)\lib\x64;$sdk\Lib\$ver\ucrt\x64;$sdk\Lib\$ver\um\x64"
$env:WindowsSdkDir = "$sdk\"
$env:WindowsSDKVersion = "$ver\"
$env:TEMP = "$root\build\temp"
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
# Fail explicitly if regeneration drops the title's reset completion hook.
if ($Game) {
  # Regeneration wipes every hand patch in src/recomp/gen; re-apply the
  # AC97 reset ack and NFL2K5_FORCE_UNBLOCK_* ones (idempotent, fails on a moved anchor).
  $genDir = if ($Pack) { "$root\build\gen-$Pack" } else { "$root\src\recomp\gen" }
  $localsDir = if ($Pack) { "$root\build\gen-$Pack-locals" } else { "$root\build\gen-locals" }
  # A pack may have changed a patched function: its misses are reported, not fatal.
  $patchArgs = if ($Pack) { @('--gen', $genDir, '--lenient') } else { @() }
  & python "$root/tools/apply-gen-patches.py" @patchArgs
  if ($LASTEXITCODE) { throw 'Re-applying generated-code patches failed; see tools/apply-gen-patches.py.' }
  # Guest registers in locals: a transformed copy in build/gen-locals that
  # config/game.cmake compiles (NFL2K5_REG_LOCALS, default ON).
  & python "$root/tools/gen-reg-locals.py" --gen $genDir --out $localsDir
  if ($LASTEXITCODE) { throw 'tools/gen-reg-locals.py failed.' }
  $audioSource = (Get-Content -Raw -Path "$genDir/recomp_*.c")
  if (-not ($audioSource -join "").Contains('nfl2k5_ack_ac97_reset(eax + 0xFEC0010Bu)')) {
    if ($Pack) { Write-Warning 'AC97 reset hook is not in this pack build.' }
    else { throw 'AC97 reset hook missing after regeneration. Restore the verified reset-write patch described in docs/PROGRESS.md.' }
  }
}
$gameOption = if ($Game) { 'ON' } else { 'OFF' }
# -Optimize: generated code compiled with optimisation (NFL2K5_OPTIMIZE=ON) in its
# own build directory, so the /Od build stays usable. A fresh cache there gets the
# current recommended force-unblock set (see PROJECT_STATUS.md, 2026-09-23).
# 2026-09-26: optimised generated code is the default, so build\Release\NFL2K5.exe
# (the exe that gets double-clicked) is the fast one. -DebugGen builds the /Od
# variant in build\<Configuration>-od; -Optimize keeps building -opt as before.
$buildDir = if ($Pack) { "$root\build\$Configuration-$Pack" } elseif ($Optimize) { "$root\build\$Configuration-opt" } elseif ($DebugGen) { "$root\build\$Configuration-od" } else { "$root\build\$Configuration" }
if (-not $DebugGen) { $Optimize = $true }
$extra = if ($Optimize) { @('-DNFL2K5_OPTIMIZE=ON', '-DNFL2K5_FORCE_UNBLOCK_AUDIO_LOCK=ON', '-DNFL2K5_FORCE_UNBLOCK_33660_DRAIN=ON', '-DNFL2K5_FORCE_UNBLOCK_NETPOLL=ON', '-DNFL2K5_FORCE_UNBLOCK_STATE9_READY=ON') } else { @() }
if ($Game) { $extra += @("-DNFL2K5_GEN_DIR=$genDir", "-DNFL2K5_LOCALS_DIR=$localsDir") }
if ($Game) {
  # The XBE this tree was generated from (tools/analyze.ps1 records it).
  $shaFile = if ($Pack) { "$root\analysis-$Pack\input-sha256.json" } else { '' }
  $xbeSha = if ($shaFile -and (Test-Path $shaFile)) { ((Get-Content -Raw $shaFile | ConvertFrom-Json).Hash).ToLower() } else { '' }
  $extra += @("-DNFL2K5_XBE_SHA256=$xbeSha")
}
# NFL2K5_CMAKE_EXTRA: more -D options for this configure (diagnostics such as
# -DNFL2K5_ABI_CHECK=ON); a cached option keeps its value until set again.
if ($env:NFL2K5_CMAKE_EXTRA) { $extra += ($env:NFL2K5_CMAKE_EXTRA -split ' ') }
& $cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DNFL2K5_BUILD_GAME=$gameOption" @extra -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl 2>&1 | Tee-Object "$root\logs\configure-$Configuration.log"
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
if ($Game) { $buildTargets = @('--target','NFL2K5','NFL2K5_toolchain_check') }
else { $buildTargets = @() }
# One compile job per logical processor (the generated files are up to ~30 MB
# each; 12 jobs peak well under the 48 GB this machine has).
$jobs = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 }
& $cmake --build $buildDir @buildTargets --parallel $jobs 2>&1 | Tee-Object "$root\logs\build-$Configuration.log"
if ($LASTEXITCODE) { throw 'Build failed; see logs.' }
& "$buildDir\NFL2K5_toolchain_check.exe"
if ($LASTEXITCODE) { throw 'Native toolchain check failed.' }
if (-not $Game -and -not (Test-Path "$root\build\$Configuration\NFL2K5_Rebuild.exe")) { throw 'Native reconstruction build missing.' }
if ($Game -and $Pack) {
  # Put the native code build beside the installed overlay.  The in-game
  # Features > Mod Packs screen switches between this executable and the
  # retail build; players never need the compiler or a launcher script.
  $installed = Get-ChildItem "$root\mods\packs" -Directory -ErrorAction SilentlyContinue | Where-Object {
    $xbe = Join-Path $_.FullName 'default.xbe'
    (Test-Path $xbe) -and ((Get-FileHash $xbe -Algorithm SHA256).Hash.ToLower() -eq $xbeSha)
  } | Select-Object -First 1
  if (-not $installed) { throw "Built pack '$Pack', but no installed pack has XBE hash $xbeSha." }
  $native = Join-Path $installed.FullName 'native'
  New-Item -ItemType Directory -Force $native | Out-Null
  Copy-Item -Force "$buildDir\NFL2K5.exe" (Join-Path $native 'NFL2K5.exe')
  if (Test-Path "$buildDir\NFL2K5.pdb") { Copy-Item -Force "$buildDir\NFL2K5.pdb" (Join-Path $native 'NFL2K5.pdb') }
  Write-Host "Installed native code pack: $native"
}
