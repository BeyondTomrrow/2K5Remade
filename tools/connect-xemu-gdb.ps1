$ErrorActionPreference = 'Stop'
$gdb = 'E:\NFL2K5-PC\dependencies\msys2\usr\bin\gdb.exe'
$commands = Join-Path $PSScriptRoot 'xemu-nfl2k5.gdb'

if (-not (Test-Path -LiteralPath $gdb)) {
    throw "GDB is not installed yet. Run tools\install-xemu-gdb.ps1 once."
}
if (-not (Get-Process xemu -ErrorAction SilentlyContinue)) {
    throw 'Start Xemu first with tools\start-xemu-gdb.ps1.'
}

& $gdb --command=$commands
