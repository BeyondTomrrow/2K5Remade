param([switch]$Recompile)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$xbe = "$root\original\default.xbe"
if (-not (Test-Path $xbe)) { throw "Place your legally extracted default.xbe at $xbe first." }
$python = "$root\dependencies\venv\Scripts\python.exe"
$env:PYTHONUTF8 = '1'
$env:PYTHONUNBUFFERED = '1'
Get-FileHash $xbe -Algorithm SHA256 | ConvertTo-Json | Set-Content "$root\analysis\input-sha256.json"
function RunPython([string[]]$Arguments) {
  & $python @Arguments
  if ($LASTEXITCODE) { throw "Pipeline step failed: $Arguments" }
}
Push-Location "$root\external\xboxrecomp"
try {
  RunPython @('-m','tools.xbe_parser',$xbe,'--json',"$root\analysis\xbe_analysis.json")
  $disasmArgs = @('-m','tools.disasm',$xbe,'--analysis-json',"$root\analysis\xbe_analysis.json",'--output',"$root\analysis\disasm",'-v')
  $seedFile = "$root\analysis\seed_functions.json"
  if (Test-Path $seedFile) { $disasmArgs += @('--seed-functions',$seedFile) }
  RunPython $disasmArgs
  RunPython @('-m','tools.func_id',$xbe,'--functions',"$root\analysis\disasm\functions.json",'--strings',"$root\analysis\disasm\strings.json",'--xrefs',"$root\analysis\disasm\xrefs.json",'--output',"$root\analysis\func_id")
  RunPython @('-m','tools.abi_analysis',$xbe,'--disasm-dir',"$root\analysis\disasm",'--func-id-dir',"$root\analysis\func_id",'--output-dir',"$root\analysis\abi")
  if ($Recompile) {
    RunPython @('-m','tools.recomp',$xbe,'--all','--split','1000','--game-name','ESPN NFL 2K5','--disasm-dir',"$root\analysis\disasm",'--func-id-dir',"$root\analysis\func_id",'--abi-dir',"$root\analysis\abi",'--output-dir',"$root\analysis\recomp",'--gen-dir',"$root\src\recomp\gen")
  }
} finally { Pop-Location }
