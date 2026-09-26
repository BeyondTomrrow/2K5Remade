$root = Split-Path $PSScriptRoot -Parent
$page = Join-Path $root 'tools\scorebug-studio\index.html'
$presentations = Join-Path $root 'mods\presentations'
$library = @()

if (Test-Path -LiteralPath $presentations) {
    Get-ChildItem -LiteralPath $presentations -Directory | ForEach-Object {
        $manifest = Join-Path $_.FullName 'presentation.json'
        if (Test-Path -LiteralPath $manifest) {
            try {
                $data = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
                $library += [pscustomobject]@{
                    name = if ($data.name) { [string]$data.name } else { $_.Name }
                    data = $data
                }
            } catch {
                Write-Warning "Skipped invalid presentation package: $manifest"
            }
        }
    }
}

$generated = Join-Path $root 'tools\scorebug-studio\library.generated.js'
$json = @($library) | ConvertTo-Json -Depth 64 -Compress
[System.IO.File]::WriteAllText($generated, "window.SCOREBUG_LIBRARY = $json;`r`n", [System.Text.UTF8Encoding]::new($false))
Start-Process $page
