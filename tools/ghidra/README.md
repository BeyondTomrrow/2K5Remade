# Ghidra + XBE loader for NFL 2K5

A second, independent view of the game code next to our own analysis. Nothing here
feeds the recompiler: it produces a function list to compare with `analysis/fdb.sqlite`.

## What is installed (under `dependencies/`, not committed)

| Component | Version | Source | Size | SHA-256 (start) |
|---|---|---|---|---|
| Temurin JDK 21 | 21.0.12.1+1 | github.com/adoptium/temurin21-binaries | 205 MB | f9d6e191… |
| Ghidra | 12.0.3 (public, 2026-02-10) | github.com/NationalSecurityAgency/ghidra | 510 MB | 90d3fffb… |
| ghidra-xbe (XboxExecutableLoader) | build-202602250354 | github.com/XboxDev/ghidra-xbe | 0.5 MB | fc689e84… |

The extension's native `XbSymbolDatabaseCLI.exe` is part of the release. Its loader
runs it for library symbol matching. It comes from the XboxDev release, not built here.

Layout after install:
- `dependencies/jdk21/jdk-21.0.12.1+1` (Java)
- `dependencies/ghidra/ghidra_12.0.3_PUBLIC` (Ghidra; the extension is in `Ghidra/Extensions/ghidra-xbe`)
- `dependencies/ghidra-headless.sh` (direct headless launcher)
- `dependencies/ghidra-projects/NFL2K5_retail` (the Ghidra project)

## Commands

Headless import + analysis of the retail XBE (about 30 to 60 minutes):

    dependencies/ghidra-headless.sh dependencies/ghidra-projects NFL2K5_retail \
        -import "original/disc/default.xbe" -loader XbeLoader -overwrite \
        -analysisTimeoutPerFile 3600

Export the function list, then compare with our analysis:

    dependencies/ghidra-headless.sh dependencies/ghidra-projects NFL2K5_retail -process default.xbe \
        -noanalysis -scriptPath tools/ghidra -postScript ExportFunctions.java logs/ghidra-functions-retail.csv
    python tools/ghidra/compare.py logs/ghidra-functions-retail.csv

## Why not the .bat launcher

`support/analyzeHeadless.bat` mangles arguments that contain spaces or parentheses, so
it cannot pass the loader name. `ghidra-headless.sh` calls Ghidra's Java entry point
directly.

## Loader name

Headless import takes the loader's short class name (`XbeLoader`). The display name
("Xbox Executable Format (XBE)") and the extension name ("XboxExecutableLoader") are
both rejected.

## Known limitation

The XBE imports from `xboxkrnl.exe`. That library is not in the project, so kernel
calls stay unresolved (`[xboxkrnl.exe] -> not found in project`). Our own kernel bridge
already names those calls.
