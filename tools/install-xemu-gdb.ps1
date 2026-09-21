$ErrorActionPreference = 'Stop'
$root = 'E:\NFL2K5-PC\dependencies\msys2'
$installer = "$env:TEMP\msys2-installer.exe"

if (Test-Path "$root\usr\bin\gdb.exe") {
    Write-Output 'GDB is already installed.'
    exit 0
}

New-Item -ItemType Directory -Force -Path (Split-Path $root) | Out-Null
winget install --id MSYS2.MSYS2 --exact --silent --accept-package-agreements --accept-source-agreements --location $root
if ($LASTEXITCODE) { throw 'MSYS2 installation failed.' }

$bash = "$root\usr\bin\bash.exe"
if (-not (Test-Path $bash)) { throw 'MSYS2 did not install in the requested project dependency folder.' }
& $bash -lc 'pacman -Sy --noconfirm mingw-w64-ucrt-x86_64-gdb'
if ($LASTEXITCODE) { throw 'GDB package installation failed.' }

$candidate = "$root\ucrt64\bin\gdb.exe"
if (-not (Test-Path $candidate)) { throw 'GDB was not found after package installation.' }
Copy-Item -LiteralPath $candidate -Destination "$root\usr\bin\gdb.exe" -Force
Write-Output "GDB ready: $root\usr\bin\gdb.exe"
