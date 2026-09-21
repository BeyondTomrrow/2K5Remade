$projectPath = "E:\NFL2K5-PC"
$sourceFile = "$projectPath\src\recomp_manual.c"

Write-Host "Gathering code context from recomp_manual.c..." -ForegroundColor Cyan
$codeContext = Get-Content -Path $sourceFile -Raw

$ollamaUri = "http://localhost:11434/api/generate"
$prompt = @"
We are fixing a stack-state crash in a native static recompilation of NFL 2K5 at E:\NFL2K5-PC.
The crash occurs inside sub_004952F8 right after the frontend state-6 branch execution.

Here is the content of src\recomp_manual.c:
$codeContext

Please provide ONLY the corrected C code block for sub_004952F8 with proper stack alignment and return handling so it fixes the crash without syntax errors. Output only valid C code.
"@

$body = @{
    model  = "qwen2.5-coder:7b"
    prompt = $prompt
    stream = $false
    options = @{
        temperature = 0.1
        num_ctx     = 8192
    }
} | ConvertTo-Json -Depth 10

Write-Host "Querying local Qwen2.5-Coder..." -ForegroundColor Green

try {
    $response = Invoke-RestMethod -Uri $ollamaUri -Method Post -Body $body -ContentType "application/json" -TimeoutSec 180
    $aiOutput = $response.response

    Write-Host "`n=== AI FIX RECEIVED ===" -ForegroundColor Yellow
    
    if ($aiOutput -match "```c\s*([\s\S]*?)\s*```") {
        $cleanCode = $Matches[1]
    } else {
        $cleanCode = $aiOutput
    }

    $backupPath = "$sourceFile.bak"
    Copy-Item -Path $sourceFile -Destination $backupPath -Force
    Write-Host "Backed up original recomp_manual.c to recomp_manual.c.bak" -ForegroundColor DarkGray

    Write-Host "Triggering project build to update NFL2K5.exe..." -ForegroundColor Cyan
    Set-Location "$projectPath\build"
    cmake --build . --config Release

    Write-Host "Build complete! Check your D3D11 window output." -ForegroundColor Green

} catch {
    Write-Host "Ollama request failed: $_" -ForegroundColor Red
}