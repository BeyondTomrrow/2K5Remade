param([string]$Scene='showcase',[string]$Out='outputs\cbs-2026\CBS-2026-preview.mp4')
$cbsRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$cbsRenderer=Join-Path $cbsRoot 'tools\presentation-render\presentation-render.exe'
if(!(Test-Path -LiteralPath $cbsRenderer)){throw 'Existing standalone renderer is missing. This script does not build or launch the game.'}
$cbsOutput=if([IO.Path]::IsPathRooted($Out)){$Out}else{Join-Path $cbsRoot $Out}
$cbsFrames=Join-Path (Split-Path $cbsOutput -Parent) ($Scene+'-frames')
New-Item -ItemType Directory -Force -Path $cbsFrames | Out-Null
$env:TEMP=Join-Path $cbsFrames ('.profile-'+[Guid]::NewGuid().ToString('N'));$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$cbsTimes=[System.Collections.Generic.List[int]]::new()
for($cbsT=0;$cbsT -le 2000;$cbsT+=50){$cbsTimes.Add($cbsT)}
if($Scene -eq 'stats-extended-reel'){
  # Small inserts: measured 440 ms entrance, six-second hold, 930 ms exit.
  foreach($cbsStart in @(2500,10000,17500,25000,32500)){
    $cbsTimes.Add($cbsStart-1)
    for($cbsT=$cbsStart;$cbsT -le $cbsStart+450;$cbsT+=33){$cbsTimes.Add($cbsT)}
    $cbsTimes.Add($cbsStart+1800);$cbsTimes.Add($cbsStart+6439)
    for($cbsT=$cbsStart+6440;$cbsT -le $cbsStart+7390;$cbsT+=33){$cbsTimes.Add($cbsT)}
  }
  # Team table's source entrance is 1230 ms including the preceding Eyebar
  # handoff. Its source exit is a cut. Capture both endpoints explicitly.
  foreach($cbsStart in @(40000,46000,52000)){
    $cbsTimes.Add($cbsStart-1)
    for($cbsT=$cbsStart;$cbsT -le $cbsStart+1260;$cbsT+=33){$cbsTimes.Add($cbsT)}
    foreach($cbsOffset in @(2800,5499,5500,5501)){$cbsTimes.Add($cbsStart+$cbsOffset)}
  }
  foreach($cbsStart in @(58000,65500)){
    $cbsTimes.Add($cbsStart-1)
    for($cbsT=$cbsStart;$cbsT -le $cbsStart+450;$cbsT+=33){$cbsTimes.Add($cbsT)}
    $cbsTimes.Add($cbsStart+1800);$cbsTimes.Add($cbsStart+6439)
    for($cbsT=$cbsStart+6440;$cbsT -le $cbsStart+7390;$cbsT+=33){$cbsTimes.Add($cbsT)}
  }
  foreach($cbsT in @(73499,73500,75000)){$cbsTimes.Add($cbsT)}
}elseif($Scene -eq 'stats-reel'){
  foreach($cbsStart in @(2500,10000,17500,25000,32500,40000)){
    $cbsTimes.Add($cbsStart-1)
    for($cbsT=$cbsStart;$cbsT -le $cbsStart+450;$cbsT+=33){$cbsTimes.Add($cbsT)}
    $cbsTimes.Add($cbsStart+1800);$cbsTimes.Add($cbsStart+6439)
    for($cbsT=$cbsStart+6440;$cbsT -le $cbsStart+7390;$cbsT+=33){$cbsTimes.Add($cbsT)}
  }
  $cbsTimes.Add(48499)
  for($cbsT=48500;$cbsT -le 50000;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(53500,58500,63249)){$cbsTimes.Add($cbsT)}
  for($cbsT=63250;$cbsT -le 64100;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(65499,65500,67500)){$cbsTimes.Add($cbsT)}
}elseif($Scene -eq 'motion-reel'){
  foreach($cbsT in @(3999)){$cbsTimes.Add($cbsT)}
  for($cbsT=4000;$cbsT -le 4400;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(6400,6500,7999,8000,8500,8550,9000,9400,10000,10999)){$cbsTimes.Add($cbsT)}
  for($cbsT=11000;$cbsT -le 13100;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(14500,15400)){$cbsTimes.Add($cbsT)}
  for($cbsT=15500;$cbsT -le 17600;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(18499,18500,18800,19999)){$cbsTimes.Add($cbsT)}
  for($cbsT=20000;$cbsT -le 22000;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(22999,23000,23300,26499)){$cbsTimes.Add($cbsT)}
  for($cbsT=26500;$cbsT -le 28500;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(29999,30499)){$cbsTimes.Add($cbsT)}
  for($cbsT=30500;$cbsT -le 33500;$cbsT+=33){$cbsTimes.Add($cbsT)}
  for($cbsT=33500;$cbsT -le 36500;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(36999,37000,39500)){$cbsTimes.Add($cbsT)}
}elseif($Scene -eq 'showcase'){
  foreach($cbsT in @(2399,2400,2800,4999,5000,5400,7799,7800,8100,10199)){$cbsTimes.Add($cbsT)}
  for($cbsT=10200;$cbsT -le 10500;$cbsT+=50){$cbsTimes.Add($cbsT)}
  $cbsTimes.Add(11699)
  for($cbsT=11700;$cbsT -le 13700;$cbsT+=50){$cbsTimes.Add($cbsT)}
  $cbsTimes.Add(14499)
  for($cbsT=14500;$cbsT -le 15900;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(16300,16600,18000,18950)){$cbsTimes.Add($cbsT)}
  for($cbsT=19000;$cbsT -le 21150;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(23500,24499,24500,24900,25500)){$cbsTimes.Add($cbsT)}
}elseif($Scene -eq 'period-sequence'){
  foreach($cbsT in @(2499,2500,4499,4500,5000,6500,7999,8000,8500,10000)){$cbsTimes.Add($cbsT)}
}elseif($Scene -like 'stats-team-*'){
  $cbsTimes.Add(2499)
  for($cbsT=2500;$cbsT -le 3760;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(5300,7999,8000,8001,8800)){$cbsTimes.Add($cbsT)}
}elseif($Scene -in @('stats-tight-end','stats-drive','stats-drive-native')){
  $cbsTimes.Add(2499)
  for($cbsT=2500;$cbsT -le 2950;$cbsT+=33){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(4300,8939)){$cbsTimes.Add($cbsT)}
  for($cbsT=8940;$cbsT -le 9890;$cbsT+=33){$cbsTimes.Add($cbsT)}
  $cbsTimes.Add(10500)
}elseif($Scene -like 'touchdown*'){
  $cbsTimes.Add(2499)
  for($cbsT=2500;$cbsT -le 4550;$cbsT+=50){$cbsTimes.Add($cbsT)}
  foreach($cbsT in @(5000,6500,6950)){$cbsTimes.Add($cbsT)}
  for($cbsT=7000;$cbsT -le 9750;$cbsT+=50){$cbsTimes.Add($cbsT)}
}else{$cbsTimes.Add(2500);$cbsTimes.Add(3500)}
$cbsBatch=[System.Collections.Generic.List[string]]::new();$cbsConcat=[System.Collections.Generic.List[string]]::new()
for($cbsI=0;$cbsI -lt $cbsTimes.Count;$cbsI++){
  $cbsFrame=Join-Path $cbsFrames ('frame-{0:D3}.png' -f $cbsI)
  $cbsBatch.Add("$($cbsTimes[$cbsI])`t$cbsFrame")
  $cbsConcat.Add("file '$($cbsFrame.Replace('\','/'))'");$cbsConcat.Add('option framerate 1000')
  $cbsDuration=if($cbsI+1 -lt $cbsTimes.Count){($cbsTimes[$cbsI+1]-$cbsTimes[$cbsI])/1000.0}else{.8}
  $cbsConcat.Add('duration '+$cbsDuration.ToString('0.000',[Globalization.CultureInfo]::InvariantCulture))
}
$cbsManifest=Join-Path $cbsFrames 'frames.tsv';[IO.File]::WriteAllLines($cbsManifest,$cbsBatch,[Text.UTF8Encoding]::new($false))
& $cbsRenderer "https://presentation.local/CBS_2026/scorebug.html?preview=$Scene&frame=0&motion=full" $cbsFrame 1920 1080 700 (Join-Path $cbsRoot 'mods\presentations') --batch $cbsManifest
if($LASTEXITCODE -ne 0){throw 'Standalone presentation capture failed'}
$cbsConcat.Add("file '$($cbsFrame.Replace('\','/'))'");$cbsConcat.Add('option framerate 1000')
$cbsList=Join-Path $cbsFrames 'concat.txt';[IO.File]::WriteAllLines($cbsList,$cbsConcat,[Text.UTF8Encoding]::new($false))
& ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i $cbsList -vf 'fps=30,scale=1280:720,format=yuv420p' -c:v libx264 -preset medium -crf 18 -movflags +faststart $cbsOutput
if($LASTEXITCODE -ne 0){throw 'Preview encoding failed'}
Write-Output "Standalone CBS preview: $cbsOutput"
