# Fetch the WebView2 SDK (headers + static loader) that the HTML presentation
# host builds against, into dependencies\webview2\sdk. The engine itself is the
# WebView2 runtime already present on Windows 10/11; players install nothing.
param([string]$Version = '1.0.3124.44')
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'dependencies\webview2'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$pkg = Join-Path $dir 'webview2.zip'
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$Version" -OutFile $pkg
Expand-Archive $pkg -DestinationPath (Join-Path $dir 'sdk') -Force
Remove-Item $pkg
"WebView2 SDK $Version -> $dir\sdk"
