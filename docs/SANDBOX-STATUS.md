# Sandbox implementation status

Updated October 23, 2026. This is an in-progress feature, not a finished
Garry's Mod equivalent. Original Situation Mode is the loading foundation;
Sandbox owns cloned setup rows/events and separate session state.

## Implemented

- Optional `Game Modes > Sandbox`, enabled by `mods/sandbox/pack.json`;
  `sandbox=0` in `mods/features.ini` disables it. `--vanilla` omits the entry.
- Independent transparent HTML browser with controller tabs, lists and
  numeric controls. Xbox and PlayStation prompts use the existing input layer.
- Guest-thread camera/time controls at traced gameplay update/viewport hooks.
  Native/interpreted code use corresponding hooks. No software renderer or
  suspended render/audio threads.
- Session score, clock, quarter, down, spot, distance and timeout editing.
- Five bounded scene files under the user Sandbox folder, checksum validation,
  backup before replacement, and up to 32 undo states. Current scene files
  contain camera/game state, not actors or poses. Team/stadium identity must
  match before restore.
- Team/stadium reselection through the native quit/load flow, queued outside
  input polling and nested FSM pop loops. Historic/custom team iteration is
  enabled for Situation flow 8 only while Sandbox owns that setup.

## Actual evidence

| Check | Result | Evidence |
|---|---|---|
| Regular compilation, first integrations | PASS | `logs/build-sandbox-20261022*.log` |
| Scene bounds/checksum/JSON validation | PASS | `logs/sandbox-validate-c.stderr.log` |
| Camera basis/FOV math validation | PASS (isolated) | Called by `--validate`; not a gameplay pass |
| Loaded private Situation rows/events and callback routing | PASS | `logs/sandbox-validate-c.stderr.log` |
| Actual Sandbox setup entry and Team Select | PASS | `logs/sandbox-live-regular-c.stderr.log` |
| Original callback-dispatch bug | Reproduced and corrected | Earlier `sandbox-live-regular-b` crash attempted to interpret FEF00000; public native lookup now handles that range |
| Editor after loading field | PASS on corrected build | Actual field and editor in `logs/sandbox-live-regular-d-013.bmp`; live viewport captured in the corresponding log |
| Corrected launch marker | PASS in Regular launch | `SANDBOX_LAUNCH_ACCEPTED` at actual flow-8 branch 002C0E7C; no longer mistakes the launch pop for cancellation |
| Free camera / pause / frame-step / live edits | PARTIAL; individual controls need verification | Native camera attachment logged; resumed game with help HUD captured in `sandbox-live-regular-d-039.bmp` |
| Scene file load/save in gameplay | NOT YET VERIFIED IN GAME | Isolated validation is not a substitute |
| Regular and SOFTDRINK startup validation | PASS | Both `sandbox-stage-validate-*` logs; isolated checks only |
| SOFTDRINK gameplay integration | NOT TESTED | Both code paths are hooked, but no gameplay pass claimed |

The user is navigating the current test. Automation inputs stopped when they
confirmed this. The game-owned numbered BMP captures and crash logs are retained.

## Unfinished scope

The requested stadium-first empty-world flow, in-world team/player spawn
browser, player move/rotate/pose/possess, props, per-player jobs/ratings,
formation duplication, route recording and outcome locks are not complete.
The player list is currently read-only and tool actions are disabled. The
animation pool has not yet been safely mapped to roster actors; no guessed
roster/stat pointer is being used as a transform.

The HTML has mouse handlers, but the offscreen host has no native pointer
forwarding yet. Controller/keyboard navigation is the connected path.

## Images

`outputs/sandbox/menu-preview-20261022.png` and
`outputs/sandbox/scene-preview-20261022.png` render the actual HTML/CSS/JS.
They are explicitly marked **PREVIEW — NOT IN GAME** and use a plain
background. They are not gameplay screenshots or evidence of working tools.

## Main files

- `src/nfl2k5_sandbox.cpp/.h`: state, command queue, controller handling,
  scene transactions and serialization.
- `src/nfl2k5_sandbox_menu.c/.h`: cloned native setup and launch/return lifecycle.
- `src/nfl2k5_sandbox_world.c/.h`: traced camera/time hooks, guarded native calls.
- `src/nfl2k5_sandbox_ui.cpp/.h`, `mods/sandbox/ui/`: separate browser host/assets.
- Small integration points: input HLE, plugin shims, presentation lifecycle/HUD,
  native callback dispatch, team flow gate, generated/interpreter hook wiring,
  build source list and startup validation.

## Session 2026-10-23 (Opus agent): static review, data validation, blockers

This session performed **source/data review only**. No build was produced and no
live game was run, for the hard reasons recorded under "Blocked" below. Nothing
here is a gameplay PASS; items I could check statically are marked as such.

### Corrected state of the "move/rotate a player" tool (build-order step 2)

The move/rotate tool the task asks for **already exists** as the safe
posed-scene ("photo pose") route, and is the fallback the task itself describes.
It does NOT write node+18 `+30/34/38`; it drives the private pose palette:

- `src/nfl2k5_sandbox.cpp`: `grab_mode`, `grab_tick()` (lines ~275-290),
  `finish_grab()`, `pose_snap`. Left stick moves in the camera plane
  (`pose.x/z`), triggers raise/lower (`pose.y` from `a[7]-a[6]`), right stick
  rotates (`pose.yaw`), optional snap to 91.44 cm (1 yd). All bounded to
  +/-500 cm / +/-180 deg, paused-only, re-validated against clock+identity
  every tick via `nfl2k5_sandbox_pose_get/_set`.
- The on-field player list is **already selectable** (`focus_player` command,
  `selected_player`, jersey-numbered rows in `sandbox_menu.c` build via
  `nfl2k5_sandbox.cpp` ~line 355), not read-only as the top section states.
- This writes a transformed PRIVATE palette copy at draw time
  (`nfl2k5_sandbox_pose.c` `begin_draw`/`end_draw` + `transform_palette`);
  the live simulation actor, original bone palette, culling, shadows and
  nameplates are untouched. On unpause the simulation position is unchanged
  (the sim actor was never moved) — this is the trace-sanctioned behaviour, so
  the "snaps back on unpause" risk does not apply to this route.

Verdict: the node+18 true-teleport in step 2 was correctly NOT attempted
(SANDBOX-ACTOR-TRACE: node+18 ownership/update semantics not established).
The safe move/rotate route is coded but still **NOT VERIFIED IN GAME**.

### Statically verified this session

| Check | Result | Evidence |
|---|---|---|
| Pose draw hooks present in generated source | PASS (static) | `nfl2k5_sandbox_pose_begin_draw`/`_end_draw` at `src/recomp/gen/recomp_0004.c:20278,20291`; defined in `tools/apply-gen-patches.py` (SANDBOX_POSE_DRAW_BEGIN/END, sub_0008FD90, object=ecx/palette=edx/packet=[esi+2C]) |
| Viewport camera + frame-delta hooks present in generated source | PASS (static) | `nfl2k5_sandbox_world_camera` at `recomp_0002.c:80642`; SANDBOX_FRAME_DELTA / SANDBOX_VIEWPORT_CAMERA in `apply-gen-patches.py` |
| `draftposes.bin` parses to the documented format | PASS (static) | 329,552 B, 8 clips (idle/loop/walk/shake/shake2/raise/trophy/walk2), frames+fps match SANDBOX-ANIMATION-TRACE exactly, 0 trailing bytes, 0 non-finite floats, quat norm^2 in 0.99981..1.00014 |

### Genuinely unfinished: the draftposes.bin clip picker (build-order step 3)

The current `nfl2k5_sandbox_pose.c` `begin_draw` transforms the **live** source
palette (translate/rotate/scale only). It does NOT load or apply the baked
clips. The Sandbox menu shows "Spawn / animation clips / possess —
Unavailable". So step 3 (pick one of the 8 clips, scrub to a frame, freeze) is
the one tool still to build. Drop-in design for the next session:

1. Extend `Nfl2k5SandboxPose` with `char clip[12]; float clip_time; uint8_t
   frozen;` (keep existing x/y/z/yaw/scale). This touches scene (de)serialize
   in `nfl2k5_sandbox.cpp` and the UI row builder — change all three together
   or the build breaks.
2. In `nfl2k5_sandbox_pose.c`, add a bounded host loader for
   `mods/eras/draft/files/draftposes.bin` (validate magic/count/frames/fps,
   reject non-finite and near-zero quats; keep last-good cache; no per-draw
   file I/O). Resolve the path via the Eras path helper; missing file ->
   disable the picker with a message, never activate Eras.
3. Sampler = the draftshow algorithm (SANDBOX-ANIMATION-TRACE "Host sampling"):
   clamp t*fps, slerp-pick shortest hemisphere, lerp+normalize quats, lerp
   trajectory.
4. Palette builder = draftshow `actor_write` math using the SELECTED player's
   own bind table read live from the validated draw object: shape `object+8`,
   count `+0x50`, table `+0x64`, per-transform `0x70` (bind-abs `+0x40`, local
   `+0x50`, name `+0x60`, parent `+0x64`). Require the 25 expected
   names/parents, finite locals, parents < children. Anchor root in place
   (preview limitation), reuse existing `palette_valid` + bounds.
5. In `begin_draw`, when `s_edits[id].clip[0]`, build the clip palette into the
   private buffer instead of copying the source, then apply the existing
   translate/rotate/scale `transform_palette` on top. Keep the `0x01000000`
   high-body exclusion and the balanced end_draw restore exactly as now.
6. Picker UI feeds clip key + a frame scrubber (frozen time) through the
   existing `pose_set` command path. Freeze = fixed `clip_time`.

### Blocked this session (recorded, not done)

- **Build + all in-game verification BLOCKED by disk.** `E:` had 4.2 GB free
  (100% full); the project rule requires > 15 GB. That is below the floor for a
  safe relink (build/ is 7.3 GB), the sandbox `NFL2K5_ROOT` with junctions + a
  sparse saves copy, and raw BMP frame capture. Did not build, did not launch,
  did not create a sandbox root, to avoid filling the drive and risking the
  real `saves\UDATA`.
- Because of that, step-1 live verification (free cam / pause / slow-mo /
  one-frame step / FOV / live game-state edits / scene save+load / clean exit)
  on `--retail` and SOFTDRINK v0.6.1, the step-2/3 live tests, and the labelled
  `logs/sandbox-sheet.png` could not be produced. All remain NOT YET VERIFIED
  IN GAME.
- No code was committed this session: the clip picker requires a cross-file
  struct change that cannot be compiled on this disk, so injecting it untested
  would risk leaving the tree non-building. The move/rotate route is already
  present from the prior (ChatGPT) work and is unchanged here.

Next session, once `E:` is above 15 GB free: implement step 3 per the design
above, then run the step-1/2/3 live matrix and build the sheet.

## Session 2026-10-23 (Opus agent, cont.): live build + partial in-game test

Disk was unblocked (37 GB free). Built `build\Release\NFL2K5_sandbox.exe` and ran
background off-screen tests on retail (`--retail`). Sheet: `logs\sandbox-sheet.png`.

### Built and compiling (clip picker, step 3)

The draftposes clip picker is now implemented end to end in
`src/nfl2k5_sandbox_pose.c` (+`.h`) and wired into the editor UI in
`src/nfl2k5_sandbox.cpp`:
- Host clip loader/validator; sampler; per-player 25-bone bind-table reader
  (`object+8` shape, `+0x50` count==25, `+0x64` table, `0x70` stride, name/parent
  checks); column->row matrix hierarchy; `build_clip_palette` anchored to the
  live head joint; `begin_draw` uses it when a clip is selected and falls back
  to the live palette on any failure (no corruption).
- `Nfl2k5SandboxPose` gained `clip` + `clip_time`; `Animation clip` and
  `Clip frame` rows + `pose_clip`/`pose_clip_time` commands (scrub + freeze).
- Compiles clean in the game build. `draftposes.bin` independently validated.
  **Not yet exercised in a live match** (see entry blocker below).

### Eras-independent pose data (coordinator decision 2026-10-23)

Sandbox ships as its **own** standalone pack and must not require Eras. Done:
the loader now prefers `mods\sandbox\files\sandboxposes.bin` and only falls back
to `mods\eras\draft\files\draftposes.bin` when present (self-contained; no Eras
runtime dependency — path resolved from the working dir, not the Eras service).
**Next step (not done, do not half-ship):** a first-run/offline builder that
bakes `mods\sandbox\files\sandboxposes.bin` from the player's disc by reusing
`tools/eras/draftshow/build-draftshow-pack.py`'s `bake_poses` (identical
`DSPOSE01` format), gitignored/disc-derived. Until then the picker uses the
Eras file as fallback when installed.

### In-game: CONFIRMED vs NOT CONFIRMED (retail, honest)

CONFIRMED on screen (frames in `logs\sandbox-test\`, montage in the sheet):
- `Game Modes > SANDBOX` entry appears (`sbx-retail-gm_submenu.png`).
- `SANDBOX SETUP` cloned-Situation screen (`sbx-retail-sandbox_setup.png`).
- `SANDBOX Team Select` (`sbx-retail-sandbox_teamselect.png`).
- Match start -> Sandbox arms and **enters the world, simulation paused, live
  viewport camera captured** (log: `world entered ... (simulation paused)`,
  `captured live viewport camera=00A82940 eye=-883.7,308.3,-3792.0`).

NOT CONFIRMED in game this session (built, unverified): free-camera move/look,
pause/slow-mo/one-frame-step effect, FOV, on-screen game-state edits, the
grab/move+rotate poser, the animation clip picker, and scene save/load.

### Blockers found (real limits)

1. **HTML editor overlay failed to load in-match** ("web error 0/9"). Diagnosed
   as WebView2's virtual-host file server refusing to serve `index.html`
   through the sandbox root's `mods\sandbox` **junction** (reparse point). Fix:
   the test harness now makes `mods\sandbox` a **real copy**, not a junction
   (verified non-reparse). The product itself is unaffected (real folder in a
   normal install). Re-verification of the overlay in a match is still pending.
2. **Scripted menu entry is unreliable on this loaded machine** (3 NFL2K5
   instances running -> ~7-13 FPS; the press-file driver drops/races repeated
   presses, and the title/attract screens are slow to respond). Reached a real
   Sandbox match once, but could not re-reach one deterministically.
3. **The press-file harness cannot inject analog sticks** (only buttons +
   triggers + bumpers), so free-camera fly and the stick grab-move can't be
   driven that way; their menu-driven equivalents (presets, FOV, pose offset
   rows, clip picker) can.

### Deterministic verifier left in place (for the next focused session)

`NFL2K5_SANDBOX_AUTOTEST=1` (off by default, in `src/nfl2k5_sandbox.cpp`
`at_tick`): once a Sandbox match is active it drives each editor control
directly via the internal command path and logs a `[SANDBOX-TEST]` marker per
step (camera presets, camera +500cm, FOV, pause, slow-mo, one-frame step,
game-state edit before/after, select+frame player, close-up, move/rotate poser,
clip pick+scrub+freeze, scene save/load), dwelling ~3.5 s per step so each is
frame-captured. It needs no HTML overlay and no analog sticks. **Next step:**
in a session with fewer competing instances (higher FPS), reach one Sandbox
match, let `AUTOTEST` run, and capture the per-control frames + markers to turn
the NOT-CONFIRMED list above into CONFIRMED.

## Session 2026-10-23 (Opus agent, cont.): AUTOTEST run end to end on a free machine

Machine free (one instance, no competing NFL2K5). Rebuilt `build\Release\NFL2K5.exe`
(incremental, EXITCODE 0), copied to `NFL2K5_sandbox.exe` (verified the 20
`[SANDBOX-TEST]` strings are compiled in; the prior copy was stale). Launched ONE
off-screen background instance on `--retail` from a sandbox `NFL2K5_ROOT`
(`E:\eras-sbx\sbx-root`: junctions + sparse saves copy + **real copy** of
`mods\sandbox`), `NFL2K5_SANDBOX_AUTOTEST=1`, `NFL2K5_PRIVATE_PACKS=1`, sandbox
`-AppData`. Drove the menus by press file to a real Sandbox match
(Game Modes > SANDBOX > SANDBOX SETUP > Team Select > Browns @ Texans) and let
AUTOTEST run all 19 steps to `COMPLETE`. Real `saves\UDATA` signature verified
unchanged before and after. Sheet: `logs\sandbox-verify-sheet.png`; per-control
frames: `logs\sandbox-test\keepers\`. Perf note: ~5 fps, CPU-bound; menu D-pad
edges drop at that frame rate, so one directional press per press-file write with
a ~5 s dwell is the reliable pattern (rapid/batched repeats are eaten).

### CONFIRMED on screen this session (frames + markers)

- **Menu path / entry**: `Game Modes > SANDBOX` row (appended last), `SANDBOX SETUP`
  cloned-Situation screen, `SANDBOX Team Select`, match start -> `[SANDBOX] Armed`
  / `world entered (simulation paused)` / `captured live viewport camera`.
- **Free camera + presets / move / FOV**: the rendered field view changes between
  presets; the overhead preset visibly drives the top-down field view
  (`sbx-06-pause.png`, `sbx-08/09`). Markers: camera move `x 0->500cm`, `FOV 60->72`.
  (Steps 0-2 are masked by the pre-snap "Offensive controls / LOADING" intro card,
  which stays up because the sim is paused on entry.)
- **Select + frame on-field player**: `focus_player` listed 22 actors, selected
  id=76 #96; camera reframed from overhead to the player (pixel delta 17-22 between
  steps). Selection + camera framing work.
- **Game-state edit on screen**: `home_score 0->3` via the command path is visible
  on the native scorebug: `HOU 0` (`sbx-08-frame-step.png`) -> `HOU 3`
  (`sbx-09-score-edit.png`).
- **Scene save -> load**: `scene SAVE slot 1 -> OK`; after editing the score down,
  `scene LOAD slot 1 -> OK` restored `home=3` and the camera. State round-trips.
- **HTML editor overlay now LOADS** (the real-copy `mods\sandbox` fix): log shows
  `[SANDBOX] Browser started ... mods\sandbox\ui`, `loading https://presentation.local/mods/sandbox/ui/index.html`,
  `UI: loading` -> `UI: running`, with **no `web error 0/9`**. The junction bug is
  fixed. It is not captured in the BMP because WebView2 composites outside the
  off-screen framebuffer.

### NOT CONFIRMED this session (honest, with cause)

- **Pause / slow-mo / one-frame step**: commands executed (`speed=100/0/25%`,
  one-frame step logged) but Sandbox arms at the **pre-snap play-call screen**,
  where no play is running, so there is no motion to slow or step. No visible
  effect to capture here. (Needs the sim armed during a live, snapped play.)
- **Move / rotate player poser**: `pose_x x10 -> 0cm`, `pose_yaw x4 -> 0deg`;
  **0.00 pixel change** across steps 12-17. `nfl2k5_sandbox_pose_get`/`_set` find
  no valid live draw actor for the selected player at the pre-snap play-call
  screen, so the private pose palette never engages (`pose_status` =
  `Native model placement`). The route compiles and is wired, but produces no
  on-field movement in this state.
- **Animation clip picker**: 8 clips load, but `pose_clip -> clip=0`,
  `scrub time=0.00s`, status `Native model placement`, 0.00 pixel change. Same root
  cause as the poser: no valid player draw object at pre-snap.

### Root cause and next step

The Sandbox editor activates at the **pre-snap play-call screen** (that is when
`world_enter()` reports the field camera ready). There the camera and game-state
tools work and are visible, but the player-manipulation tools (move/rotate poser,
clip picker) have no live draw actor to act on, and pause/slow-mo/step have no
running play to affect. Next step: arm/activate the editor during a **live snapped
play** (or drive the sim one play in, then hand control to Sandbox) so the poser,
clip picker, pause and slow-mo have a running simulation and valid per-player draw
objects to manipulate; re-run AUTOTEST there. The overlay should be re-verified in
a foreground (non-off-screen) capture since WebView2 is not in the off-screen BMP.
