<#
.SYNOPSIS
    Lay out, pack and sign the UWP package from build-uwp (run build-uwp.ps1 first).

.DESCRIPTION
    Layout:   build-uwp\layout (exe, AppxManifest.xml, Assets, dxcompiler/dxil)
    Package:  dist\uwp\NFL2K5_<version>_x64.msix

    -Sign signs with a self-signed code-signing certificate kept OUTSIDE the
    repo (default E:\2K5Remade-local\uwp-cert\NFL2K5Dev.pfx). -NewCert creates
    it: the certificate is generated in CurrentUser\My, exported to the .pfx
    (+ .cer), then removed from the store again. Nothing is installed or
    trusted system-wide. Its subject must equal the package Publisher
    (build-uwp.ps1 -Publisher, default 'CN=NFL2K5 Dev').

    Xbox Dev Mode: upload the .msix in Device Portal (Add), plus the
    Microsoft.VCLibs.x64.14.00 framework .appx as a dependency.
    Windows: see docs/UWP-PORT.md (Developer Mode + Add-AppxPackage -Register,
    or trust the .cer).
#>
[CmdletBinding()]
param(
    [switch]$Sign,
    [switch]$NewCert,
    [string]$CertPath = 'E:\2K5Remade-local\uwp-cert\NFL2K5Dev.pfx',
    [string]$CertPassword = 'nfl2k5-dev',
    [switch]$NoDxc
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\uwp-env.ps1"
$build = "$Root\build-uwp"
$layout = "$build\layout"
if (-not (Test-Path "$build\NFL2K5.exe")) { throw 'build-uwp\NFL2K5.exe missing: run tools\uwp\build-uwp.ps1' }

if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
New-Item -ItemType Directory -Force "$layout\Assets" | Out-Null
Copy-Item "$build\NFL2K5.exe", "$build\AppxManifest.xml" $layout
if (-not $NoDxc) {
    $dxcBin = 'E:\2K5Remade-local\dxc\bin\x64'
    foreach ($d in 'dxcompiler.dll', 'dxil.dll') {
        if (Test-Path "$dxcBin\$d") { Copy-Item "$dxcBin\$d" $layout }
        elseif (Test-Path "$Root\build\Release\$d") { Copy-Item "$Root\build\Release\$d" $layout }
    }
}

# Logos from src\nfl2k5.ico, centred on the tile colour.
Add-Type -AssemblyName System.Drawing
# .NET Framework cannot decode the PNG-compressed large frames; take the
# largest classic frame that loads.
$src = $null
foreach ($sz in 128, 64, 48, 32) {
    try { $src = (New-Object System.Drawing.Icon("$Root\src\nfl2k5.ico", $sz, $sz)).ToBitmap(); break } catch { }
}
if (-not $src) { throw 'could not read src\nfl2k5.ico' }
$assets = @{ 'Square44x44Logo.png' = @(44, 44); 'Square150x150Logo.png' = @(150, 150); 'StoreLogo.png' = @(50, 50);
             'Wide310x150Logo.png' = @(310, 150); 'SplashScreen.png' = @(620, 300) }
foreach ($name in $assets.Keys) {
    $w, $h = $assets[$name]
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.Clear([System.Drawing.Color]::FromArgb(255, 13, 18, 32))
    $s = [Math]::Min($w, $h) * $(if ($name -like 'Square44*' -or $name -like 'Store*') { 1.0 } else { 0.8 })
    $g.DrawImage($src, [int](($w - $s) / 2), [int](($h - $s) / 2), [int]$s, [int]$s)
    $g.Dispose()
    $bmp.Save("$layout\Assets\$name", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

[xml]$manifest = Get-Content "$layout\AppxManifest.xml"
$version = $manifest.Package.Identity.Version
$publisher = $manifest.Package.Identity.Publisher
New-Item -ItemType Directory -Force "$Root\dist\uwp" | Out-Null
$msix = "$Root\dist\uwp\NFL2K5_${version}_x64.msix"
& "$UwpSdkBin\makeappx.exe" pack /o /d $layout /p $msix
if ($LASTEXITCODE) { throw 'makeappx failed' }

if ($NewCert) {
    $dir = Split-Path $CertPath
    New-Item -ItemType Directory -Force $dir | Out-Null
    $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature `
        -FriendlyName 'NFL2K5 UWP test signing' -CertStoreLocation 'Cert:\CurrentUser\My' `
        -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}') -NotAfter (Get-Date).AddYears(3)
    $pw = ConvertTo-SecureString $CertPassword -Force -AsPlainText
    Export-PfxCertificate -Cert $cert -FilePath $CertPath -Password $pw | Out-Null
    Export-Certificate -Cert $cert -FilePath ([IO.Path]::ChangeExtension($CertPath, '.cer')) | Out-Null
    Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)"
    Write-Host "Certificate: $CertPath (subject $publisher)"
}
if ($Sign) {
    if (-not (Test-Path $CertPath)) { throw "No certificate at $CertPath (use -NewCert)" }
    & "$UwpSdkBin\signtool.exe" sign /fd SHA256 /f $CertPath /p $CertPassword $msix
    if ($LASTEXITCODE) { throw 'signtool failed' }
}
Write-Host "Package: $msix"
