# Isolated native CBS review. The existing m10 driver shares one fixed root;
# this harness instead keeps each run's saves, settings, input and composed
# D3D11 captures together. It never stops a process it did not launch.
param(
    [ValidateSet('prepare','launch','press','shot','status','stop')][string]$Action = 'status',
    [ValidatePattern('^[a-zA-Z0-9_-]{1,32}$')][string]$Tag = 'cbs',
    [string[]]$Buttons = @(), [string[]]$SetEnv = @(),
    [switch]$EnableCardAck, [switch]$Visible,
    [ValidateRange(1,16)][int]$RenderScale = 1
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$review = Join-Path $repo "outputs\cbs-2026\native-review-$Tag"
$runtime = Join-Path $review 'root'
$privateHome = Join-Path $review 'userdata'
$exe = Join-Path $runtime 'NFL2K5_CBSReview.exe'
$record = Join-Path $review 'process.json'
$press = Join-Path $review 'press.txt'
function LinkDirectory([string]$destination, [string]$source) {
    if (Test-Path -LiteralPath $source) {
        New-Item -ItemType Junction -Path $destination -Target $source | Out-Null
    }
}
function OwnedProcess {
    if (-not (Test-Path -LiteralPath $record)) { return $null }
    $saved = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    $p = Get-Process -Id $saved.id -ErrorAction SilentlyContinue
    if ($p -and $p.Path -eq $exe -and $p.StartTime.ToUniversalTime().Ticks -eq $saved.startTicks) { return $p }
    return $null
}
switch ($Action) {
'prepare' {
    if (Test-Path -LiteralPath $review) { throw 'Use a fresh tag; existing review data is preserved.' }
    New-Item -ItemType Directory -Path $runtime, $privateHome, "$review\frames", "$runtime\mods", "$runtime\saves", "$runtime\logs", "$runtime\cache", "$runtime\original\disc\VC_53450030" -Force | Out-Null
    # Private disc directory: base banks are hard links; generated extra banks
    # are NOT linked. Archive extension can write its own G without changing
    # a bank in the source disc. default.xbe is only read by the game.
    foreach ($f in Get-ChildItem "$repo\original" -File) {
        New-Item -ItemType HardLink -Path "$runtime\original\$($f.Name)" -Target $f.FullName | Out-Null
    }
    foreach ($entry in Get-ChildItem "$repo\original\disc") {
        if ($entry.Name -eq 'VC_53450030') { continue }
        if ($entry.PSIsContainer) { LinkDirectory "$runtime\original\disc\$($entry.Name)" $entry.FullName }
        else { New-Item -ItemType HardLink -Path "$runtime\original\disc\$($entry.Name)" -Target $entry.FullName | Out-Null }
    }
    foreach ($bank in '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F') {
        New-Item -ItemType HardLink -Path "$runtime\original\disc\VC_53450030\$bank" -Target "$repo\original\disc\VC_53450030\$bank" | Out-Null
    }
    foreach ($d in 'data','font','Music') { LinkDirectory "$runtime\$d" "$repo\$d" }
    foreach ($d in Get-ChildItem "$repo\mods" -Directory) {
        if ($d.Name -notin @('packs','patches','downloads')) {
            if ($d.Name -eq 'presentations') {
                New-Item -ItemType Directory -Path "$runtime\mods\presentations" | Out-Null
                foreach ($pkg in Get-ChildItem $d.FullName -Directory) {
                    # WebView2 does not serve sibling scripts through a
                    # junction escaping the mapped presentation root.
                    if ($pkg.Name -in @('CBS_2026','_runtime')) { Copy-Item -LiteralPath $pkg.FullName -Destination "$runtime\mods\presentations\$($pkg.Name)" -Recurse }
                    else { LinkDirectory "$runtime\mods\presentations\$($pkg.Name)" $pkg.FullName }
                }
            } else { LinkDirectory "$runtime\mods\$($d.Name)" $d.FullName }
        }
    }
    foreach ($d in 'UDATA','TDATA','settings','UserData','TitleData','SystemData') {
        if (Test-Path -LiteralPath "$repo\saves\$d") { Copy-Item -LiteralPath "$repo\saves\$d" -Destination "$runtime\saves\$d" -Recurse }
    }
    foreach ($ini in Get-ChildItem $repo -File -Filter *.ini) { Copy-Item -LiteralPath $ini.FullName -Destination $runtime }
    @('package = NFL on CBS 2026','intro = 0','outro = 0','animations = 1','theme_volume = 0','custom_commentary = 0') | Set-Content "$runtime\mods\presentation.ini" -Encoding ascii
    if ($EnableCardAck) {
        $manifestPath = "$runtime\mods\presentations\CBS_2026\mod.json"
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $manifest | Add-Member -MemberType NoteProperty -Name replaces -Value @('player_cards') -Force
        $manifest | Add-Member -MemberType NoteProperty -Name player_cards_ack -Value $true -Force
        [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding $false))
    }
    Copy-Item -LiteralPath "$repo\build\Release\NFL2K5.exe" -Destination $exe
    "Prepared $review"
}
'launch' {
    if (-not (Test-Path -LiteralPath $exe)) { throw 'Prepare this tag first.' }
    if (OwnedProcess) { throw 'This review is already running.' }
    if (Get-Process cl,link,ninja,MSBuild -ErrorAction SilentlyContinue) { throw 'A build is running; wait before launching.' }
    $settings = @{
        NFL2K5_ROOT=$runtime; NFL2K5_PORTABLE='0'; LOCALAPPDATA=$privateHome;
        NFL2K5_SKIP_INTRO='1'; NFL2K5_PRESS_FILE=$press; NFL2K5_NO_HOST_PAD='always';
        RECOMP_BACKGROUND='1'; RECOMP_AUDIO_MUTE='1'; RECOMP_GPU='1';
        NFL2K5_NO_PACK_DISPATCH='1'; NFL2K5_PRIVATE_PACKS='1'; NFL2K5_RENDER_SCALE=[string]$RenderScale;
        NFL2K5_PRES_LOG='1'; NFL2K5_PRESENT_SHOT="$review\frames\composed";
        NFL2K5_PRESENT_SHOT_SECONDS='1'; NFL2K5_PRES_TEST=$null; NFL2K5_PRES_EVENT=$null;
        NFL2K5_AUTO_PRESS=$null; NFL2K5_ERAS=$null; NFL2K5_VANILLA=$null
    }
    foreach ($item in $SetEnv) { $key,$value = $item -split '=',2; $settings[$key]=$value }
    # Presence of RECOMP_BACKGROUND hides the game, even when its value is 0.
    # An explicitly visible review clears it and opens a normal window.
    if ($Visible) { $settings.RECOMP_BACKGROUND=$null; $settings.RECOMP_BACKGROUND_WINDOW=$null }
    $old = @{}
    try {
        foreach ($key in $settings.Keys) { $old[$key]=[Environment]::GetEnvironmentVariable($key,'Process'); [Environment]::SetEnvironmentVariable($key,$settings[$key],'Process') }
        $windowStyle = if ($Visible) { 'Normal' } else { 'Hidden' }
        $p = Start-Process -FilePath $exe -ArgumentList '--retail' -WorkingDirectory $runtime -WindowStyle $windowStyle -PassThru -RedirectStandardError "$review\stderr.log" -RedirectStandardOutput "$review\stdout.log"
        @{id=$p.Id; startTicks=$p.StartTime.ToUniversalTime().Ticks} | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding ascii
        "Launched private CBS review PID $($p.Id); logs: $review"
    } finally { foreach ($key in $old.Keys) { [Environment]::SetEnvironmentVariable($key,$old[$key],'Process') } }
}
'press' {
    if (-not (OwnedProcess)) { throw 'The owned review process is not running.' }
    if (-not $Buttons.Count) { throw 'Supply button names with -Buttons.' }
    if (Test-Path -LiteralPath $press) { throw 'Previous input has not been consumed.' }
    ($Buttons -join ' ') | Set-Content -LiteralPath $press -Encoding ascii
}
'shot' {
    $frame = Get-ChildItem "$review\frames" -Filter '*.bmp' | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($frame) { $frame.FullName } else { 'No composited frame yet.' }
}
'status' {
    $p=OwnedProcess
    if ($p) { $p | Select-Object Id,CPU,StartTime,Responding }
    if (Test-Path -LiteralPath "$review\stderr.log") { Get-Content -LiteralPath "$review\stderr.log" -Tail 15 }
}
'stop' { $p=OwnedProcess; if ($p) { Stop-Process -Id $p.Id; "Stopped review PID $($p.Id)" } else { 'Owned review is not running.' } }
}
