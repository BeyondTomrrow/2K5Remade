# NFL 2K5 native bring-up

Input: North American retail ESPN NFL 2K5, title ID 0x53450030,
entry 0x00016BD1, XDK 5849, 11,948,032-byte XBE.
SHA-256: 73105B17A3161C546FEA792A1C84CE37F9966A67C416F474CDBFAB74B911A4A9.

Analysis includes the executable game and XDK sections. The .string_ section
contains UTF-16 text and is excluded despite its executable flag. The original
XBE and extracted assets remain under original/ and are ignored by Git.

The first host integration uses the upstream runtime for guest memory, kernel
calls and dispatch. It verifies the title and entry point before initialization.
Game data uses original/; runtime saves use saves/.

Local upstream fixes:

- Add stdlib.h to the APU hook so getenv has the correct pointer return type.
- Exclude .string_ from the disassembler and recompiler code-section lists.
- Allocate emulated TLS/PRCB scratch through the guest allocator instead of
  writing fixed addresses inside this game's .rdata.
- The host allocates a separate main guest stack, because the upstream fixed
  stack range overlaps the large NFL 2K5 image. Its initial TLS base stays intact.

The host checks the original bytes at the former scratch locations after memory
initialization. This catches regressions in the startup memory-corruption fix.

Commands from PowerShell:

    powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\analyze.ps1 -Recompile
    powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\build.ps1 -Game
    powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\run-bringup.ps1 -ValidateOnly
    powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\run-bringup.ps1 -Seconds 20

The bring-up executable contains debug symbols and leaves generated code
unoptimized by default for faster iteration and useful fault locations.
NFL2K5_OPTIMIZE can be enabled in CMake once startup works.

Kernel coverage before title-specific implementation: 164 of 186 imports have
bridge/thunk handling. Twenty-two are unhandled; import coverage does not prove
semantic compatibility, and unused imports may never be reached. Detailed list:
analysis/kernel-coverage.txt.

DX12 and Vulkan remain the long-term rendering backends; Streamline/DLSS is still
deferred. This bring-up does not implement either modern renderer.
