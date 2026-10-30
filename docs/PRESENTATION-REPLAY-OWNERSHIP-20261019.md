# Automatic replay ownership — CBS integration

## Why a title-derived lifecycle is necessary

The automatic replay is screen descriptor `004E3F38`, not a popup whose
caption happens to contain REPLAY. Its state at `AFA078` follows
`1 → 2 → 3 → 4 → 5 → 0`; state7 is the separate diversion into the pause
menu. The stack at `A84B18` includes the descriptor at its **inclusive top
index**, stored at `A84C18`. Checking only slots below that index misses the
currently executing screen. Retained raw phase values without the descriptor
do not prove an active replay.

An actual CPU-versus-CPU baseline (`native-review-sack01`) exposed a second
bug: the presentation host was stopped after2.5seconds of stationary match
and play clocks during an ordinary automatic replay, and recreated later.
The active replay descriptor now supplies match activity. HTML state exports
`context.replayActive`; CBS hides its Eyebar and cancels unrelated stat cards
through that interval. This does not change the football clock.

The subsequent native trial proved that a pre-replay quiet camera can also
expire the old activity grace. A copied entrance seed now survives initial
page startup and binds to its new generation; until painted it cannot hide
native art. More fundamentally, successful native match initialization
(`64C70 → 64710`, after return64C82) and actual match exit (`645D0`) now
provide an authoritative host lifetime. The failed initialization branch
jumps64C88 and cannot mark a match active. Quiet match cameras keep the page
warm; exit clears activity immediately, including retained replay/clock
globals. A generic `4E7EC0` descriptor alone would be incorrect because the
title also uses it in the front end.

## Source-supported native seams

* After `57BF0` returns to `1173B`/`11754`: entrance record initialized.
* After `57BF0` returns to `13027`: accepted return record initialized.
* Private copied transition **structure** starts at `AFA07C`; it is not an
  indirect pointer. Resource+0, scene+4, midpoint+10, end+14, speed+18,
  current+1C, draw callback+20 and init callback+24 are hexadecimal offsets.
* The four `13610 → 57CB0` calls advance that transition only.
* Entrance handoff is after the original `55070` call returning to`136DF`.
  Return handoff is after native actor restoration and `AFA010=0` at`137C9`.
* Only `119E0 → 57CE0` at`11A4D` is conditionally omitted. The following
  matrix restoration at`11A52` always executes; other57CE0 callers remain.

The original guest code still performs midpoint/end comparisons, replay
actor/camera changes, native callbacks, screen transitions and cleanup.
All hooks are anchored in `tools/apply-gen-patches.py`; no source is patched
directly in `src/recomp/gen`, and no regeneration is required.

## Paint and handoff protocol

The HTML manifest opts in with `replay_wipes_ack:true`. Each initialized
transition receives a nonzero uint32 token with package, page generation,
clock, both rosters and exact resource/scene/callback/timing identity.

1. `NATIVE_REPLAY_PREPARE`: copy-only page request; load actual vector assets
   and await fonts. Page replies `NFL2K5_REPLAY_READY:<token>`.
2. `NATIVE_REPLAY_START`: show the initial controlled frame. Page waits two
   animation frames before START acknowledgment and advancing the timeline.
3. The controlled timeline pauses at the measured opaque cover: entrance
   **880ms**, return **40ms**. Two painted frames precede COVER acknowledgment.
4. The original native midpoint comparison is allowed through. After the
   actual original camera/actor handoff, `NATIVE_REPLAY_HANDOFF` resumes the
   remaining CBS layers.
5. Page seeks to the transparent1050ms frame, waits two paints, sends DONE.
   Only then can the original native end comparison complete normally.

This is an intentional **visual transition clock adapter**. The delta
argument to the four private57CB0 calls is adapted piecewise to CBS motion;
it does not alter the game's match/play clock or any football result. The
stored original midpoint/end/speed values are not overwritten. This differs
from preserving the original ESPN transition's1.67–3second visual duration.
Native baseline records really vary, including initialized null-art records.

No timer or browser callback executes recompiled code. Page callbacks parse
canonical tokens and change mutex-protected host flags only. The guest's
normal update calls service the visual delta; there is no spin or blocking
wait. Unsupported records, other packages, disabled game Animations, stale
tokens, changed matches/records, pause, play selection, page failure and
watchdogs retain/restore original native artwork. Pre-paint waiting is
bounded750ms; whole-transition ownership is bounded4500ms. No success or
native completion is fabricated on timeout.

CBS honors the existing explicit Presentation > Animations setting. ON
overrides the desktop window-animation reduction preference for broadcast
motion; OFF releases replay ownership. Preview fixtures that omit the game
setting still honor `prefers-reduced-motion`. Reasons for unavailable
controlled assets/motion are logged as `CBS_REPLAY_FALLBACK`.

## Motion evidence and scope

The official NFL Chiefs–Cowboys Thanksgiving2025 source `PLR04ljZ5DI`
shows a full opaque blue/metallic return cover at approximately107.433 and
107.467seconds, followed by the live close-up by107.500seconds. The previous
CBS return reconstruction never fully concealed the picture. A separately
editable blue optical plane now supplies that initial source-backed cover;
it is removed by96.6ms and does not make the later lens opaque.

Actual1920×1080 Edge alpha captures prove every pixel is alpha255 at
entrance880ms and return40ms, including a1500ms held cover. The completed
1050ms frame has alpha0 everywhere. Controlled and standalone sampled
frames are byte-identical. Source sampling is about33ms, not a claim of
sub-frame broadcast timing precision. Original production3D meshes,
extrusion/materials and exact CBS fonts remain unavailable; those shapes
are editable approximations, not an exact original CBS asset export.

Evidence: `outputs/cbs-2026/replay-coverage-20261019/README.md`,
`outputs/cbs-2026/native-replay-browser-20261019/coverage.json`.
Native runtime acceptance and per-run limitations belong in PROJECT_STATUS.md.
