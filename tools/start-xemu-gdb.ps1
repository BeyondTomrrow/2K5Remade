param(
    [switch]$PauseAtBoot
)

$ErrorActionPreference = 'Stop'
$xemu = 'E:\xemu-0.8.136-windows-x86_64\xemu.exe'
$port = 1234

if (-not (Test-Path -LiteralPath $xemu)) {
    throw "Xemu was not found at $xemu"
}
if (Get-Process xemu -ErrorAction SilentlyContinue) {
    throw 'Close Xemu before starting the debugger-enabled copy.'
}

$args = @('-gdb', "tcp:127.0.0.1:$port",
          '-monitor', 'tcp:127.0.0.1:4444,server=on,wait=off')
if ($PauseAtBoot) { $args += '-S' }

Start-Process -FilePath $xemu -ArgumentList $args -WorkingDirectory (Split-Path $xemu) | Out-Null
Write-Output "Xemu started with its local GDB server on 127.0.0.1:$port."
Write-Output 'Restore the NFL 2K5 menu snapshot in Xemu, then run tools\capture-xemu-menu.ps1.'
