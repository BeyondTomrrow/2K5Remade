# Native SKY CAM popup ownership — October 20, 2026

This source audit narrows one ESPN camera annotation. It does not establish
that every replay annotation uses the same object, or that CBS has a SKY CAM
label. The CBS replacement uses its already displayed network corner;
no invented camera label is added. Build and native runtime verification are
owned by the primary task. Implementation and isolated validation were
subsequently completed below; those tests do not establish in-game removal.

## Established identity

The owned, read-only retail XBE and existing `native-review-replay04` trace
establish layout **19 / 0x13**, descriptor **AAD984** in the catalog initialized
at **BE5214**. Descriptor fields are:

| Offset | Value | Meaning established by callers |
|---|---:|---|
| +00 | E86400 | UTF-16 `skycam` |
| +04 | E861A8 | UTF-16 `OVERLAY` |
| +08 | FFFFFFFF | No nonnegative script selector for `1BD410` |
| +0C | 0 | Generic table renderer selector, if a table exists |
| +2C | 50D650 | Shared material callback array |
| +30 | 50D268 | Shared text callback array |
| +34 | 50D898 | Shared art callback array |
| +38 | 50D908 | Shared texture callback array |
| +3C | 0 | No layout-specific show initializer array |
| +40 | 41700000 | Default timeout 15.0 seconds, not ownership duration |

Replay04 lines 4971–4974 show opcode 133, request **B73C44**, layout19,
slot1, empty title/message, subtype0 and zero cells. The generic diagnostic
also prints subject type1/player B330B8 and nonsensical table dimensions
(48 / 7733313). Those dimensions are not meaningful for subtype0. The request
log alone is not proof that `8ACF0` accepted the object.

## Accepted instance without another generated hook

`nfl2k5_presentation_native_popup_frame(ctx)` is already called at **8A170**
entry with **ESI**, the actual draw slot. Its two possible addresses are
**B61B50** and **B62A70**, separated by **F20**. It can observe a successful
active native instance directly; the reusable builder B73C44 is never the
owner. A new generic post-`8ACF0` hook is unnecessary for this family.

The entry may run before draw readiness. Bind only a conservative actual
instance after all of:

- `ctx+00` equals the slot index and `ctx+04 == 1`;
- `ctx+0C == 0` for initial binding and `ctx+F08 == 0`;
- `ctx+48 == 19`, catalog base resolves to the verified descriptor strings;
- `ctx+1D8 == 5`: the exact readiness test in `1BCE30` compares layout+190
  with 5, where layout begins at popup+48;
- `ctx+1DC` is a readable, nonzero resource object, and its first dword is a
  readable, nonzero scene pointer. `1BCC30` loads MRKS type534B524D into
  layout+194; `1BC3F0 -> 151530` returns that resource's first dword;
- `ctx+F1C == 1`: the first eligible draw store is at **8A1C0**, after the
  resource and callback-readiness tests;
- current valid match, clock object E6028C, both roster pointers, selected
  HTML package and page generation all match the owner's captured values.

Bind and recheck the actual resource, scene, completion callback +F14, native
subtype and any meaningful payload fields, rather than layout number alone.
Do not freeze mutable elapsed/timeout/alpha fields as identity. A snapshot
of the accepted instance is still needed to determine the callback and any
functional payload constraints; the request's empty title is insufficient.

The source proves no dedicated show initializer, but shared dynamic scene
callbacks remain. It does **not**, on its own, prove the loaded scene contains
no text, controls or functional data. Inspect the accepted subtype/payload
and loaded node inventory in the native run before asserting that broader
claim. Preserve all callbacks regardless.

## Implemented per-instance protocol and draw seam

Implemented host owner fields:

```cpp
struct NativeCameraMarkOwner {
    bool active = false, queued = false, delivered = false;
    bool acknowledged = false, closing = false;
    uint32_t token = 0, generation = 0, ctx = 0, resource = 0, scene = 0;
    uint32_t callback = 0, clock = 0, rosters[2]{};
    int package = -1, slot = -1;
};
```

Use a mutex for copied owner flags and atomics for selected-host package,
page generation/permitted state and token sequence, matching the existing
review owner. Guest memory is read only from established guest/HUD boundaries.
The WebView callback validates only copied host fields; it never reads/writes
guest RAM or invokes guest code.

Package capability is `camera_marks_ack: true`. Native events are
`CAMERA_MARK_SHOW/HIDE` with `source: "native"`, `nativeVisible: true/false`
and canonical uint32 string `nativeCameraToken`. Host messages are
`NFL2K5_CAMERA_MARK_SHOW/HIDE:<token>`. A decimal number, leading zero,
out-of-range value, stale token, stale generation, or stale package cannot
claim ownership.

`CBSCameraMark` in `camera-mark.js` renders nothing. It checks the existing
`#network` and its real image, geometry, opacity through ancestors, document
visibility and font readiness for two actual animation frames. A loaded,
painted CBS corner can then acknowledge the native instance. Hidden/fading or
failed artwork never acknowledges. A one-second failure is logged, without
faking paint. The adapter keeps checking so the same still-valid native token
can reacquire paint after a temporary hidden state. Browser previews never
send host ownership messages.

The existing CBS corner remains visible during actual replay context while
the lower Eyebar is hidden. Therefore a camera mark owner must use that
replay context, not the lower `scorebugVisible` flag or the short wipe owner.
Pause, play selection, replay end, changed match or native HIDE releases the
page token. Ownership is not inferred from the mere existence of `#network`.

Extend `nfl2k5_presentation_pregame_draw_replaced(ctx)` with an exact
camera-owner predicate. It should return true only after delivery/paint ACK,
with current slot/resource/scene/callback/match/package/generation. The
existing outgoing table/layout/effective/dynamic-art/dynamic-texture and
scoped scene-text alpha seams then cover this popup. Keep native scene
loading, updates, animations, camera state, first-draw policy, timers and
callbacks intact. Never clear pointers or close the native popup early.

## Native close and fallback

The existing `DIAG_POPUPCLOSE` at `8A340` calls
`nfl2k5_presentation_player_fact_hide(slot)` for **any active slot**, despite
the narrow function name. Add the camera close notification there, before
its player-only early return. On close, mark the copied owner closing and
notify page HIDE; preserve already acquired native draw suppression through
the original native closing animation. A closing HIDE acknowledgment must
not resurrect ESPN art. Finally release on inactive/reused slot or an actual
identity/context/package change. Other teardown paths can bypass the close
callback, so frame/HUD identity checks are also required.

Until paint is acknowledged, native art is the fallback. After unexpected
loss of CBS visibility, revoke native suppression. Page reset/change clears
ownership. Never suppress a whole replay renderer or every instance of
layout19 without matching the active owner.

## Required runtime evidence

1. Log the accepted actual slot, layout, subtype, resource, scene, callback,
   first-draw latch, subject/cell count and bounded strings once per instance.
2. Correlate SHOW, two-paint page ACK, exact draw predicate and actual close.
3. Capture the replay angle with the CBS corner visible and the original
   ESPN SKY CAM annotation absent, then verify another angle/native close.
4. Test missing capability, failed image/page and pause/replay-end fallback.
5. Verify no unrelated popup or replay camera/actor cleanup was suppressed.

`AFA108` and `11C20` remain a separate scene draw without a proved load link
to `skycam`. Do not intercept or label them from this evidence.

## Isolated validation

`tools/cbs-scorebug/verify-camera-mark.js` exercises the shipped adapter in
installed Edge with real CSS/image loading and controlled paint boundaries.
It does not launch a game, build binaries, alter gameplay or certify native
SKY CAM replacement. Record its actual completed result separately from
native runtime evidence.

Completed isolated run: **59 assertions PASS in real Chromium/Edge**,
including two-frame ACK, no artwork mutation, lost-visibility revocation,
same-instance repaint, canonical/stale tokens, failed image, timeout without
fake paint, match/context changes, and preview silence. This is browser
component verification only; accepted native scene identity and visual
removal remain to be demonstrated in the primary run.

The native owner is now implemented in `src/nfl2k5_presentation.cpp`, with
package capability parsing and exact-slot frame/draw/close, HUD, page,
pending-event and reset integration. The initial subset deliberately accepts
only subtype0, subject count0, cells0 and empty title/message. It emits one
lightweight actual-instance diagnostic instead of hiding art if that subset
does not match. Mutable timeout/alpha/table-inactive storage is untouched.

`tools/presentation-audit/verify-camera-owner.py` extracts the production
owner, replay-lifetime predicate and real state/event declarations, compiles
them with MSVC into its own output directory, and completed **1192 actual
native owner assertions PASS**. Source SHA-256:
`7cad6f59a08b503e7393130a596a5c869ac50acc3cac437edba873346869056a`.
It validates acceptance, prepaint fallback, both slots, stale identities,
resource/scene reuse, real replay screen lifetime, failed subset guards,
live visibility revocation/reacquisition, closing suppression, canonical
tokens and host callbacks making zero guest reads. No Release build, game
launch or generated-code mutation was performed by this isolated verifier.
