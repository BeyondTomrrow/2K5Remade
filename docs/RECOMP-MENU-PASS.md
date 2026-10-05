# Recompiled game boot pass — September 8, 2026

Active path is the original translated game, build/Release/NFL2K5.exe. The separate reconstruction viewer is paused and currently has three unresolved references added by another edit (scan_hitx_blocks, load_texture_by_index, get_total_texture_count). Do not confuse its build with the recompiled game.

tools/build.ps1 -Game now explicitly builds NFL2K5 and NFL2K5_toolchain_check. This succeeds, while preserving the separate viewer sources. Generated functions still emit pre-existing uninitialized-ebp warnings; they were not globally zeroed or declared correct.

src/recomp_manual.c now supplies nfl2k5_ack_ac97_reset, which clears the AC97 channel-reset bit in the mapped control register and logs the bypass. src/recomp/gen/recomp_0030.c calls it immediately after the reset write at loc_004542FE inside sub_004542EA. This is a boot-only hardware acknowledgement, not audio playback. Regenerating C removes this call and its extern declaration; restore them before a new boot test. Do not rerun full generation unnecessarily.

Runtime evidence: reset acknowledgements at FEC0011B and FEC0017B; DirectSound initialize status 00000000; DirectSound create status 00000000, valid object 01902C04. The earlier null-device crash and reset spin are cleared.

Next blocker: startup creates worker routine 000170B4 with context 000359B0, then main thread waits in host CRT logging. Main-thread native stack sample includes ZwWaitForAlertByThreadId, RtlEnterCriticalSection, fputs, _stdio_common_vfprintf, fprintf, kernel bridge functions, sub_0001714C, sub_0004D900, sub_0004D920, sub_003D5D70. Kernel logging budget 0 did not remove this wait. Both stdout/stderr logging and the existing watchdog can be blocked by the CRT lock; the external 20-second diagnostic timeout still stops the process.

src/main.c writes logs/native-sample.txt directly with Win32 WriteFile, independently of CRT stream locks. It captures a main-thread context and scans possible host return addresses; these are diagnostic candidates, not a guaranteed full call stack. Next work: inspect worker stack/lock owner and bridge logging around PsCreateSystemThreadEx. Worker context 000359B0 calls 00035950, a loop using a critical section at B0282C.

No main menu loop has been identified or reached. No SDL2/DX12/Vulkan menu renderer or game input hookup was added in this pass. Do not substitute a custom asset viewer or blank window for proof of a real menu boot. Recompilation still has unresolved indirect calls, including 003CB1C0.

Build: powershell -NoProfile -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\build.ps1 -Game

Validation: tools/run-bringup.ps1 -ValidateOnly

Bounded run: tools/run-bringup.ps1
