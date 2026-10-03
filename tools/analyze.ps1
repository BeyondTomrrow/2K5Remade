# -Xbe/-Analysis/-Gen: convert another XBE (a mod pack's patched default.xbe)
# into its own analysis and generated-code folders. The defaults are the
# retail game's: original\default.xbe -> analysis, src\recomp\gen.
param([switch]$Recompile, [string]$Xbe = '', [string]$Analysis = '', [string]$Gen = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$xbe = if ($Xbe) { (Resolve-Path $Xbe).Path } else { "$root\original\default.xbe" }
if (-not (Test-Path $xbe)) { throw "Place your legally extracted default.xbe at $xbe first." }
$an = if ($Analysis) { $Analysis } else { "$root\analysis" }
$gendir = if ($Gen) { $Gen } else { "$root\src\recomp\gen" }
New-Item -ItemType Directory -Force $an, $gendir | Out-Null
$an = (Resolve-Path $an).Path            # the steps run from external\xboxrecomp
$gendir = (Resolve-Path $gendir).Path
$python = "$root\dependencies\venv\Scripts\python.exe"
$env:PYTHONUTF8 = '1'
$env:PYTHONUNBUFFERED = '1'
Get-FileHash $xbe -Algorithm SHA256 | ConvertTo-Json | Set-Content "$an\input-sha256.json"
function RunPython([string[]]$Arguments) {
  # Progress goes to stderr: only the exit code says whether a step failed.
  $ErrorActionPreference = 'Continue'
  & $python @Arguments 2>&1 | ForEach-Object { "$_" }
  if ($LASTEXITCODE) { throw "Pipeline step failed: $Arguments" }
}
Push-Location "$root\external\xboxrecomp"
try {
  RunPython @('-m','tools.xbe_parser',$xbe,'--json',"$an\xbe_analysis.json")
  $disasmArgs = @('-m','tools.disasm',$xbe,'--analysis-json',"$an\xbe_analysis.json",'--output',"$an\disasm",'-v')
  # A pack's own seeds if it has them, else the retail game's (same addresses
  # for the code the pack leaves alone).
  $seedFile = if (Test-Path "$an\seed_functions.json") { "$an\seed_functions.json" } else { "$root\analysis\seed_functions.json" }
  if (Test-Path $seedFile) { $disasmArgs += @('--seed-functions',$seedFile) }
  RunPython $disasmArgs
  RunPython @('-m','tools.func_id',$xbe,'--functions',"$an\disasm\functions.json",'--strings',"$an\disasm\strings.json",'--xrefs',"$an\disasm\xrefs.json",'--output',"$an\func_id")
  RunPython @('-m','tools.abi_analysis',$xbe,'--disasm-dir',"$an\disasm",'--func-id-dir',"$an\func_id",'--output-dir',"$an\abi")
  if ($Recompile) {
    RunPython @('-m','tools.recomp',$xbe,'--all','--split','1000','--game-name','ESPN NFL 2K5','--disasm-dir',"$an\disasm",'--func-id-dir',"$an\func_id",'--abi-dir',"$an\abi",'--output-dir',"$an\recomp",'--gen-dir',$gendir)
  }
} finally { Pop-Location }
