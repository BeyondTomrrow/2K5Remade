<#
  Undoes install-nfl2k5-uwp.ps1, using install-state.json to touch only what
  the installer changed. Run elevated, by "Uninstall NFL2K5 UWP.cmd".
#>
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Transcript -Path (Join-Path $here 'uninstall-log.txt') -Force | Out-Null
$statePath = Join-Path $here 'install-state.json'
$state = if (Test-Path $statePath) { Get-Content $statePath -Raw | ConvertFrom-Json } else { $null }
try {
    # The package (its LocalState, saves included, goes with it).
    Get-Process NFL2K5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*\WindowsApps\NFL2K5.Recompiled_*' } | Stop-Process -Force
    $pkgs = Get-AppxPackage NFL2K5.Recompiled
    foreach ($p in $pkgs) { Remove-AppxPackage -Package $p.PackageFullName; Write-Host "Removed $($p.PackageFullName)" }
    if (-not $pkgs) { Write-Host "Package not installed" }

    # Certificate: only if the installer added it.
    $thumb = if ($state) { $state.certThumbprint } else { (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $here 'NFL2K5Dev.cer'))).Thumbprint }
    $added = if ($state) { [bool]$state.certAdded } else { $true }
    if ($thumb -and $added -and (Test-Path "Cert:\LocalMachine\TrustedPeople\$thumb")) {
        Remove-Item "Cert:\LocalMachine\TrustedPeople\$thumb"
        Write-Host "Removed the test certificate ($thumb) from Local Machine > Trusted People"
    } elseif ($thumb -and -not $added) {
        Write-Host "Certificate was already trusted before the install; left as it was"
    }

    # ACL entries: only on the folders the installer granted.
    $folders = if ($state) { @($state.aclFolders) } else { @() }
    foreach ($dir in $folders) {
        if (-not $dir -or -not (Test-Path $dir)) { continue }
        & icacls $dir /remove:g '*S-1-15-2-1' /Q | Out-Null
        Write-Host "Removed the ALL APPLICATION PACKAGES grant on $dir"
    }
    if (-not $state) { Write-Host "No install-state.json: folder permissions were not changed by this script" }
    if (Test-Path $statePath) { Remove-Item $statePath }
    Write-Host "Uninstall complete."
} finally {
    Stop-Transcript | Out-Null
}
Start-Sleep 3
