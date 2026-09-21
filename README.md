# NFL2K5-PC development workspace

This is an initial original Xbox recompilation workspace, not a playable port.

1. Put your legally extracted file at E:\NFL2K5-PC\original\default.xbe.
2. In PowerShell, run: powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\analyze.ps1
3. To also generate C, add -Recompile. Review reports and generated code before wiring the game executable.
4. Build the runtime and native development check: powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\build.ps1

The build script detects Visual Studio through vswhere, with your F: installation as fallback. It locates MSVC and SDK files directly because installer registration on this machine is incomplete. Dependencies and build artifacts stay on E:; existing tools remain in their installed locations.

The native check is NFL2K5_toolchain_check.exe, not NFL2K5.exe. The game target is gated until actual XBE analysis and title-specific integration exist. The upstream starter is external/xboxrecomp/templates/new-game; its sample entry-point constants must not be used for NFL 2K5.

See docs/architecture.md for DX12, Vulkan and deferred Streamline design. See logs for build output. Original files and generated code are excluded from Git. Upstream carries MIT and LGPL components; retain its licenses and review applicable distribution obligations before redistribution.