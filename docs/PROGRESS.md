# Recompiled NFL 2K5 progress log

## Latest pass: memory copy and I/O completion

Replaced the truncated translated memmove entry 000145B0 with an overlap-safe host implementation operating on mapped guest pointers. The original generated function omitted reverse-copy targets including 00014854. Validation passed for both overlapping directions, lengths 0/1/3/7/31/64, return pointer and guest-stack cleanup; allocator checks also passed (validate-20260908-172422 logs). The missing copy target no longer appears in the startup log.

The full worker run still stalls around host logging after worker 000359B0 starts. A separate diagnostic run temporarily skipped only that worker, leaving the restored I/O worker 0004D810 active. It reached save-directory and CdRom0 path operations and exposed missing completion callback 0004C3C0. Restored that callback directly from its original push/call/tail-jump instructions. Build passed; io-completion logs confirm the callback is no longer unresolved, but no menu boot was reached. The inspected io-completion-frame.bmp remains gray. Main-thread sampling showed a host WriteFile wait in this run. Neither first-worker stability nor asset loading completion is established. The worker bypass was used only for this diagnostic and is not enabled in the normal launcher.

Next: inspect why output/file operations block and trace completion signaling/worker state around the disc path; do not infer rendered menus or successful asset reads from callback counts. Existing remaining unresolved targets include 00016856, 004DA7E0 and 004DB047. Changes in generated recomp_0000.c must be preserved when regenerating code.

## Deterministic boot pass in progress

The normal diagnostic launcher now sets `RECOMP_CS_MODE=single` and `XBOX_LOG_LEVEL=0`. This is a bisecting configuration: it serializes guest critical sections and suppresses runtime informational logs so the next run can distinguish guest lock behavior from host output contention. It is not a final threading model. The immediate test criteria are whether startup progresses beyond worker 000359B0, whether I/O completion target 0004C3C0 remains resolved, and whether DMA PUT advances past the initialization range.

## Progress assessment saved September 8, 2026

The native EXE builds, loads the extracted XBE and enters translated game code. Bounded runs have reached audio compatibility initialization, worker startup, display setup and a repeating scheduler loop. A software NV2A pushbuffer executor applied a clear; captured output has been gray or black. No actual menu screen, menu navigation or gameplay has been verified. Linking D3D11 is not evidence that these frames use the D3D11 renderer.

The prior conversation estimated menu work at 25–35% complete and a playable port at 1–3%, with build/XBE entry at 100%, initialization at 70%, loop bring-up at 65%, visible menu pixels at 10%, and navigation/gameplay at 0%. These were subjective estimates, not measured completion percentages or reliable estimates of remaining time. Remaining scope cannot yet be quantified. Verified milestones above should guide planning.

Remaining work includes locating the boot/scheduler blocker, verifying asset reads and draw submission, implementing required GPU behavior, connecting input, and testing real menus and gameplay. The user's direct-launch failure remained unresolved after the previous GUI-subsystem change.

Current correction: default settings were written with Win32 SetEnvironmentVariable while runtime code reads CRT getenv. Defaults now use _putenv_s so CRT consumers can see them. Also corrected the manual 00041810 callback to retain its caller return address on the guest stack throughout nested calls, matching the XBE push/pop sequence. Build and direct-launch verification follow.

Verification: rebuilt successfully. A no-argument launch from build/Release with no inherited RECOMP settings stayed alive for the 15-second test; it was then stopped by the test harness. It stalled around worker startup, so this does not verify a visible window or menus. The diagnostic stack again shows a host CRT output lock wait (`fputs` / `RtlEnterCriticalSection`), contradicting earlier claims that worker startup was reliably stable.

Restored missing worker target 0004D810 from its XBE jump table and original calls. It was previously unresolved, causing that worker to terminate without performing its state machine. Build succeeded, but the subsequent 20-second test stalled on the earlier host logging lock before this restoration could be validated. Next work must resolve that lock stall and then verify worker state transitions and game asset reads. No new menu pixels were observed.

## 2026-09-08 — current boot pass

The user ran `tools/run-bringup.ps1` successfully. The host validated the original XBE and entered recompiled code. DirectSound was previously blocked by AC97 reset; the current generated-code bypass clears that reset and the latest runtime evidence shows DirectSound succeeds.

Current stop: `PsCreateSystemThreadEx #2` starts guest routine `0x000170B4` with context `0x000359B0`; the main thread then waits in a host CRT lock. This pass will temporarily bypass only that worker creation to establish whether it is non-essential to the menu path. Every bypass must be identified in this log and verified by a bounded run.

DX12 status: not connected to recompiled game rendering. Native menu boot remains the priority because a DX12 clear window cannot display the game menu until NV2A commands and menu textures reach a host renderer.

Added a temporary opt-in bypass for worker routine `000170B4` only when its context is `000359B0`. `run-bringup.ps1` enables it. The next bounded run determines whether this removes the host lock stall; it is not a claim that the worker is permanently unnecessary.

The worker bypass advanced the game through `AvSetDisplayMode` with format `0x12`, pitch `3072`, and guest framebuffer `0x0367C000`. The run then remained live for the watchdog period in `sub_0003A1C0` work-queue processing, with about 25 million indirect calls. The bring-up launcher now enables the runtime framebuffer window and requests a BMP capture at the window's 600th frame.

Framebuffer window verified open. NFL 2K5 first selects a 640-pixel-pitch display buffer at `0x00014000`, then selects the later buffer at `0x0367C000` with 3072-byte pitch. The capture threshold was shortened from 600 to 60 frames so a bounded watchdog test produces a framebuffer image for inspection.

The captured framebuffer is uniform gray. `RECOMP_NV2A_TRACE` is now enabled in the bounded launcher to record framebuffer checksums, CRTC scanout selection, and DMA pushbuffer activity. This identifies whether the menu is not yet issuing GPU work or whether the GPU compatibility layer is failing to draw submitted work.

NV2A trace confirmed DMA pushbuffer activity during two display-mode setups, then no new work after the second mode. Manual dispatch now supplies simple return stubs for two missing executable gap targets: `0003448E` is verified `xor eax,eax; ret`; `003CB1C0` is temporarily treated as a boot registration callback returning zero. The next run tests whether removing these repeated unresolved calls lets menu initialization submit more GPU work.

Result: the `003CB1C0` return-zero stub prevented normal video setup, so it was removed. The verified `0003448E` return-zero gap stub remains. Do not stub `003CB1C0` without reconstructing its actual behavior.

`003CB1C0` has now been reconstructed in `src/recomp_manual.c` from its original ten instructions: it calls `003784D0`, branches on EAX, calls the original `00016CFF` and `00016CAD` initialization helpers when needed, and otherwise tail-jumps to `00016EAC`. This replaces the failed no-op stub.

The first reconstructed run reached `KeSetBasePriorityThread` through `00016CAD`. The current bridge incorrectly treated the Xbox guest thread object as a Win32 `HANDLE`, causing the host to block in `NtSetInformationThread`. The bounded launcher now enables a narrow compatibility bypass for this one priority call. It returns the normal initial priority of zero and logs the guest object; it does not skip the surrounding registration logic. The next run will determine whether boot resumes into later video/menu work.

The compatibility bypass exposed a hot registration/release loop (`003CB1C0`/`003CB1F0`) whose priority object is currently zero. Its initial unlimited diagnostics themselves became the host's visible wait, so logging is capped. The next bounded run measures actual title progress with the instrumentation out of the way; resolving the zero guest object remains the active functional blocker.

The clean follow-up run restored full progress through both display-mode changes and approximately 22 million translated calls. The strongest remaining recurring unresolved target is `00041810`, the scheduler-installed per-frame callback that updates render queues. It has been identified as a 93-byte valid XBE function missed because it is stored as callback data, not called directly. A direct instruction-level translation has been added to the manual dispatcher for the next run.

The callback translation removed `00041810` from diagnostics and kept the per-frame loop alive. The framebuffer is still uniform gray. The next two scanner-boundary gaps (`0042A260` and `0042B220`) are short D3D pushbuffer state emitters, so direct translations have now been added. `0042ADE0` remains a larger D3D state routine and is the next unresolved rendering target if this run remains blank.

`0042A260` and `0042B220` now emit their expected state packets: DMA PUT advanced from `03E50AAC` to `03E50ABC`. The only recurring D3D unresolved target is now `0042ADE0`, a mode/state packet sequence. Its direct translation has been added for the next bounded native run.

The command executor already exists in the NV2A compatibility runtime, but the bring-up launcher had only enabled tracing. The next run enables `RECOMP_PB_EXEC` and `RECOMP_PB_SCAN`, allowing the runtime to consume supported clear and geometry methods while producing an inventory of anything still unsupported.

The enabled executor consumed the title's first colour clear, proving it is connected to the real command stream; the capture changed from gray to black. A worker-enabled run is now stable and reaches roughly 28 million translated calls, versus roughly 20 million with the temporary worker skip. The normal launcher now runs that worker and retains the executor. The next task is to find why the subsequent front-end draw calls are not submitted after the initial clear.

The host now treats a no-argument `NFL2K5.exe` launch as a normal native bring-up run. It enables its required audio compatibility, framebuffer window, pushbuffer executor, and safe thread-priority handling internally. `--validate` remains a non-game diagnostic mode. This removes the immediate close caused by the previous command-line-only workflow.

Framebuffer capture from the current recompiled run is `logs/framebuffer-latest.bmp` (uniform gray). NV2A trace recorded pushbuffer submissions through `0x03E50AAC`; the GPU acknowledgement mirrored GET to PUT. No later submissions occurred during the 10-second watchdog period. This establishes that the current stall is after initial video/GPU setup, before menu drawing into the scanout buffer.

## 2026-09-08 — XPP input initialization recovery

The native build now resolves the previously missing XPP timer initialization target `004DB047` from its original instructions. A bounded run confirms that target no longer appears in indirect-call diagnostics. The run has a live 640×480 native framebuffer window, an initial NV2A clear, and three pushbuffer segments (713 command words), but no menu draw has yet been submitted.

The remaining XPP target `004DA7E0` is a real controller-device registration routine, not a safe return-zero candidate. Its verified XBE entry point, plus short callback `00016856`, were added to `analysis/seed_functions.json`. `tools/analyze.ps1` now passes this seed file into disassembly so the normal recompiler can emit both routines. Full seeded analysis/recompilation is in progress; do not build from `src/recomp/gen` until it completes. No visible menu, controller navigation, or gameplay has been verified.

## 2026-09-08 � scheduler and startup handoff trace

Seeded recompilation completed and the native x64 executable builds. A clean bounded run no longer reports unresolved guest indirect calls. It reaches the game's real startup scheduler (`00074BF0`): the frame callback is registered, the first display mode is selected, and the NV2A executor consumes the initial clear command. It still submits no `BEGIN_END` geometry, so the menu renderer has not yet been reached.

The Xemu reference log confirms that a successful Xbox boot eventually emits textured `NV097_SET_BEGIN_END` / `NV097_INLINE_ARRAY` menu draw sequences. That makes the immediate native blocker a game-state/synchronization handoff, rather than absent DX12 or Vulkan presentation.

The current trace also exposes a title request for the largest contiguous physical-memory block. The allocator's expected retry loop reaches a usable size after reserving the small GPU blocks; it is noisy but is not currently proven to be the permanent stall. An opt-in `RECOMP_BOOT_UNSTICK` diagnostic was added to clear the native bridge's stale callback-pending flag after the frame callback. The project rebuilds with this switch. The controlled run still did not reach menu geometry, so it is retained only for diagnosis and is not enabled by the normal launcher.

The next active target is the post-video startup handoff: native traces show repeated file/save initialization and completion behavior following the frame registration. The next pass will inspect the exact event/file completion object and the worker state that must complete before the front-end begins submitting menu commands. No visible menu pixels, playable input, or gameplay has been verified.

## 2026-09-08 � Direct3D 11 framebuffer presenter

Replaced the native framebuffer window's GDI blit path with a Direct3D 11 swap-chain presenter. It uploads the converted Xbox scanout buffer as BGRA8, copies it to the D3D11 backbuffer, and presents at the display cadence. The game executable rebuilds successfully and the non-game validation checks pass. This makes the visible native window use DX11, but it cannot invent menu pixels: the current game state still clears the framebuffer and submits no menu geometry.

## 2026-09-08 — Live Xemu scheduler comparison

A live Xemu demo was queried through its local debugger. It confirms the retail title has reached a real game frame and supplies a reference scheduler state: nine callbacks are registered (`0003E910`, `003CD120`, `0003A1C0`, `0003A310`, `00051F00`, `00039380`, `00042BD0`, `00045F20`, and the native frame callback `00041810`). The native bring-up reaches Direct3D 11 framebuffer presentation and `AvSetDisplayMode`, but its same scheduler contains only `00041810`; it therefore clears the scanout surface without submitting menu geometry.

A native opt-in state sampler now records the equivalent guest globals in `logs/native-sample.txt`. An early forced callback-registration test was rejected: it ran before required title initialization and blocked display startup. The recovery remains limited to the existing frame-callback point for controlled testing while the real delayed registration event is traced.

## 2026-09-09 — startup continuation trace

The native trace now reaches `003CD400`, which starts the title's frontend object initializer and registers `0003E910`. It does not return to the following initializer instructions, so the master callback registration routine (`00038FC0`) is still not reached.

The largest-memory probe was identified as `MmAllocateContiguousMemoryEx` from guest return site `000206C3`, with the standard unrestricted address range. The game decrements the request in 4 KiB steps until a 64,868,352-byte probe succeeds, then frees that probe. It is diagnostic noise and not yet evidence of the menu blocker.

The current confirmed blocker is a guest-tail-call/host-C-stack mismatch around `00041880 -> 00038D20`: the scheduler callback registers, but the caller's translated continuation (`003CB6B9`) is never observed. A narrow continuation patch was built and tested, but did not yet reach that continuation. The next pass must inspect the recompiler's tail-call model at this boundary instead of forcing callbacks. No menu geometry or playable native game is verified.


## 2026-09-09: verified AC97 startup spin fix

The retail bytes at 00454320 are `84 c9 75 fc`: TEST CL,CL; JNZ back to TEST. The reset register is read only before this loop. Native sub_004542EA wrote bit 1 and sampled it before any acknowledgement, so its cached CL never changed. Connected the existing nfl2k5_ack_ac97_reset helper immediately after the reset write in src/recomp/gen/recomp_0030.c. This models reset completion only; it does not synthesize audio.

Build succeeded. logs/startup-20260909-102716.stderr.log records acknowledgements at FEC0011B and FEC0017B, followed by title APU startup. The matching native sample showed B04D1C=5 (previously 1), B057D4=1, and initialized B068F0/B068F4. B04EC8 remains zero; no menu rendering verified. The later sample waits inside host CRT logging during subsequent worker startup. That is the next investigation, not proof of a particular lock owner.

Correction to earlier diagnosis: the retail import at 004E3AE4 contains ordinal 294 (RtlLeaveCriticalSection), already mapped by kernel_bridge.c. It was not established as a broken callback. Earlier clear/flip command streams were already present before the timer-wait change; they were not a new draw milestone.

The reset-site edit is in generated source and must be preserved when regenerating recomp_0030.c.
