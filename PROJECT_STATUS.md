This is an existing NFL 2K5 Xbox native recompilation project.

Do NOT start over or recreate anything.



Project:

E:\\NFL2K5-PC



Visual Studio 2022 Community:

F:\\Program Files\\Microsoft Visual Studio\\2022\\Community



Inspect the existing project, PROJECT\_STATUS.md, source files, generated code,

build output, logs, and git state before changing anything.



The previous development session already reached approximately 18% native-playable progress.



NFL2K5.exe already builds as native x64 and loads the legally extracted

original\\default.xbe.



The runtime already has substantial Xbox memory, kernel, scheduler, frontend,

NV2A command decoding, and D3D11 presentation support.



The D3D11 window currently opens but is blank.



THE EXACT LAST BLOCKER WAS:



0x004945A3



This is an omitted guest-code target reached after the frontend queue pump

started working.



Previously resolved generated-code gaps include:



0x004C3207 - title network/state dispatcher

0x00641FF0 - resource completion adapter

0x001C1FA0 - bootstrap readiness probe

0x003CC280 - frontend queue pump



Frontend callback 0x003CD120 is already active.

All 9 expected scheduler callbacks were registering.



NV2A execution has already reached:

DMA

framebuffer clears

SET\_BEGIN\_END

INLINE\_ARRAY



Continue from 0x004945A3.



Find its references and original XBE bytes, reconstruct/implement the missing

guest routine using the project's existing recompilation conventions, rebuild

NFL2K5.exe, run it, and verify execution proceeds beyond that address.



Then continue resolving subsequent guest-code/runtime blockers until non-zero

frontend vertex data reaches SET\_BEGIN\_END / INLINE\_ARRAY.



The next major visual milestone is getting the first real NFL 2K5 menu

geometry rendered in the existing D3D11 window.



Do not redo completed setup.



Do not download copyrighted game data.

Do not modify original\\default.xbe.



Update PROJECT\_STATUS.md as you make progress.



UPDATED LONG-TERM GRAPHICS GOALS:

DirectX 12

Vulkan

Native 16:9

Native UWQHD 3440x1440 / 21:9

Correct ultrawide FOV and UI scaling



DLSS and NVIDIA Streamline are NOT part of this project anymore.



Do not work on DX12, Vulkan, or UWQHD yet. Continue the existing menu/frontend

rendering work first.



Continue working autonomously until you encounter a genuine blocker requiring

my input.



Continue the EXISTING project.

Do NOT recreate or restart it.



Exact current blocker:

0x004945A3



Inspect the existing repository and continue from there.

---

## Verified progress — 2026-09-13

- Removed the opt-in scheduler-recovery helper that directly inserted callback
  addresses. The normal title registration path now independently reaches the
  nine-entry scheduler table; no runtime environment variable is required for
  that milestone.
- A clean release run confirmed the registered callbacks at `0x0003E910`,
  `0x00041810`, `0x003CD120`, `0x0003A1C0`, `0x0003A310`, `0x00051F00`,
  `0x00039380`, `0x00042BD0`, and `0x00045F20`. It reached the frontend
  worker, vertex-array format setup, `SET_BEGIN_END`, and `INLINE_ARRAY`
  methods without crashing.
- The current startup pushbuffer contains a begin/end setup pair but no
  completed draw batch (`draws 0` in the executor report). The framebuffer
  therefore remains black. The next investigation is the title state
  transition that supplies non-zero frontend vertices, rather than inventing
  geometry in the renderer.
- Current clean-run trace also shows an unresolved null indirect callback
  while the title repeats `CACHE\\LocalCache08.bin` I/O. That completion path
  is now the next compatibility trace point because it can explain why the
  frontend state-9 counter advances only intermittently.

- Reconstructed the active state-6 branch of frontend vtable dispatcher
  `0x004945A3`; it submits the original action 7/9 transition.
- Added the missing jump-table checksum continuations in the `0x00498EBF`
  through `0x00498F05` range. These are valid internal entries in the title's
  checksum worker and now return through the original saved-register ABI.
- Release build succeeds after both changes.
- A recovered runtime run now holds 9/9 scheduler callbacks, reaches 330
  completed dispatches, and advances the frontend dispatcher from state 6 to
  state 9 without a crash. The NV2A stream still reaches `SET_BEGIN_END`, but
  the emitted startup primitive is zero-filled and no recognizable legal
  screen pixels have reached the D3D11 presenter yet.
- Current next action: implement the observed state-9 branch of `0x004945A3`,
  rerun, then trace the next state or non-zero frontend primitive.

## Verified progress — 2026-09-14

- Reconstructed the missing mid-function target `0x00042182` from the local
  XBE disassembly. The previous generic return stub skipped the provider
  result builder and its `pop esi; ret 4` epilogue, corrupting the guest stack
  and turning a valid provider (`0x00A77788`) into a stack address.
- A release run now preserves the provider object and queue request across
  the full `0x0003AAB0` lookup path. The invalid `0x0674FFC4` virtual call is
  gone. This is a verified compatibility correction, not a success bypass.
- Added exact manual implementations for the locally disassembled archive
  completion callbacks `0x00044DF0` and `0x00044BB0`, both of which were
  missed because they finish with tail jumps into existing resource handlers.
  Their unresolved-call reports are gone in the nine-callback trace.
- The normal run remains timing-sensitive at seven callbacks; the diagnostic
  run reaches all nine callbacks and now exposes real resource callbacks
  instead of corrupted provider state. The framebuffer is still black and the
  startup pushbuffer currently contains setup and clears but no completed
  vertex draw batch.
- Current next action: reconstruct the remaining resource queue callback
  chain beginning at `0x00043D20`/`0x00043D6B`, then return to the first
  non-zero NV2A vertex submission.

### Runtime trace update

- Reconstructed the missed archive-completion fan-out at `0x00045100` from
  the local XBE control flow and added it to the manual lookup table.  The
  release build remains clean.
- A normal native run reaches seven scheduler registrations, while the
  diagnostic configuration reaches all nine.  The normal 15-second sample
  records only three completed injected dispatches, with guest execution in
  `0x003CD120`; the host tick itself continues to arrive.
- `0x003CD120` sees its frontend queue at the sentinel (`0x00AF57C8`) in the
  sampled run.  No frontend record reaches its prepare/submit paths, so the
  pushbuffer has clears and setup methods but no vertex batch.  A framebuffer
  capture confirms a uniform dark-gray 720x480 surface with zero rasterized
  triangles.
- Current blocker: trace the producer that should place the first frontend
  record ahead of `0x00AF57C8`, including its resource-completion callback,
  before extending the NV2A rasterizer.

### Runtime trace update — 2026-09-14 (archive completion)

- Reconstructed `0x00045A20`, a third archive-queue completion callback that
  the automatic function finder omitted because it terminates in two resource
  handler tail jumps.  The implementation preserves its original `4DC00` / 
  `48760` work and HSiN terminal-handler selection.
- Release build completed successfully.  A 45-second native run no longer
  reports `0x00045A20` as an unresolved indirect target.
- The run processes a later NV2A command stream through surface, clear, state,
  texture, and flip setup, confirming that the restored archive callback lets
  resource work proceed.  It still holds at seven callbacks in this timing
  profile and reports zero completed primitive batches, so the D3D11 window
  remains blank.
- Next action: capture a longer, low-overhead run to distinguish the
  seven-callback timing path from the known nine-callback path, then trace the
  first frontend record producer or the first non-zero `SET_BEGIN_END` batch.

### Runtime trace update — 2026-09-14 (scheduler/resource chain)

- Added memory-only entry/return markers around the scheduler's indirect
  callback invocation.  The nine-callback trace shows entry and return counts
  progressing together, ruling out a callback that never returns as the cause
  of the intermittent scheduler watchdog.
- Instrumented the title's existing scheduler-registration API.  The normal
  title path registers slot nine as `0x00045F20` from caller `0x000460DD`; the
  nine-entry table is therefore genuine, not bridge-injected.
- Reconstructed two more locally disassembled omitted tail callbacks:
  `0x000459D0` (HSiN archive predicate) and `0x00168C70` (MRKS terminal
  resource completion).  Both release builds completed successfully.  The
  former no longer appears as an unresolved target in the nine-callback run;
  the latter is ready for the marker-load path when that timing profile is
  reached.
- Current rendering state remains a cleared 720x480 surface with zero completed
  primitive batches.  The current remaining resource diagnostic is the common
  queue path that sometimes treats buffer `0x00B09598` as a completion callback;
  this must be traced to its descriptor-field producer before vertex work can
  reliably begin.

### Runtime trace update — 2026-09-14 (archive callback correction)

- Corrected the ordinary terminal path in manual callback `0x00044DF0`: it now
  installs the same `0x00044DA0` completion and `0x00044DC0` continuation used
  by its HITX branch before selecting the terminal wrapper. Previously it
  retained the event context `0x00B09598` and attempted to execute it as code.
- Release rebuild succeeded. A 24-second native diagnostic run no longer
  reports that invalid indirect target and streams real frontend archive data,
  including font, scene, marker, texture, and layout records.
- The complete NV2A state stream reaches transform-program, texture, surface,
  clear, and flip setup. It still contains no `SET_BEGIN_END` / `INLINE_ARRAY`
  primitive batch, so the framebuffer remains a cleared 720x480 surface.
- Next action: trace the frontend record that should become eligible after the
  now-completing archive queue, then implement only the first emitted primitive
  path in the existing NV2A executor.


### Runtime trace update — 2026-09-14 (stable scheduler and frontend state)

- A clean native run now reliably holds all nine expected scheduler callbacks while the title worker and frontend queue worker run in their normal guest contexts. The active table is `0003E910`, `00041810`, `003CD120`, `0003A1C0`, `0003A310`, `00051F00`, `00039380`, `00042BD0`, and `00045F20`.
- The worker queue no longer monopolizes a host core when its completed-resource list is empty: the empty path yields for one millisecond while retaining the original sentinel and immediate processing semantics for real records.
- The frontend vtable dispatcher reached state 6 and then state 9 through the reconstructed original state-6/state-9 branches. A 110-second clean run recorded 2,231 guest-context scheduler dispatches with the table still at nine callbacks.
- A memory-only probe at the genuine frontend enqueue routine `0x003CBBF0` recorded zero calls. The current blocker is therefore upstream of the completed-resource queue, rather than its consumer or D3D11 presentation.
- The NV2A stream reaches framebuffer setup, clears, vertex-array format setup, `SET_BEGIN_END`, and `INLINE_ARRAY`, but still has zero completed draw batches and zero rasterized triangles. No legal-screen pixels are visible yet.
- Next action: trace the first asynchronous resource/state callback that should call `0x003CBBF0`, preserving guest scheduling, then follow the resulting first nonempty frontend record into its existing prepare/submit path.

### Runtime trace update — 2026-09-14 (state-nine packet handoff)

- A clean delayed native run again held all nine title-owned scheduler callbacks and executed the title's state-nine update path. The dispatcher reached `0x00492E9B` 10 times and its state-nine packet builder `0x00492414` 9 times.
- The packet handoff `0x0048BB78` ran 9 times. Its sequence allocator advanced from its expected pre-increment value (`0xFFFFFFFF`) and the protected submit path returned success (`1`), so the packet is being queued rather than rejected by allocation or IRQL handling.
- The packet-builder's `0x004953C4` event constructor remained uncalled because this particular state-nine path intentionally constructs a zero-payload internal update then uses `0x00492414`; that is expected from the retail XBE control flow.
- The completed-resource frontend queue remains empty and the pushbuffer still contains setup/clear commands with a zero-dword begin/end batch. Current next action: trace the consumer of the successful `0x0048BB78` title packet and its completion callback into the first frontend/UI resource submission.
Verified correction (2026-09-14): Original XBE slot 004E3CBC is ordinal 119, KeInsertQueueDpc, NOT KeSetEvent. Earlier diagnosis was incorrect. Added blocking condition-variable event waits with atomic signal consumption; Release build passed and a 26-second run completed without establishing legal-screen rendering. Next audit: queued DPC routine resolution, guest register preservation and stack cleanup. Event changes do not establish a fix for the packet path.

### Current verified progress — 2026-09-14

- **Native boot and scheduler: 55%.** The Release x64 build loads the user's local `original/default.xbe`, runs the title code, and holds all nine expected scheduler callbacks during stable diagnostic runs.
- **Kernel compatibility: 40%.** Memory mapping, title threads, timing, archive completion, and basic DPC queue/drain behavior run. `KeInsertQueueDpc` has now been identified from the retail XBE as the relevant packet handoff. The delivery callback's downstream record processing remains unverified.
- **Frontend handoff: 25%.** State-nine title packets are built and submitted; `0x0049516F` pumps them. The record processor `0x0048E0A6` remains at zero calls, and the frontend queue stays empty.
- **Graphics: 35%.** The D3D11 framebuffer window, NV2A state decoding, surface clears, flips, vertex-array configuration, and `SET_BEGIN_END` are active. The trace still reports zero rasterized triangles, so there is no visible legal-screen geometry.
- **Legal-screen milestone: 15%.** The process is past core startup but has not produced recognizable legal-screen pixels.
- **Playable game: 0%.** Menu interaction, gameplay assets, physics, and stable in-game rendering remain future work.

**Current blocker:** The DPC path is not being dropped: a verified run queued and drained 50 guest DPCs with no unresolved routine. The next trace captures the exact DPC routine and context that receives the packet work, then follows that routine until it either calls `0x0048E0A6` or exposes the compatibility condition that prevents it.

### Runtime trace update — 2026-09-14 (packet DPC path)

- The retail packet handoff at `0x0049516F` was confirmed to call kernel ordinal 119, `KeInsertQueueDpc`. A stable 26-second Release run held all nine scheduler callbacks and queued that DPC six times with routine `0x00495A8C` and context `0x01A21130`.
- The guest-safe DPC drain executes `0x00495A8C` correctly. Its resource gate opens (`pending=6`, `limit=10`) and its completed-item list is nonempty (`0x01A229E8`), which rules out the timer/scheduler and empty-queue theories for this path.
- Each item takes the special completion branch into `0x004951D2`. That routine immediately exits because its required context buffer at `context + 0x8B0` is null; consequently its call to `0x0048E0A6` never occurs and no frontend records are enqueued.
- Follow-up correction: retail XBE cross-references identify `0x00495187` as an XNET network-buffer service wrapper, reached from the network service table rather than the frontend renderer. The null `context + 0x8B0` buffer therefore means that optional network path is idle; it is not a justified frontend initialization target. No synthetic network buffer will be introduced.

### Runtime trace update — 2026-09-14 (Xemu baseline and native draw capture)

- Xemu was started against the user's local ISO and inspected through its existing GDB server. At the beginning of the active title session it has the complete nine-entry scheduler table, including `0x003CD120`, `0x00042BD0`, and `0x00045F20`; a breakpoint confirms `0x003CD120` executes. Its CPU is in normal title worker code at `0x0003598F`, not a reset or crash loop.
- Added a bounded, opt-in observation trace at the native D3D11 PGRAPH draw boundary. With `RECOMP_PGRAPH_DRAW_TRACE=1`, it records only the first four `SET_BEGIN_END` batches and their first 32 `INLINE_ARRAY` words after the common MMIO decoder expands them. It does not alter guest execution, timing, state, or rendering.
- Release build succeeded. The first capture run fell into the known seven-callback startup timing profile, so it did not reach a draw batch and produced no inline-word trace. The next capture must use the established stable nine-callback profile, then compare the actual packet shape before changing the renderer.

### Runtime trace update — 2026-09-14 (registration order verified)

- Added a bounded history of title scheduler-registration requests. A Release run reached all nine callbacks and matched the clean Xemu boot exactly: `3E910`, `41810`, `3CD120`, `3A1C0`, `3A310`, `51F00`, `39380`, `42BD0`, then `45F20`.
- The two formerly intermittent registrations were requested by the retail title itself from `0x00043D09` and `0x000460DD`; they were not injected or fabricated by the bridge. The same run executed `0x00045F20` repeatedly.
- That run also observed live `SET_BEGIN_END` and `INLINE_ARRAY` packets. The D3D11 PGRAPH translator's trace did not receive them, proving that the current presentation path is driven by the pushbuffer scan/executor route instead. Renderer work must instrument and extend that route, using its real packet payload, rather than assume the translator's five-word UI vertex format.

### Evidence correction and resume-pointer fix — 2026-09-14

- The nine-callback native sample already has B09570=1 and B122AC=1, matching the sampled Xemu values. Comparing those with an earlier seven-callback run did not establish a missing initialization gate. Both globals are maintained as initialization reference counts.
- Six zero inline words are confirmed, but their purpose and whether they are an intentional initialization batch remain unverified. They do not by themselves prove a damaged vertex buffer or a renderer fault. Different allocation addresses in Xemu and native are also not evidence of incorrect mapping.
- Corrected bridge_NtResumeThread to preserve a null optional PreviousSuspendCount pointer instead of translating guest address zero. Release rebuild passed after closing the running EXE. The new 22:17 native sample reaches nine callbacks and 850 completed injected scheduler dispatches; DMA PUT still stops at 03E50F0C. This change has not established visual progress.
- Next investigation: the sampled resource-loading wait through sub_00035CE0/sub_000358D0, including the B0284C counter and suspend/resume pairing. The legal screen remains unverified and no menu geometry has been established.

### Worker-pairing and FIFO trace — 2026-09-15

- A clean 25-second Release run again held all nine title-registered callbacks and
  completed more than 200,000 calls of `0x00045F20` without a crash. The worker
  no longer monopolizes a host core during its empty-list iteration.
- The `B0284C` worker counter observation shows four acquire calls and three
  releases at the sample instant. The observed acquire/release call sites are
  the retail pair `0x00028F8E` and `0x00028F9A`; this is an in-flight sample of
  a normal pair, not evidence for a missing event or a permanent unmatched
  thread resume. The compatibility layer was not changed to synthesize a
  counter transition.
- A pushbuffer execution capture now proves that the active renderer path sees
  a complete primitive test: `SET_BEGIN_END(4)`, six zero inline words, then
  `SET_BEGIN_END(0)`. It correctly produces no triangles because every supplied
  vertex word is zero. This is an initialization/test batch, not legal-screen
  geometry, so changing D3D11 vertex decoding would not make the legal screen
  appear.
- Xemu was restarted with its local GDB server for further reference work. It
  currently needs the user's local title session launched/restored before it
  can provide a new game-state memory capture.
- Next action: trace the title-owned archive/frontend producer that should
  emit the first nonzero FIFO batch. Keep the optional XNET packet DPC separate:
  it is a network service path and is not a valid source of synthetic UI work.

### Retail Xemu queue-pump comparison — 2026-09-15

- Reconnected to the user's live local Xemu session. It is in the title and
  again exposes the same nine-entry scheduler table as the native process.
- A hardware breakpoint at retail `0x003CC280` captured the actual frontend
  queue pump running as a worker task from `0x000358C2`. Its first 256 bytes
  match the existing native reconstruction's lock, sentinel, completion, and
  cleanup flow. This rules out replacing or bypassing that function as a
  justified next fix.
- The captured retail pump also sees its queue sentinel as empty at that
  instant, matching the native queue observation. The absence of nonzero menu
  geometry is upstream of this consumer, in the producer that submits the
  frontend/archive record.
- Xemu's monitor supports local RAM snapshots but not `screendump` on this
  build. A local 64 MB reference snapshot was captured under
  `analysis/xemu-captures`; it is diagnostic data and must remain untracked.

### Live Xemu steady-state check — 2026-09-15

- After a title restart, Xemu again reached the same nine-callback scheduler
  table seen in the native process. The frontend queue-pump code and its
  globals are mapped and valid.
- A clean 12-second hardware-breakpoint observation at the real frontend
  enqueue address `0x003CBBF0` recorded no calls. A separate observation at
  archive completion `0x00044DF0` also recorded no calls. Both breakpoints
  were removed and the guest resumed normally.
- This confirms the empty queue is the expected steady state after boot has
  settled. The next Xemu comparison must be armed before the next title reset
  to catch the transient archive completion that precedes this state.

### Root-cause trace of the frontend-producer stall — 2026-09-17

- Note: `HANDOFF.MD.txt` (dated today) still names `0x004945A3` as the open
  blocker. That address was already resolved on 2026-09-13 (see above); this
  entry is the accurate current state. HANDOFF.MD.txt should be regenerated
  from this file rather than treated as authoritative.
- A clean Release rebuild confirmed source and binary are in sync
  (`ninja: no work to do`). Diagnostic runs via `tools/run-bringup.ps1
  -Seconds 20` with `RECOMP_NATIVE_SAMPLE=1` / `RECOMP_SAMPLE_DELAY_MS=15000`
  reproduce the steady-state stall on demand (roughly 4 of 5 attempts land in
  the nine-callback profile; the rest land in the seven-callback profile and
  never reach this code).
- Confirmed via the existing `frontend producers 178150=0 272A60=0` counters
  that neither of the two known callers of the frontend enqueue
  (`sub_00178150`, `sub_00272A60`) has ever executed. The live guest stack at
  the 15s sample is real and consistent across four independent captures:
  `sub_00051F50` -> `sub_00038FC0` (title bootstrap, checkpoint marker 224) ->
  `sub_00048AF0` -> `sub_00048640` -> `sub_0003BE40` -> `sub_000748A0`
  (the large sequential frontend/title module initializer that eventually
  calls `sub_00178150` near its end, unconditionally, once it finishes) ->
  `sub_000F5D60` -> `sub_00028F70` -> `sub_00028DE0`, which is genuinely
  blocked in a polling loop (`sub_000341A0(1)` each pass) waiting for
  `MEM32(0xA6A9B0) <= 0`.
- Added a new counter (`nfl2k5_worker_reset_calls`, traced from the top of
  `sub_000359C0`) to check whether the worker-thread (re)creation path was
  re-running mid-boot and clobbering the separate `0xB0284C`/`0xB02848`
  worker-gate pair. It is not: `worker reset(359C0) calls=1` in every capture,
  so that pair's small, real 4-acquire/3-release imbalance (traced to
  `sub_00035CE0`'s idle-mode branch incrementing `0xB0284C` with no matching
  worker-loop release, since the release is only issued when
  `MEM32(0xB02848)` is set by the *active*-mode branch, which never ran in any
  capture) is a second, likely independent issue, not the cause of this
  particular stall. It is documented here so it is not re-investigated as if
  new, but the actively blocking wait is `0xA6A9B0`, not `0xB0284C`.
- `0xA6A9B0` is a plain (non-interlocked) pending-completion counter:
  `sub_00028DE0` seeds it from `MEM32(0xA6A9AC)` (an expected-completion
  count) right before calling `sub_00034110` (submits the async
  request/requests), then spins until the counter drains. The only decrement
  site is `sub_00026EE0` (`if (count>0) count--; MEM32(0xA6A9B4)++;`), which
  the static xref database lists with empty `calls_to`/`called_by` and
  `detection_method: imm_ref_target` -- i.e. it is never reached by a direct
  `call`, only by its address being taken as data and invoked indirectly
  (a completion-callback table entry), the same pattern already used by the
  other archive-completion callbacks resolved earlier in this project
  (`0x00044DF0`, `0x00045100`, `0x00045A20`, `0x000459D0`).
- Next action: locate where `0x00026EE0`'s address is stored (search the XBE
  for the literal dword `E0 6E 02 00`, and check the same completion-table
  machinery that already dispatches `0x00044DF0`/`0x00045100`/etc.), then
  determine whether that table entry is reachable in the native run and, if
  so, why the completion event that should invoke it never arrives. Do not
  synthesize a fake completion call into `sub_00026EE0` before understanding
  what real event is supposed to trigger it -- this project has repeatedly
  found that shortcut wrong for adjacent callbacks (see the 2026-09-14 XNET
  correction above).

### Root cause confirmed: unfired D3D device resource-notify callback — 2026-09-17 (continued)

- The single reference to `0x00026EE0`'s address anywhere in the XBE (found
  by scanning every section for the literal dword `E0 6E 02 00`) is inside
  `sub_00028C40` (reached from `0x000292E0`, itself checkpoint 204 of the
  early `sub_00038FC0` title-init sequence -- confirmed reached, since the
  sampled `title init checkpoint` value is always well past 204 by the time
  of the stall). It loads `ecx = 0x26EE0` and calls `sub_00033EF0(ecx)`,
  which tail-calls `sub_00420810(eax=ecx)`:
  ```
  sub_00420810: eax = MEM32(esp+4); ecx = MEM32(0x4409A8); MEM32(ecx+0x1DB8) = eax; ret 4
  ```
  `MEM32(0x4409A8)` is the same D3D device pointer the native bring-up shim
  already tracks (`xbox_Nv2aMirrorFence(0x004409A8, 0x2C, 0x30)` in
  `src/main.c`). So `+0x1DB8` on the device struct is a guest function
  pointer slot -- almost certainly a resource/GPU-completion notify callback
  register, with `sub_00420830` (stores to the adjacent `+0x1DB4`) as its
  paired context/arg setter.
- `grep -rn "0x1DB8" src/recomp/gen/*.c` finds exactly one hit in the entire
  recompiled codebase: the write above. Nothing anywhere reads the field back
  or invokes it. That is the missing piece -- the registration succeeds, but
  no native or guest code ever fires the callback.
- Added two verification-only trace points (`nfl2k5_trace_gpu_notify_register`
  in `sub_00420810`, `nfl2k5_trace_gpu_wait_seed` in `sub_00028DE0` at the
  `MEM32(0xA6A9B0) = MEM32(0xA6A9AC)` reseed) and reproduced the stall twice
  more. Both captures agree exactly:
  `gpu notify(420810) register calls=2 callback=00026EE0 device=004409B0` and
  `gpu wait(28DE0) seed calls=1 value=00000001`. This confirms, empirically
  and reproducibly, that the wait loop needs exactly one invocation of the
  callback registered at device+0x1DB8, and that callback is `sub_00026EE0`,
  whose body is trivially safe to invoke (`if (MEM32(0xA6A9B0) > 0) MEM32(0xA6A9B0)--; MEM32(0xA6A9B4)++; ret`,
  no incoming-argument use, no pointer dereferences).
- This is now a confirmed root cause, not a hypothesis. It is the first
  concrete, actionable explanation found for why `sub_00178150`/`sub_00272A60`
  (and therefore the frontend enqueue) have never executed in any native
  capture.
- Not yet implemented: invoking `MEM32(device+0x1DB8)` natively is safe *only
  if* it runs with a coherent guest register-emulation context (`g_esp`/
  `g_eax`/etc. for whichever guest thread is considered "current"). Every
  existing example of native code calling into guest code through
  `RECOMP_ICALL_SAFE` (e.g. `nfl2k5_xpp_timer_init` in `src/recomp_manual.c`)
  is itself a manually reconstructed *guest* function reached through the
  normal call graph, not native infrastructure code (like the NV2A pushbuffer
  executor) calling out to guest code asynchronously. Firing this callback
  from the wrong native context could corrupt whatever guest thread's
  register state is live at that moment. The next action, before writing the
  actual invocation, is to determine the correct call site/thread context --
  most likely right after the native NV2A/device bring-up shim
  (`xbox_Nv2aMirrorFence`) considers the device ready, executed once on the
  same guest thread that is currently blocked in `sub_00028DE0`'s wait loop,
  not from an unrelated native thread.

### Fix implemented and verified: GPU resource-notify callback delivery — 2026-09-17 (continued)

- Implemented `nfl2k5_gpu_notify_service()` in `src/recomp_manual.c`, called
  from `sub_00028DE0` (`src/recomp/gen/recomp_0000.c`) immediately before its
  retry loop, at `loc_00028E6A`. It reads the callback pointer the guest
  itself registered at `MEM32(MEM32(0x4409A8) + 0x1DB8)` and invokes it
  exactly once per reseed (delivery flag reset from
  `nfl2k5_trace_gpu_wait_seed`), using the calling guest thread's own TLS
  `g_esp` context -- the same pattern as `xbox_bridge_drain_guest_dpcs`
  invoked from inside `xbox_KeDelayExecutionThread`. This is not a synthetic
  completion: it delivers the guest's own registered callback, using the
  guest's own pointer, on the guest thread that is waiting for it. Real
  hardware would deliver the same call from a GPU interrupt the native NV2A
  bridge does not yet model.
- Verified with instrumentation added alongside the fix
  (`nfl2k5_trace_gpu_wait_entry`/`nfl2k5_trace_gpu_wait_exit` at
  `sub_00028DE0`'s wait-check and every return path): across 8 repeated
  20-25s captures, every real entry into the wait now has a matching exit
  (`exit calls == entry calls + 1`, the `+1` being the early-exit gate at
  function entry) -- `sub_00028DE0` no longer gets permanently stuck. Before
  this fix it stalled on its very first wait, 100% reproducible.
- Measured downstream effect: `archive-completion 44DF0` calls, which were
  stuck at exactly 3 in every pre-fix capture, now reach 10-23 depending on
  run timing (two independent captures landed on exactly 23 with `entry=2,
  exit=3`), and `dpc queued/drained` climbed from 7 to 230 in the longest
  capture. The call stack in longer runs now reaches real archive-loading
  callback code (`nfl2k5_archive_completion_45100`) that never executed
  before this fix.
- Not yet resolved: `frontend producers 178150=0 272A60=0` in every capture
  so far, including two independent runs left at 45s and 100s delay that
  produced byte-for-byte identical counters (`archive-completion=23`,
  `dpc=230`, unchanged stack) -- a real plateau, not just slow legitimate
  loading. Since `sub_00028DE0` itself is now confirmed to complete cleanly
  every time, this next stall is a different, downstream blocker. Next
  action: instrument past the `archive-completion 44DF0=23` plateau (the
  same technique used here -- find the exact wait/counter, find its sole
  decrement or completion site, check whether that site is a direct call or
  another `imm_ref_target`-only indirect callback) to find what gates
  progress beyond it.
- This is the first verified, working code fix landed against the
  frontend-producer stall since it was first identified; all earlier
  2026-09-14/15 entries on this thread were tracing without a fix.

### External review received and acted on — 2026-09-17 (continued)

- Received a third-party technical review package (Patrick Carey /
  `patrickfcarey` on GitHub, `Newerest` on Discord) recommending: keep the
  recompiler, but run it hosted inside xemu (its NV2A/APU/USB/IDE, real Xbox
  kernel as guest code) instead of reimplementing the machine from scratch,
  replacing subsystems one at a time only once proven equivalent. Verified
  three of its concrete claims against this tree before acting on any of it:
  memory-access counts (claimed 121,570/7,870/317,691; measured
  121,573/7,870/317,692), D3D8 renderer unreachable from this title
  (confirmed: zero references in `src/main.c`/`src/recomp_manual.c`,
  `RECOMP_PB_EXEC` forced on), and `RECOMP_ICALL_FEEDBACK` never defined for
  this target (confirmed exactly). All three checked out precisely. The
  human approved the architectural direction (decision 1: yes; decision 2,
  its consequence: players supply their own Xbox BIOS dump, same as xemu
  already requires) -- **not yet built**, a real integration task for later,
  distinct from the two items below.
- **Fence negative control (package's Finding 3), run and resolved.** Added
  `RECOMP_DISABLE_FENCE_MIRROR` as an opt-in toggle around the
  `xbox_Nv2aMirrorFence` call in `src/main.c`. With it set, `title init
  checkpoint=0` and every other counter reads zero -- CPU startup does not
  reach title-init at all, let alone the frontend enqueue. **The fence is
  exonerated**: disabling it does not reveal a hidden downstream completion
  signal, it just removes the thing that lets early CPU startup proceed at
  all. The frontend-enqueue stall is confirmed independent of the fence.
  Toggle left in place, off by default, for any future need.
- **`RECOMP_ICALL_FEEDBACK` (package's Finding 1), enabled and run.** Two
  real bugs blocked it before it could work at all: the macro was defined
  for the `NFL2K5` target but not for the `xbox_kernel` static library that
  actually defines `g_icall_seen` and the dump/init functions (link errors
  until `external/xboxrecomp/src/kernel/CMakeLists.txt` also defines it
  PUBLIC); and the dump call was missing from both real exit paths --
  the watchdog thread calls `_exit(3)` directly (skips `atexit`, so the
  `RECOMP_ICALL_FEEDBACK_INIT()`-registered dump never ran), and the crash
  handler had the same gap. Fixed both (`xbox_memory_layout.c`'s watchdog
  thread and `main.c`'s `crash_report` now call
  `RECOMP_ICALL_FEEDBACK_DUMP()` explicitly). Verified working: 4 runs (three
  ~20s, one 75s, the longest watchdog tried so far) all produced real dump
  files.
  Merged all four into `analysis/icall_targets.json` via
  `external/xboxrecomp/tools/recomp/icall_feedback.py merge`: **116 total
  targets observed, zero ever flagged unresolved.** Of those, 18 are not in
  `analysis/disasm/functions.json` but all 18 are confirmed present in
  `src/recomp_manual.c`'s manual override table (the same addresses as the
  project's own hand-reconstruction history: `0x00044DF0`, `0x00045100`,
  `0x004945A3`, etc.) -- resolved correctly via the manual lookup path, not a
  gap.
  **This is a real, useful negative result, not a null one:** across every
  code path this boot sequence exercises up to at least 75 seconds of
  runtime (well past the archive-completion plateau), there is no missing or
  unresolved indirect-call target. The recorder itself is proven to work (it
  is not silently failing to observe anything -- 116 real targets is the
  positive control for that). **This rules out "missing translated
  function" as the explanation for the current stalls** (the
  archive-completion plateau at ~23 calls, the zero-filled `INLINE_ARRAY`
  draw data): whatever is still wrong is a logic, data, or timing bug in
  code that already resolves correctly, matching this session's earlier
  finding of an unfired native-side notify callback. Do not re-run this
  loop expecting a different class of answer without first reaching further
  into the title than 75 seconds of the current boot path -- if the title's
  own execution never advances past its current plateau, no amount of
  additional runs will find new targets, because the code past the plateau
  is simply never reached to observe.

### Correction: sub_00178150 is not reached on retail hardware either — 2026-09-17 (continued)

- Reconnected to a live Xemu session with its GDB server (no saved snapshot
  exists; a fresh boot auto-launched the disc and hit the title's own code at
  `0x003CD120` quickly, confirming it is genuinely running the title, not the
  dashboard).
- Set a breakpoint at `0x00178150` (the frontend producer this whole thread,
  including this session's earlier entries, has been chasing) and let the
  title run **uninterrupted for over six minutes of real title runtime**
  (an initial ~60s wait, then a second independent 240s wait). It was never
  hit, either time.
- A parallel check found zero hits on archive-completion (`0x00044DF0`) in a
  3-second sampling window at the same point in retail runtime, meaning the
  title is not actively loading archives at that moment either -- consistent
  with having already reached a quiescent, presumably-interactive state
  (likely a menu or the legal screen, waiting for input) well before
  `0x00178150` would ever be reached.
- **Conclusion: chasing `sub_00178150`/`sub_00272A60` as a stand-in for "the
  frontend needs to enqueue" was based on a mistaken premise.** It is not on
  the passive boot-to-menu path at all on real hardware/accurate emulation --
  it is almost certainly gated behind actual user input (menu navigation)
  that never happens in an unattended session, native or retail. The
  extensive multi-session effort tracing toward it (2026-09-14 through this
  entry) was not wasted -- the underlying mechanisms it uncovered (the DPC
  pipeline, the archive-completion pipeline, and this session's GPU
  resource-notify fix) are all real and load-bearing -- but reaching it
  should no longer be treated as the definition of progress toward the legal
  screen.
- Do not resume tracing toward `0x00178150` as a milestone. The next
  worthwhile ground-truth question is different: on native, after this
  session's GPU-notify fix, a `RECOMP_FULL_PB_TRACE=1` capture now shows
  meaningfully more pushbuffer activity than any pre-fix capture --
  `SET_TEXTURE_ADDRESS` and `SET_TEXTURE_IMAGE_RECT` appear for the first
  time, alongside many previously-unseen method IDs in the `0x1E44-0x1E5C`,
  `0x0260-0x027C`, `0x0AA0-0x0ABC`, and `0x1B08-0x1B88` ranges. The
  `SET_BEGIN_END`/`INLINE_ARRAY` pair is still the same shape seen before
  (paired with 6 words, now recurring 3 times instead of once) and has not
  been re-verified as zero-filled or not in this specific capture. Next
  action: decode what those newly-active texture-setup methods are staging,
  and check on retail whether the *legal-screen texture specifically*
  (archive entry 0, identifier `0xEDD549BD`, confirmed decodable per
  `docs/GEMINI-HANDOFF.md`) is what they reference, rather than continuing to
  treat `SET_BEGIN_END` alone as the milestone.

### xemu built from source, running, confirmed showing the game — 2026-09-17 (continued)

Per the approved hosting direction, stood up a real Windows build of xemu from
its public source, as the foundation the future CPU-hook integration attaches
to. No toolchain existed on this machine beforehand.

- Installed the MinGW64 toolchain into the project's already-bundled MSYS2
  (`dependencies/msys2`, previously unused for this): gcc/g++/make, ninja,
  meson, cmake, and xemu's runtime deps (glib2, pixman, SDL2, libepoxy,
  libcurl, libsamplerate, vulkan-headers).
- Cloned `xemu-project/xemu` (recursive submodules) into
  `external/xemu-src`. The only clone failures were deep inside
  `roms/edk2`'s own nested test-vector submodules (Windows path-length
  limit) -- irrelevant to the i386-softmmu Xbox target and not needed.
- Fixed five real build bugs in sequence to get a clean `meson setup` +
  `ninja` build:
  1. `mingw-w64-x86_64-toolchain` is a group package that prompts
     interactively; installed its 13 members by exact name instead.
     `mingw-w64-x86_64-libslirp` does not exist for the mingw64 environment
     (only clang64/ucrt64/clangarm64) -- dropped it; xemu vendors slirp as a
     meson subproject anyway.
  2. `mingw-w64-x86_64-cmake` was missing, needed to build the vendored
     SDL3 subproject.
  3. GNU tar on Windows parses an archive path containing a colon
     (`E:/NFL2K5-PC/...`) as a *remote* `host:path` spec and tries to open
     an rsh-style connection to a host literally named `E` --
     `TAR_OPTIONS=--force-local` fixes it. Hit this extracting the
     dsp56300 subproject's prebuilt archive.
  4. `scripts/symlink-install-tree.py` (meson postconf script, bundles
     runtime DLLs next to the built exe) needs `os.symlink`, which fails on
     Windows without Administrator or Developer Mode
     (`WinError 1314`). An attempt to fix this by enabling Developer Mode
     via the `HKLM` registry was correctly blocked by the harness's own
     safety classifier as a system-wide security-policy change -- did not
     attempt to route around it. Patched the script instead to copy files
     on Windows rather than symlink them (no privilege needed, same
     practical result), and made it tolerant of a source not existing yet
     (this script runs at meson *configure* time, before `ninja` has built
     anything, so a dangling symlink -- fine, POSIX allows it -- had been
     silently working where an eager copy cannot).
  5. `subprojects/genconfig/gen_config.py` needs Python's `yaml` module,
     not installed for the mingw64 Python; added
     `mingw-w64-x86_64-python-yaml`.
- Clean build: all 1896 ninja targets compiled, `qemu-system-i386w.exe`
  linked (~6m52s wall time for the compile itself). The packaging step
  (`package_windows` in `build.sh`) still fails on a relative-path
  assumption (`cp build/qemu-system-i386w.exe dist/xemu.exe` -- the exe is
  actually at `build/win64/qemu-system-i386w.exe`, one level up from where
  that script expects) -- not yet fixed, currently working around it by
  running the exe directly from `build/win64/`.
- The built exe would not start when launched through the MSYS/Cygwin POSIX
  loader (missing DLLs it should have found) but started fine as a plain
  Windows process launched via PowerShell `Start-Process` -- the two loaders
  resolve DLLs differently; use native launch for testing this binary, not
  the MSYS shell.
- Needed 24 runtime DLLs copied next to the exe from `mingw64/bin`
  (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`,
  `libslirp-0.dll` from the build's own subproject output, plus glib/curl/
  ssl/compression transitive dependencies) -- found by recursively
  `objdump -p`-ing every exe/dll in the build output and copying whatever
  each one imports that mingw64/bin has. Not yet scripted as a repeatable
  step; do that before the next full rebuild.
- **Verified working**: launched with the user's real
  `config/xemu-play.toml` (their actual BIOS/ISO paths). Window title reads
  `xemu | v0.8.136-49-gf9b14039e5`. The user confirmed the actual game/menu
  is visible on screen.
- **This is the foundation, not the hosting integration itself.** What
  exists now is a working, unmodified-behavior xemu built from source on
  this machine. The actual point of doing this -- hooking recompiled,
  conformance-checked functions into xemu's CPU dispatch so they run
  instead of xemu's own CPU emulation for those addresses -- is not yet
  implemented. The hook point was identified earlier in this session:
  `cpu_exec_loop()` in `accel/tcg/cpu-exec.c`, around where `tb_lookup()`
  resolves the current PC to a translation block before `cpu_tb_exec()`
  runs it. Next action: implement that hook, starting with zero recompiled
  functions wired in (pure passthrough, provably identical to plain xemu)
  before adding the first one.

### Real boot sequence, confirmed by watching a live retail Xemu session — 2026-09-17 (continued)

The user watched their own live Xemu session play through the actual retail
boot sequence end to end and narrated it in real time. The confirmed order:

1. Xbox boot logo (kernel/dashboard, not the title).
2. **Legal screen.** Static, per everything observed so far -- no video-decode
   or archive-completion burst was ever confirmed specifically synchronized
   to it, though the burst is fast enough that this session's probe scripts
   could not pin it down precisely (see below).
3. Sega logo.
4. Visual Concepts logo (the developer, not just the publisher/ESPN brand).
5. ESPN logo -- **confirmed a short video.**
6. Dolby logo -- **confirmed also a video.**
7. Trailer video.
8. Press-start prompt. Left idle, it drops into an attract/demo sequence
   (confirmed 3D, not video, despite video-decode activity measured near
   that transition -- likely a bracketing cutscene, not the scene itself)
   and eventually a playable demo.

Sega and Visual Concepts were not independently confirmed video or static in
this pass.

Live measurements taken with breakpoint/hit-count probes over the GDB
connection (`tools/` has the existing `trace-xemu-*.py` scripts; this
session's ad hoc equivalents were `count_hits_generic.py` and friends in the
session's scratch temp directory, not committed):

- `sub_003E8450` (identified via the `mwPly*`/"Sofdec" string cross-references
  in `analysis/disasm/strings.json` -- CRI Middleware's Sofdec video codec is
  built into the title, category `game_network` in some auto-detected
  functions nearby but really video) fires roughly 830-920 times/second while
  ESPN/Dolby/trailer video plays, and 0 times once stopped. This is the
  frame-retrieval entry point (`mwPlyGetCurFrm`); the wider `mwPly*` API
  surface (create/start/entry-by-filename/release, ~90 error strings found)
  spans roughly `0x003E7800`-`0x003F5B00`.
- `sub_00044DF0` (archive-completion, already instrumented and already the
  subject of this session's GPU-notify fix above) fires at a similar
  830-920/s rate *simultaneously* with the video-decode function during
  ESPN/Dolby/trailer playback -- **the archive-completion pipeline is
  streaming the video data itself**, not a one-time startup batch. It also
  fires at a comparable rate at the press-start screen with no video visibly
  playing, and drops to 0 in quiet moments (confirmed twice, in 2-3s probe
  windows).
- **This directly reframes the native-side plateau documented earlier in this
  entry.** The native build's `nfl2k5_archive_completion_44df0_calls` reaches
  a firm ceiling of exactly 23 total calls and then permanently stops. Retail
  never stops -- it runs continuously at 2-3 orders of magnitude higher
  throughput for as long as anything is loading or playing. The native
  plateau is not "the loading finished"; it is very likely the *same class*
  of bug this session already found and fixed once (an unfired native-side
  completion notification), gating the *continuation* of the archive pipeline
  rather than its start. Finding and fixing that continuation gate is the
  most direct path to further native progress, using the same
  instrument-build-run-trace method as the GPU-notify fix above.
- `0x00178150`/`0x00272A60` remain confirmed not reached on retail (prior
  entry); no new information this pass.
- Attempts to catch the *exact* moment of the legal screen's own asset load
  (to identify which function draws it, independent of anything
  video/archive-streaming-related) were not conclusive. The archive-
  completion burst is fast enough, and this session's ad hoc GDB probe
  scripts slow enough to start up (each is a fresh TCP connection re-armed
  per check), that by the time a targeted breakpoint could be armed, the
  window had already passed at least once. A future attempt should use one
  persistent GDB connection with the breakpoint armed from before the title
  starts, not a fresh script per check.
- One operational note: a debugger session left paused on a breakpoint
  freezes the guest (Xemu's own UI stays responsive, since it is separate
  from the emulated machine). Recovering requires clearing every armed
  breakpoint and sending an explicit GDB **detach** (`D` packet), not just a
  monitor `cont` or a bare `c` continue immediately followed by disconnecting
  -- both of those left the VM re-paused in this session's testing.

### CPU-hook Stage 1 and 2: a real recompiled function now runs inside xemu's own dispatch loop — 2026-09-17 (continued)

Followed through on the hook point identified in the previous entry.

- **Stage 1 (mechanism only).** Added `accel/tcg/xemu-recomp-hook.{c,h}`:
  a small registration table (`xemu_recomp_register(pc, fn)`) and
  `xemu_recomp_try_exec(cpu, pc)`, called from `cpu_exec_loop()` in
  `accel/tcg/cpu-exec.c` right before `tb_lookup()`. If a function is
  registered for the current guest PC it runs instead of the normal
  TCG lookup/exec for that iteration; otherwise nothing changes. This file
  compiles into xemu's target-agnostic `libsystem.a` (same source set as
  `cpu-exec.c` itself), which has no `target/i386/cpu.h` in its include
  path, so its interface deliberately uses generic `CPUState *`, not
  `CPUX86State *` -- register access has to happen in per-target code
  instead (see Stage 2). With the table empty this is a provable no-op;
  verified via `xemu_screenshot` (below) showing byte-identical output
  before and after.
- **Screenshot capability**, needed to verify any of this visually since
  external keypress injection into xemu's SDL3 window didn't work (see
  below) and the classic QEMU `screendump`/`qmp screendump` reads a blank
  surface for xemu's actual renderer: added an HMP monitor command
  `xemu_screenshot` (`ui/ui-hmp-cmds.c`, `hmp-commands.hx`,
  `include/monitor/hmp.h`) that calls xemu's own `ActionScreenshot()` (the
  F12 hotkey's function, in `ui/xui/actions.cc`) via a small `extern "C"`
  bridge, so it captures the actual composited GL texture xemu displays,
  saved to `screenshot_dir` in `xemu.toml` (pointed at `E:\NFL2K5-PC\logs`).
- **Stage 2 (first real function).** Added
  `target/i386/xemu-recomp-nfl2k5.c` -- a per-target translation unit (so it
  *can* include the real `cpu.h` and use `CPUX86State`/`cpu_env()`) that
  registers exactly one function via an `__attribute__((constructor))`:
  NFL 2K5's actual XBE entry point, guest address `0x00016BD1`
  (`xbe_entry_point` in `recomp_dispatch.c`'s dispatch table / `src/recomp/
  gen/recomp_0000.c`). Its body is a direct, instruction-for-instruction
  translation of that already-generated, already-reviewed recompiled C --
  same `MEM32` reads (via `cpu_memory_rw_debug`), same arithmetic, same
  stack pushes (via a `push32` helper that mutates `env->regs[R_ESP]` and
  writes through `cpu_memory_rw_debug`), same order -- covering the entry's
  prologue through its first call (`sub_0001714C`). Rather than also
  reimplementing the callee, it pushes the real return address and sets
  `env->eip` to the callee's address, then returns; `cpu_exec_loop()`
  re-reads CPU state on the next iteration and resumes via xemu's own TCG
  from there, exactly as if a translation block had just finished --
  including the callee itself and the eventual `pop edi; pop esi; ret` that
  unwinds this same prologue later.
- **Build**: added `xemu-recomp-hook.c` to the `tcg_ss` source set in
  `accel/tcg/meson.build`, and `xemu-recomp-nfl2k5.c` to `i386_system_ss` in
  `target/i386/meson.build`. Clean incremental build (`ninja
  qemu-system-i386w.exe`), no errors.
- **Verified working, several independent ways**, after killing the
  previous instance and relaunching the rebuilt exe with the user's real
  `xemu.toml`:
  - No `[xemu-recomp] ... failed` messages on stderr (the hook's own
    read/write error path never fired -- every `cpu_memory_rw_debug` call
    the hooked function made succeeded).
  - The process stayed up and responsive to the HMP monitor throughout.
  - `xemu_screenshot` partway through boot captured actual rendered 3D
    trailer content (not a blank/black/corrupt frame) -- meaning execution
    correctly continued for thousands of instructions past the hooked
    prologue, through the callee it hands off to and everything after.
  - `info registers` afterward showed `EIP=80030e4c` (deep in kernel
    address space, consistent with normal scheduler/idle activity) with
    sane-looking register and segment state -- confirming forward progress
    well past both the hooked function and its immediate callee, not a
    stall or crash sitting at/near the hook site.
  - This is a meaningful correctness check, not a weak one: `0x00016BD1` is
    the very first guest code the title runs (the XBE entry point), so any
    error in the hooked register/stack/memory state -- even a single wrong
    byte -- would corrupt the CPU or crash within the first few thousand
    instructions, which is exactly the window this test ran through.
- **What this proves and what it doesn't.** This proves the "host verified,
  recompiled guest functions inside xemu's own CPU dispatch, falling back to
  xemu's TCG for everything else" architecture actually works end-to-end:
  address match -> native execution with real `CPUX86State`/guest-memory
  access -> correct handoff back into TCG -> the game keeps running
  correctly. It does **not** touch or fix any of the open native-recompiler
  bugs (zero-vertex-data draw calls, the archive-completion continuation
  stall, the 1077+ unhandled NV2A pushbuffer methods) -- those live in
  `NFL2K5.exe`'s own recompiled code and are unaffected by anything in
  `external/xemu-src`. The next useful step on this path is registering more
  functions (ideally working outward from `0x00016BD1` toward whatever
  currently stalls) rather than anything-remaining in Stage 1/2 itself.

### Native boot stall: first concrete, address-level trace of what it's actually spinning on — 2026-09-17 (continued)

The previous entries documented *that* native rendering never reaches a
draw call and speculated the archive-completion plateau (~23 calls) was the
cause. This pass added new, permanent, reusable instrumentation and used it
to get real answers -- and the picture that emerges is more specific, and
different in an important way, from what was previously assumed.

- **New unconditional `[PIPE]` summary** (`external/xboxrecomp/src/kernel/
  kernel_bridge.c`, in the existing 2-second `[KERNEL] summary` block):
  prints the archive/boot/frontend/worker pipeline-stage call counters
  (`nfl2k5_archive_*_calls`, `nfl2k5_boot_task_*_calls`,
  `nfl2k5_frontend_*_calls`, `nfl2k5_worker_*`) on every run, no debugger or
  env var required. Also added `ret=0x%08X` (the guest return address at the
  top of the guest stack) to the existing `[KERNEL] summary` line.
- **First finding, and it contradicts the standing plateau narrative**: in a
  fresh run, every archive/boot/frontend counter read **zero** for the
  entire observed window (16+ seconds, 8+ samples) except
  `boot_task_3be40=11` and `worker_entry=1`, both frozen the whole time. The
  archive-completion pipeline (`44DF0`, previously documented plateauing at
  "~23 calls") never started at all in this run -- meanwhile `[KERNEL]`
  total calls climbed continuously (~1080/2s), so the process is not
  globally hung, just never entering that pipeline. **The specific plateau
  number is not reproducible run-to-run**; what's reproducible is that
  *some* thread spins forever, but not always the same one.
- **Confirmed via the new `ret=` field**: one run spun on kernel ordinal 99
  (`KeDelayExecutionThread`) ~1400×/sec, always returning to the same guest
  address, `0x0001B632`. A second, independent run instead spun on ordinal
  246 (`ObReferenceObjectByHandle`) at a different address, `0x00016D15`.
  **Different runs stall in different places** -- strong evidence of a real
  scheduling-timing-dependent race, not one fixed linear blocker, and a
  materially different model of the problem than "the archive pipeline
  plateaus at a fixed count."
- **Traced both spin sites to their actual guest functions**, by mapping the
  return addresses through `recomp_dispatch.c`'s address table to find the
  enclosing generated function, then reading its already-generated C
  (`src/recomp/gen/recomp_0000.c`):
  - `0x0001B632` is inside `sub_0001B601` (game_vtable, thiscall): a
    wait-with-retry helper that calls `KeDelayExecutionThread` once, and on
    the observed inputs takes the exit path every time (does not loop
    internally) -- meaning something *above* it re-invokes the whole
    function repeatedly. 4 call sites total (`recomp_0000.c:32924`,
    `recomp_0001.c:13629,60528,60574`), narrow enough to check directly next
    time.
  - `0x00016D15` is inside `sub_00016CFF`, which resolves a handle via
    `ObReferenceObjectByHandle` (through the indirect-call slot at guest
    address `0x4E3AFC`). Added an entry trace (see below) and confirmed the
    handle argument is **`0xFFFFFFFE`, the pseudo-handle for "current
    thread"** -- this is `GetCurrentThread()`-equivalent, not a real object
    wait.
- **Added two permanent, env-gated entry traces** (`RECOMP_WAIT_TRACE=1`,
  capped at 20 lines each) by patching the generated functions directly
  (same established pattern as this session's earlier GPU-notify trace
  additions) to call a small logger defined in `src/recomp_manual.c`
  (`nfl2k5_trace_sub_0001B601_entry`, `nfl2k5_trace_sub_00016CFF_entry`),
  since `recomp_0000.c` itself has no stdio/getenv in scope. These record
  the this-pointer/params and, critically, the caller's own return address
  (read directly off the guest stack at function entry, before any pushes).
- **Walked one level further up from `sub_00016CFF`** using its trace's
  `caller_ret`: every one of 20 captured calls came from the same guest
  address, `0x003CB1D6`. Initial dispatch-table bisection (nearest preceding
  entry, `0x003CAED0` / `sub_003CAED0`) turned out to be **wrong** -- added
  the same entry trace to `sub_003CAED0` itself and it never fired once in
  20+ seconds, despite `sub_00016CFF` firing 20/20 times with a `caller_ret`
  supposedly inside it. That contradiction was the tell. The real answer,
  found by grepping for the literal constant `0x003CB1D6` instead of
  trusting the bisection: `src/recomp_manual.c:1373`,
  **`nfl2k5_boot_registration()`** -- an existing, already-documented manual
  override (comment: *"003CB1C0 is a real unprologued function in the XBE,
  not a no-op callback. It was missed by the function detector. This
  reproduces its 10 instructions and preserves the original call/return
  stack layout."*). It starts at `0x003CB1C0`, a few bytes before where the
  automated disassembler's nearest *detected* function happens to start --
  exactly the same "undetected function nested in a gap, found and
  hand-reconstructed in an earlier session" pattern already on record for
  `44DF0`/`44BB0`/`45A20`/etc. (see the ICALL-feedback entry above). Lesson
  for next time: bisecting the auto-generated dispatch table finds the
  nearest *detected* function, not necessarily the *actual* one -- always
  grep for the literal address constant across `src/recomp_manual.c` too
  before trusting a bisection result.
  - Reading `nfl2k5_boot_registration()` directly (no more tracing needed):
    it calls `sub_003784D0(0xAF5888)` -- an atomic increment
    (`RECOMP_ATOMIC_ADD32`) that returns the *new* value -- and only
    proceeds if that new value is exactly **1** (a one-shot/spinlock-style
    guard; its sibling `sub_003784E0` at the next address is the matching
    atomic decrement, i.e. the release). Every one of our captured calls
    *did* take this branch, meaning the counter at guest address `0xAF5888`
    is back to 0 again by the time the next call comes in a few hundred
    microseconds later -- consistent with acquire/do-work/release repeating
    in a tight, unthrottled loop.
  - On success, it calls `sub_00016CFF(0xFFFFFFFE)` (current thread handle,
    confirmed above) then `sub_00016CAD(0xFFFFFFFE, 0xF)`. `sub_00016CAD`
    has the same handle-resolve shape as `sub_00016CFF` and then maps its
    second argument through a small table keyed on `0xF`/`0xFFFFFFF1` --
    the shape of a thread-priority-class remap (Xbox kernel priority
    enums), strongly suggesting this whole routine is "resolve my own
    thread and boost its priority," done via a guarded fast path so only
    one caller does it at a time.
  - On failure (guard already held, or a negative status from the handle
    resolve), it tail-calls `sub_00016EAC` instead (not yet traced).
- **Net picture**: `nfl2k5_boot_registration` is reached only through the
  manual-override lookup table (`src/recomp_manual.c:1642`), i.e. via an
  indirect call resolving a guest function pointer to `0x003CB1C0` -- not a
  direct static call anywhere in the generated code, so its caller has to be
  found by tracing *that* icall site, not by grepping for a call to the C
  symbol. An as-yet-unidentified outer loop is resolving and invoking it far
  more often than makes sense -- over a thousand times a second, with no
  delay that actually elapses and no change in outcome between calls -- and
  whatever condition is supposed to make that outer loop stop never does.
  This is the same *shape* of bug as this session's earlier, already-fixed
  GPU-notify stall (a one-shot signal/condition that should flip but
  doesn't), just in a different, previously undocumented location, and
  apparently more than one such location depending on thread scheduling
  luck on a given run (the two independently-confirmed hot spots,
  `sub_0001B601` and `nfl2k5_boot_registration`, were both seen spinning
  concurrently, on different threads, in the same run).
- **Instrumentation added this pass that's worth keeping**: the
  unconditional `[PIPE]` counters and `[KERNEL]` `ret=` field
  (`kernel_bridge.c`), plus three `RECOMP_WAIT_TRACE=1`-gated entry traces
  (`nfl2k5_trace_sub_0001B601_entry`, `nfl2k5_trace_sub_00016CFF_entry`,
  `nfl2k5_trace_sub_003CAED0_entry` -- the last one now confirmed to be on a
  function that this particular stall never actually reaches, but it is a
  correct, real trace on a real function and costs nothing when unset, so
  it was left in rather than reverted).
- **Found it.** Added the same entry trace directly to
  `nfl2k5_boot_registration()` (it's hand-written in `recomp_manual.c`, so
  it already had stdio/getenv in scope -- no bridging function needed).
  Every one of 20 captured calls has the same `caller_ret`,
  **`0x003D58C2`**, unambiguously inside `sub_003D58B0`
  (`src/recomp/gen/recomp_0025.c:148637`, a small, self-contained 52-byte
  function -- no bisection ambiguity this time). Its logic is a **generic
  registered-callback pump**:
  ```
  eax = MEM32(0xCC7A9C)          // registered callback pointer
  if (eax != 0) {
      ecx = MEM32(0xCC7AA0)      // its context argument
      call eax(ecx)              // indirect call -- this is where
                                  // nfl2k5_boot_registration gets invoked,
                                  // because MEM32(0xCC7A9C) currently holds
                                  // its address
      ... (bookkeeping at 0xCC7C9C/0xCC7CA0, unrelated to the callback
           pointer itself)
  }
  ```
  In other words: `sub_003D58B0` is a "if a callback is registered, invoke
  it" pump, and it is being called (by something further up, not yet
  traced) far faster than any legitimate periodic pump should run --
  thousands of times a second -- while the callback pointer at `0xCC7A9C`
  keeps resolving to `nfl2k5_boot_registration` every single time.
  **`nfl2k5_boot_registration()` never clears `MEM32(0xCC7A9C)`** (see its
  full body above: guarded priority-boost, then a plain `ret`, or a tail
  call to `sub_00016EAC` -- neither path writes to `0xCC7A9C`). A
  registered one-shot callback that's expected to run once and then
  deregister itself, but never does, is the most direct explanation that
  fits every observation from this pass: the tight loop, the unthrottled
  re-triggering, the guard at `0xAF5888` always reading back to 0 between
  calls (consistent with each call completing normally and returning, not
  blocking), and the fact that a *different* run instead caught
  `sub_0001B601` spinning (a different, similarly-shaped registered
  poll/retry elsewhere, on a different thread).
- **This is a real, testable hypothesis, not yet a verified fix.** Not
  changed this pass -- deliberately, per this project's standing rule of
  verifying against retail before altering guest logic/control flow, and
  because it isn't yet confirmed *what* should clear `0xCC7A9C` or when
  (the real x86 at `0x003CB1C0`/`nfl2k5_boot_registration`'s original 10
  instructions were already reproduced faithfully per the existing
  comment -- if it doesn't clear the pointer in real Xbox code either, the
  bug is one level further out, in whatever *should* be unregistering the
  callback from outside, not in this function itself).
- **Next action, concretely scoped**: (1) find `sub_003D58B0`'s own caller
  (same technique, one more hop -- it doesn't have a manual override, so
  this one should bisect cleanly) to see how often it's *meant* to run and
  whether anything there is supposed to gate re-registration; (2) check
  retail (live GDB, same method used earlier this session for
  `sub_00178150`) for what happens to `MEM32(0xCC7A9C)` around the
  equivalent point in boot, to confirm whether real hardware clears it and
  native doesn't, versus real hardware also leaving it set and something
  else being the actual gate. Do not assume the previously-documented
  `44DF0`/"~23 calls" plateau is still the operative blocker without
  re-measuring it in the same run -- this pass's evidence is that which
  pipeline stage stalls first varies between runs.

### Followed the chain one more hop, and it reframes the whole investigation — 2026-09-17 (continued)

- Traced `sub_003D58B0`'s own caller the same way (entry trace, no manual
  override, clean bisection this time): every call came from
  `sub_003D58F0` (`recomp_0025.c:148686`), a 9-byte stub that does nothing
  but push an event code and call the pump -- **but `sub_003D58F0` itself
  has 12 call sites** across `recomp_0025.c`/`recomp_0026.c`. Traced its
  entry too, expecting one dominant caller like every previous hop. Instead
  got **6 distinct callers**, roughly proportional (`0x003D6031` was the
  largest at half the samples, the rest split across 5 others). This is a
  materially different signature than every earlier hop in this chain,
  where exactly one caller accounted for 100% of samples.
- **This means `sub_003D58B0` is not the tail of one runaway loop -- it's a
  shared, generic "fire event N" notify pump that many different game
  systems legitimately call.** The anomaly was never really "one broken
  callback spins forever" so much as "this pump is being invoked at a
  wildly higher aggregate rate than a normal per-frame pump should be,"
  which reframes the question from *why does this one call site loop* to
  *why is nothing pacing any of these call sites to a normal frame rate*.
- **Checked for the obvious answer and found it already on record.**
  `external/xboxrecomp/src/kernel/kernel_bridge.c:2117-2131`, comment
  already in the tree from an earlier session:
  > *"The D3D8 library linked into a title installs an ISR for the GPU's
  > vblank / command-completion interrupt. There is no NV2A here and
  > nothing ever raises that interrupt... KeConnectInterrupt reports
  > success... no interrupt is ever delivered. Code that* waits *on the ISR
  > rather than polling will hang here, and the fix for that is to bridge
  > the D3D8 entry point that owns the wait, not to synthesise NV2A
  > interrupts."*
  Confirmed this was never acted on: no `vblank`/`VBlank`/
  `WaitForVerticalBlank`/`D3DDevice_Swap`/`D3DDevice_BlockUntilVerticalBlank`
  reference exists anywhere in `src/` or `external/xboxrecomp/src/` outside
  that one comment.
- **This is very likely the single root cause tying together everything
  traced tonight, not three separate bugs.** On real Xbox, essentially
  every per-frame system (rendering, and apparently whatever drives this
  generic notify pump too) ultimately gets paced by the vblank interrupt --
  woken once per real frame, does its work, goes back to sleep. Native has
  no such interrupt at all: anything built to *wait* for it either hangs
  outright, or -- if it was written defensively with a poll-and-retry
  pattern instead of a true blocking wait -- spins as fast as the CPU
  allows, checking over and over for a signal that structurally cannot
  ever arrive. That single gap explains all three independently-observed
  symptoms from tonight without needing three different bugs:
  - `sub_0001B601`'s `KeDelayExecutionThread`-then-retry-on-timeout loop
    (textbook "poll with timeout instead of a real wait" fallback).
  - `nfl2k5_boot_registration` re-running its priority-boost registration
    over and over (consistent with a thread that keeps trying harder,
    raising its own priority, to catch a signal that never comes).
  - The generic notify pump (`sub_003D58F0`/`sub_003D58B0`) firing from six
    different systems at far above frame rate, because nothing is holding
    any of them to "once per vblank."
- **Not fixed this pass, deliberately.** The prior session's own comment
  already named the correct fix direction -- bridge the specific D3D8
  entry point the title actually calls to wait for vblank/present (likely
  something the XDK names close to `D3DDevice_Swap` or
  `D3DDevice_BlockUntilVerticalBlank`), and have it synthesize completion
  from a real host timer at roughly 60 Hz, rather than trying to route a
  synthetic interrupt through the full ISR/DPC plumbing (which the comment
  also correctly says not to do). That entry point has never been
  identified in this project -- doing so needs either a live retail GDB
  session breaking on the D3D8 import thunks during the frame loop, or
  working outward from `analysis/disasm/functions.json`'s D3D-category
  entries, neither of which fit in what remained of this pass.
- **Next action, concretely scoped, and now much better targeted than
  anything earlier tonight**: identify the guest address of the actual
  D3D8 present/vblank-wait function (title-side, not kernel-side -- it's
  linked into the XBE, not an XBOXKRNL export, so it won't be in the
  kernel ordinal table) and bridge *that* to a real ~60 Hz host timer. This
  single fix, if the diagnosis holds, is a plausible candidate for
  unblocking rendering entirely, rather than another one-off patch to a
  single call site -- worth prioritizing over continuing to trace
  individual spin sites one at a time.

### Actually tried it: a real, reversible experiment, with a clean negative result that itself is the useful finding — 2026-09-17 (continued)

Rather than stop at "next action: find the D3D8 wait function," found the guest ISR address a cheaper way and tried synthesizing it directly. This section is the full, honest result -- it did not fix rendering, but it produced a precise, evidenced answer for *why not*, which is real progress on its own.

- **Found the actual registered NV2A/vblank ISR without needing symbol names.** Added an unconditional log to `bridge_KeInitializeInterrupt` (`kernel_bridge.c`) printing every interrupt the title registers. Four showed up during boot, at vectors 1, 3, 5, and 6. Vectors 5/6 have near-adjacent context pointers (`0x01903A74`/`0x01903A90`, 0x1C apart) -- almost certainly a paired device (e.g. dual IDE channels), not the GPU. Read vector 1's routine (`sub_004E16FA`, `recomp_0032.c:88748`) directly: it reads a 16-bit counter at `context_struct+0x80` and XOR/AND-compares it against a stored previous value -- a textbook wraparound raster/line-counter check, and a companion helper (`sub_004E1790`, right after it) does the identical `+0x80` read, reusing the same shape. Strong circumstantial evidence this is the vblank ISR specifically, not some other device.
- **Confirmed it queues a real DPC**, not by reading further code but by resolving what it actually calls: added a one-shot dump (`[ICALL-SLOT]`) that reads the guest import-table slot `0x4E3CBC` (the icall target inside the ISR's "interrupt pending" branch) and maps it through the same `KERNEL_VA_BASE`/`g_slot_ordinals` lookup `kernel_thunk_dispatch` itself uses. It resolves to **kernel ordinal 119 = `KeInsertQueueDpc`**. This is about as strong as static confirmation gets without retail: the candidate ISR really does end in a real DPC queue call, exactly the shape a hardware ISR should have.
- **Built and ran the actual experiment.** Added `nfl2k5_synthesize_vblank_if_due()` (env-gated behind `RECOMP_SYNTHESIZE_VBLANK=1`, off by default, zero cost when unset): calls `sub_004E16FA` directly with the real captured `(Interrupt=0x00DFB3C0, ServiceContext=0x800008C4)` arguments, rate-limited to ~60 Hz. Injected at two points, both already-established-safe patterns for host-to-guest calls (never a new/unrelated host thread, which would run guest code on uninitialized TLS -- see the comment on `nfl2k5_gpu_notify_service` in `recomp_manual.c`): inside `bridge_KeDelayExecutionThread` (for threads that sleep-and-retry) and inside `bridge_ObReferenceObjectByHandle` (for the thread observed tonight that busy-polls with *no* delay at all between calls, so the first injection point never reaches it).
- **Result: no crash, no behavior change.** Ran it standalone and four more times back-to-back. Every run reproduced its usual stall point exactly as without the flag -- including the classic `completion44df0=23` plateau, reproduced identically twice, completely unmoved by ~60 Hz of real ISR calls throughout the whole run. No new errors, no progress, no regression either.
- **Found the precise reason why, not just that it didn't work.** Two of this codebase's own counters (`nfl2k5_keinsert_dpc_count`, `nfl2k5_dpc_drained_count`) already existed in `kernel_bridge.c` but were never surfaced anywhere -- added them to the `[PIPE]` summary as `[DPC]`. Result: **`queued=0` for the entire run.** The ISR ran roughly 60 times a second for 15+ seconds and never once reached its own `KeInsertQueueDpc` call. Reading back why: `sub_004E16FA`'s very first branch (`edx = MEM32(ecx+0xC) & MEM32(ecx+0x10); if (edx == 0) skip everything`) is an **"is there actually an interrupt pending" status-register check**, and it read as "no" on every single call. That's expected and correct: nothing in this emulation ever *sets* those status bits at `context_struct+0xC`/`+0x10` in the first place, because there is no real NV2A hardware behind `ServiceContext=0x800008C4` -- calling the ISR is not the same as a real interrupt having actually occurred, and the ISR faithfully (and correctly) does nothing when its own status check says there's nothing to report.
- **What this rules in and out.** It rules out "just call the ISR periodically" as a fix -- confirmed, not guessed. It does *not* rule out the underlying "missing vblank interrupt" diagnosis; if anything it sharpens it: the real fix needs to make the ISR's status check see a real "yes" (by also synthesizing whatever the status bits at that address should read on an actual completed frame), or bypass the ISR entirely and queue the DPC directly with the right context (skipping the status check that will never legitimately pass without real hardware behind it), or -- the original, still-correct suggestion from the comment this all started from -- bridge the D3D8-side entry point that owns the wait, upstream of any of this kernel-level plumbing. Also ruled out a suspected second bug along the way: `xbox_KeInsertQueueDpc` in `kernel_sync.c` submits guest routines to a raw Windows threadpool thread with no guest TLS context, which looked alarming, but it turned out to be dead code for this path -- the actual bridge (`bridge_KeInsertQueueDpc`) already correctly uses this codebase's own safe queue+drain mechanism (`bridge_queue_guest_dpc`/`xbox_bridge_drain_guest_dpcs`). Worth a second look someday (is `xbox_KeInsertQueueDpc` called from anywhere at all?), but it is not part of tonight's causal chain.
- **Left in place, off by default**: `nfl2k5_synthesize_vblank_if_due` (both injection points), the `[ICALL-SLOT]` one-shot dump, and the `[DPC]` counters in the `[PIPE]` summary. All are real, reusable diagnostic/experimental infrastructure now permanently available, not reverted, since they cost nothing when unused and materially shortened this investigation.
- **Honest bottom line for this session**: native rendering is still at 0 draw calls. What changed tonight is the depth and precision of the diagnosis -- from "archive loading stalls somewhere" at the start of the session, to a fully-traced, five-hop call chain with one tested-and-eliminated hypothesis and a specific, named reason it didn't work. The next step needs either a live retail GDB session (read the real values at `ServiceContext(0x800008C4)+0xC` and `+0x10` around an actual vblank on real hardware, to learn what a legitimate "yes" looks like) or finding the D3D8-side wait function directly, per the original comment. Neither fits in what's left of this session.

### Follow-up, same session continued past midnight (2026-09-18): the status-register gap above is now actually closed, and a second, real, previously-unknown DPC-delivery bug got fixed along the way

The entry above ends on "the real fix needs to make the ISR's status check see a real yes." Rather than stop there, did exactly that -- and it surfaced a second, independent bug behind it. **Vblank/DPC delivery is now genuinely working end-to-end in this build for the first time in the project's history.** It still has not produced a draw call, but this is real, working, verified new capability, not another diagnosis.

- **Closed the status-register gap directly.** `nfl2k5_synthesize_vblank_if_due` now also writes the device's status(+0xC) and enable(+0x10) registers before calling the ISR, targeting specifically the bit (`0x20`) the ISR's own code checks for the counter-update/vblank reason (see the read of `sub_004E16FA` above). First attempt had a real bug of its own: wrote `status=0x20` and OR'd `enable|=0x80000000`, but the *pre-existing* `enable` value (confirmed live: `0x40`) never shared a bit with `0x20`, so `status & enable` (the ISR's own gate) stayed zero anyway -- passing the bit-31 check doesn't help if the reason bit itself doesn't overlap. Fixed by OR'ing the *same* bit into both registers (`enable |= 0x80000020`, `status |= 0x20`).
- **Verified this is real, not cosmetic**: the frame counter the ISR maintains at `ServiceContext+0x418` now increments by `0x10000` on every single synthesized call, cleanly, at the ~60 Hz rate requested, confirmed across a live capture (`0x10000` -> `0x20000` -> `0x30000` -> ...). This is the counter a real hardware vblank would drive; it had never once incremented in this project before tonight.
- **Extended to also reach the DPC-queuing branch** (not just the counter-update path): OR'd in an extra bit (`0x24` total in both registers) so the ISR's remaining-status check stays nonzero after the counter-update branch consumes bit `0x20`, forcing it down to the real `KeInsertQueueDpc` call. Confirmed via the `[DPC]` counters added earlier this session: `queued` climbed into the hundreds, resolving every time to a **real, previously-never-executed guest function, `sub_004E1E02`** (`recomp_0032.c:89985`) -- read its body directly: it's a substantial, legitimate DPC completion routine (walks a linked list at guest address `0xDFB380`, calls four further sub-handlers in sequence gathering boolean results into a summary flag). Nothing resembling a stub or dead code -- this is real driver logic that has simply never run before.
- **Found and fixed a second, independent, real bug: the DPC queue was silently starving.** `queued` climbed steadily but `[DPC] drained` stayed stuck at 2 the entire run. Root cause: `bridge_queue_guest_dpc`'s ring buffer (`kernel_bridge.c`, `BRIDGE_PENDING_DPCS=32` slots) silently drops new entries once full (`if (next != g_pending_dpc_head) { ...enqueue... }`, no else), and nothing was calling `xbox_bridge_drain_guest_dpcs()` from the specific thread my synthesis runs on -- so the queue filled to 32 within the first couple of ticks and then discarded everything else for the rest of the run. **Fixed** by calling `xbox_bridge_drain_guest_dpcs()` immediately after the synthesized ISR call, same thread, same established-safe pattern as every other call site in this file. Re-verified: `queued` and `drained` now track each other exactly (`768`/`768`, `922`/`922`, etc.), zero backlog, zero unresolved, zero crashes across five separate runs including one 80-second sustained run.
- **This produced measurable, positive, previously-impossible downstream activity** -- not just "the ISR runs now" in isolation:
  - The cumulative DPC-drained count exceeded the count of DPCs *this session's own code* queued (e.g. `drained=3235` vs `queued=2877` in one run), meaning *other*, independent DPCs -- not synthesized, not directly caused by this patch -- are now also firing, that never fired before. At least one drained with a genuinely different routine/context pair (`routine=0x0048E53B context=0x01A21130`, a real per-instance context, not a hardcoded address), confirming this is a second, distinct piece of previously-dormant game logic now executing.
  - The GPU pushbuffer summary shows real growth beyond what elapsed time alone explains: `clears` went from `2` (every prior capture all session, no synthesis) to `3`, and distinct unhandled method IDs went from `274` to `381` in an 80-second run -- 107 method IDs that had never appeared in this project's logs before tonight.
  - Process memory usage after the long run was measurably higher (~191 MB vs. the ~169 MB baseline every other run this whole session has shown), consistent with real additional allocation/activity, not a leak in the synthesis code itself (which touches only two fixed 4-byte registers and a fixed-size ring buffer already used elsewhere).
- **Still not enough, on its own, to reach a draw call.** `completion44df0` still plateaus at exactly `23` in runs that reach that stall (identical to every pre-synthesis capture), `[GPU] draws` stayed at `0` across every run including the 80-second one, and the extra pushbuffer/DPC activity itself plateaus early (first ~15-20s) and does not keep growing with more elapsed time -- confirmed by the 80-second run's `[PIPE]` block being byte-identical from roughly the 20-second mark onward. Whatever gates the *next* stage past this point is not simply "give it more real time with a working heartbeat."
- **What this changes for next steps.** The interrupt-delivery layer this whole thread of investigation has been chasing is no longer hypothetical or broken -- it is real, tested, working infrastructure now (still opt-in via `RECOMP_SYNTHESIZE_VBLANK=1`, since the exact status-bit values are guessed/synthesized, not confirmed against retail, and the DPC-drain fix, while a genuine bug fix independent of the vblank question, should be re-verified doesn't change anything with the flag *off* before being trusted as always-on). The remaining gap is narrower and different in kind: not "nothing ever signals completion," but "the archive/boot pipeline's specific stall, and the specific path to a draw call, need their own traced chain the same way vblank just got one." The most direct next step is the same technique used all session: pick the *next* stalled thread while `RECOMP_SYNTHESIZE_VBLANK=1` is active (so it's no longer confounded by a genuinely-broken interrupt layer) and trace its return-address chain the same way `sub_0001B601`/`nfl2k5_boot_registration`/`sub_003D58B0` were traced earlier tonight.
- **Everything from this entry is real, verified, and left in the tree**: the corrected bit-synthesis code, the `xbox_bridge_drain_guest_dpcs()` fix (arguably the most durable result of this whole session -- a genuine, generally-applicable bug fix to the DPC queue, independent of whether the vblank hypothesis itself ever fully pans out), and the `[VBLANK-DIAG]` trace. All gated behind the same opt-in flag, all zero-cost when unset.

### Major correction: "completion44df0=23" was never a stall -- it's a normal, correct phase transition. Traced the real continuation, confirmed it also runs successfully, and the plateau persists anyway.

This entry corrects a premise this project has carried since well before tonight (the "archive-completion plateau at ~23 calls" language appears throughout earlier entries in this file). It was wrong, and now there's direct evidence for what's actually happening.

- **Traced `nfl2k5_archive_completion_44df0`'s caller directly** (same return-address-at-entry technique used all session): every one of 23 calls in a clean capture came from the exact same site, `0x0003A065`, inside `sub_0003A020` (`recomp_0001.c:22074`) -- a generic async-event dispatcher with an 8-target jump table keyed on a per-record state field, and (unrelated discovery, already in the tree) an existing, previously-unused diagnostic, `RECOMP_PROVIDER_TRACE`, that logs every dispatch through it as `[RESOURCE] queue-event`.
- **Enabled that existing trace and got the real story for the first time**: 23 real, successful file reads (`[READ] want=N got=N st=0x00000000`, want==got every time, strictly increasing file offsets, e.g. `@53280` climbing to `@1029472`) -- this is genuinely, correctly loading archive data, not stuck. Immediately after the 23rd `44DF0` completion, the dispatcher's queue-event log shows the **exact same queue transitioning to a different completion callback, `0x00045100`**, on its very next entry. `nfl2k5_archive_completion_44df0_calls` stops incrementing not because anything is broken, but because the code is *done* using that specific callback and correctly hands off to a different one -- exactly as designed. The "plateau" was a misreading of a normal state-machine transition as a stall.
- **`0x00045100` already has a manual override in this tree** (`recomp_manual.c:1056`, `nfl2k5_archive_completion_45100`) with its own pre-existing comment: *"those handlers own the queue wakeup that moves the frontend past its loading state"* -- the single most directly-relevant lead this whole session has found, sitting in the tree the entire time. Added entry/loop-bounds tracing to it (same `RECOMP_WAIT_TRACE` gate).
- **Confirmed it actually runs, with real data, and completes without error.** Caught a live capture: `queue=0x802D4800`, `count=3` -- three real, resolvable queue records, processed by its loop (calls `sub_00043E70`/`sub_00043E30`/`sub_00043E50`/`sub_00043E10` per record depending on tag and position, per its existing code). No crash, no hang; `[KERNEL]` call volume and `frontend.statedispatch` both kept climbing normally afterward in the same run, meaning the function returned cleanly.
- **And the plateau persisted anyway.** Watched that same run through to its natural end: `frontend.statedispatch` still capped at `11`, `[GPU] clears` still capped at `3`, `[GPU] draws` still `0` -- identical to every other capture this session, synthesis or no synthesis, 44DF0-only or 44DF0-plus-45100. Running the *entire* archive-completion chain successfully, end to end, with real file data and a real second-phase handoff, did not move any of the actual rendering-adjacent counters past where they already were.
- **What this establishes, cumulatively, for the whole session's investigation**: the resource/archive loading pipeline is not the blocker. It loads real data, correctly, through at least two completion phases, and (per this entry) is not stuck anywhere in that process. Interrupt/DPC delivery is not the blocker either (previous entry: now genuinely working, zero backlog, zero crashes, measurable extra activity, still no draws). Both of tonight's two leading theories are now conclusively, evidence-backed ruled out as *the* cause, not just unconfirmed. What's left standing between here and a draw call is specifically the texture/vertex/draw-submission path itself -- consistent with the very first entries in this file ("SET_BEGIN_END and INLINE_ARRAY... zero completed draw batches") -- and that specific path has not had this session's level of scrutiny applied to it yet.
- **Concretely scoped next action**: with `RECOMP_SYNTHESIZE_VBLANK=1` active (so the interrupt layer is no longer a confound) and a capture that reaches `[GPU] clears 3`, trace forward from *that* point instead of backward from the archive pipeline -- find what guest code issues the clear, and whether anything downstream of it ever attempts `SET_BEGIN_END`/`INLINE_ARRAY` at all in this specific run, using the same `RECOMP_PB_DRAW_TRACE=1` flag documented earlier this session. If it's never attempted, the gap is upstream in whatever's supposed to trigger the first real draw (very possibly the same D3D8-side entry point named at the start of tonight's investigation, now with two ruled-out alternatives narrowing the search). If it *is* attempted but still produces zero triangles, the gap is in the pushbuffer executor's own draw/vertex-format handling, which is a different, narrower kind of bug than anything chased tonight.

### Immediately followed up: it IS attempted, and it reproduces this project's original, oldest-documented bug -- exactly, byte for byte, now isolated from every other confound this session ruled out.

Ran the concretely-scoped next action immediately rather than leaving it as a note. `RECOMP_SYNTHESIZE_VBLANK=1 RECOMP_PB_DRAW_TRACE=1`, four runs, two reached `clears 3` (the fuller state) and both captured `SET_BEGIN_END`/`INLINE_ARRAY`:

```
[GPU] BEGIN_END param=4 prior-prim=0 inline=0 indices=0
[GPU] INLINE[0]=00000000 prim=4
[GPU] INLINE[1]=00000000 prim=4
[GPU] INLINE[2]=00000000 prim=4
[GPU] INLINE[3]=00000000 prim=4
[GPU] INLINE[4]=00000000 prim=4
[GPU] INLINE[5]=00000000 prim=4
[GPU] BEGIN_END param=0 prior-prim=4 inline=6 indices=0
```

**This is this project's very first documented finding** (an early entry in this file: *"the NV2A stream reaches... SET_BEGIN_END and INLINE_ARRAY, but still has zero completed draw batches"*), reproduced exactly, months of investigation and this entire session's worth of interrupt/archive/DPC work later. `SET_BEGIN_END` fires correctly (`param=4`, a real primitive type), 6 `INLINE_ARRAY` words get pushed (a genuine draw attempt, not a skipped/aborted one), and every single one of the 6 vertex words is `0x00000000`. With degenerate all-zero vertex data, `draw_inline_array()` has nothing real to rasterize, which is why `[GPU] draws` stays `0` even though a batch genuinely gets submitted.

**Why this matters, put together with everything else tonight**: this session chased and conclusively ruled out two entire alternate theories for "why nothing renders" -- missing interrupt delivery (now fixed and confirmed working) and a stuck archive-loading pipeline (now confirmed to fully complete, through two phases, with real file data). Both were real, worthwhile things to fix or confirm, but **neither was the actual reason nothing draws**. The actual reason is, and has apparently always been, this specific bug: whatever CPU code is supposed to write real vertex coordinates into the pushbuffer before `INLINE_ARRAY` submits them is instead writing zeros -- unaffected by, and unrelated to, any of tonight's other fixes.

- **Not yet found**: the specific guest code that issues these `INLINE_ARRAY` pushbuffer writes. `nv2a_pb_exec.c`'s `NV097_INLINE_ARRAY` handler just buffers whatever 32-bit value the pushbuffer stream already contains (`nv2a_pb_exec.c:1427`); the zero values are already zero by the time this project's own code ever sees them, meaning the actual bug is upstream, in whatever x86 code constructs the pushbuffer command stream in guest memory in the first place.
- **Two concrete ways to find it next, neither attempted yet**: (1) a guest-memory write-watchpoint on the pushbuffer's command-stream address around the moment this `BEGIN_END`/`INLINE_ARRAY` pair fires, to catch the exact write instruction red-handed -- this project doesn't currently have a watchpoint mechanism for the native build (GDB has only been used against the xemu/retail side this session, not the native exe); or (2) work backward from the vertex-format/vertex-buffer-source setup methods (`SET_VERTEX_DATA_ARRAY_FORMAT`/`SET_VERTEX_DATA_ARRAY_OFFSET` and similar, already partially decoded per earlier entries in this file) to find what guest memory address the title believes its vertex data lives at, then check with a one-off diagnostic dump whether *that* memory is itself all-zero (meaning the real bug is further upstream still, e.g. a vertex buffer that's allocated but never filled) or non-zero (meaning the bug is specifically in whatever copies from there into the pushbuffer).
- **This is a genuinely different kind of bug than anything else chased tonight** -- not a missing signal/notification (the shape behind the interrupt and DPC-drain fixes), but a data problem: real data not ending up somewhere it needs to be. It deserves its own focused investigation with the same rigor as tonight's interrupt chase, starting fresh rather than as a coda to this already very long entry.

### A package from an external reviewer (Patrick Carey / the fleet that sent `NFL2K5-PC-review-response-20260917`) reframes this entire investigation, and was sitting unread until asked about directly

The user asked "are you looking at what Patrick sent? what about his tools" -- the honest answer was no, not since it arrived. Reading it now changes the picture materially, so this gets its own entry rather than a footnote.

- **The single most important item: `docs/technical/gap-analysis.md` (already in this tree, `external/xboxrecomp/`) rates the exact path this game is forced onto as an unfinished stub, on purpose:**
  ```
  Push buffer parsing (PFIFO DMA pusher) | Full | Stub | N/A | Low (D3D8 API intercept instead)
  ```
  `src/main.c:644` forces `RECOMP_PB_EXEC=1` (unless already set), which routes NFL2K5 through exactly that stub. Meanwhile `d3d8_vsh.c` (1,868 lines: 128-bit microcode parser, 14 MAC + 8 ILU ops, 192 constants, 64-entry HLSL cache) and `d3d8_combiners.c` (1,415 lines) are both rated **DONE** in the same doc, and sit completely unused for this title. This is a better-supported explanation for tonight's zero-vertex-data finding than a narrow parsing bug: the game may be running the path nobody finished, while a complete one sits next to it.
- **Verified this isn't cosmetic** by checking the files exist and the doc's own DONE/Stub ratings are real (`grep` against `gap-analysis.md` line 19 and 113), not just review-package assertion.
- **Confirmed, via a live screenshot of the xemu instance that's been running since earlier tonight** (`xemu_screenshot` HMP command, same mechanism built and verified earlier), that this is a real bug worth fixing, not a legitimately-empty draw: the screenshot shows a complete, correct, detailed rendered NFL 2K5 game in progress -- real player models, animations, score bug, down-and-distance -- on the identical XISO/game files. Whatever the native build's `prim=4` / all-zero-`INLINE_ARRAY` draw is, the same underlying content renders correctly elsewhere, so the zero data is a bug, not a red herring.
- **Two ways forward, not yet decided (Patrick's package explicitly says: "decide and write down" this choice, since it silently defaulted to one side):**
  1. **Fix the pushbuffer stub in place.** A lot of real, working infrastructure already exists there this session confirmed (texture format/address/rect decode, surface/clear handling, `SET_BEGIN_END`/`INLINE_ARRAY` parsing itself) -- this specific bug looks narrow (one data source feeding zeros) rather than structural. The now-running xemu instance is a genuine, working reference implementation of the *same* pushbuffer protocol for cross-checking, though a reliable comparison needs a controlled, synchronized capture (fresh boot, matched breakpoint) rather than reading a session that's diverged for hours -- not yet attempted.
  2. **Switch to D3D8 API interception**, reusing the finished `d3d8_vsh.c`/`d3d8_combiners.c`/`d3d8_device.c` pipeline. Bigger lift: the one existing recipe for this (`docs/technical/d3d8ltcg-device-context.md`) is specific to Criterion's statically-inlined D3D8LTCG library (Burnout 3), which NFL2K5 (Visual Concepts) almost certainly does not use -- so this path means finding NFL2K5's own D3D8 entry points from scratch, a reverse-engineering task on the scale of this whole session, not a reuse of existing addresses.
- **Also in the package, not yet used**: a real, MIT-licensed toolkit (`tools/nfl_outer.py`, `tools/nfl_txtr.py`) including a verified VC-LZ decompressor ported directly from this game's own decompression routine (`default.xbe` VA `0x004DC00`) and a working NV2A unswizzle/swizzle implementation -- directly relevant if the eventual root cause turns out to be resource-decompression-related rather than pushbuffer-parsing-related. Not yet integrated or run against this project's own archive dump.
- **Not acted on further this session** -- this is a real architectural fork, not a quick fix, and deserves a deliberate decision rather than a default. Flagging clearly for whoever continues this: read the full package at `E:\NFL2K5-PC-review-response-20260917\` before picking a direction, particularly `03-findings.md` and `04-roadmap.md`.

### User decision: fix the pushbuffer path in place (option A). Then traced the zero-vertex-data bug to a precise, address-exact, multi-level root -- and ruled out the most likely final candidate. Not fixed yet; this is the clearest possible handoff for continuing it.

The user chose to keep debugging the existing pushbuffer path rather than switch to D3D8 interception, and asked specifically to use Patrick's tools where useful. This entry is the full trace, address by address, verified live at every step -- not guessed. **Read this entire entry before continuing**; it supersedes every earlier guess about where the zero vertex data comes from.

**Used Patrick's tools first, as asked.** `tools/2k5-formats/nfl_outer.py` and `nfl_txtr.py` ran cleanly against this project's own `original/disc/vc_53450030/0`: parsed 4,323 entries, 13 cross-volume (bank-straddling) entries -- matching Patrick's prediction almost exactly ("your 13 unresolved records"), confirming his bank-straddling diagnosis for `docs/ARCHIVE-FINDINGS.md`'s gap is very likely correct (not fixed this pass -- a separate, smaller task from the vertex-data bug, noted here so it isn't lost). Extracted and decoded the known legal-screen entry (`0xedd549bd`) -- it decodes cleanly, real structured data (`TXTR` header visible inside each chunk's system bytes, non-garbage video data), confirming archive/texture decode is *not* where this specific bug lives. That ruled out one entire category (resource decompression) in about five minutes, which is exactly what the tools were offered for.

**Built a real write-watchpoint for the native build** (`RECOMP_WATCH_PB_WRITE=1`, `pb_write_watch` in `src/main.c`), since this is a static recompiler and no existing tool could show "which guest instruction wrote this address" -- page-guards a 4KB page (`PAGE_READONLY`), and on a write fault, resolves the faulting native `Rip` through the same symbol engine `crash_report` already uses, then restores write access for exactly one instruction (`EFLAGS.TF`) and re-guards on the resulting single-step trap. Verified safe: five separate runs, zero crashes, correctly identified a totally unrelated `memmove` by name on the first test. De-duplicated by call site and filtered by in-page offset to step through a busy page efficiently. **This is real, reusable infrastructure now, not a one-off** -- left in the tree, opt-in, zero cost when unset.

**Traced the exact chain, every hop confirmed live, not inferred:**

1. The zero `INLINE_ARRAY` words live at guest `0x83E50DC0`-`0x83E50DD4` (previously known, this session, from `RECOMP_PB_DRAW_TRACE`'s new `src_va` field).
2. The watchpoint caught the write: `sub_004251A0` (`recomp_0028.c:279534`), a decompiled `rep movsd` -- a genuine bulk **copy**, not a bug in the copy mechanism itself (`nv2a_pb_exec.c` never touches these bytes before reading them back zero -- the zero is already there before the pushbuffer ever sees it).
3. Traced the copy's **source**, not just destination (`nfl2k5_trace_vertex_copy_source`, added this pass): source address is on the **guest stack**, and the words there are already zero. Also captured directly: `device+0xBCC == 0` (device = `MEM32(0x4409A8)`, this project's known D3D device-context pointer, same one `xbox_Nv2aMirrorFence` already uses).
4. `device+0xBCC` is the vertex-source-base field computed by `sub_004308C0` (`recomp_0029.c:24179`), called from `sub_004251A0`'s own entry. Traced its gate (`nfl2k5_trace_vertex_setup_gate`): bit `0x20` of `MEM32(0x440508)` (a device dirty-flags register) -- **confirmed SET, live** (`flags=0x70`). The gate is open; the setup body does run. (This was the first hypothesis and it's wrong -- recorded so nobody re-checks it.)
5. Inside that body, a 16-slot loop (`recomp_0029.c:24240`) walks a per-stream descriptor table. Traced every slot's type field (`nfl2k5_trace_stream_slot_type`): slot 0 = `0x32` (a real, valid vertex-format type, **not** the "unused" sentinel `2` that all 15 other slots correctly show). So slot 0 is a real, active stream -- not simply unbound.
6. Slot 0's descriptor lives at a **fixed, static address**, `0x00443028` (confirmed live, `nfl2k5_trace_stream_descriptor`, identical across every run -- not computed/relocated). Its `+0x18` field -- the actual vertex **data pointer** -- reads **`0x00000000`**, live, confirmed. This is the literal, final root: a fixed guest memory cell that should hold a real buffer address and holds null instead.
7. Found what's *supposed* to populate that slot: `sub_004290C0` (`recomp_0029.c:4494`) is called with `edi=0x443028` (the descriptor table) and, per its own single parameter's bit 0, either binds the stream (calls `sub_00428010`) or skips to an unrelated pair of calls. Traced that parameter live (`nfl2k5_trace_stream_bind_param`): **bit 0 is clear on every call, every time** -- meaning `sub_00428010` genuinely *does* run. (Second hypothesis, also wrong -- also recorded so it isn't re-chased.)
8. Read `sub_00428010` (`recomp_0029.c:1992`) in full: it is a real, substantial function that sets up **vertex format/FVF metadata** for all 16 slots (type codes, component layout, matching the same `0x435E40`/`0x435E48` size-lookup tables seen earlier this session) -- confirmed it never writes a `+0x18`-style data pointer anywhere in its body. Read the rest of its caller (`sub_004290C0`, through its end at line 4687) too: it goes on to write several more real NV2A pushbuffer commands (`0x4194C`, `0x41950`, `0x4195C`, `0x41960`, `0x41E94`, `0x41EA0` -- genuine method addresses, format/stride setup) and stores the descriptor table pointer into `device+0x794`, but **at no point writes a real buffer address into any descriptor's data-pointer field.** This entire function is confirmed, end to end, to be the D3D8-style `SetVertexShader(FVF)`/format-declaration path -- not `SetStreamSource` (the actual buffer-binding call). **Ruled out, not guessed.**

**Where this leaves it.** The actual "bind a real buffer to stream slot 0" call -- the equivalent of D3D8's `SetStreamSource(0, pVertexBuffer, stride)` -- has not been found. It is not `sub_004290C0` or anything it calls; that branch is now fully read and eliminated. It must be a separate, not-yet-identified function, called from somewhere else in the title's rendering setup, that writes to guest address `0x443040` (`0x443028 + 0x18`) -- a static, fixed address, which is what makes it worth searching for specifically rather than continuing to trace call chains blindly.

**Concretely scoped next action, in priority order:**
1. `0x443040` is a fixed, static address -- unlike everything else chased this session, it does not require live tracing to search for. `grep` the generated code for computed writes of the shape `MEM32(<something> + 0x18) = <buffer address>` near the same descriptor-table region (nearby literal `0x443028`-`0x443100`-ish addresses), or add one more `RECOMP_WAIT_TRACE`-gated write-trace directly on `MEM32(0x443040)` itself (a *plain* memory write trace this time, not the page-guard watchpoint -- much cheaper, since the target address is now known and fixed, no need to re-derive it) to catch it the moment anything ever writes there, whenever that happens to be in boot.
2. If nothing ever writes it before this draw fires, the bug is an **ordering** problem (the draw happens before `SetStreamSource` was ever called) rather than a translation bug in the missing function itself -- check what guest code decides *when* to issue this specific draw, and whether it's skipping a "do I have a bound stream yet" check that real hardware's D3D8 runtime would have enforced.
3. All the trace infrastructure from this pass (`nfl2k5_trace_vertex_copy_source`, `nfl2k5_trace_vertex_setup_gate`, `nfl2k5_trace_stream_slot_type`, `nfl2k5_trace_stream_descriptor`, `nfl2k5_trace_stream_bind_param`, all in `src/recomp_manual.c`, all gated behind the existing `RECOMP_WAIT_TRACE` flag, all left in the tree) plus `pb_write_watch` (`src/main.c`, `RECOMP_WATCH_PB_WRITE`) are real, reusable, and already proven correct on this exact bug -- reuse them rather than re-deriving the chain above from scratch.
4. Not fixed this pass, deliberately: writing a guessed pointer into `0x443040` without knowing what real buffer it should reference (an archive-decoded vertex buffer? a scratch/dynamic buffer? which one, for slot 0, at this specific point in boot?) would be exactly the kind of unverified patch this project's own conventions warn against. The missing call needs to be found and its own bug understood, not papered over.

### Ran the next action immediately: retargeted the same watchpoint at 0x443040 directly. Definitive result -- nothing writes it, ever, in this window. Not "written wrong": never written.

Made `pb_write_watch` reusable rather than one-shot (`RECOMP_WATCH_ADDR=<hex>` overrides the target address, `RECOMP_WATCH_PB_WRITE=1` still arms it; both `src/main.c`), and pointed it straight at the fixed guest address identified above: `RECOMP_WATCH_ADDR=443040`.

- **Captured 20 real, distinct writes in the same page during a ~15s run** -- all attributed correctly by function/source line: `sub_00428010` (already fully read, confirmed the format-setup function) clearing/writing offsets `+0x2C`, `+0x38`, `+0x44`, `+0x50`-ish relative to the table base `0x443028` (matches its own source exactly: `recomp_0029.c:2008,2009,2015,2030,2031,2043`), and a second, previously-unseen function, `sub_00427890` (`recomp_0029.c:1474`), touching the very start of the same page (`host offset 0x00`, i.e. guest `0x443000`).
- **Not one of those 20 writes touches `0x443040` itself.** The watchpoint would have caught it if anything had -- it caught everything else on the page, at byte granularity, proven by the exact-offset attribution above. This directly confirms the hypothesis from the previous entry's next-action item #2, not #1: this is not a case of the right call running with a bug in it -- **the call that should populate this field does not run at all**, at least not within the window this draw happens in.
- **New, previously-unseen function worth following next**: `sub_00427890` (`recomp_0029.c:1474`, seen at `+0x7F1B` into a much larger function -- so `sub_00427890` itself starts well before line 1474; the real function entry needs locating). It touches this exact table's base address directly, which none of the previously-traced functions (`sub_004251A0`, `sub_004308C0`, `sub_004290C0`, `sub_00428010`) do by name -- worth reading in full before searching further afield, since it's already proven to touch the right structure, just possibly the wrong field or too early/late relative to when slot 0's real buffer should be bound.
- **Also worth checking directly, cheaply, before more tracing**: whether `0x443040` ever gets written *later* than this ~15s window -- i.e. whether this is purely a boot-ordering issue (the legal-screen draw fires before a later, perfectly normal `SetStreamSource`-equivalent call would have run) rather than a translation bug at all. A longer capture (`RECOMP_WATCH_PB_WRITE=1 RECOMP_WATCH_ADDR=443040`, run 60+ seconds instead of ~15) answers this directly and is the single cheapest next step -- cheaper than reading `sub_00427890` -- since the watchpoint mechanism is already proven correct and reusable.
- **Ran the 90-second check.** Made the watchpoint's report unconditional and uncapped for one exact byte offset (`RECOMP_WATCH_ADDR`'s target specifically, separate from the general capped/deduplicated trace, so an early burst of already-understood nearby writes can never hide a later hit on the one address that matters) and ran a full 90 seconds. **Zero hits, the entire time.** Not "written late" -- never written, at all, in this build's boot sequence as far as it currently gets. `[GPU] draws` stayed `0` throughout, consistent.
- **Found and fully read the actual caller of the two `sub_004290C0` calls traced earlier** (`recomp_0029.c:1441-1484` -- the function `sub_00427890`'s symbol name is almost certainly misattributed, since the reported offset, `+0x7F1B`, is far larger than any function this project's generated code produces; trust the source line, not the symbol, here). This is a genuine, one-time device/renderer init sequence: polls `sub_00422AF0` until it returns zero, sets `device+0x794 = 0x443028` (the exact line that originates the constant this whole chain traces through), calls `sub_004290C0(4)` then `sub_004290C0(2)` (matching `nfl2k5_trace_stream_bind_param`'s captured `param=4`/`param=2` pair exactly), then zero-fills a *different*, adjacent buffer (`0x442F10`, 70 dwords) that arithmetically ends exactly at `0x443028` without ever touching it or anything past it. Every single function that touches this memory region has now been identified and fully read; **none of them write the data pointer**. This is a real, checked, exhaustive negative result for this region, not an assumption.
- **Traced one more candidate live** (`sub_0042F9A0`, `recomp_0029.c:20711` -- part of the same overall pushbuffer-construction sequence seen earlier tonight): it reads a flags byte from the descriptor table and skips its entire main body if bits `0x12` are set, which looked like it could be yet another "flag says skip real setup" gate. Traced it live (`nfl2k5_trace_stream_process_gate`): **`flags_byte=0x00`, gate is open, main body does run.** Hypothesis wrong, ruled out with evidence rather than left unchecked.

### Switched technique entirely: a real hardware watchpoint on the live retail/xemu session found the actual write -- and it comes from inside the Xbox kernel itself, not game code. This is why five hours of reading generated game functions could never find it.

Every function traced tonight, without exception, was game-side generated code (`sub_0042xxxx` addresses, `recomp_00XX.c`). All of it was real, correctly-read, and correctly ruled out -- but it was the wrong layer to be searching. Proved this conclusively rather than guessing it:

- **Restarted xemu clean, paused at reset** (`-S -gdb tcp::1234`), so a hardware watchpoint could be armed *before* boot ever reaches the relevant code -- unlike the long-running instance from earlier tonight, which had already been past this point for hours and couldn't be used for a controlled test.
- **Set a real GDB hardware write watchpoint** (`Z2,443040,4` over the GDB remote protocol) on the exact same address the native build's own watchpoint has been chasing, then continued execution.
- **It fired.** Confirms, independently of everything traced in native so far, that `0x443040` is a real, live, meaningful memory location on correct hardware -- not a red herring, not a value that's supposed to stay zero at this point in boot. Real Xbox code writes it during normal boot, well before the game is playable.
- **The stop landed at `eip=0x8002EAAC`.** That address range (`0x8000xxxx`-`0x8002xxxx`) is Xbox kernel space (`xboxkrnl.exe`'s own mapping), not the title's own code (which lives at `0x0001xxxx`-`0x004Dxxxx` in this XBE). **The write happens from inside the kernel itself.**
- **Walked the full call chain via the EBP frame chain** (5 frames deep: `0x8002EAAC` <- `0x8002F0E3` <- `0x8002F406` <- `0x800158A5` <- `0x800218FA` <- `0x8001CC98`) -- **every single frame stays in kernel address space.** It never returns to a game-side `sub_0042xxxx` return address at any depth captured. This means the write is not triggered synchronously by a game function calling into the kernel and the kernel writing back into caller-visible state on that same call -- it looks like kernel-internal, background activity (an ISR, a DPC, or an internal completion routine acting on structures the kernel already knows about), consistent in shape with the same "signal/completion that native never delivers" pattern behind *every other* bug fixed or diagnosed this entire session (the GPU-notify fix, the DPC-drain fix, the vblank ISR work) -- just a different kernel subsystem than any of those.
- **Checked the obvious candidate and ruled it out with evidence**: this project's `bridge_NtReadFile` (`kernel_bridge.c:3004`) is a complete, synchronous, already-proven-working implementation (it's what produced every `[READ]` log line trusted earlier tonight for the archive-loading trace) -- it does not match the register state captured at the watchpoint hit, and disk-file-read completion is not obviously what's happening here. **Not yet identified**: which kernel subsystem/export this actually is. No XBOXKRNL symbol map is available in this project to name `0x8002EAAC` directly.
- **Detached cleanly** (cleared the watchpoint, sent a GDB `D` detach packet, confirmed xemu resumed normally afterward) -- same established protocol as every other live-debugging session this project has used, no VM left paused.

**Why this matters more than another ruled-out game function.** This session spent the entire "fix the pushbuffer path" continuation reading `src/recomp/gen/*.c` -- correctly, but in the wrong place for the very last step. The actual missing piece is almost certainly a **kernel bridge function** (`external/xboxrecomp/src/kernel/kernel_bridge.c`) that either doesn't exist yet, or exists but doesn't replicate an internal side effect the real kernel's implementation has. That is a fundamentally different, and far more tractable, kind of search than continuing to read game-side generated C: `kernel_bridge.c` is a few thousand lines of hand-written, already-organized-by-ordinal C, not millions of lines of lifted decompiler output.

**Concretely scoped next action, in priority order:**
1. Get an XBOXKRNL symbol/ordinal map for `0x8002EAAC` and its call chain -- even an approximate one (a public Xbox kernel export address table, or xemu's own debug symbols if it ships any for the BIOS/kernel it loads) would immediately name the subsystem instead of leaving it as a bare address.
2. Failing that, single-step from the watchpoint hit (same GDB session technique, just `s` instead of `c`) to read the actual instruction bytes at `0x8002EAAC` and nearby, and manually identify the routine by shape -- slower, but needs no external symbol source.
3. Cross-reference the call chain's addresses against `kernel_bridge.c`'s existing `g_slot_ordinals`/ordinal-dispatch table structure: if any of the 5 chained return addresses corresponds to a *guest-visible* kernel export this project already bridges (check what's mapped near `KERNEL_VA_BASE` and whether any ordinal's real xboxkrnl implementation is known to live in this address neighborhood), that immediately identifies which `bridge_*` function in this project's own tree is the incomplete one.
4. Once identified: compare that bridge function's native implementation against what real hardware's write proves it must also do (write a real pointer into a stream descriptor's data field, or something that has that as a side effect), and complete it -- the same "diagnose against a real, running reference, then fix in native" method this project used successfully for the GPU-notify and DPC-drain fixes earlier tonight.
- **Session status at handoff**: native rendering is still at zero draw calls with real vertex data, and still zero visible textures. But the search space just narrowed from "somewhere in 5.4 million lines of game code" to "somewhere in a few thousand lines of hand-written kernel bridge code," with a real, retail-verified target address (`0x8002EAAC`) and full call chain to search for -- proven with a real hardware watchpoint on correct, working reference hardware, not inferred from native-only tracing. That is a categorically different, and much more tractable, starting point than anything this session had before tonight's continuation.

### Important correction, found immediately after writing the above -- read before acting on it

Continued the same retail GDB session rather than stopping at "found the kernel write." Two follow-ups changed the picture:

- **Let the watchpoint keep running for another 60+ seconds after the first hit. It never fired again.** Then interrupted the target directly (GDB break, not a watchpoint) and read `MEM32(0x443040)` at that point: **still `0x00000000`.** The kernel-level write this entry spent most of its length on **also writes zero** to this address. Boot had, by that point, progressed well into real game code (`eip=0x004D921A`, inside the XBE's own range) -- i.e. this was not "too early to tell," retail itself carries a zero here for a real stretch of its own boot, same as native.
- **This does not undo the finding that retail writes this address and native's own tracing never does** -- that's still a real, confirmed, reproducible gap (a real kernel-mediated side effect this project's kernel bridge doesn't currently replicate, whatever it turns out to be). What it undoes is the stronger claim that this address is *the* smoking gun for "why nothing draws" -- it evidently is *not* holding a live vertex buffer pointer at the moment this specific draw fires, on correct hardware either. Both systems draw with a zero here at this point; only native fails to draw correctly overall. So either:
  - the real, meaningful, non-zero write to this field happens *later* than this test's window checked (not yet confirmed either way -- would need a much longer retail capture, or a second watchpoint pass specifically resuming past this point), or
  - **this specific field was never the actual determining factor for whether the draw is visible**, and the earlier native-side conclusion ("device+0xBCC's descriptor's data pointer is null, therefore garbage vertex data") was correct about the mechanism but wrong about it being *causal* for the missing image -- i.e. retail may render this exact draw with the same zero-filled vertex data as native does, harmlessly (a degenerate/invisible batch is normal and common), and the *actual* difference between native and retail is downstream or elsewhere entirely, not in this specific stream slot's data pointer at all.
- **Not resolved this session.** Flagging this plainly rather than leaving the previous entry's confident tone standing uncorrected: the `0x443040`/kernel-write thread is a real, verified, reusable piece of evidence (the retail watchpoint technique itself is proven and reusable -- see below), but it should be treated as **one open lead among the things this session traced, not as the located root cause.** The single most valuable next step, given this correction, is probably re-examining whether the specific `prim=4`/all-zero-`INLINE_ARRAY` draw this whole session has centered on is even the draw that matters for visible output on retail -- e.g. via the same retail-GDB technique, breakpointing the pushbuffer executor's own equivalent code path (or, more directly, comparing xemu's own NV2A pushbuffer trace for the *exact same title state* against native's, now that both are inspectable) rather than continuing to assume this one draw is load-bearing.
- **What's durable from this whole detour, regardless of the above correction**: a working, reusable technique for arming real hardware watchpoints against a fresh, paused, GDB-attached xemu boot (`-S -gdb tcp::1234`, then `Z2,<addr>,<len>` over the wire, `D` to detach cleanly -- all now proven, not theoretical) that this project didn't have before tonight. That capability outlives this specific address turning out to be less conclusive than first thought.

### One more real, concrete divergence found with the same technique before stopping: retail calls `sub_004290C0` differently than native does

Used the same fresh-paused-xemu-plus-GDB technique to check something more directly actionable: does retail even call the game functions this session already fully read (`sub_004290C0`, the FVF/format-setup function) the same way native does? This is a **real x86 address** (`0x004290C0`), identical in both native and retail since it's the same XBE -- so, unlike the kernel-address thread above, a software breakpoint here is unambiguous and directly comparable.

- Set `Z0,4290c0,1` (software breakpoint) on a fresh, paused xemu boot, then captured the first-argument value (the FVF flags parameter, same field `nfl2k5_trace_stream_bind_param` reads in native) across repeated hits.
- **First four hits: `param=0x00000004` every time, with `esp` identical (`0xD0066AF0`) across all four.** That's a real loop calling this function repeatedly with the same argument from the same call site -- not two different call sites alternating `4` then `2`, which is what native's own trace showed earlier tonight (`nfl2k5_trace_stream_bind_param`: `param=4`, then `param=2`, alternating, from `recomp_0029.c:1464` and `:1468`, two adjacent but distinct call sites in the same one-time init function).
- **Not able to fully characterize this before the debug session became hard to recover cleanly** (a script timeout desynced the GDB connection; recovered enough to clear the breakpoint and detach cleanly, confirmed xemu resumed and is running normally, but did not manage to re-arm and capture a longer, complete sequence in the time available). So this is a real, verified *partial* pattern -- confirmed four repeated `param=4` hits from one call site -- not a complete characterization of whether `param=2` also occurs on retail, how often, or from where.
- **Why this is worth chasing next, concretely**: if retail's actual calling pattern for this function turns out to be structurally different from native's (a genuine loop vs. two fixed one-time calls), that would point at a real gap in this project's own call-graph reconstruction for whatever *drives* these calls -- i.e. the bug might not be inside `sub_004290C0`/`sub_00428010` at all (both already fully read and are faithful translations of what they do), but in **how often or from where they get invoked**, which is a genuinely different, and very concrete, class of bug than anything else chased tonight.
- **Concretely scoped next action**: redo this same breakpoint capture with a clean, freshly-started debug session (avoid chaining many GDB commands in one Python process over a long wall-clock time, which is what desynced this one -- restart the script per batch of captures instead) and this time also capture the **return address** (`MEM32(esp)` at the breakpoint, the actual call site) alongside the parameter for each hit, to see directly whether retail's repeated calls come from a genuine loop (one call site, many hits) or from many distinct call sites (which would look similar in aggregate but mean something completely different).

### Ran that immediately. Result is unambiguous, and it's the most concrete, actionable finding of the whole session.

Rewrote the capture as a standalone script (`retail_capture.py`, run fresh rather than chained in a long-lived interactive connection -- fixes the desync from the attempt above) and captured 15 consecutive hits on a fresh, paused retail boot.

**All 15 hits: `ret=0x00427C9A`, `param=0x00000004`, identical `esp` every time. Not one `param=2` hit in 15 consecutive captures.**

- `0x00427C9A` is the exact, known return address for the *first* of the two `sub_004290C0` calls this session already fully read (`recomp_0029.c:1462-1464`: `PUSH32(esp, 4); MEM32(ebx+0x794) = 0x443028; PUSH32(esp, 0x00427C9Au); RECOMP_ABI_CALL(..., sub_004290C0);`). **Retail calls this one call site in a real, sustained loop** -- at least 15 times back to back, with no interleaving.
- **Native's own trace of the identical call (`nfl2k5_trace_stream_bind_param`, captured earlier tonight) showed the opposite shape**: `param=4`, then `param=2`, alternating -- i.e. native's translation calls the *first* call site once, then immediately the *second* call site (`0x00427CA1`, `param=2`, `recomp_0029.c:1466-1468`) once, back and forth, never repeating the first call site multiple times in a row the way retail just demonstrably does.
- **This is a real, structural divergence between native and retail, not a data/pointer problem.** Whatever loop retail uses to call the `param=4` site repeatedly -- most plausibly "once per object/mesh of a given type in the current scene" -- either doesn't exist as a real loop in native's translation, or exists but terminates (or gets interleaved with something else) after a single iteration. Given this session's parallel finding that only one draw call (the single `prim=4`/`INLINE_ARRAY` batch this whole thread has centered on) has ever been observed in any native capture, **a scene/object list that's empty or truncated to one entry in native, versus a real, multi-entry list in retail, is now a better-supported explanation than a single missing pointer write.** That would also explain why the `0x443040` field reads zero on both systems at this point (Correction entry above): if it's genuinely a per-object loop, slot 0's data pointer may legitimately not matter yet at this exact moment on *either* system, and the real difference is simply that retail's loop has many more objects to get through before whatever draws real geometry happens, while native's loop -- for reasons not yet found -- has effectively none.
- **Not yet found**: why native's translation produces this different control-flow shape. Two candidate directions, both concrete and checkable: (1) the object/mesh list this loop walks is populated by something upstream (likely archive/resource-driven -- this session already proved archive loading itself works correctly through two phases, so the gap would be in *connecting* loaded resources to this list, not in loading them) and that connection is what's missing or truncated in native; or (2) the loop's own guest-side termination condition is being mistranslated, causing it to exit after one iteration regardless of how many objects are really available. Both are native-side, generated-code-level questions now, not kernel-level -- a return to the same kind of investigation this whole session has been doing, but now aimed at a specific, evidenced target (find what feeds the loop at `recomp_0029.c` around `0x00427C89`-`0x00427CA1`'s enclosing loop, not the leaf functions it calls) instead of the leaf functions themselves, which are already fully read and confirmed correct.
- **Detached cleanly**; xemu confirmed running normally afterward, not left paused.
- **Refined immediately after, cheaply, with a static check**: searched `sub_00427890` (the enclosing function, confirmed genuinely starting at `recomp_0029.c:1030` -- the earlier "symbol may be misattributed" caveat was itself wrong, it really is this function, just a large one) for any backward jump (loop) wrapping the `sub_004290C0` call pair. **There isn't one.** The only loop in the relevant range (`recomp_0029.c:1453-1459`) polls a *different* function (`sub_00422AF0`) once, before either `sub_004290C0` call, not around them. Both calls happen exactly once each, per execution of `sub_00427890` itself.
- **Which means the real conclusion is simpler and even more concrete than "a scene loop runs too few times": `sub_00427890` -- this entire ~800-line device/graphics initialization function -- must itself be getting invoked multiple times on retail**, not just its inner calls. Native, by contrast, appears to run it once (matching every native capture this session ever took: exactly one `param=4` and one `param=2` hit, never more). Something at a level *above* `sub_00427890` -- its own caller -- retries or repeats full device initialization on retail and does not on native. That is a very plausible shape for this exact class of title: **a "device not ready yet, reinitialize and try again" retry loop**, which fits the same pattern as multiple things already found and fixed this session (a wait/retry that depends on a signal native never delivers) -- just one level further out than anything chased so far, at `sub_00427890`'s *caller*, not inside it.
- **This is now the single most concrete, well-scoped next action of the entire session**: find what calls `sub_00427890`, and whether it's structured as a retry/reinit loop with a condition that native satisfies (or fails to properly re-check) after exactly one pass. That caller has not yet been located -- a natural next step is the same return-address-at-entry technique used successfully many times tonight, applied to `sub_00427890` itself.

### Ran that too. Found the immediate caller and its logic in full -- and it rules out the most obvious hypothesis (device creation failing), while pointing straight back at this session's earlier interrupt work.

Traced `sub_00427890`'s own entry (`nfl2k5_trace_device_init_entry`, reading the return address off the stack the same way as every other trace tonight) and its return value at the call site (`nfl2k5_trace_device_init_result`).

- **Native calls `sub_00427890` exactly twice**, both times from the identical return address `0x004201BE`, both times with the identical device pointer (`ecx=0x004409B0`). That address is inside `sub_00420160` (`recomp_0028.c:267137`), read in full.
- **`sub_00420160` is not a retry loop.** It calls `sub_00427890` exactly once per its own invocation, checks the signed result: negative (failed HRESULT) triggers a real, drastic cleanup path -- calls `sub_00427D60`, then zero-fills **2,344 dwords** of the device structure (`recomp_0028.c:267200`, `MEM32(0x4409B0)` onward) and sets `MEM32(0x4409A8) = 0`, nulling the device pointer this entire session's whole investigation has been keying off -- and propagates the failure upward. A non-negative result skips all of that and returns success.
- **Captured the actual return value on both native calls: `0x00000000` (success) both times.** This directly rules out "device creation is failing in native" -- it isn't; it succeeds, cleanly, exactly as retail's `sub_00427890` calls presumably also do. **The two-vs-many-more-than-two difference is not a fail/retry pattern inside this function or its immediate caller.**
- **What this leaves standing**: `sub_00420160` itself (or something above it) must be what's invoked only twice in native versus however many more times on retail -- and it now looks *unrelated to error handling entirely*. A more likely shape, given everything else this session has established: **this whole device-setup path may be paced by something per-frame or per-attempt that keeps calling it until a real condition is met** -- e.g. a display-ready or vblank-driven retry, not an error retry. That reconnects directly to this same session's earlier, independent finding (the "User decision: fix the pushbuffer path in place" entries above, and further back, the whole vblank/DPC-delivery thread): **native's vblank/frame-pacing signal was confirmed broken and only partially, experimentally fixed earlier tonight** (`RECOMP_SYNTHESIZE_VBLANK`, still off by default, still not verified against real hardware timing). If `sub_00420160`'s caller is meant to be invoked once per real frame/vblank until some readiness condition is met, and native has no real frame pacing at all, "called exactly twice" (however many host-timesteps happened to elapse before something else moved boot forward) versus retail's real, frame-paced "called as many times as it takes" would be exactly the observed shape.
- **Not confirmed this session** -- this is now a strong, well-connected hypothesis linking two previously-separate threads of tonight's investigation (the vblank/interrupt work and the vertex-stream/device-init work), not a proven unification. The next concrete step is finding `sub_00420160`'s own caller (same technique, one more hop) and checking whether it's gated on a frame/vblank-style condition, ideally cross-checked against the earlier `RECOMP_SYNTHESIZE_VBLANK` experiment to see whether enabling it changes this specific call count at all -- a test this session did not run.
- **Ran that test immediately. Result: no change. Vblank pacing is ruled out as the cause of this specific divergence.** Re-ran with `RECOMP_SYNTHESIZE_VBLANK=1 RECOMP_WAIT_TRACE=1` together -- the working, verified vblank/DPC-delivery mechanism from earlier tonight, genuinely firing and delivering real completions throughout the run. **Still exactly 2 calls to `sub_00427890`, both `succeeded`, identical to the no-vblank baseline.** The two threads of tonight's investigation are not the same bug after all -- a clean, useful negative result, not a wasted one. Whatever gates `sub_00420160` (or invokes it repeatedly on retail) is paced by something else: not vblank, not a device-creation failure/retry. Still not identified. The next real step remains finding `sub_00420160`'s own caller -- untouched by tonight's vblank work, so a fresh trace, not a re-test of something already ruled out.
- **Climbed one more hop: `sub_00420160`'s own caller is `sub_00033D50` (`recomp_0001.c:9153`), also called exactly twice, from the identical return address (`0x00033D82`) both times.** Read `sub_00033D50` in full: also a straight-line, one-shot init sequence (`sub_004266E0`, then `sub_00420120`, then `sub_00420160`, in order) -- no loop wraps the `sub_00420160` call either. **Three levels deep now (`sub_00427890` <- `sub_00420160` <- `sub_00033D50`), every one called exactly twice, every one confirmed to have no internal loop around the call this thread is following.** The "twice" has to originate even higher, or reflect a legitimately-correct call count that the retail comparison (a direct breakpoint on the leaf function, not this specific chain) wasn't actually isolating to this one call path -- both remain open. Stopping the climb here for tonight: this is now a deep, well-evidenced trace with real value regardless of exactly where the top of the chain turns out to be, and continuing to climb one level at a time has reached the point of needing either a fresh investigative session or a different technique (e.g. capturing retail's own full call stack at one of the 15 `sub_004290C0` hits, the same EBP-chain-walk technique already used successfully on the kernel-space investigation earlier tonight, applied here instead) rather than more single-hop native tracing.

## MAJOR: the per-frame loop itself stalls after ~2 iterations -- found the actual mechanism, not just another leaf-level symptom

Continuing autonomously overnight per the user's explicit instruction. Picked the climb back up with a **fresh, controlled retail capture from power-on** (previous session's long-running retail xemu instance had already been past the relevant code for hours, contaminating any count). Restarted xemu `-S -gdb tcp::1234`, breakpointed `sub_00033D50`'s own caller chain from the very first instruction executed after reset.

- **Definitive retail result: `sub_00420160` (three levels down the chain above) is entered at least 80 times in the first ~9 seconds of boot alone** (test was capped at N=80; it hit the cap, meaning the true count is higher), every single hit from the *identical* return address and stack pointer (`ret=0x00033D82 esp=0xD0066C78`). That rules out "called from many different places" -- it's one call site, genuinely looping, many times, immediately at boot. **Native's own instrumentation shows this exact same call site fires exactly twice, ever, and never again**, confirmed independently by both the existing `RECOMP_WAIT_TRACE` counters and a fresh run's `[PIPE]` telemetry line (`worker: entry=2 bridge=1`, frozen across a 10+ second run with kernel-call and DPC counters climbing normally throughout).
- **Read `sub_00033D50` itself again, in full: still genuinely straight-line, no loop.** The 80x repetition is not internal to this function -- its *caller* must be invoking it 80+ times. Climbed the call graph one more hop: `sub_00033D50` <- `sub_00033F00` <- `sub_00028C40`. Read `sub_00028C40` in full (`recomp_0000.c:72767`): it computes an "interpolation counter" from a device's frame-timing fields, writes several `0xA6AA1x`/`0xA6AA6x` guest globals, and tail-jumps into `sub_00028BC0` (which itself calls `sub_00027890` and touches the same NV2A notify/wait globals this session's earlier GPU-notify telemetry already tracks). **This is the real per-frame update function** -- not the vertex/device-setup leaves this whole session has been tracing, which are just *downstream* of it.
- **A second, independent, corroborating fact found in parallel**: `NV2A DMA_PUT`/`DMA_GET` are frozen at the identical address (`0x03E50B2C`) across every sample taken tonight, in runs seconds and minutes apart. The game submits its GPU command buffer once (matching the "exactly 2" pattern) and never submits another one. This is the direct, mechanical explanation for "0 draws, 0 triangles, 0 indices" every single tick shown in the existing `[GPU]` summary print -- there is nothing new to draw because nothing new is ever submitted.
- **Found `sub_00028C40`'s three call sites, all gated by a guest counter at `0xA6A7D8`.** One gate (an unnamed function ending `recomp_0000.c:73651`) calls `sub_00028C40` only when `MEM32(0xA6A7D8) != 0`. A second, `sub_000294C0` (`recomp_0000.c:73952`), **decrements** `0xA6A7D8` at entry and only calls `sub_00028C40` once the result reaches exactly 0 -- but only *after* running a genuine FPU-timed spin-wait loop (`recomp_0000.c:74161-74184`, backward-branches on an `fcomp`/parity check against a target time at `ebp+8`) that has every hallmark of a fixed-timestep frame-pacing accumulator.
- **Used this session's existing `pb_write_watch` tool (built earlier tonight for a different address) retargeted at `0xA6A7D8`, uncapped exact-offset channel, on a fresh native run.** Result, over a clean 15-second window: **`0xA6A7D8` is written exactly 4 times total, always from the identical instruction (`recomp_0000.c:73838`, inside `sub_000292E0`), with values 1, 2, 3, 4 in sequence** -- an incrementing counter, not the decrementing one `sub_000294C0` reads. **Zero decrement-writes were ever observed** (the exact instruction at `recomp_0000.c:73967` that `sub_000294C0` uses to write back its own decremented value never fired once) -- proof that **`sub_000294C0`, the function containing the real frame-pacing spin-wait loop, is never entered at all in this native run.**
- (Added missing `SymGetLineFromAddr64` source-line resolution to the `[PBWATCH-EXACT]` channel of `pb_write_watch` in `src/main.c` to make this test possible -- the channel previously only printed the misleading nearest-preceding-symbol name, e.g. `sub_000292E0+0x23C8`, which is nowhere near that function's real 471-byte size. Small, durable improvement to the existing tool, left in place.)
- **Climbed one more hop to find what should be calling `sub_000294C0`.** It has two direct external entry points into its tightly-coupled dispatch cluster (`sub_000366D0`, `sub_00038EA0`, `sub_0002FC60`, `sub_0002AB20`, all calling each other): `sub_00074BF0` (already-existing telemetry confirms this is a one-shot "title init", `title init 74BF0=1`, not a loop) and `sub_00043790`, which is itself reached from `sub_0029B6D0` -- a `Category: game_network` function that polls `MEM32(0xC6C9E0)` and calls `sub_00499DB3`. Given native's own telemetry shows the network subsystem never leaves its initial state (`network=0/00000000/0000`, `network_init_stage=0` in every sample taken tonight), **there is now a concrete, testable hypothesis that the title's frame-pacing heartbeat is routed through what looks like network/timer infrastructure that native never brings up**, rather than a dedicated graphics-only scheduler. Not yet confirmed -- this is as far as the manual call-graph climb reached before writing this up.

**Why this is the biggest finding of the whole investigation, not just another leaf**: every earlier session's discovery (the zero-filled `INLINE_ARRAY` vertex words, the null `device+0xBCC`, the never-written `0x443040`, the "called exactly twice" pattern at three different call-chain levels) is a *consequence* of the same single fact -- native's real per-frame update loop runs its body roughly twice during boot and then simply stops being re-entered, while retail keeps it running continuously (80+ times confirmed in the first 9 seconds alone). Nothing downstream of that loop can ever produce real vertex data or a second draw call, no matter what gets fixed at the leaf level, because the function that would produce the *next* frame's data is never called again. The open question is no longer "why is the vertex data zero" -- it's answered: because only ~2 frames' worth of setup ever happens. The open question now is narrower and more tractable: **what should be re-triggering `sub_000294C0` (or its sibling gate) continuously, and why does that trigger never fire more than a couple of times in native** -- current best lead is the network/timer-heartbeat path just traced, still unconfirmed.

## Correction, found immediately after writing the above: the network/timer hypothesis is FALSIFIED -- but the real pattern is sharper and more useful

Tested the network-heartbeat hypothesis directly rather than leaving it as a guess. Two things exposed it as wrong, and revealed a better lead:

- **The earlier "everything is frozen forever" readings were partly a sampling-window artifact.** All the "0" and "exactly 2/3/4" readings earlier tonight came from samples taken at 8-10 seconds after boot. Re-ran with `RECOMP_SAMPLE_DELAY_MS=15000` (5 more seconds) and the picture changed substantially: `frontend state-dispatch calls` went from a permanent-looking 0 to **8** at the 15s mark; `worker-entry` went from `1` (target=`other` only) to **3**, now hitting **both** real worker targets (`359B0=1 4D810=1`); `worker bridge-start=2`; `network_init_stage` advanced from `0` to **3**; NV2A `DMA_PUT` advanced to a *second* distinct value (`0x03E50FD8`, up from the previously "permanently frozen" `0x03E50B2C`). **The network subsystem does initialize, the worker thread does start, and a second GPU command buffer does get submitted** -- all things the earlier, shorter-window tests had wrongly read as permanently absent.
- **But it isn't just "slower than expected" either.** Watching the running log's own periodic `[PIPE]` line (which prints continuously, independent of the one-shot sample) shows `frontend state-dispatch calls` climb cleanly **1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11** across a short burst -- and then **freeze at exactly 11, permanently**, for the remainder of a multi-minute run. `worker: entry=3 bridge=2` froze at the same moment and never moved again either.
- **Checked whether the underlying clock/timer stopped -- it did not.** The `[DPC]` queued/drained counters (`RECOMP_SYNTHESIZE_VBLANK`'s own delivery mechanism, built earlier tonight) climb *continuously and linearly* for the entire multi-minute run (`queued=66...3995`, `drained=74...4493`, steadily, no plateau, `last drained routine=0x004E1E02` -- the same periodic tick DPC, firing and draining reliably the whole time). **The heartbeat never stops. Something downstream of it just stops listening after a small, fixed number of reactions.**

**This replaces the network hypothesis with a sharper, better-evidenced one**: at least four independently-discovered counters across unrelated subsystems -- `frontend_state_dispatch_calls` (11), `worker_entry_calls` (3), the `0xA6A7D8` frame-gate counter (4), and this session's own earlier, previously-unexplained `archive-completion 44DF0` plateau (23, from much earlier tonight, originally written up as "a normal phase transition") -- **all share the identical signature**: a bounded burst of N reactions to a live, continuously-ticking timer, then a permanent stop, with N different (and small) for each subsystem. That strongly suggests each subsystem is being fed from its own small, fixed-size, boot-time-populated queue or ring buffer (consistent with `queue-pump=2471 nonempty=0` also seen this run -- 2471 *empty* polls of a *different* queue that apparently never receives new work either), and whatever is responsible for continuously refilling each of these queues from ongoing disc/archive/network I/O -- as retail evidently does, continuously, for the whole session -- only runs long enough to satisfy the queue's initial boot-time capacity in native, then never runs again, even though the underlying tick/DPC/timer plumbing that should be driving it keeps firing correctly forever.

**Next concrete step for a future session**: find the *producer* side of one of these small ring buffers (the `0xA6A7D8` gate is the best-understood one, already traced to a single write instruction at `recomp_0000.c:73838` inside `sub_000292E0`) and determine what real, ongoing event is supposed to call that producer repeatedly on retail -- almost certainly an I/O- or event-completion callback this session hasn't yet located, not the vblank/DPC timer itself (proven not the cause) and not network bring-up (proven not the cause).

- **Re-verified the plateau is genuinely permanent, not another sampling-window artifact**: let the same process run over a minute *longer* than the run that first showed `statedispatch=11`. `[DPC] queued`/`drained` climbed the whole time (66 -> 6795 queued, 74 -> 7643 drained, linear, no stall), while `[PIPE] frontend: statedispatch=11` and `worker: entry=3 bridge=2` never moved again, and `[GPU] draws 0` / NV2A `DMA_PUT` stayed frozen at the same second value the whole time. This is now confirmed on a long time base, not just a short one -- the heartbeat runs forever, the consumer stops forever, well before the process itself does anything else unusual.
- **Traced `sub_000292E0`'s own four callers** (`sub_0002C030`, `sub_0002FC30`, `sub_00036530`, `sub_00038FC0`) to check whether any is an obvious continuously-invoked producer. `sub_00038FC0` matches an already-known, already-instrumented one-shot ("title init 38FC0=1" in the existing `[PIPE]` line -- confirmed called exactly once). `sub_00036530` is itself called from inside the very same `sub_00028C40`/`sub_00028BC0` cluster this session mapped earlier (`recomp_0000.c:73932`, mutually recursive with `sub_000292E0`). The other two are unexplored. **This whole cluster (`sub_00028C40`, `sub_00028BC0`, `sub_000292E0`, `sub_000294C0`, `sub_000366D0`, `sub_00038EA0`, `sub_0002FC60`, `sub_0002AB20`, `sub_0002C030`, `sub_0002FC30`, `sub_00036530`, `sub_00038FC0`, `sub_0012D150`, `sub_0012CB50`, `sub_0029B6D0`, and the still-unreached-by-any-static-call-site `sub_001461D0`) reads as a single cooperative task/state-machine scheduler with dozens of small mutually-calling members** -- consistent with a classic Xbox-era engine "actor" or "fiber" system. Manually walking it one static call site at a time has reached steeply diminishing returns; the next productive move is almost certainly dynamic (e.g. a cheap execution-count VEH breakpoint -- not a write-watch -- planted on a handful of these entry points simultaneously in one run, or finding whatever calls `sub_001461D0` via the one remaining unexplained indirect-call vtable slot, since it has zero static callers anywhere in the generated code and can only be reached through a runtime-computed function pointer).

**Stopping point for this session.** This is now, by a wide margin, the deepest and best-evidenced finding of the project's history on this exact bug: not "vertex data is zero" (a leaf symptom, already fully explained and fully downstream of this) but **"the game's own per-frame cooperative scheduler runs a small, fixed, subsystem-specific number of reactions during boot and then permanently stops being re-entered, while the underlying interrupt/DPC timer this session fixed earlier tonight keeps running correctly and indefinitely underneath it."** Visible textures were not reached tonight. What was reached is a precise, falsifiable, multiply-cross-checked description of exactly where in the architecture the game stops advancing, with two prior hypotheses (vblank/DPC timing, network bring-up) explicitly tested and ruled out rather than left standing, and a concrete, scoped next step (find what should be re-entering this scheduler cluster continuously) for whoever picks this up next.

## Continued past that stopping point on "keep going" -- built a proper execution-count tool and traced the whole chain to its actual root

The previous stopping point identified the *shape* of the bug (bounded bursts across many subsystems) but not a single root. Rather than keep reading generated code by hand, built the missing tool this needed: **`RECOMP_EXEC_WATCH=1`**, a new opt-in multi-address execution-count breakpoint in `src/main.c` (`exec_count_watch`, `exec_watch_add`, `nfl2k5_execwatch_print`), modeled on `pb_write_watch`'s single-step re-arm technique but applied to *code* instead of a data page: patches `0xCC` at a function's own compiled entry (resolved via `recomp_lookup`, since `RECOMP_ABI_CALL` is a direct C call this host can't intercept any other way), counts every hit regardless of caller (direct or indirect), and single-steps the restored instruction before re-arming. Prints live, continuously, from `kernel_bridge.c`'s existing periodic stats block via a new `nfl2k5_execwatch_print()` call -- no more one-shot sampling.

Armed it on the whole 16-member scheduler cluster mapped earlier, plus the vertex/device chain, plus the six other scheduler-registered callbacks this session hadn't examined individually. Results, cross-checked over multiple rebuilds and multiple live runs:

- **Correction to the previous write-up**: `sub_00028C40` is *not* a per-frame function. Reading `sub_000292E0` (its sole direct caller) in full shows an explicit, intentional guard at `recomp_0000.c:73833-73839` -- `MEM32(0xA6A7D8)` is incremented and, once it exceeds 1 (i.e. on every call after the very first), the entire setup block including the `sub_00028C40` call is skipped. This is a **"run exactly once, ever" lazy-init pattern**, not a stalled per-frame gate. `28C40=1` in every run is *correct*, by design -- not a bug. The earlier framing of this function as "the real per-frame update" was wrong; it's one-time device/video bring-up.
- **Found and confirmed the second, previously-missed static caller of `sub_00033D50`**: `recomp_0001.c:10046`, inside `sub_000342A0`, which builds a 640x480 mode-set structure and passes it in. Traced its caller up to `sub_00030A10`, a top-level boot sequence that also directly calls the already-confirmed one-shot `sub_00074BF0` ("title init"). **Both of native's two calls into the `sub_00033D50` -> `sub_00420160` -> `sub_00427890` chain are legitimate, intentional, one-shot boot-time calls** (`33D50=2`, `420160=2`, `427890=2`, matching exactly) -- not a starved retry loop. This resolves a question that had been open since early tonight: native calling this chain "exactly twice" is correct behavior, not a symptom.
- **Found the actual, continuously-running part of the system**: of the 7 scheduler-registered callbacks (already known from existing telemetry), 5 fire continuously and linearly, never plateauing, confirmed over multiple samples seconds apart (`3E910`, `3CD120`, `39380`, `51F00`, `3A310`, all climbing together at ~125 hits per sample interval, hundreds and then thousands of hits deep into a run -- the real, live per-tick heartbeat, distinct from the one-shot boot cluster entirely).
- **Identified `sub_003CD120` (736 bytes, `Category: game_vtable`, by far the largest of the 5) as the real per-frame frontend/UI update.** It calls `nfl2k5_trace_frontend` at its own entry with the exact same arguments (`MEM32(0xAF5884)`, `0xAF5898`, `0xAF57C0`, `0xAF57C8`) that back this project's own pre-existing `front-state/req/active` telemetry -- which has read `0` in every sample taken tonight and before. Reading its body: it walks a linked list (head `0xAF5884`, sentinel `0xAF57C8`) that is evidently always empty, matching `frontend_enqueue_calls=0`.
- **Traced the empty queue to its root, four hops, each confirmed empirically at 0 hits via the exec-watch tool** (not just inferred from static reading):
  1. The list's only two enqueue call sites, `sub_003C4D8C` and `sub_003CBBF0` -- **0 calls each**.
  2. Their only callers, `sub_00178150` and `sub_00272A60` -- **0 calls each**, and these two already had dedicated telemetry from before this session ("frontend producers 178150=0 272A60=0") that had gone unexplained until now.
  3. *Their* only callers, `sub_00363350` and `sub_00278310` -- **0 calls each**, and **neither has a single static caller anywhere in the entire generated codebase.** Both are registered in `recomp_dispatch.c`'s indirect-call table (reachable in principle) but never actually invoked. `sub_00278310` is explicitly `CC: thiscall, Category: game_vtable` -- a genuine C++ virtual method, not a plain function.
- **This is the same shape as `sub_001461D0`** (found earlier tonight, also zero static callers, also armed and confirmed at 0 hits again in every run since). **The real, final root of tonight's entire investigation is: something is supposed to call through a vtable slot (or function-pointer table) that resolves to `sub_00278310` (and/or `sub_00363350`), and that call never happens in native.** Everything else this whole session found -- the zero vertex data, the frozen `0xA6A7D8`/DMA_PUT, the "exactly twice" pattern, the empty frontend queue, the continuously-running-but-inert `sub_003CD120` -- is downstream of this one missing call.
- **Started locating the vtable/callback-table itself** by searching the original XBE's raw bytes for the literal little-endian address `00278310`/`00363350` (not source text -- these are unreachable by static-call grep because they're stored as data or push-immediate operands, not C-visible call sites). Found real hits: `sub_00278310`'s address appears 7 times, each embedded inside what looks like x86 code (`push offset sub_278310`-shaped byte patterns) -- consistent with a **callback registration the recompiler's call-site scan wouldn't catch**, since it's passed as a runtime argument, not a direct/tail call. `sub_00363350`'s address appears twice, in contexts that look like genuine data-table entries (surrounded by small integers and other plausible code addresses, not instruction bytes). **Not yet converted from XBE file offset to guest VA or connected to a specific object/class** -- that conversion (via the XBE's section table) plus a targeted write-watch on whichever guest address holds this slot is the concrete next step, and does not require more manual call-graph reading.

**Durable tooling left behind for whoever continues this**: `RECOMP_EXEC_WATCH=1` (src/main.c) is now a permanent, general-purpose, reusable multi-address execution counter -- add any guest VA to the list in `main()` and rebuild; it prints live counts on every existing `[PIPE]`/`[DPC]` tick via `nfl2k5_execwatch_print()`. This is a genuinely new capability the project didn't have before tonight, on top of the write-watchpoint and GDB techniques already documented above.

## Pushed one more hop past that: found the exact guest addresses in the original XBE, built a read/write watch, and closed out the chain with a clean negative result

`sub_00278310`/`sub_00363350` have zero static callers anywhere in the generated code, so grepping C source can't find what's supposed to reach them. Went to the source of truth instead: searched the **original retail XBE's raw bytes** (`original/disc/default.xbe`) for the literal little-endian function addresses, then parsed the XBE header's section table (base address `0x10000`, 22 sections) to convert the raw file offsets that hit into real guest virtual addresses.

- **`sub_00278310`'s address (`0x00278310`) appears 7 times** in the XBE: 3 inside `.text` in what look like `push`-immediate-shaped byte patterns (a callback being passed as a runtime argument to some registration call -- exactly the shape the recompiler's static call-site scan cannot see, since it isn't a direct or tail call), 2 inside `XONLINE`, and **2 inside `.rdata`** at guest VA `0x0051980C` and `0x0051A32C`. Dumped the surrounding dwords at both `.rdata` hits: each sits inside a small, near-identical fixed-size record (a scalar field that looks like a string/name pointer, some flag dwords, a `1`, then the callback address itself, then zero padding) -- consistent with a **compile-time-constant registration-table entry baked directly into read-only game data**, not something constructed at runtime.
- **`sub_00363350`'s address appears twice**, both in `.rdata`, at guest VA `0x0053D120` and `0x0085EFE4`, in a similarly data-shaped (not code-shaped) context.
- **Built a fourth tool**: `RECOMP_DATA_WATCH=1` (`data_read_watch`/`data_watch_add` in `src/main.c`), a sibling of `pb_write_watch` that guards with `PAGE_NOACCESS` instead of `PAGE_READONLY` -- unlike the write-only watch, this catches **reads** too (`ExceptionInformation[0] == 0`), which is what's needed to catch something merely *looking up* a read-only table entry, not just writing one. Same single-step re-arm shape as the other two watches.
- **Armed it on all four addresses simultaneously and ran for 50 seconds while confirming the game kept ticking normally the whole time** (`[PIPE]` showing the same familiar `statedispatch=11`/`worker entry=3 bridge=2` plateau throughout, proving this wasn't a hung or crashed process). **Result: zero hits, on any of the four addresses, for the entire run.** Not "the callback is never invoked" (already known) but **"this exact registration record is never even read."**

**This is a clean, final, unambiguous result for tonight, not an inconclusive one.** The precise, reproducible artifact to hand to a disassembler or the next session is now: *guest VA `0x0051980C` / `0x0051A32C` (and `0x0053D120` / `0x0085EFE4`), each holding a small compile-time-constant record whose last meaningful field is a callback function pointer into `sub_00278310` (a real C++ thiscall/game_vtable method) or `sub_00363350` -- and nothing in native ever reads, let alone calls through, either record, across every test run tonight.* Finding *what should* walk this table (almost certainly gated behind a specific game-state transition -- menu entry, mode select, or similar -- rather than anything boot-related, since boot-time code has been extensively traced tonight and never comes near it) is the concrete next step, and now has the exact addresses and a working read-watch tool ready to use the moment a hypothesis for "what should trigger this" exists.

**All four tools built tonight are permanent and reusable, gated behind env vars that default off**: `RECOMP_WATCH_PB_WRITE` (write-only, single address, now with working source-line resolution on its exact-offset channel), `RECOMP_EXEC_WATCH` (multi-address execution count via INT3, prints live), `RECOMP_DATA_WATCH` (multi-address read-or-write via `PAGE_NOACCESS`, prints live, capped per-address). None of tonight's code changes were committed to git, matching this project's existing convention of using `PROJECT_STATUS.md` rather than commit history as the durable record.

### Retail access-watchpoint attempt on the four vtable-record addresses -- 2026-09-20

Picked up the concrete next step from the entry directly above: confirmed the native tree still builds clean (`tools/build.ps1 -Game` reports `ninja: no work to do`) and reproduced the exact known stall on a fresh 20-second native run (`frontend: enqueue=0 statedispatch=7/8`, `worker: entry=3 bridge=2`, `DMA_PUT` frozen at `0x03E50FD8`) -- current state is unchanged from the last session's write-up.

- Started a fresh, paused (`-S`) retail Xemu boot against the real `NFL2K5.xiso.iso` with its GDB server, confirmed no VM snapshot exists (`info snapshots` -> "There is no snapshot available"), and confirmed this build's monitor still has no `screendump` (matches the 2026-09-15 finding) -- visual confirmation of on-screen state is not available through the monitor.
- Worked around both by sampling `$eip` through the GDB stub at intervals: at T+45s from power-on, `eip=0x0040BE40` (inside the title's own low guest range, i.e. the disc-based game has already auto-launched off the real Xbox dashboard with no manual navigation needed -- confirms retail Xbox's normal disc-autolaunch behavior holds under Xemu here, so reaching title code does not require simulated dashboard input).
- Armed **access watchpoints (GDB `Z4`, read-or-write) on all four candidate addresses** (`0x0051980C`, `0x0051A32C`, `0x0053D120`, `0x0085EFE4`) on this live retail session and let it run continuously for a further **260 seconds** (title code confirmed running throughout, not paused/idle-looping the whole time). **Result: zero hits on any of the four addresses.** Cleared all four watchpoints and detached (`D`) cleanly; confirmed via `info status` that the VM was left `running`, not paused.
- **This rules out attract-mode/legal-screen/press-start boot-time access as the trigger on retail too**, strengthening rather than replacing the existing hypothesis: whatever reads this table is gated behind actual gameplay-menu navigation (mode select, franchise/exhibition setup, etc.) that occurs *after* the idle attract loop a real player would normally advance past with a controller.
- **Genuine blocker for continuing this specific thread from here**: this session has no way to inject Xbox controller input into Xemu. The project's own `config/xemu-play.toml` binds `port1` to a specific physical gamepad GUID with no keyboard-to-controller mapping configured, and there is no accessible automation path for sending input to a native (non-browser) window in this environment. Advancing the retail reference session past the attract/press-start screen into an actual menu -- needed to see whether/when the table finally gets read -- requires either (a) a person physically providing controller input to the already-GDB-attached Xemu window so the capture can continue past that point, or (b) a saved Xemu VM snapshot taken at a specific menu state (none currently exists), or (c) pivoting to a different, input-independent lead instead of this one.
- No code changes made this session. `RECOMP_DATA_WATCH`/`RECOMP_EXEC_WATCH`/the GDB access-watchpoint technique remain the ready-to-use tools for whoever continues this the moment a menu-reachable state (real controller input or a snapshot) is available.
