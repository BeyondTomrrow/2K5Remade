# Builds harness.exe (in build\interp-difftest) with MSVC: the reference interpreter
# from commit 5ffca1d (symbols renamed orig_*) against the working tree's.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$out = "$root\build\interp-difftest"
New-Item -ItemType Directory -Force $out | Out-Null
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -property installationPath
if (-not $vs) { $vs = 'F:\Program' }
$msvc = (Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).FullName
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$ver = (Get-ChildItem "$sdkRoot\Include" -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
$env:PATH = "$msvc\bin\Hostx64\x64;$sdkRoot\bin\$ver\x64;$env:PATH"
$env:INCLUDE = "$msvc\include;$sdkRoot\Include\$ver\ucrt;$sdkRoot\Include\$ver\shared;$sdkRoot\Include\$ver\um"
$env:LIB = "$msvc\lib\x64;$sdkRoot\Lib\$ver\ucrt\x64;$sdkRoot\Lib\$ver\um\x64"
git -C $root show 5ffca1d:src/interp/x86_interp.c | Set-Content -Encoding ascii "$out\orig_interp.c"
git -C $root show 5ffca1d:src/interp/x86_interp_fpu.inc.c | Set-Content -Encoding ascii "$out\x86_interp_fpu.inc.c"
Set-Location $out
$inc = @('/I', "$root\src", '/I', "$root\src\recomp\gen", '/I', "$root\src\interp", '/I', "$root\external\xboxrecomp\src")
$ren = @('/Drecomp_interp_call=orig_recomp_interp_call', '/Drecomp_interp_dirty=orig_recomp_interp_dirty',
         '/Drecomp_interp_add_hook=orig_recomp_interp_add_hook', '/Dg_interp_dirty_bits=orig_g_interp_dirty_bits',
         '/Dg_interp_dirty_limit=orig_g_interp_dirty_limit', '/Dg_interp_active=orig_g_interp_active',
         '/Dnfl2k5_test_interp_flags=orig_nfl2k5_test_interp_flags', '/Dinterp_x87=orig_interp_x87',
         '/Dinterp_0f_sse=orig_interp_0f_sse')
& cl /nologo /O2 /Zi /c /I $out @inc @ren orig_interp.c /Fo:orig_interp.obj; if ($LASTEXITCODE) { exit 1 }
& cl /nologo /O2 /Zi /c @inc "$root\src\interp\x86_interp.c" /Fo:new_interp.obj; if ($LASTEXITCODE) { exit 1 }
& cl /nologo /O2 /Zi @inc "$PSScriptRoot\harness.c" orig_interp.obj new_interp.obj dbghelp.lib winmm.lib /Fe:harness.exe /link /DEBUG; if ($LASTEXITCODE) { exit 1 }
"built $out\harness.exe"
