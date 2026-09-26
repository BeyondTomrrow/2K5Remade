param([string]$Path)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (-not $Path) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [System.Windows.Forms.OpenFileDialog]::new()
    $dialog.Title = 'Choose a scorebug or presentation JSON file'
    $dialog.Filter = 'JSON files (*.json)|*.json|All files (*.*)|*.*'
    $dialog.InitialDirectory = $root
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
    $Path = $dialog.FileName
}

$source = Get-Item -LiteralPath $Path
if ($source.Extension -notmatch '^\.json$') { throw 'Choose a .json file.' }

$name = $source.BaseName
try {
    $json = Get-Content -LiteralPath $source.FullName -Raw | ConvertFrom-Json
    if ($json.name) { $name = [string]$json.name }
} catch {
    throw "The selected file is not valid JSON: $($_.Exception.Message)"
}

$bad = [IO.Path]::GetInvalidFileNameChars() + [char[]]'\\/:*?"<>|'
foreach ($ch in $bad) { $name = $name.Replace([string]$ch, '') }
$name = $name.Trim().TrimEnd('.')
if (-not $name) { $name = 'Imported Scorebug' }

$destination = Join-Path $root "mods\presentations\$name"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $destination 'presentation.json') -Force

# A package often has its artwork and themes next to its JSON. Carry those
# standard folders with it so relative paths in presentation.json still work.
foreach ($folder in 'images','music','video') {
    $from = Join-Path $source.DirectoryName $folder
    if (Test-Path -LiteralPath $from -PathType Container) {
        Copy-Item -LiteralPath $from -Destination $destination -Recurse -Force
    }
}

Write-Host "Installed scorebug package: $destination"
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show(
    "Installed to:`n$destination`n`nRestart NFL2K5, then select it at Coach Match Up > Presentation.",
    'Scorebug installed', [System.Windows.Forms.MessageBoxButtons]::OK,
    [System.Windows.Forms.MessageBoxIcon]::Information
) | Out-Null
