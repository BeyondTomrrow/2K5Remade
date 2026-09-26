param([string]$Path, [switch]$Quiet, [switch]$Rebuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
function Pick($Value, $Fallback) { if ($null -ne $Value -and "$Value" -ne '') { return $Value }; return $Fallback }

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

# Common AI scorebug schema: { canvas, style, layout: { team_left, center,
# team_right } }. Convert it rather than discarding its carefully authored
# rectangles and colours. The game uses a different set of live placeholders.
if (($Rebuild -or -not $json.PSObject.Properties['scorebug'] -or -not $json.scorebug) -and
    $json.PSObject.Properties['layout'] -and $json.layout) {
    $srcCanvas = $json.canvas
    $cw = if ($srcCanvas -and $srcCanvas.width) { [int]$srcCanvas.width } else { 1000 }
    $ch = if ($srcCanvas -and $srcCanvas.height) { [int]$srcCanvas.height } else { 56 }
    $elements = [System.Collections.ArrayList]::new()
    $add = {
        param($item)
        [void]$elements.Add([pscustomobject]$item)
    }
    $left = $json.layout.team_left
    $right = $json.layout.team_right
    $center = $json.layout.center
    if ($left) {
        & $add @{type='box';x=$left.x;y=$left.y;w=$left.width;h=$left.height;radius=0;fill=(Pick $left.background '#20242B')}
        if ($left.logo) { & $add @{type='image';src='{away.logo}';x=$left.logo.x;y=$left.logo.y;w=$left.logo.width;h=$left.logo.height} }
        if ($left.score) { & $add @{type='text';text='{away.score}';x=$left.score.x;y=$left.score.y;w=$left.score.width;h=$left.score.height;size=$left.score.font_size;weight=800;color=(Pick $left.score.color '#FFFFFF');align=(Pick $left.score.align 'center')} }
        if ($left.timeouts) { & $add @{type='timeouts';team='away';x=$left.timeouts.x;y=$left.timeouts.y;count=(Pick $left.timeouts.slot_count 3);bar_w=(Pick $left.timeouts.slot_width 18);bar_h=(Pick $left.timeouts.slot_height 5);gap=(Pick $left.timeouts.gap 6);on=(Pick $left.timeouts.active_color '#FFFFFF');off=(Pick $left.timeouts.inactive_color '#666666')} }
    }
    if ($center) {
        $down = $center.down_distance
        if ($down) {
            & $add @{type='box';x=($center.x+$down.x);y=($center.y+$down.y);w=$down.width;h=$down.height;radius=0;fill=(Pick $down.background '#E6E6E6')}
            & $add @{type='text';text='{down_distance}';x=($center.x+$down.x);y=($center.y+$down.y);w=$down.width;h=$down.height;size=(Pick $down.font_size 28);weight=800;color=(Pick $down.color '#111111');align=(Pick $down.align 'center')}
        }
        $clock = $center.clock
        if ($clock) {
            & $add @{type='box';x=($center.x+$clock.x);y=($center.y+$clock.y);w=$clock.width;h=$clock.height;radius=0;fill=(Pick $clock.background '#050505')}
            if ($clock.quarter) { & $add @{type='text';text='{quarter}';x=($center.x+$clock.x+$clock.quarter.x);y=($center.y+$clock.y+$clock.quarter.y);w=90;h=40;size=(Pick $clock.quarter.font_size 26);weight=800;color=(Pick $clock.quarter.color '#FFFFFF');align='center'} }
            if ($clock.game_clock) { & $add @{type='text';text='{clock}';x=($center.x+$clock.x+$clock.game_clock.x);y=($center.y+$clock.y+$clock.game_clock.y);w=120;h=40;size=(Pick $clock.game_clock.font_size 26);weight=800;color=(Pick $clock.game_clock.color '#FFFFFF');align='center'} }
        }
    }
    if ($right) {
        & $add @{type='box';x=$right.x;y=$right.y;w=$right.width;h=$right.height;radius=0;fill=(Pick $right.background '#20242B')}
        if ($right.logo) { & $add @{type='image';src='{home.logo}';x=$right.logo.x;y=$right.logo.y;w=$right.logo.width;h=$right.logo.height} }
        if ($right.score) { & $add @{type='text';text='{home.score}';x=($right.x+$right.score.x);y=($right.y+$right.score.y);w=$right.score.width;h=$right.score.height;size=$right.score.font_size;weight=800;color=(Pick $right.score.color '#FFFFFF');align=(Pick $right.score.align 'center')} }
        if ($right.timeouts) { & $add @{type='timeouts';team='home';x=($right.x+$right.timeouts.x);y=($right.y+$right.timeouts.y);count=(Pick $right.timeouts.slot_count 3);bar_w=(Pick $right.timeouts.slot_width 18);bar_h=(Pick $right.timeouts.slot_height 5);gap=(Pick $right.timeouts.gap 6);on=(Pick $right.timeouts.active_color '#FFFFFF');off=(Pick $right.timeouts.inactive_color '#666666')} }
    }
    $json.font = if ($json.style -and $json.style.font_family) { [string]$json.style.font_family } else { $json.font }
    $json | Add-Member NoteProperty scorebug ([pscustomobject]@{
        canvas=[pscustomobject]@{width=$cw;height=$ch;overflow_top=40}
        placement=[pscustomobject]@{center_x=0.5;center_y=0.90;width=0.78}
        fade_time=0.25; elements=@($elements)
    }) -Force
}
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
