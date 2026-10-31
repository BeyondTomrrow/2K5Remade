# Makes the NFL 2K5 folder for the Android version on the PC, so the phone
# never has to import (and briefly store twice) the 7 GB disc image.
#
#   tools\make-phone-folder.ps1 -Source "D:\ISOs\ESPN NFL 2K5 (USA).iso"
#   tools\make-phone-folder.ps1 -Source "C:\Games\ESPN NFL 2K5\original\disc"
#
# -Source is the disc image, an extracted disc folder (default.xbe plus
# vc_53450030), or an installed PC copy's folder. The result is
# <Out>\NFL2K5\original\... (about 5.9 GB): copy the NFL2K5 folder to the
# phone's Internal storage, replacing nothing else there, and start the app.
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Out = (Join-Path ([Environment]::GetFolderPath('Desktop')) 'NFL2K5 for phone')
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-Disc([string]$dir) {
    foreach ($d in @($dir, (Join-Path $dir 'original\disc'), (Join-Path $dir 'original'), (Join-Path $dir 'disc'))) {
        if ((Test-Path (Join-Path $d 'default.xbe')) -and (Test-Path (Join-Path $d 'vc_53450030'))) { return $d }
    }
    return $null
}

$dest = Join-Path $Out 'NFL2K5\original'
if (Test-Path $dest) { throw "$dest already exists: choose another -Out or move it away first." }
$work = $null

if (Test-Path $Source -PathType Leaf) {
    $xiso = @((Join-Path $here 'extract-xiso.exe'), (Join-Path $here '..\tools\extract-xiso.exe')) |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $xiso) { throw 'extract-xiso.exe was not found next to this script.' }
    $work = Join-Path $Out 'extracting'
    New-Item -ItemType Directory -Force $work | Out-Null
    Write-Host "Extracting $Source ..."
    & $xiso -x -d $work $Source
    if ($LASTEXITCODE -ne 0) { throw 'extract-xiso failed: is this the Xbox disc image?' }
    $disc = Find-Disc $work
} else {
    $disc = Find-Disc $Source
}
if (-not $disc) { throw "No default.xbe and vc_53450030 found in $Source." }

$need = (Get-ChildItem (Join-Path $disc 'vc_53450030') -File | Measure-Object Length -Sum).Sum + 64MB
New-Item -ItemType Directory -Force $Out | Out-Null
$free = (Get-PSDrive -Name (Split-Path -Qualifier (Resolve-Path $Out)).TrimEnd(':')).Free
if ($free -lt $need) {
    throw ("{0:N1} GB free on that drive, the phone folder needs {1:N1} GB: pick another -Out drive." -f ($free / 1GB), ($need / 1GB))
}

# Only what the game reads from the disc: the executable and its data
# archives. The dashboard update files on the disc are left out.
New-Item -ItemType Directory -Force (Join-Path $dest 'vc_53450030') | Out-Null
Copy-Item (Join-Path $disc 'default.xbe') $dest
Get-ChildItem (Join-Path $disc 'vc_53450030') -File | ForEach-Object {
    Write-Host ("  {0} ({1:N0} MB)" -f $_.Name, ($_.Length / 1MB))
    Copy-Item $_.FullName (Join-Path $dest 'vc_53450030')
}
if ($work) { Remove-Item $work -Recurse -Force }   # the temporary extraction made above

$size = (Get-ChildItem (Join-Path $Out 'NFL2K5') -Recurse -File | Measure-Object Length -Sum).Sum
@"
NFL 2K5 for Android: the game folder, made on a PC.

1. Plug the phone into the PC (USB, "File transfer").
2. Copy this NFL2K5 folder to the phone's Internal storage.
   If the phone already has an NFL2K5 folder, copy just the "original" folder into it.
3. Start NFL 2K5 on the phone. It finds the game at once; no disc image needed.
"@ | Set-Content -Encoding UTF8 (Join-Path $Out 'NFL2K5\READ ME - phone.txt')
Write-Host ("Done: {0} ({1:N2} GB). Copy the NFL2K5 folder to the phone's Internal storage." -f (Join-Path $Out 'NFL2K5'), ($size / 1GB))
