# Presentation Studio: a local editor for HTML presentation packages.
# Serves the project folder on http://127.0.0.1:<port>/ (this computer only)
# so the editor can open a package page in an iframe with full access to its
# elements, and saves layout/asset changes back into the package.
#
#   powershell -ExecutionPolicy Bypass -File tools\presentation-studio\serve.ps1
#
# Writes are limited to mods\presentations\<package>\layout.css and
# mods\presentations\<package>\assets\<file>. Nothing else can be written.
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
function ReadBody($ctx) {
    $ms = New-Object IO.MemoryStream
    $ctx.Request.InputStream.CopyTo($ms)
    return $ms.ToArray()
}

$listener = New-Object Net.HttpListener
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
$listener.Start()
$url = "http://127.0.0.1:$Port/tools/presentation-studio/index.html"
"Presentation Studio: $url  (Ctrl+C to stop)"
if (-not $NoBrowser) { Start-Process $url }

while ($listener.IsListening) {
    $ctx = $listener.GetContext()
    try {
        $path = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
        $q = $ctx.Request.QueryString
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
