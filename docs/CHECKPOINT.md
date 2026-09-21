# Native development checkpoint — September 7, 2026

The native build exists at build/Release/NFL2K5.exe and executes translated game initialization. It is NOT playable: no game window or completed renderer. DX12/Vulkan and DLSS are not implemented. Do not repeat the full analysis/recompilation to resume debugging.

## Resume

Run tools/build.ps1 -Game for incremental builds. Run tools/run-bringup.ps1 -ValidateOnly to verify the original image, memory placement, and contiguous allocation/free/reuse. Run tools/run-bringup.ps1 for a bounded startup attempt; this enables RECOMP_AC97_READY=1 and the ten-second watchdog. Logs are timestamped in logs/. Original legally extracted files remain in original/.

## Completed work

32,860 functions translated, one failed, 620 unresolved stubs. Generated C is in src/recomp/gen; disassembly, ABI, and translation reports are in analysis/. The generated executable is distinct from NFL2K5_toolchain_check.exe. Build uses the F: Visual Studio installation and dependencies on E:.

Fixed upstream scratch/TLS/kernel-data allocations that overwrote this large game's read-only data. Allocated a separate main guest stack. Fixed missing C declarations affecting getenv and the watchdog. Implemented contiguous allocation release/reuse and allocation-size queries so the game's memory-capacity probe no longer exhausts the arena. Validation passed. The host verifies the original XBE SHA-256 and selected original data after initialization.

Host registers the existing synthetic GPU fence mirror for device pointer 0x004409A8 (submitted offset 0x2C, completion pointer offset 0x30). This only permits CPU initialization to progress; it does not render or prove GPU commands completed.

## Current blocker

DirectSoundCreate at 0x0044910E previously returned 0x88780078 (codec readiness timeout at 0x00453EDB). The game then used a null sound object and crashed at sub_004440BA. Enabling the existing codec-ready option and connecting the existing APU initialization/MMIO exception handler in src/main.c removes that access violation.

Latest tested startup exits via watchdog (code 3), sampled at sub_004542EA+0x6AF. Inspect analysis/disasm/asm/DSOUND.asm at 0x004542EA: it writes AC97 reset bit 2 to a register selected by the table at 0x00468C78 plus 0xFEC0010B, then reads CL and loops at 0x00454320 while that bit remains set. AC97 is still plain backing memory; the APU trap covers only 0xFE800000..0xFE87FFFF. The read occurs before the loop, so merely clearing backing memory later will not change cached CL. Implement proper AC97 reset-register semantics on writes/reads; do not blindly bypass audio initialization or force success. Further DSP compatibility may also be required.

Latest diagnostic log: logs/startup-20260907-215249.stderr.log. Generated recomp_0029.c currently has temporary [AUDIO] diagnostics for construction, initialization, creation, and the failing sound-object path. These are observations only, not behavior changes; full regeneration will remove them.

Upstream local changes are exported to config/xboxrecomp-local.patch. Apply only to a clean matching upstream checkout (5a05181fbe7a454fba468c9f56336ecf6a958ffd); the current checkout already contains them. Preserve src/main.c, src/recomp_manual.c, config/game.cmake, tools/, analysis/, and generated source when continuing.
