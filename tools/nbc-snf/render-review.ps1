param([string]$Scene = 'qb-stat',[string]$Out = 'outputs\snf-previews\review\player-stat-motion.mp4',[string]$Label = 'SNF PRESENTATION PREVIEW')
$snfRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$snfExe = Join-Path $snfRoot 'tools\presentation-render\presentation-render.exe'
if (!(Test-Path -LiteralPath $snfExe)) { throw 'Existing presentation renderer is missing. This script does not build it.' }
$snfOutput = Join-Path $snfRoot $Out
$snfFrames = Join-Path (Split-Path $snfOutput -Parent) ($Scene + '-frames')
New-Item -ItemType Directory -Force $snfFrames | Out-Null
$env:TEMP = Join-Path $snfFrames ('.runtime-' + [Guid]::NewGuid().ToString('N'))
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
# Every PNG is a different actual Web Animations frame. Hold periods repeat a
# completed frame; entrances and exits are sampled at 30 fps, not five stills.
$snfTimes = [System.Collections.Generic.List[int]]::new()
$snfTimes.Add(500)
if($Scene -eq 'intro') {
  # The real fixture waits through crowd before opening at warmup. Preserve
  # the boundary and later toss/kickoff frames to show there is no late title.
  for($snfMs=600;$snfMs -le 930;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(1399)
  for($snfMs=1400;$snfMs -le 2600;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(7399)
  for($snfMs=7400;$snfMs -le 8000;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(8099);$snfTimes.Add(8100);$snfTimes.Add(9399)
  for($snfMs=9400;$snfMs -le 10000;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(10100)
} elseif($Scene -eq 'career-fact') {
  for($snfMs=600;$snfMs -le 1800;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(4949)
  for($snfMs=4950;$snfMs -le 5650;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(5750)
} elseif($Scene -eq 'coin-toss') {
  for($snfMs=600;$snfMs -le 1800;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(2599)
  for($snfMs=2600;$snfMs -le 2900;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(6199)
  for($snfMs=6200;$snfMs -le 6900;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(7000)
} elseif($Scene -eq 'replay-wipe') {
  for($snfMs=600;$snfMs -le 1750;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(1900)
} elseif($Scene -in @('touchdown','field-goal')) {
  for($snfMs=600;$snfMs -le 6200;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(9599)
  for($snfMs=9600;$snfMs -le 11400;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(11500)
} elseif($Scene -in @('timeout','player-profile','profile-receiver')) {
  for($snfMs=600;$snfMs -le 1800;$snfMs+=33){$snfTimes.Add($snfMs)}
  if($Scene -eq 'player-profile') {
    $snfTimes.Add(4599)
    for($snfMs=4600;$snfMs -le 5500;$snfMs+=33){$snfTimes.Add($snfMs)}
  }
  $snfExit = if($Scene -eq 'timeout'){5600}elseif($Scene -eq 'player-profile'){8600}else{8100}
  $snfTimes.Add($snfExit-1)
  for($snfMs=$snfExit;$snfMs -le ($snfExit+600);$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add($snfExit+700)
} elseif($Scene -eq 'watermark') {
  $snfTimes.Clear();$snfTimes.Add(0)
  for($snfMs=33;$snfMs -le 330;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add(2500)
} elseif($Scene -in @('qb-stat','receiver-stat','player-context','player-feature','drive-stat','player-comparison','team-stats')) {
  for($snfMs=600;$snfMs -le 1800;$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfExit = if($Scene -eq 'player-feature'){7100}elseif($Scene -eq 'drive-stat'){6600}elseif($Scene -in @('player-comparison','team-stats')){7600}else{6100}
  $snfTimes.Add($snfExit-1)
  for($snfMs=$snfExit;$snfMs -le ($snfExit+900);$snfMs+=33){$snfTimes.Add($snfMs)}
  $snfTimes.Add($snfExit+1000)
} else {
  throw 'Use the interactive preview for this scene; this script records player inserts, pregame inserts, intro, watermark, timeout, replay and scoring sequences.'
}
if($Scene -eq 'career-fact') {
  foreach($snfMs in @(600,633,900,1200,4950,4983,5125,5300)){$snfTimes.Add($snfMs)}
} elseif($Scene -eq 'coin-toss') {
  foreach($snfMs in @(600,633,900,1200,6200,6233,6375,6550,6700)){$snfTimes.Add($snfMs)}
}
$snfTimes = @($snfTimes | Sort-Object -Unique)
$snfConcat = [System.Collections.Generic.List[string]]::new()
$snfFrameIndex = [System.Collections.Generic.List[string]]::new()
$snfFrameIndex.Add('file,fixture_ms')
for($snfI=0;$snfI -lt $snfTimes.Count;$snfI++){
  $snfT=$snfTimes[$snfI];$snfPng=Join-Path $snfFrames ('frame-{0:D3}.png' -f $snfI)
  $snfFrameIndex.Add(('frame-{0:D3}.png,{1}' -f $snfI,$snfT))
  # Explicit full motion belongs only to this animation review. The live
  # package continues to honor the operating system's motion preference.
  & $snfExe "https://presentation.local/NBC_SNF/scorebug.html?preview=$Scene&frame=$snfT&motion=full" $snfPng 1920 1080 700 (Join-Path $snfRoot 'mods\presentations')
  if($LASTEXITCODE -ne 0){throw "Renderer failed at frame $snfT"}
  if(($snfI % 10) -eq 0){Write-Output "$Scene frame $($snfI + 1)/$($snfTimes.Count) at ${snfT}ms"}
  $snfConcat.Add("file '$($snfPng.Replace('\','/'))'")
  # PNG demuxing otherwise rounds every 33ms sample to a 25fps timebase.
  # A millisecond source timebase preserves fixture timing before fps=30.
  $snfConcat.Add('option framerate 1000')
  $snfLength = if($snfI -lt $snfTimes.Count-1){($snfTimes[$snfI+1]-$snfT)/1000.0}else{0.7}
  $snfConcat.Add('duration '+$snfLength.ToString('0.000',[Globalization.CultureInfo]::InvariantCulture))
}
$snfConcat.Add("file '$($snfPng.Replace('\','/'))'")
$snfConcat.Add('option framerate 1000')
$snfList=Join-Path $snfFrames 'frames.txt'
[IO.File]::WriteAllLines($snfList,$snfConcat,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines((Join-Path $snfFrames 'frame-times.csv'),$snfFrameIndex,[Text.UTF8Encoding]::new($false))
$snfFilter = 'fps=30,scale=1280:720,format=yuv420p'
if($Label) {
  # Label only the exported review, not the live package or fixture.
  $snfLabelFile = Join-Path $snfFrames 'preview-label.txt'
  [IO.File]::WriteAllText($snfLabelFile,$Label,[Text.UTF8Encoding]::new($false))
  $snfLabelFilterPath = $snfLabelFile.Replace('\','/').Replace(':','\:').Replace("'","'\''")
  $snfFont = (Join-Path $env:WINDIR 'Fonts\arial.ttf').Replace('\','/').Replace(':','\:')
  $snfFilter += ",drawtext=fontfile='$snfFont':textfile='$snfLabelFilterPath':fontsize=18:fontcolor=white:box=1:boxcolor=black@0.66:boxborderw=8:x=24:y=24"
}
$snfTotalSeconds = (($snfTimes[$snfTimes.Count-1] - $snfTimes[0]) / 1000.0 + 0.7).ToString('0.000',[Globalization.CultureInfo]::InvariantCulture)
& ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i $snfList -vf $snfFilter -t $snfTotalSeconds -c:v libx264 -preset medium -crf 18 -movflags +faststart $snfOutput
if($LASTEXITCODE -ne 0){throw 'Video encoding failed'}
Write-Output "Rendered continuous motion: $snfOutput"
