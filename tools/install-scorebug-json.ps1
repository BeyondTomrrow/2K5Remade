param([string]$Path, [switch]$Quiet)

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
    if ($json -and $json.PSObject.Properties['name'] -and $json.name) { $name = [string]$json.name }
} catch {
    throw "The selected file is not valid JSON: $($_.Exception.Message)"
}

$bad = [IO.Path]::GetInvalidFileNameChars() + [char[]]'\\/:*?"<>|'
foreach ($ch in $bad) { $name = $name.Replace([string]$ch, '') }
$name = $name.Trim().TrimEnd('.')
if (-not $name) { $name = 'Imported Scorebug' }

$destination = Join-Path $root "mods\presentations\$name"
New-Item -ItemType Directory -Force -Path $destination | Out-Null

# Accept ordinary AI-produced settings JSON as well as a complete game package.
# Missing presentation pieces get a working, dynamic scorebug rather than an
# import failure. Existing scorebug/animation/music data is retained.
if (-not ($json -is [PSCustomObject])) { $json = [pscustomobject]@{} }
if (-not $json.PSObject.Properties['format']) { $json | Add-Member NoteProperty format 1 }
if (-not $json.PSObject.Properties['name']) { $json | Add-Member NoteProperty name $name }
if (-not $json.PSObject.Properties['network']) { $json | Add-Member NoteProperty network $name }
if (-not $json.PSObject.Properties['font']) { $json | Add-Member NoteProperty font 'Bahnschrift' }
if (-not $json.PSObject.Properties['scorebug'] -or -not $json.scorebug) {
    $json | Add-Member NoteProperty scorebug ([pscustomobject]@{
        canvas = [pscustomobject]@{ width = 1000; height = 56; overflow_top = 40 }
        placement = [pscustomobject]@{ center_x = 0.5; center_y = 0.915; width = 0.78 }
        fade_time = 0.25
        elements = @(
            [pscustomobject]@{ type = 'box'; x = 0; y = 0; w = 1000; h = 56; radius = 8; fill = '#141820'; stroke = '#FFFFFF24'; stroke_width = 1 },
            [pscustomobject]@{ type = 'text'; text = '{network}'; x = 14; y = 7; w = 110; h = 42; size = 18; weight = 800; color = '#FFFFFF'; align = 'center' },
            [pscustomobject]@{ type = 'text'; text = '{away.abbr}  {away.score}'; x = 160; y = 7; w = 220; h = 42; size = 28; weight = 800; color = '#FFFFFF'; align = 'center' },
            [pscustomobject]@{ type = 'text'; text = '{home.abbr}  {home.score}'; x = 400; y = 7; w = 220; h = 42; size = 28; weight = 800; color = '#FFFFFF'; align = 'center' },
            [pscustomobject]@{ type = 'text'; text = '{quarter}  {clock}'; x = 660; y = 7; w = 170; h = 42; size = 25; weight = 800; color = '#FFFFFF'; align = 'center' },
            [pscustomobject]@{ type = 'text'; text = '{down_distance}'; x = 840; y = 7; w = 150; h = 42; size = 20; weight = 700; color = '#F4C542'; align = 'center' }
        )
    }) -Force
}
if (-not $json.scorebug.PSObject.Properties['canvas'] -or -not $json.scorebug.canvas) { $json.scorebug | Add-Member NoteProperty canvas ([pscustomobject]@{ width = 1000; height = 56; overflow_top = 40 }) -Force }
if (-not $json.scorebug.PSObject.Properties['placement'] -or -not $json.scorebug.placement) { $json.scorebug | Add-Member NoteProperty placement ([pscustomobject]@{ center_x = 0.5; center_y = 0.915; width = 0.78 }) -Force }
if (-not $json.scorebug.PSObject.Properties['elements'] -or -not $json.scorebug.elements) { $json.scorebug | Add-Member NoteProperty elements @() -Force }
if (-not $json.PSObject.Properties['music']) { $json | Add-Member NoteProperty music ([pscustomobject]@{ volume = 0.15; fade_out = 2.5; duck_game_audio = 1; mute_game_music = $false; intro = @(); outro = @() }) }

$targetJson = Join-Path $destination 'presentation.json'
$json | ConvertTo-Json -Depth 64 | Set-Content -LiteralPath $targetJson -Encoding utf8

# A package often has its artwork and themes next to its JSON. Carry those
# standard folders with it so relative paths in presentation.json still work.
foreach ($folder in 'images','music','video') {
    $from = Join-Path $source.Directory.FullName $folder
    $to = Join-Path $destination $folder
    if ((Test-Path -LiteralPath $from -PathType Container) -and
        -not [string]::Equals($from, $to, [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $from -Destination $destination -Recurse -Force
    }
}

Write-Host "Installed scorebug package: $destination"
if (-not $Quiet) {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show(
        "Installed to:`n$destination`n`nRestart NFL2K5, then select it at Coach Match Up > Presentation.",
        'Scorebug installed', [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    ) | Out-Null
}
