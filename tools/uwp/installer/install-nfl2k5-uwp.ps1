<#
  Installs the NFL2K5 UWP test build on this PC. Run elevated, by
  "Install NFL2K5 UWP.cmd" (which says what it changes first).
  Records what it changed in install-state.json so the uninstaller undoes
  exactly that and nothing else.
#>
param([string]$GameRoot = 'E:\2K5Remade')
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Start-Transcript -Path (Join-Path $here 'install-log.txt') -Force | Out-Null
$statePath = Join-Path $here 'install-state.json'
$state = [ordered]@{ installed = (Get-Date).ToString('s'); certThumbprint = $null; certAdded = $false;
                     package = $null; aclFolders = @(); gameRoot = $GameRoot }
try {
    $cer = Join-Path $here 'NFL2K5Dev.cer'
    $msix = Get-ChildItem $here -Filter 'NFL2K5_*_x64.msix' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not (Test-Path $cer)) { throw "NFL2K5Dev.cer not found next to this script" }
    if (-not $msix) { throw "No NFL2K5_*_x64.msix next to this script" }

    # 1. Trust the test certificate (Local Machine > Trusted People).
    $c = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($cer)
    $state.certThumbprint = $c.Thumbprint
    if (Test-Path "Cert:\LocalMachine\TrustedPeople\$($c.Thumbprint)") {
        Write-Host "Certificate already trusted ($($c.Thumbprint))"
    } else {
        Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
        $state.certAdded = $true
        Write-Host "Trusted test certificate $($c.Subject) ($($c.Thumbprint))"
    }

    # 2. VCLibs runtime (vcruntime140_app.dll) and the package.
    if (-not (Get-AppxPackage Microsoft.VCLibs.140.00 | Where-Object { $_.Architecture -eq 'X64' -and $_.Name -eq 'Microsoft.VCLibs.140.00' })) {
        $vc = Get-ChildItem $here -Filter 'Microsoft.VCLibs.x64.14.00*.appx' | Select-Object -First 1
        if (-not $vc) { throw "The Microsoft.VCLibs.140.00 (x64) runtime is not installed and Microsoft.VCLibs.x64.14.00.appx is not in this folder" }
        Add-AppxPackage -Path $vc.FullName
        Write-Host "Installed $($vc.Name)"
    } else {
        Write-Host "Microsoft.VCLibs.140.00 (x64) already installed"
    }
    Add-AppxPackage -Path $msix.FullName -ForceApplicationShutdown
    $pkg = Get-AppxPackage NFL2K5.Recompiled | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $pkg) { throw "The package did not register" }
    $state.package = $pkg.PackageFullName
    Write-Host "Installed $($pkg.PackageFullName)"

    # 3. Read access for UWP apps on the game folders (no copies).
    foreach ($name in 'original', 'mods', 'Music', 'assets', 'data', 'font', 'moment-audio') {
        $dir = Join-Path $GameRoot $name
        if (-not (Test-Path $dir -PathType Container)) { continue }
        $had = (Get-Acl $dir).Access | Where-Object { $_.IdentityReference.Value -match 'ALL APPLICATION PACKAGES' -and $_.FileSystemRights -match 'Read' }
        if ($had) { Write-Host "$dir already readable by UWP apps"; continue }
        & icacls $dir /grant '*S-1-15-2-1:(OI)(CI)RX' /Q | Out-Null
        if ($LASTEXITCODE) { throw "icacls failed on $dir" }
        $state.aclFolders += $dir
        Write-Host "Granted ALL APPLICATION PACKAGES read/execute on $dir"
    }
    # The root itself (this folder and the files directly in it, not its other
    # subfolders): the game makes it the current directory at boot.
    if (-not ((Get-Acl $GameRoot).Access | Where-Object { $_.IdentityReference.Value -match 'ALL APPLICATION PACKAGES' -and $_.FileSystemRights -match 'Read' })) {
        & icacls $GameRoot /grant '*S-1-15-2-1:(OI)RX' /Q | Out-Null
        if ($LASTEXITCODE) { throw "icacls failed on $GameRoot" }
        $state.aclFolders += $GameRoot
        Write-Host "Granted ALL APPLICATION PACKAGES read/execute on $GameRoot (folder and its files only)"
    }
    $state | ConvertTo-Json | Set-Content -Path $statePath -Encoding ascii

    # 4. root.txt in the app's LocalState. The folder appears when the app
    #    first runs, so start it once if it is not there yet.
    $local = Join-Path $env:LOCALAPPDATA "Packages\$($pkg.PackageFamilyName)\LocalState"
    $aumid = "shell:AppsFolder\$($pkg.PackageFamilyName)!App"
    if (-not (Test-Path $local)) {
        Start-Process explorer.exe $aumid
        for ($i = 0; $i -lt 40 -and -not (Test-Path $local); $i++) { Start-Sleep -Milliseconds 500 }
        Get-Process NFL2K5 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*$($pkg.PackageFamilyName.Split('_')[0])*" } | Stop-Process -Force
        Start-Sleep 1
    }
    if (-not (Test-Path $local)) { throw "LocalState not found at $local" }
    # That short first run was stopped on purpose: it must not count as a
    # failed XAML start (which would switch the app to CoreWindow mode).
    Remove-Item (Join-Path $local 'xaml-starting.marker') -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText((Join-Path $local 'root.txt'), $GameRoot + "`r`n", (New-Object System.Text.ASCIIEncoding))
    Write-Host "Wrote $local\root.txt -> $GameRoot"

    # 5. Start it.
    Start-Process explorer.exe $aumid
    Write-Host "Started NFL2K5 (UWP)."
    Write-Host "Log files: $local\nfl2k5.log and $local\NFL2K5-PC\logs"
} catch {
    Write-Host "INSTALL FAILED: $($_.Exception.Message)" -ForegroundColor Red
    $state | ConvertTo-Json | Set-Content -Path $statePath -Encoding ascii
} finally {
    Stop-Transcript | Out-Null
}
Start-Sleep 3
