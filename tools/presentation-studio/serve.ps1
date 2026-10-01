# Presentation Studio: a local editor for HTML presentation packages.
# Serves the project folder on http://127.0.0.1:<port>/ (this computer only)
# so the editor can open a package page in an iframe with full access to its
# elements, and saves layout/asset changes back into the package.
#
#   powershell -ExecutionPolicy Bypass -File tools\presentation-studio\serve.ps1
#
# Writes stay inside mods\presentations\<package>\ (an HTML package: a folder
# with mod.json "type": "html"), only for web file types, never outside it.
# New packages are created by importing HTML.
param([int]$Port = 8735, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$presentations = Join-Path $root 'mods\presentations'
$mime = @{ '.html' = 'text/html; charset=utf-8'; '.css' = 'text/css; charset=utf-8'; '.js' = 'text/javascript; charset=utf-8'
           '.json' = 'application/json; charset=utf-8'; '.png' = 'image/png'; '.jpg' = 'image/jpeg'; '.jpeg' = 'image/jpeg'
           '.gif' = 'image/gif'; '.svg' = 'image/svg+xml'; '.webp' = 'image/webp'; '.ttf' = 'font/ttf'; '.otf' = 'font/otf'
           '.woff' = 'font/woff'; '.woff2' = 'font/woff2'; '.mp4' = 'video/mp4'; '.webm' = 'video/webm'; '.mp3' = 'audio/mpeg' }

function Send($ctx, [int]$code, [byte[]]$body, [string]$type) {
    $ctx.Response.StatusCode = $code
    $ctx.Response.ContentType = $type
    $ctx.Response.Headers['Cache-Control'] = 'no-store'
    $ctx.Response.ContentLength64 = $body.Length
    $ctx.Response.OutputStream.Write($body, 0, $body.Length)
    $ctx.Response.Close()
}
function SendText($ctx, [int]$code, [string]$text, [string]$type = 'text/plain; charset=utf-8') {
    Send $ctx $code ([Text.Encoding]::UTF8.GetBytes($text)) $type
}
# A package folder name, checked to be a direct child of mods\presentations.
function PackageDir([string]$name) {
    if (-not $name -or $name -match '[\\/:*?"<>|]' -or $name -match '^\.') { return $null }
    $d = Join-Path $presentations $name
    if ((Test-Path -LiteralPath (Join-Path $d 'mod.json'))) { return $d }
    return $null
}
# A file path inside a package: relative, no '..', web file type only.
function PackageFile([string]$dir, [string]$rel) {
    if (-not $dir -or -not $rel) { return $null }
    $rel = $rel.Replace('/', '\').TrimStart('\')
    if ($rel -match '(^|\\)\.\.(\\|$)' -or $rel -match ':') { return $null }
    if (-not $mime.ContainsKey([IO.Path]::GetExtension($rel).ToLower())) { return $null }
    $full = [IO.Path]::GetFullPath((Join-Path $dir $rel))
    if (-not $full.StartsWith($dir + '\', [StringComparison]::OrdinalIgnoreCase)) { return $null }
    return $full
}
function ReadBody($ctx) {
    $ms = New-Object IO.MemoryStream
    $ctx.Request.InputStream.CopyTo($ms)
    return $ms.ToArray()
}

$page = 'tools/presentation-studio/index.html'
# Already running (another window)? Just open it.
try {
    $null = Invoke-WebRequest "http://127.0.0.1:$Port/api/packages" -UseBasicParsing -TimeoutSec 2
    $url = "http://127.0.0.1:$Port/$page"
    "Presentation Studio is already running: $url"
    if (-not $NoBrowser) { Start-Process $url }
    return
} catch { }
# Otherwise take the first free port from $Port up.
$listener = $null
for ($p = $Port; $p -lt $Port + 20; $p++) {
    $l = New-Object Net.HttpListener
    $l.Prefixes.Add("http://127.0.0.1:$p/")
    try { $l.Start(); $listener = $l; $Port = $p; break } catch { $l.Close() }
}
if (-not $listener) { throw "No free port between $Port and $($Port + 19)." }
$url = "http://127.0.0.1:$Port/$page"
"Presentation Studio: $url  (Ctrl+C to stop)"
if (-not $NoBrowser) { Start-Process $url }

while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    try {
        $path = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
        $q = $ctx.Request.QueryString
        if ($path -eq '/' -or $path -eq '/tools/presentation-studio' -or $path -eq '/tools/presentation-studio/') {
            $ctx.Response.Redirect("/$page")
            $ctx.Response.Close()
            continue
        }
        if ($path -eq '/api/packages') {
            $list = @()
            Get-ChildItem -LiteralPath $presentations -Directory | ForEach-Object {
                $m = Join-Path $_.FullName 'mod.json'
                if (Test-Path -LiteralPath $m) {
                    try {
                        $j = Get-Content -LiteralPath $m -Raw | ConvertFrom-Json
                        if ($j.type -eq 'html') { $list += [pscustomobject]@{ folder = $_.Name; name = $j.name; entry = $j.entry; canvas = $j.canvas } }
                    } catch { }
                }
            }
            SendText $ctx 200 (ConvertTo-Json @($list) -Depth 6) 'application/json; charset=utf-8'
            continue
        }
        if ($path -eq '/api/save-layout' -and $ctx.Request.HttpMethod -eq 'POST') {
            $d = PackageDir $q['package']
            if (-not $d) { SendText $ctx 400 'unknown package'; continue }
            [IO.File]::WriteAllBytes((Join-Path $d 'layout.css'), (ReadBody $ctx))
            SendText $ctx 200 'saved'
            continue
        }
        if ($path -eq '/api/upload-asset' -and $ctx.Request.HttpMethod -eq 'POST') {
            $d = PackageDir $q['package']
            $name = [IO.Path]::GetFileName([string]$q['name'])
            if (-not $d -or -not $name -or -not $mime.ContainsKey([IO.Path]::GetExtension($name).ToLower())) { SendText $ctx 400 'bad upload'; continue }
            $assets = Join-Path $d 'assets'
            New-Item -ItemType Directory -Force -Path $assets | Out-Null
            [IO.File]::WriteAllBytes((Join-Path $assets $name), (ReadBody $ctx))
            SendText $ctx 200 "assets/$name"
            continue
        }
        if ($path -eq '/api/files') {
            $d = PackageDir $q['package']
            if (-not $d) { SendText $ctx 400 'unknown package'; continue }
            $files = @(Get-ChildItem -LiteralPath $d -Recurse -File | ForEach-Object {
                $_.FullName.Substring($d.Length + 1).Replace('\', '/') })
            SendText $ctx 200 (ConvertTo-Json $files) 'application/json; charset=utf-8'
            continue
        }
        if ($path -eq '/api/write' -and $ctx.Request.HttpMethod -eq 'POST') {
            $d = PackageDir $q['package']
            $f = PackageFile $d $q['path']
            if (-not $f) { SendText $ctx 400 'bad path'; continue }
            New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
            [IO.File]::WriteAllBytes($f, (ReadBody $ctx))
            SendText $ctx 200 'saved'
            continue
        }
        if ($path -eq '/api/create-package' -and $ctx.Request.HttpMethod -eq 'POST') {
            # Body: mod.json text. The folder must not exist yet.
            $name = [string]$q['name']
            if (-not $name -or $name -match '[\\/:*?"<>|]' -or $name -match '^[._]' -or $name.Length -gt 64) { SendText $ctx 400 'bad package name'; continue }
            $d = Join-Path $presentations $name
            if (Test-Path -LiteralPath $d) { SendText $ctx 409 'a package with that folder name already exists'; continue }
            $body = ReadBody $ctx
            try { $j = [Text.Encoding]::UTF8.GetString($body) | ConvertFrom-Json } catch { SendText $ctx 400 'mod.json is not valid JSON'; continue }
            if ($j.type -ne 'html') { SendText $ctx 400 'mod.json type must be html'; continue }
            New-Item -ItemType Directory -Path $d | Out-Null
            [IO.File]::WriteAllBytes((Join-Path $d 'mod.json'), $body)
            SendText $ctx 200 $name
            continue
        }
        # Static files from the project folder.
        $rel = $path.TrimStart('/').Replace('/', '\')
        $file = [IO.Path]::GetFullPath((Join-Path $root $rel))
        if (-not $file.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $file -PathType Leaf)) {
            SendText $ctx 404 'not found'; continue
        }
        $type = $mime[[IO.Path]::GetExtension($file).ToLower()]
        if (-not $type) { $type = 'application/octet-stream' }
        Send $ctx 200 ([IO.File]::ReadAllBytes($file)) $type
    } catch {
        try { SendText $ctx 500 $_.Exception.Message } catch { }
    }
}
