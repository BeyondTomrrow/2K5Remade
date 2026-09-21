# Verified setup, 2026-09-07

Workspace: E:\NFL2K5-PC
Git: 2.55.0.windows.3 (existing C: installation)
Python: 3.13.12 (installed on E:, signed Python Software Foundation installer)
Capstone: 5.0.7 (isolated E: virtual environment)
Visual Studio: F:\Program Files\Microsoft Visual Studio\2022\Community
MSVC: compiler 19.44.35213, tool directory 14.44.35207
CMake: 3.31.6-msvc6, bundled with Visual Studio
Ninja: bundled with Visual Studio
Windows SDK: 10.0.26100.0 selected from F:\Program Files (x86)\Windows Kits\10
Upstream: https://github.com/sp00nznet/xboxrecomp.git
Exact upstream revision: config/xboxrecomp-revision.txt

PASS: CMake configuration, all 60 build steps, runtime static libraries and native x64 toolchain-check executable execution.
PASS: parser, disassembler, ABI analyzer and recompiler command-line loading.
NOT TESTED: NFL 2K5 analysis, generated game code, runtime linkage/guest execution or rendering; no user XBE has been supplied.

Build logs are in logs/. Upstream compiled with warnings, including an undeclared getenv in apu_mmio_hook.c; review this before enabling APU guest execution. A successful static-library build does not establish runtime correctness.

The Visual Studio installer query returned no registered instances; vcvars64 located MSVC but failed to locate the Windows SDK. tools/build.ps1 uses the installed compiler and SDK directories directly, without changing registry or global PATH.

No game files downloaded. No DLSS integration. DX12/Vulkan architecture and directories prepared; modern backend implementation, Vulkan SDK and optional Ghidra/JDK remain for later milestones.

Next: place the legally extracted default.xbe in E:\NFL2K5-PC\original\default.xbe, then run tools/analyze.ps1. Preserve other extracted assets under original with their directory structure.