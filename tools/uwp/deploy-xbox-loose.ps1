<#
.SYNOPSIS
    Copy the loose-folder install (make-xbox-loose.ps1) to an Xbox in Dev Mode
    and register it, through the console's Device Portal.

.DESCRIPTION
    1. Lists installed apps. If an NFL2K5 package installed from the .msix is
       there, it asks before uninstalling it (uninstalling deletes that app's
       LocalState on the console: its saves and any partly uploaded game
       files). Nothing is uninstalled without a "y".
    2. Copies dist\xbox-loose\NFL2K5 to D:\DevelopmentFiles\LooseApps\NFL2K5:
         Smb  - the console's developer file share (credentials from
                /ext/smb/developerfolder), copied with robocopy, which skips
                files already there and resumes interrupted ones.
         Http - file by file through /api/app/packagemanager/upload, skipping
                files already on the console with the same size, 3 tries each.
         Auto - Smb, and Http if the share cannot be reached.
       Run it again after an interruption: finished files are skipped.
    3. Registers the folder (/api/app/packagemanager/register) and waits for
       the app to show up in the installed list.

    The Device Portal username/password are typed in when asked and are only
    kept in memory for this run. The console's self-signed certificate is
    accepted for the console's address only.

    Microsoft docs used (Xbox Device Portal REST API):
      https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/reference
      https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/wdp-loose-folder-register-api
      https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/wdp-folder-upload
      https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/wdp-smb-api
      https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core
        (packagemanager/packages, DELETE packagemanager/package, filesystem/apps/files)
      https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal
        (CSRF: the CSRF-Token cookie is sent back as the X-CSRF-Token header)

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\uwp\deploy-xbox-loose.ps1 -Xbox 192.168.1.50 -User me
#>
[CmdletBinding()]
param(
    [string]$Xbox,
    [string]$User,
    [string]$Folder,
    [string]$Name = 'NFL2K5',
    [ValidateSet('Auto', 'Smb', 'Http')] [string]$Method = 'Auto',
    [switch]$NoRegister,
    [int]$Port = 11443,
    [string]$Scheme = 'https',      # 'http' only for the offline mock test
    [string]$PasswordForTest        # mock test only; normally prompted
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Folder) { $Folder = "$Root\dist\xbox-loose\$Name" }
function Say($m, $c = 'Gray') { Write-Host $m -ForegroundColor $c }
function Fail($m) { Say "ERROR: $m" Red; exit 1 }

if (-not (Test-Path "$Folder\AppxManifest.xml")) { Fail "$Folder\AppxManifest.xml missing: run make-xbox-loose first." }
if (-not (Test-Path "$Folder\original\default.xbe")) { Fail "$Folder has no original\default.xbe: run make-xbox-loose first." }
if (-not $Xbox) {
    Say "The console's address is in Dev Home on the Xbox (Remote Access / Device Portal, e.g. https://192.168.1.50:11443)."
    $Xbox = Read-Host "Xbox IP address"
}
$Xbox = ($Xbox -replace '^https?://', '' -replace '[:/].*$', '').Trim()
if (-not $Xbox) { Fail "no address given" }
if (-not $User) { $User = Read-Host "Device Portal username (blank if you did not set one)" }
$pass = ''
if ($User) {
    if ($PasswordForTest) { $pass = $PasswordForTest }
    else {
        $sec = Read-Host "Device Portal password" -AsSecureString
        $pass = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($sec))
    }
}
$base = "${Scheme}://${Xbox}:$Port"

if (-not ('WdpCert' -as [type])) {
    Add-Type @"
using System; using System.Net; using System.Net.Security; using System.Security.Cryptography.X509Certificates;
public static class WdpCert {
    public static string Host;
    // Accept a certificate error only for the console's own address.
    static bool Check(object s, X509Certificate c, X509Chain ch, SslPolicyErrors e) {
        if (e == SslPolicyErrors.None) return true;
        HttpWebRequest r = s as HttpWebRequest;
        return r != null && Host != null && string.Equals(r.RequestUri.Host, Host, StringComparison.OrdinalIgnoreCase);
    }
    public static void Install(string host) {
        Host = host;
        ServicePointManager.ServerCertificateValidationCallback = Check;
        ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
        ServicePointManager.Expect100Continue = false;
    }
}
"@
}
[WdpCert]::Install($Xbox)
$cookies = New-Object System.Net.CookieContainer
$auth = if ($User) { 'Basic ' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("${User}:$pass")) } else { $null }

function B64([string]$s) { [Uri]::EscapeDataString([Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s))) }

function New-Req([string]$verb, [string]$pathq) {
    $r = [Net.HttpWebRequest]::Create("$base$pathq")
    $r.Method = $verb
    $r.CookieContainer = $cookies
    $r.Timeout = 120000
    $r.ReadWriteTimeout = 600000
    if ($auth) { $r.Headers['Authorization'] = $auth }
    $csrf = $cookies.GetCookies([Uri]$base) | Where-Object { $_.Name -eq 'CSRF-Token' } | Select-Object -First 1
    if ($csrf -and $verb -ne 'GET') { $r.Headers['X-CSRF-Token'] = $csrf.Value }
    return $r
}
function Read-Resp($r) {
    try { $resp = $r.GetResponse() }
    catch [Net.WebException] {
        $code = if ($_.Exception.Response) { [int]$_.Exception.Response.StatusCode } else { 0 }
        $body = ''
        if ($_.Exception.Response) { $body = (New-Object IO.StreamReader($_.Exception.Response.GetResponseStream())).ReadToEnd() }
        throw "HTTP $code $($r.Method) $($r.RequestUri.AbsolutePath): $($_.Exception.Message) $body"
    }
    $sr = New-Object IO.StreamReader($resp.GetResponseStream())
    $txt = $sr.ReadToEnd(); $resp.Close()
    return $txt
}
function Wdp([string]$verb, [string]$pathq) {
    $r = New-Req $verb $pathq
    if ($verb -ne 'GET') { $r.ContentLength = 0 }
    $t = Read-Resp $r
    if ($t) { try { return $t | ConvertFrom-Json } catch { return $t } }
}

# --- 0. reach the console ------------------------------------------------
Say "Connecting to Device Portal at $base ..."
try { $pk = Wdp GET '/api/app/packagemanager/packages' }
catch {
    if ("$_" -match 'HTTP 401') { Fail "the console refused the username/password (HTTP 401). Check them in Dev Home > Remote Access settings." }
    Fail "could not reach Device Portal at ${base}: $_`nIs the Xbox in Dev Mode with Dev Home open, and is this PC on the same network?"
}

# --- 1. an NFL2K5 installed from the .msix blocks the same name ----------
$mine = @($pk.InstalledPackages | Where-Object { $_.Name -eq 'NFL2K5.Recompiled' })
foreach ($p in $mine) {
    # PackageOrigin 4 = DeveloperUnsigned: an earlier loose registration of
    # this folder, which registering again simply updates.
    if ($p.PackageOrigin -eq 4) { Say "Found the earlier loose install ($($p.PackageFullName)); it will be updated in place."; continue }
    Say ""
    Say "NFL2K5 is already installed from the .msix: $($p.PackageFullName)" Yellow
    Say "It has to go before the loose install can use the same name." Yellow
    Say "Uninstalling deletes that app's data on the Xbox: its saves, settings and the partly uploaded game files." Yellow
    $a = Read-Host "Uninstall it now? (y/N)"
    if ($a -notmatch '^(y|yes)$') { Fail "left installed. Uninstall NFL2K5 in Dev Home (or say y next time), then run this again." }
    Wdp DELETE "/api/app/packagemanager/package?package=$([Uri]::EscapeDataString($p.PackageFullName))" | Out-Null
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 2
        $left = (Wdp GET '/api/app/packagemanager/packages').InstalledPackages | Where-Object { $_.PackageFullName -eq $p.PackageFullName }
        if (-not $left) { break }
    }
    if ($left) { Fail "the old package is still listed after 2 minutes; uninstall it in Dev Home, then run this again." }
    Say "Uninstalled." Green
}

# --- 2. copy the folder --------------------------------------------------
$files = Get-ChildItem -LiteralPath $Folder -Recurse -File -Force | Where-Object { $_.Name -ne '.made-by-make-xbox-loose' }
# The folder as the file listing spells it (a short 8.3 path given on the
# command line comes back long), so relative paths cut cleanly.
$folderFull = (Get-ChildItem -LiteralPath $Folder -Filter AppxManifest.xml -File).DirectoryName.TrimEnd('\')
$total = ($files | Measure-Object Length -Sum).Sum
Say ("Copying {0} files, {1:N2} GB, to DevelopmentFiles\LooseApps\{2}" -f $files.Count, ($total / 1GB), $Name)

function Copy-Smb {
    $smb = Wdp GET '/ext/smb/developerfolder'
    $share = $smb.Path
    if (-not $share) { throw "no share path from /ext/smb/developerfolder" }
    $mapped = $false
    if ($share -like '\\*') {
        $shareRoot = ($share -split '\\')[0..3] -join '\'
        cmd /c "net use `"$shareRoot`" /user:`"$($smb.Username)`" `"$($smb.Password)`" >nul 2>&1"
        if ($LASTEXITCODE -ne 0) { throw "could not open the console's file share $shareRoot" }
        $mapped = $true
    }
    try {
        $dest = Join-Path $share "LooseApps\$Name"
        Say "  file share: $dest"
        # /E all folders, /XJ never follow junctions, /Z restartable,
        # /R /W retries. Files already there unchanged are skipped.
        robocopy $Folder $dest /E /XJ /Z /R:5 /W:5 /XF .made-by-make-xbox-loose /NDL /NJH /ETA
        if ($LASTEXITCODE -ge 8) { throw "robocopy failed (exit $LASTEXITCODE); run this again to resume" }
    } finally {
        if ($mapped) { cmd /c "net use `"$shareRoot`" /delete /y >nul 2>&1" }
    }
}

function Remote-Sizes([string]$relDir) {
    $map = @{}
    $p = "LooseApps\$Name" + $(if ($relDir) { "\$relDir" } else { '' })
    try { $l = Wdp GET "/api/filesystem/apps/files?knownfolderid=DevelopmentFiles&path=$([Uri]::EscapeDataString($p))" } catch { return $map }
    foreach ($it in $l.Items) { if (-not $it.SubPath) { $map[$it.Name] = [int64]$it.FileSize } }
    return $map
}

function Send-File($f, [string]$relDir) {
    $dest = $Name + $(if ($relDir) { "\$relDir" } else { '' })
    $bound = '----nfl2k5' + [Guid]::NewGuid().ToString('N')
    $head = [Text.Encoding]::UTF8.GetBytes("--$bound`r`nContent-Disposition: form-data; name=`"$($f.Name)`"; filename=`"$($f.Name)`"`r`nContent-Type: application/octet-stream`r`n`r`n")
    $tail = [Text.Encoding]::UTF8.GetBytes("`r`n--$bound--`r`n")
    $r = New-Req POST "/api/app/packagemanager/upload?destinationFolder=$(B64 $dest)"
    $r.ContentType = "multipart/form-data; boundary=$bound"
    $r.AllowWriteStreamBuffering = $false
    $r.SendChunked = $false
    $r.ContentLength = $head.Length + $f.Length + $tail.Length
    $r.Timeout = [int]::MaxValue
    $s = $r.GetRequestStream()
    $in = [IO.File]::OpenRead($f.FullName)
    try {
        $s.Write($head, 0, $head.Length)
        $buf = New-Object byte[] (1MB)
        $done = 0L
        while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
            $s.Write($buf, 0, $n); $done += $n
            if (($done % (16MB)) -lt $n) {
                $pct = [int](100 * ($script:sent + $done) / [Math]::Max(1, $total))
                Write-Progress -Activity "Uploading to the Xbox" -Status ("{0}  {1:N0}/{2:N0} MB" -f $f.Name, ($done / 1MB), ($f.Length / 1MB)) -PercentComplete $pct
            }
        }
        $s.Write($tail, 0, $tail.Length)
    } finally { $in.Close(); $s.Close() }
    Read-Resp $r | Out-Null
}

function Copy-Http {
    $script:sent = 0L
    $byDir = $files | Group-Object { $_.DirectoryName.Substring($folderFull.Length).TrimStart('\') }
    foreach ($g in $byDir) {
        $rel = $g.Name
        $have = Remote-Sizes $rel
        foreach ($f in $g.Group) {
            $label = if ($rel) { "$rel\$($f.Name)" } else { $f.Name }
            if ($have.ContainsKey($f.Name) -and $have[$f.Name] -eq $f.Length) {
                Say "  already there: $label"; $script:sent += $f.Length; continue
            }
            $ok = $false
            for ($try = 1; $try -le 3 -and -not $ok; $try++) {
                try { Send-File $f $rel; $ok = $true }
                catch { Say "  try $try failed for ${label}: $_" Yellow; Start-Sleep 3 }
            }
            if (-not $ok) { throw "could not upload $label after 3 tries; run this again to resume" }
            # Check the console now has the whole file.
            $now = Remote-Sizes $rel
            if ($now.Count -and (-not $now.ContainsKey($f.Name) -or $now[$f.Name] -ne $f.Length)) {
                throw "$label arrived with the wrong size; run this again to resume"
            }
            $script:sent += $f.Length
            Say ("  sent {0} ({1:N0} MB)" -f $label, ($f.Length / 1MB))
        }
    }
    Write-Progress -Activity "Uploading to the Xbox" -Completed
}

$copied = $false
if ($Method -in 'Auto', 'Smb') {
    try { Copy-Smb; $copied = $true }
    catch {
        if ($Method -eq 'Smb') { Fail "$_" }
        Say "File share not available ($_); uploading through Device Portal instead." Yellow
    }
}
if (-not $copied) {
    try { Copy-Http } catch { Fail "$_" }
}
Say "Copy finished." Green

# --- 3. register ---------------------------------------------------------
if ($NoRegister) { Say "Skipped registering (-NoRegister)."; exit 0 }
Say "Registering LooseApps\$Name ..."
try { Wdp POST "/api/app/packagemanager/register?folder=$(B64 $Name)" | Out-Null }
catch { Fail "registering failed: $_`nIn Device Portal: Apps > Add > 'Register' with the folder LooseApps\$Name is the manual way." }
$found = $null
for ($i = 0; $i -lt 60 -and -not $found; $i++) {
    Start-Sleep 2
    $found = (Wdp GET '/api/app/packagemanager/packages').InstalledPackages | Where-Object { $_.Name -eq 'NFL2K5.Recompiled' } | Select-Object -First 1
}
if (-not $found) { Fail "the app did not show up within 2 minutes. Check Device Portal > Apps for an error." }
Say ""
Say "Installed: $($found.PackageFullName)" Green
Say "Last step on the Xbox: Dev Home > select NFL2K5 > View details > App type: Game." Cyan
Say "(Without it the console limits the app to 1 GB of memory and it will crash.)" Cyan
