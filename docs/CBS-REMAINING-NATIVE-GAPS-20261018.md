# CBS remaining native bindings — 2026-10-18

Read-only follow-up to the final 2026-10-18 entry in `PROJECT_STATUS.md`.
No game was launched, attached to or controlled, and no native/generated
source or game data was changed for this audit. The owned retail XBE was
read through `tools/eras/result_capture/stat_selectors.py`'s `Xbe` reader and
disassembled locally with Capstone. Findings below distinguish a title
counter from a complete modern NFL statistic.

## Receiver targets: a real “Thrown To” counter exists, with a material limit

The existing JSON catalog has 183 base selectors, 49 derived selectors and
271 stat-screen labels. None of its decoded labels names receiver targets.
That catalog only discovers particular stat-screen descriptor formats; its
absence was **not** proof that the game lacked the counter.

The owned XBE supplies stronger evidence:

| Evidence | Address / behavior |
| --- | --- |
| UTF-16 abbreviated heading | `0x00EA9050`: `TT` |
| UTF-16 full heading | `0x00EA9058`: `Thrown To` |
| Header property records | `0x00544EE8` references TT; `0x00544F00` references Thrown To |
| Adjacent field property | `0x00544F18` contains selector `0x38`; the same field group uses player stat adapter `0x003216C0` at `0x00544F30` |
| Player adapter | `sub_003216C0` forwards its selector to `sub_000CB240` |
| Base selector descriptor | `0x00A8A510 + 0x38*0x1C`: type 2, player getter `0x0024EDC0`, team getter `0x000CAA90`, history field 50 |
| Current-match player getter | `0x0024EDD0` gets receiving block `[edx+0x10]`; `0x0024EDDF` returns its unsigned byte `+0x17` for scope 0 |
| Receiving stat builder | `sub_001ED470` clears/builds the 0x18-byte receiving block from the title's actual recorded events |
| Successful reception | Builder's low event type 2 branch adds receiving yards, increments a reception bucket and increments receiving `+0x17` at `0x001ED612` |
| Unsuccessful throw | Builder's low event type 3 branch resolves the receiver ID through `sub_000BBA60`, checks that player's record, and increments receiving `+0x17` at `0x001ED6FF` |

**Do not yet present this as an interception-inclusive NFL TARGETS count.**
The receiving builder skips low event type 4: it accepts types 2 and 3 for
the two increments above, then tests type 0xD for a continuation adjustment.
Type 4 is independently proven to be an intercepted pass: the passing
builder `sub_001ED0C0`, at `0x001ED324–0x001ED336`, increments passing block
`+0x21` for that type, and verified passing-interception getter
`sub_0024E430`/selector `0x16` returns precisely byte `+0x21`.

This proves that the title's TT counter exists, but its reconstruction
does not count interception events as receiver throws. Whether all such
events retain an intended-receiver identity, or the title deliberately
omits those targets, is not yet established. Do not add another team's or
quarterback's attempts to compensate, and do not silently advertise exact
NFL targets. A raw binding named `timesThrownTo` with these semantics is
supportable; a binding named `targets` needs an explicit compatibility
decision or further intended-receiver proof.

Two related unlabeled fields must not be substituted: receiving `+0x14`
and `+0x15` increment only under the special incomplete-event flag test at
`0x001ED702–0x001ED720`; `+0x15` resets on completion. They are not all
targets. Reception `+0x0A..+0x12` is a 3×3 bucket grid, classified by
`sub_001EC750` and `sub_001EC760`, not nine period counters.

## Tight ends: the collector uses the same receiving family as wide receivers

`src/recomp_manual.c`, `nfl2k5_live_collect_player`, reads the actual
position byte at player `+0x35`. Position 9 is TE in the verified 17-entry
position table. Its offense branch explicitly accepts positions 3 (WR),
7 (HB), 8 (FB) and 9 (TE) and samples the same eight offense selectors.
Receiving fields are scope-0 `0x2E` receptions, `0x4F` yards, `0x3E` TD,
and `0x1B` longest reception. There is no TE-specific exclusion or
different receiving-selector mapping in that collector.

The same player/roster/live-slot and match-ownership checks apply. A TE
without a valid title receiving block remains unavailable, rather than
being assumed to have zero catches. The final status records actual
read-only runtime evidence of absent TE receiving blocks in the sampled
snapshot. This audit cannot determine whether that was because those TEs
had not received a recorded pass, roster/depth-chart usage, or a deeper
title issue. A natural TE reception followed by a captured slot/block and
event delta is the next useful runtime test. Do not force an event.

## Active drives: a real title record exists; fields are not all mapped yet

The host `DriveTrack` in `nfl2k5_presentation.cpp` remains observational:
it counts down/spot changes and derives distance/time from sampled state.
Its `source:"tracked"` is correctly distinguishable from actual completed
`DRIVE_SUMMARY` popup data. Neither whole-game team TOP nor an
opportunistically observed TOP difference establishes this drive's time.

Newly isolated title structures provide a concrete next investigation:

| Source | Verified role |
| --- | --- |
| `[0x00E57674]` | Current title drive record |
| `0x00B72A68`, `0x00B72AE0`, `0x00B72B58` | Three 0x78-byte records installed during `sub_000CCE00` initialization |
| `sub_000CD0B0` | Starts/rotates a drive, sets record identity, start event index `+4`, team `+0x10`, and clears counters |
| `sub_000CD2F0` | Closes the current drive and sets its result fields |
| `sub_000CDEF0` | Records actual play events and updates current-drive counters |
| `sub_000CD830` | Maintains record `+8`, the count/range of recorded events belonging to the drive |
| `sub_000CCF80` | Addresses the 20-byte event ring: `0x00E53874 + (eventIndex % 0x300)*20` |
| `sub_000D6FA0–sub_000D7940` | Existing presentation/script getters for the current title drive and related counters |

Promising **not yet display-approved** fields/getters:

- Current record `+0x28`: incremented by the ordinary run branch of
  `sub_000CDEF0` at `0x000CE44B`; getter `sub_000D70E0` returns it.
- Current record `+0x38`: incremented by several pass-result branches
  (`0x000CE351–0x000CE35C`, `0x000CE381–0x000CE38E`,
  `0x000CE3E2–0x000CE3ED`, `0x000CE412`, `0x000CE437`);
  getter `sub_000D6FA0` returns it. The exact handling of sacks,
  penalties and non-counting plays must be verified before calling it
  the CBS PASS count.
- `sub_000D7350` counts actual low-type-1 events in the current drive
  range matching the currently selected runner; `sub_000D73C0` sums
  that runner's yards. These are **player-filtered**, not unconditional
  drive totals.
- `sub_000D7460` computes the selected receiver's completion percentage
  from types 2 and 3 in the drive range. It is not a drive play count.
- Record float `+0x60`, returned by `sub_000D7280`, is updated by summing
  signed event yard values at `0x000CE5B5–0x000CE5F2`. It is **not time**.

`sub_00250040`/team selector `0x5F` proves how the title maintains whole
team TOP, adding elapsed clock time since the last applicable recorded
event to the accumulated team field. `sub_000CD050` returns that latest
event through `sub_000CCFF0`; it does **not** return a drive-start event.
Do not use that last-event timestamp as if it were a drive-start time.

The next bounded trace should compare the native Drive Summary's exact
plays/yards/time with the current record, its start/range and title
getter results over one complete possession, including an incomplete
pass and a penalty. Until that agrees, keep the completed summary and
leave the continuously advancing THIS DRIVE field unbound.

### Later source discovery: actual drive-time accessor

`sub_000BCA10(ecx=driveIndex)` is the title's real elapsed-drive-time getter, returning x87 ST(0). It obtains start/end event indices via `sub_000BBDB0`/`sub_000BBDD0`, addresses the 20-byte event ring through `sub_000CCF80`, and extracts elapsed-game timestamps from `(event.dword0 >> 14) & 0x1FFF`. For the current drive (`end == [E53804]`) it uses the latest recorded timestamp and, when `sub_00064BF0` reports active gameplay, adds the live `sub_000CD3F0(clock,period)` increment. A closed drive uses the recorded end/start difference.

`sub_000BD850` calls this getter, converts its result through `sub_000BB3E0` and divides by 60 for the title's minutes/seconds summary formatting. This independently supports seconds as its unit. `sub_000D7D70` is a script adapter, not an alternative host wall-clock implementation. The getter depends on the larger title drive history (`E53800`, `E57474`) as well as the three cached records; do not conflate those identities.

This is source proof, not a claimed live binding. Before publishing an active-drive snapshot, validate the current drive index/history range and match owner, call only inside the established guest-context sampler with full register/x87 preservation, and compare time/plays/yards against a completed native Drive Summary. Rush/pass treatment of sacks, penalties and non-counting plays remains unresolved. No guessed split or host timer has been added to the production API.

## Replay-ended and play-started delivery

Verified code facts:

- `Nfl2k5BroadcastEvent` declares REPLAY_BEGIN and REPLAY_END, and
  `nfl2k5_broadcast_event` maps them to queued native events. A repository
  search found the declaration and implementation but no production
  caller for REPLAY_END. PLAY_STARTED has no native enum/producer here.
- The HTML name adapter maps replay_begin/replay_end to
  REPLAY_STARTED/REPLAY_ENDED. That is a serializer, not an actual
  replay-end producer.
- `nfl2k5_presentation_popup(title)` can queue replay_begin when the
  popup title contains REPLAY; the popup title is not proof of every
  automatic replay boundary.
- `s_native_visible` and phase 4 are useful HUD eligibility signals but
  cannot distinguish a snap from all camera cuts or menus. Do not emit
  PLAY_STARTED merely because the scorebug disappears or returns.

Existing service points permit a future binding **without editing generated
code**:

1. `LIVE_QB_SAMPLE`, installed at `sub_000FCE70`, already calls
   `nfl2k5_live_qb_guest_tick` in `src/recomp_manual.c` in guest context.
   A bounded, read-only native event-state sampler can live beside that
   collector after replay/snap state addresses are proven.
2. `DIAG_SCRIPTOP`, already installed in `sub_000DBC10`, calls
   `nfl2k5_diag_script(ctx,op,start)` in `src/main.c`. Its command table
   at `0x00A8F7B8+op*12` contains `{type,start,poll}`. The current callback
   logs starts/first polls, not a verified completion notification.
3. Existing FSM hooks call `nfl2k5_diag_fsm` for push/pop, and its pop
   path forwards to `nfl2k5_hook_fsm_pop`. A specifically identified and
   runtime-verified replay FSM state could use that existing hook. No
   such automatic-replay state was established in this audit.

These are safe investigation boundaries, not claims that the missing
events have been implemented. Bind only a confirmed replay lifecycle or
snap transition, preserve match ownership, and coalesce duplicates.
