<#
.SYNOPSIS
    Build ESPN NFL 2K5 Native PC (or only the runtime and tools) with MSVC + Ninja.

.DESCRIPTION
    The script runs in four stages, each a function below:

      1. Toolchain   Find Visual Studio and the Windows SDK and set up the
                     MSVC environment for this process only (no Developer
                     Prompt needed).
      2. Generated   (-Game only) Prepare the recompiled game code: re-apply
                     hand patches, make the register-locals copy, check the
                     AC97 reset hook, write the function-range table.
      3. CMake       Configure and build into a build directory chosen from
                     the switches (table below).
      4. Finish      Run the toolchain check; for -Pack, copy the exe into
                     the installed mod pack.

    Build directories:

      (no switches)      build\<Configuration>         optimised (the default)
      -Optimize          build\<Configuration>-opt     optimised, separate dir
      -DebugGen          build\<Configuration>-od      generated code at /Od
      -Pack <name>       build\<Configuration>-<name>  a mod pack's code

    Logs go to logs\configure-<build dir>.log and logs\build-<build dir>.log.

.PARAMETER Configuration
    Release (default) or Debug.

.PARAMETER Game
    Build the game itself (needs generated code from tools/analyze.ps1).
    Without it, only the xboxrecomp runtime and the tools are built.

.PARAMETER Optimize
    Optimised generated code in build\<Configuration>-opt. Optimised is
    already the default; this only keeps the older separate directory.

.PARAMETER DebugGen
    Compile generated code at /Od in build\<Configuration>-od.

.PARAMETER Pack
    Build a mod pack's code. Analyse it first:
      tools/analyze.ps1 -Xbe <pack default.xbe> -Analysis analysis-<name> -Gen build\gen-<name>

.EXAMPLE
    .\tools\build.ps1 -Game
    Retail game, optimised, into build\Release.

.EXAMPLE
    .\tools\build.ps1 -Game -Pack MyPack
    A mod pack's code, installed beside the pack in mods\packs.

.NOTES
    Environment overrides:
      NFL2K5_VS_PATH       Visual Studio install folder (skips vswhere)
      NFL2K5_CMAKE_EXTRA   extra -D options for configure, space-separated
                           (e.g. "-DNFL2K5_ABI_CHECK=ON"). A cached option
                           keeps its value until it is set again.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [switch]$Game,
    [switch]$Optimize,
    [switch]$DebugGen,

    [ValidatePattern('^[A-Za-z0-9_.-]*$')]
    [string]$Pack = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path $PSScriptRoot -Parent

# Force-unblock options every optimised build gets. This is the recommended
$OptimizedCMakeOptions = @(
    '-DNFL2K5_OPTIMIZE=ON'
    '-DNFL2K5_FORCE_UNBLOCK_AUDIO_LOCK=ON'
    '-DNFL2K5_FORCE_UNBLOCK_33660_DRAIN=ON'
    '-DNFL2K5_FORCE_UNBLOCK_NETPOLL=ON'
    '-DNFL2K5_FORCE_UNBLOCK_STATE9_READY=ON'
)

# The verified AC97 reset write that regeneration must keep
$Ac97HookText = 'nfl2k5_ack_ac97_reset(eax + 0xFEC0010Bu)'


# =============================================================================
# Helpers
# =============================================================================

<# Run a native program, optionally teeing its output to a log, and throw on a
   non-zero exit code. ErrorActionPreference is relaxed while it runs because
   Windows PowerShell 5.1 turns any stderr line (a CMake warning, say) into a
   terminating error under 'Stop' when stderr is redirected. #>
function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$LogFile,
        [Parameter(Mandatory)][string]$FailMessage
    )
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($LogFile) {
            & $FilePath @Arguments 2>&1 | ForEach-Object { "$_" } |
                Tee-Object -FilePath $LogFile | Out-Host
        } else {
            & $FilePath @Arguments | Out-Host
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($code) { throw "$FailMessage (exit code $code)" }
}

function Invoke-Python {
    param(
        [Parameter(Mandatory)][string]$Script,
        [string[]]$Arguments = @()
    )
    # Build the list explicitly: an empty argument array passed through
    # PowerShell can arrive as $null and become a stray '' argument.
    $all = @("$Root/tools/$Script")
    if ($Arguments) { $all += $Arguments }
    Invoke-Native -FilePath 'python' -Arguments $all -FailMessage "tools/$Script failed"
}


# =============================================================================
# Stage 1: toolchain
# =============================================================================

<# Visual Studio install folder with the C++ x64 tools. vswhere lives under
   Program Files (x86) whatever drive VS itself is on. #>
function Find-VisualStudio {
    if ($env:NFL2K5_VS_PATH) {
        if (-not (Test-Path $env:NFL2K5_VS_PATH)) { throw "NFL2K5_VS_PATH not found: $env:NFL2K5_VS_PATH" }
        return $env:NFL2K5_VS_PATH
    }
    $vswhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        'F:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    )
    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path $vswhere)) { continue }
        $path = & $vswhere -latest -products '*' `
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                    -property installationPath
        if ($path) { return $path }
    }
    # Last resort: the original build machine's install.
    $fallback = 'F:\Program Files\Microsoft Visual Studio\2022\Community'
    if (Test-Path $fallback) { return $fallback }
    throw 'Visual Studio with the "Desktop development with C++" workload was not found. Set NFL2K5_VS_PATH to its install folder.'
}

<# Newest Windows SDK that has both the headers and the x64 libraries. #>
function Find-WindowsSdk {
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10"
        'F:\Program Files (x86)\Windows Kits\10'
        'C:\Program Files (x86)\Windows Kits\10'
    ) | Where-Object { Test-Path "$_\Include" } | Select-Object -Unique

    foreach ($sdkRoot in $roots) {
        $version = Get-ChildItem "$sdkRoot\Include" -Directory |
            Where-Object {
                (Test-Path "$($_.FullName)\um\Windows.h") -and
                (Test-Path "$sdkRoot\Lib\$($_.Name)\um\x64\kernel32.lib")
            } |
            Sort-Object { [version]$_.Name } -Descending |
            Select-Object -First 1
        if ($version) {
            return [pscustomobject]@{ Root = $sdkRoot; Version = $version.Name }
        }
    }
    throw 'No complete Windows 10/11 SDK found (needs um\Windows.h and x64 kernel32.lib).'
}

<# Point PATH/INCLUDE/LIB at MSVC and the SDK for this process, and return the
   CMake that ships with Visual Studio. #>
function Enable-MsvcEnvironment {
    $vs = Find-VisualStudio
    $msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $msvc) { throw "No MSVC toolset under $vs\VC\Tools\MSVC." }
    $sdk = Find-WindowsSdk
    $inc = "$($sdk.Root)\Include\$($sdk.Version)"
    $lib = "$($sdk.Root)\Lib\$($sdk.Version)"
    $cmakeDir = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"

    $env:PATH = "$($msvc.FullName)\bin\Hostx64\x64;$($sdk.Root)\bin\$($sdk.Version)\x64;$cmakeDir\Ninja;$env:PATH"
    $env:INCLUDE = "$($msvc.FullName)\include;$inc\ucrt;$inc\shared;$inc\um;$inc\winrt"
    $env:LIB = "$($msvc.FullName)\lib\x64;$lib\ucrt\x64;$lib\um\x64"
    $env:WindowsSdkDir = "$($sdk.Root)\"
    $env:WindowsSDKVersion = "$($sdk.Version)\"

    # Keep compiler temp files inside the tree (the generated sources are
    # large, and some machines have a small system drive).
    $env:TEMP = "$Root\build\temp"
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Force $env:TEMP | Out-Null

    Write-Host "MSVC $($msvc.Name), Windows SDK $($sdk.Version)"
    "$cmakeDir\CMake\bin\cmake.exe"
}


# =============================================================================
# Build layout
# =============================================================================

<# Every path and mode this run uses, decided once from the switches. #>
function Get-BuildLayout {
    if ($Optimize -and $DebugGen) { throw '-Optimize and -DebugGen cannot be combined.' }

    $dirSuffix = if ($Pack) { "-$Pack" } elseif ($Optimize) { '-opt' } elseif ($DebugGen) { '-od' } else { '' }
    $buildDir = "$Root\build\$Configuration$dirSuffix"

    [pscustomobject]@{
        BuildDir    = $buildDir
        LogName     = Split-Path $buildDir -Leaf
        # Optimised generated code is the default (2026-09-26), so the exe
        # that gets double-clicked is the fast one; -DebugGen opts out.
        Optimize    = -not $DebugGen
        GenDir      = if ($Pack) { "$Root\build\gen-$Pack" } else { "$Root\src\recomp\gen" }
        LocalsDir   = if ($Pack) { "$Root\build\gen-$Pack-locals" } else { "$Root\build\gen-locals" }
        AnalysisDir = if ($Pack) { "$Root\analysis-$Pack" } else { "$Root\analysis" }
        RangesFile  = "$buildDir\generated\nfl2k5_func_ranges.c"
        XbeSha256   = Get-PackXbeSha256
    }
}

<# SHA-256 of the XBE a pack's code was generated from (tools/analyze.ps1
   records it). Empty for the retail build. #>
function Get-PackXbeSha256 {
    if (-not $Pack) { return '' }
    $shaFile = "$Root\analysis-$Pack\input-sha256.json"
    if (-not (Test-Path $shaFile)) {
        throw "Missing $shaFile. Run tools/analyze.ps1 for pack '$Pack' first."
    }
    ((Get-Content -Raw $shaFile | ConvertFrom-Json).Hash).ToLower()
}


# =============================================================================
# Stage 2: generated code (-Game only)
# =============================================================================

function Update-GeneratedCode {
    param([Parameter(Mandatory)]$Layout)

    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw 'Python is required for -Game builds and was not found on PATH.'
    }
    if (-not (Test-Path "$($Layout.GenDir)\recomp_*.c")) {
        throw "No generated code in $($Layout.GenDir). Run tools/analyze.ps1 -Recompile first."
    }

    # Regeneration wipes every hand patch in the generated code; re-apply the
    # AC97 reset ack and the NFL2K5_FORCE_UNBLOCK_* patches. Idempotent, and
    # fails if an anchor moved. A pack may have changed a patched function, so
    # its misses are reported instead of being fatal.
    $patchArgs = @()
    if ($Pack) { $patchArgs = @('--gen', $Layout.GenDir, '--lenient') }
    Invoke-Python 'apply-gen-patches.py' $patchArgs

    # Retail compiles the faster register-locals copy. Large code packs can
    # contain detour caves with private register/flag conventions that the
    # retail ABI assumptions do not preserve, so packs compile the generated
    # code directly until the converter can prove those boundaries safe.
    if (-not $Pack) {
        Invoke-Python 'gen-reg-locals.py' @('--gen', $Layout.GenDir, '--out', $Layout.LocalsDir)
    }

    Assert-Ac97Hook -GenDir $Layout.GenDir

    # Address ranges of the converted functions, for the mod-pack interpreter
    # (src/interp/interp_setup.c).
    Invoke-Python 'gen-func-ranges.py' @($Layout.AnalysisDir, $Layout.RangesFile)
}

<# Fail if regeneration dropped the title's AC97 reset completion hook.
   Without it audio never initialises. #>
function Assert-Ac97Hook {
    param([Parameter(Mandatory)][string]$GenDir)
    $found = Select-String -Path "$GenDir\recomp_*.c" -SimpleMatch -Pattern $Ac97HookText -Quiet
    if ($found) { return }
    if ($Pack) {
        Write-Warning 'AC97 reset hook is not in this pack build.'
    } else {
        throw 'AC97 reset hook missing after regeneration. Restore the verified reset-write patch described in docs/PROGRESS.md.'
    }
}


# =============================================================================
# Stage 3: CMake
# =============================================================================

function Get-CMakeArguments {
    param([Parameter(Mandatory)]$Layout)

    $cmakeArgs = @(
        '-S', $Root
        '-B', $Layout.BuildDir
        '-G', 'Ninja'
        "-DCMAKE_BUILD_TYPE=$Configuration"
        '-DCMAKE_C_COMPILER=cl'
        '-DCMAKE_CXX_COMPILER=cl'
        "-DNFL2K5_BUILD_GAME=$(if ($Game) { 'ON' } else { 'OFF' })"
    )
    if ($Layout.Optimize) { $cmakeArgs += $OptimizedCMakeOptions }
    if ($Game) {
        $cmakeArgs += "-DNFL2K5_GEN_DIR=$($Layout.GenDir)"
        $cmakeArgs += "-DNFL2K5_LOCALS_DIR=$($Layout.LocalsDir)"
        $cmakeArgs += "-DNFL2K5_XBE_SHA256=$($Layout.XbeSha256)"
        if ($Pack) { $cmakeArgs += '-DNFL2K5_REG_LOCALS=OFF' }
    }
    if ($env:NFL2K5_CMAKE_EXTRA) {
        $cmakeArgs += @($env:NFL2K5_CMAKE_EXTRA -split '\s+' | Where-Object { $_ })
    }
    $cmakeArgs
}

function Invoke-CMakeBuild {
    param(
        [Parameter(Mandatory)][string]$CMake,
        [Parameter(Mandatory)]$Layout
    )
    New-Item -ItemType Directory -Force "$Root\logs" | Out-Null

    Invoke-Native -FilePath $CMake -Arguments (Get-CMakeArguments -Layout $Layout) `
                  -LogFile "$Root\logs\configure-$($Layout.LogName).log" `
                  -FailMessage 'CMake configure failed; see the configure log'

    # One compile job per logical processor. The generated files are up to
    # ~30 MB each; 12 jobs peaked well under 48 GB on the build machine.
    $jobs = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 }
    $buildArgs = @('--build', $Layout.BuildDir, '--parallel', $jobs)
    if ($Game) { $buildArgs += @('--target', 'NFL2K5', 'NFL2K5_toolchain_check') }

    Invoke-Native -FilePath $CMake -Arguments $buildArgs `
                  -LogFile "$Root\logs\build-$($Layout.LogName).log" `
                  -FailMessage 'Build failed; see the build log'
}


# =============================================================================
# Stage 4: finish
# =============================================================================

<# Copy a pack build beside its installed overlay. The in-game Features > Mod
   Packs screen switches between this exe and the retail one, so players never
   need the compiler or a launcher script. #>
function Install-PackBuild {
    param([Parameter(Mandatory)]$Layout)

    $installed = Get-ChildItem "$Root\mods\packs" -Directory -ErrorAction SilentlyContinue |
        Where-Object {
            $xbe = Join-Path $_.FullName 'default.xbe'
            (Test-Path $xbe) -and ((Get-FileHash $xbe -Algorithm SHA256).Hash.ToLower() -eq $Layout.XbeSha256)
        } |
        Select-Object -First 1
    if (-not $installed) {
        throw "Built pack '$Pack', but no pack in mods\packs has XBE hash $($Layout.XbeSha256)."
    }

    $native = Join-Path $installed.FullName 'native'
    New-Item -ItemType Directory -Force $native | Out-Null
    Copy-Item -Force "$($Layout.BuildDir)\NFL2K5.exe" (Join-Path $native 'NFL2K5.exe')
    if (Test-Path "$($Layout.BuildDir)\NFL2K5.pdb") {
        Copy-Item -Force "$($Layout.BuildDir)\NFL2K5.pdb" (Join-Path $native 'NFL2K5.pdb')
    }
    Write-Host "Installed native code pack: $native"
}


# =============================================================================
# Main
# =============================================================================

if ($Pack -and -not $Game) { throw '-Pack builds game code; add -Game.' }

$cmake  = Enable-MsvcEnvironment
$layout = Get-BuildLayout
Write-Host "Building into $($layout.BuildDir) ($(if ($layout.Optimize) { 'optimised' } else { '/Od generated code' }))"

if ($Game) { Update-GeneratedCode -Layout $layout }

Invoke-CMakeBuild -CMake $cmake -Layout $layout

Invoke-Native -FilePath "$($layout.BuildDir)\NFL2K5_toolchain_check.exe" `
              -FailMessage 'Native toolchain check failed'

if ($Pack) { Install-PackBuild -Layout $layout }

Write-Host 'Build complete.' -ForegroundColor Green
