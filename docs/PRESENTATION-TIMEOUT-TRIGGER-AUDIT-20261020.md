# Native timeout trigger and scoreboard boundary audit — 2026-10-20

This is a read-only source, retail-XBE and existing-log audit. No application was launched or stopped, no build or generation ran, and no production or generated source was changed. Halftime is outside this audit's implementation scope.

## Finding

The exact accepted **team timeout** decision and its native presentation state can be recovered. They are not the current host `TIMEOUT` event, which is inferred from a timeout-count decrease. The reviewed evidence does **not** establish a separate original large timeout popup or an admission/close lifecycle equivalent to the period layout-7 board. Do not claim that `endofqrt` was shown at either logged timeout.

`native-review-table10/stderr.log` records home timeout-count events at lines 3715 and 4152. There is no `[POPUPOP] show` between the quarter-stat request at line 3517 and Drive Summary at line 4776. Popup logging in `src/main.c:nfl2k5_diag_popup` has no count cap. The original ordinary-scorebug signal remains `native 1` during each stopped-clock timeout interval (for example lines 3727–3814 and 4169 onward). Layout 7 appears at line 3444 for Q1 and line 5234 for Q2/HALF; those are separate native instances.

Consequently there are two different candidate contracts:

1. Accepted team timeout / native timeout presentation: recoverable now.
2. Original large timeout board actually admitted and drawing: not proven by this audit. An accepted timeout must not be silently relabeled as proof of that graphic.

## Exact accepted team-timeout path

`sub_000A00A0` receives the requesting team in ECX and retains it in ESI. Its failed paths return without reaching the acceptance seam.

| Guest address | Actual operation |
|---|---|
| `A00B7..A00D3` | Rejects the expired raw game clock except phase `E602B4 == 3`. |
| `A00D9..A00E0` | Rejects when `12080()` is nonzero. |
| `A00E2..A00EB` | Calls `55C70(team)` and rejects zero. |
| `A00EE` | Saves previous flow `E602B8` in EDI. |
| `A00F6` | Calls `B8810(team)`. |
| **`A00FB`** | **Return from the actual accepted call; ESI is still the team, EDI is the previous flow.** |
| `A0104` | Calls `1CF1E0(team, previous_flow)` for the original reactions. |
| `A010E` | Calls `1D17F0`, which requests the literal `Official: Timeout` (`E86B10`). This is an official/camera action, not a score popup. |
| `A0129` | Mode >= 4 queues original event `0x3C` through `1BADB0`; this is gamecast bookkeeping, not scoreboard acceptance. |

`55C70` requires a normal phase in 1–4, allows flow `0x12/0x0B/0x0C/0x0D`, checks raw game clock (with phase-3 exception), and checks both nonzero timeouts and `[team_score + 0x14]`. It also rejects specific active native-play conditions. Entry to `A00A0` alone therefore is not acceptance.

`B8810` performs the native changes:

| Guest address | Actual operation |
|---|---|
| `B8813..B8823` | Stops the actual game clock and calls `B6EB0`. |
| `B8823..B884D` | Updates native flags `E602FC`: preserves native semantics and sets `0x800`. |
| `B8852` | Calls `B6DE0`, which resets the native play clock to 25 seconds. |
| `B885D` | Stores the nonzero team pointer into **`E602F8`**. |
| `B8868` | Clears `[team_score + 0x14]`. |
| `B886C` | Tail-calls **`158890(team)`**. |
| `B8871` | Null-team branch stores zero to `E602F8` and returns; it does not call `158890`. |

`158890` decrements `[team + 8] + 4` at `158893`, then calls `E9520` at `158896` (return `15889B`). After this call it schedules `E602C8 = 0x18` and `E602CC = 100.2f` (`0x42C86666`). This is a native fallback/pending transition, not a graphic duration to reproduce in a host timer.

These instructions were cross-checked by disassembling the owned `original/disc/default.xbe` in memory with Capstone. They agree with current generated functions:

- `src/recomp/gen/recomp_0004.c`: `A00A0` at line 55533; accepted return label at 55602.
- `src/recomp/gen/recomp_0005.c`: `B8810` at line 19873.
- `src/recomp/gen/recomp_0010.c`: `158890` at line 802136.
- `src/recomp/gen/recomp_0002.c`: `55C70` at line 34811.

## Two state spaces, plus the native close transition

Do not confuse **football flow `0x13` (decimal 19)** with **presentation/camera state `0x19` (decimal 25)**.

`E9520` performs the following exact path:

```text
old football flow 0x0C or 0x0D:
    894A0(1)                         original camera choice
    88370(0x19, 2.5f)                schedule timeout presentation
otherwise:
    89260(0x19)                      request timeout presentation immediately

E9554: AF260(0x13)                   original football state-entry callback
E955E: E602B8 = 0x13                 actual timeout flow now committed
E9568: E602C8 = 0
E9572: tail E94A0                    original team/control work
```

`89260` has its own rejection conditions: it ignores the transition when `B61704 != 0`, and ignores a same-state request. The actual change to **`B616C0`** is at **`892A0`**, before `A2D40(old, new)` (return `892C1`). A generic `89260` entry observation is not proof a requested timeout state was admitted.

Delayed `88370` stores requested state at `B61700`, duration at `B6171C`, then starts native timer `B6170C`. It may also reject the schedule when `B61704 != 0`. The delayed consumer `89BA0` calls `881D0`, then `89260(pending)` at **`89C2E`** (return **`89C33`**). Observe the committed state transition, not just the scheduled request.

`A2D40`'s actual `0x19` branch is `A2F54` (retail two-stage table `A3178` / `A3160`). It uses `E602F8` to choose an original official/actor timeout action. Null team selects action 7; nonzero team selects action 5/6 according to native direction. That branch does not call popup show `8ACF0` or install layout 7.

The native exit is another committed presentation transition. When pending `0x18` expires, `55BD0` requests **presentation `0x1C`** with 0.5 seconds (`55C1F..55C29`), then clears the pending fields; it does not set football flow 0x18. Script/button paths can request different transitions earlier. `A2D40` handles entering `0x1C` at `A2F36..A2F4F` by transitioning onward to presentation 5, subject to native flow checks. A conservative timeout-presentation owner should release on actual **old state `0x19`, new state != `0x19`** through the `892A0` committed transition, rather than waiting 100.2 seconds or a counter change. It must also cancel on football-flow departure from 0x13, native identity loss, pause/menu/match reset, package/generation change or page reset.

`AF260` is the native football-flow entry dispatcher. It calls `A14C0(old_flow, new_flow)` before the caller writes `E602B8`; the new flow is ESI and the old flow is read from the global at `AF263`. Hooking after the caller's committed store, or copying the explicit transition arguments, avoids reading the old global as though it were the new one. `E602F8` is not cleared on every flow exit, so a retained nonzero requesting-team pointer is insufficient by itself.

## Original scorebug admission is separately identifiable

The ordinary original ESPN scene is `A95528`; its render object is `A9552C`. `FC360` is its actual draw routine. Its early gates are:

1. `A95520 != 0` at `FC36C..FC376`.
2. `A95524 != 0` at `FC37C..FC383`.
3. `8AB40() == 0` at `FC389..FC390`.

**`FC396` is after all these gates.** The first original render submission is `21860(A9552C)` at `FC3B2` (return `FC3B7`); text submission follows in the same routine. This is a better source seam than the host `nativeScorebug` approximation if an owner is intended to follow actual native scoreboard submission during an accepted team timeout.

The fixed text tables in `A95880..A95C08` contain city, quarter, clocks, scores, down/distance, FLAG, FUMBLE and ball-on formatters. This audit did not find a timeout-specific text/layout variant in that table. `A95A00` is specifically the `drop_down` element's visibility switch, not acceptance of a separate timeout board. `FC9C0` setting `A95524` in flow C/D is ordinary scoreboard state and is not a team-timeout trigger.

Possible narrowly truthful future owner:

```text
capture one accepted team-timeout token at A00FB
    require ESI == E5FC20 (home) or E5FC60 (away)
    require E602F8 == ESI and committed football flow == 0x13
    copy native clock/period/rosters and package/generation

observe actual admitted presentation state 0x19 at 892A0/892C1
observe actual ordinary-scorebug draw at FC396
    validate same token, native identities and flow
    show supported CBS timeout break score panel; await matching paint ACK

release on committed native presentation exit from 0x19,
football-flow departure/identity loss, missing native draw eligibility,
pause/menu/match reset, package/generation/page reset
```

This would implement **a break score panel during an accepted native timeout's scorebug submission**. It would not prove that a large original timeout board existed. The draw boundary must be serviced from guest callbacks, and failed draw gates must also publish the current copied eligibility so an owner does not survive a skipped native draw. The browser thread must touch copied token/permission flags only; no guest reads there. No new native state writes, early closes, skipped update/callback paths, or host timers are needed.

## Negative cases that exclude broader hooks

| Case | Why a broad hook would be false |
|---|---|
| Rejected team timeout | `A00A0` returns before `A00FB`; observing its entry would overcount. |
| Two-minute warning | `A0190 -> 1588B0` explicitly stores `E602F8 = 0`, sets flag bits 0x60, then calls the same `E9520`, producing flow 0x13 / presentation 0x19 and the same pending 0x18. It requests `Offical: Two Minute Warning` at `1D1820`. |
| Injury interruption | `B7330` checks `136D60`, calls `A0170 -> B8810(NULL)`, then invokes `A2C20` and schedules presentation 0x17. Shared `B8810` entry does not imply a charged team timeout. |
| Challenge interruption | `A1DB0` invokes challenge work `1E8070 -> 1B1420`, then calls `B8810(NULL)` at A1E4E (return A1E53). It is not an accepted ordinary team-timeout request. |
| Failed-challenge timeout charge | A counter decrement / textual timeout charge can occur under the review owner; it does not prove ordinary `A00FB` acceptance or a timeout scoreboard. |
| Official/TV-style break or warning | Null-team `E9520`/state0x19 does not establish a requesting team. No independent TV-timeout score graphic was proven here. Preserve native fallback unless its actual graphic is identified. |
| Penalty | A stopped clock or native camera transition is not enough. Require the accepted ordinary timeout token; do not infer it from flow interruption, popup prose, replay or count changes. |
| Quarter/halftime | Layout7 is a separate acknowledged native popup lifetime. Do not derive timeout kind from that period formatter or auto-start a halftime show. |

## Integration limit

The accepted decision, native identities, camera/presentation admission and ordinary scorebug draw seams above are source-proven. No new native frame capture was permitted for this audit. The reviewed run proves two team timeout-count changes and continuing ordinary-scorebug eligibility, but contains no timeout board popup. A production claim about replacing the original **large timeout board** still requires the real graphic's admission/draw/close identity, or an explicit product decision to use the narrower timeout-scorebug contract above.
