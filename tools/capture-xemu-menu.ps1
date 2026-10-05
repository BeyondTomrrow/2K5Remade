$ErrorActionPreference = 'Stop'
$out = 'E:\NFL2K5-PC\analysis\xemu-captures'
New-Item -ItemType Directory -Force -Path $out | Out-Null

try {
    $client = [System.Net.Sockets.TcpClient]::new('127.0.0.1', 4444)
} catch {
    throw 'Xemu monitor is not available. Start it with tools\start-xemu-gdb.ps1, then restore the menu snapshot.'
}

$stream = $client.GetStream()
$writer = [System.IO.StreamWriter]::new($stream)
$writer.NewLine = "`r`n"
$writer.AutoFlush = $true
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$ram = "$out\nfl2k5-menu-$stamp.ram.bin".Replace('\', '/')

# Freeze at the menu, save all 64 MB of Xbox RAM, then resume Xemu. These are
# local monitor commands; no game data leaves the PC.
$writer.WriteLine('stop')
Start-Sleep -Milliseconds 300
$writer.WriteLine("pmemsave 0 67108864 `"$ram`"")
Start-Sleep -Seconds 3
$writer.WriteLine('cont')
Start-Sleep -Milliseconds 200
$writer.Dispose()
$stream.Dispose()
$client.Dispose()

if (-not (Test-Path $ram)) {
    throw 'Xemu did not produce a RAM capture. Keep the menu visible and run this script again.'
}
Write-Output "Captured menu RAM: $ram"
