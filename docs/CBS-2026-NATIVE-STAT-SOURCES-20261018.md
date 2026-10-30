# CBS native statistics: verified sources and limits

October 18, 2026. Read-only audit of the existing title accessors, decoded selector catalog, presentation serializer and earlier retail Gamecast captures. No production source was edited for this audit; no game, build, regeneration or APU task was run.

## Findings that affect the CBS implementation

- Current-match player statistics use **`sub_000CB240` with scope 0**. Current-match team totals use **`sub_000CB2D0` with scope 0**. The same numeric selector can have different player/team getters.
- Team tackles, sacks and interceptions have their **own native team counters**. Read the team accessor; do not synthesize a team total by summing whichever players happen to be collected.
- **`0xB8` is sacks in normal units, including halves.** It is the game's derived selector `0x2F * 0.5`; the base `0x2F` is stored in half-sack units. Preserve the returned double.
- **Team `0x5F` is time of possession in seconds.** The early static note saying its units were unsettled is superseded by the later runtime evidence in RESULT-CAPTURE section 8.1 and M1.
- There is **no verified current-drive plays/yards/time getter in this audit**. Use the game's actual `DRIVE_SUMMARY` payload for a drive graphic; keep total-team possession time separate. Do not label play elapsed time or a difference of opportunistically observed team totals as authoritative drive TOP.
- A finite accessor return alone does not prove availability: unsupported/missing entities can return a valid numeric zero. Validate match ownership, active accessor mappings and actual live records before setting a validity bit.

## Evidence and reproducibility

The read-only retail XBE matches the catalog's source SHA-256:

`73105b17a3161c546fea792a1c84ce37f9966a67c416f474cdbfab74b911a4a9`

Sources inspected:

- [Decoded selector catalog](../data/eras/stat_selectors_2k5.json): 183 base and 49 derived descriptors, with getter addresses, descriptor types, scope information and the title's own screen labels.
- [Result-capture research](eras/research/RESULT-CAPTURE.md), especially sections 4.4 and 8.1. Section 8.1 contains later runtime corrections; its evidence takes precedence over earlier UNVERIFIED labels.
- [M1 retail verification](eras/results/M1.md): Gamecast comparisons against captured accessor values.
- [Existing capture table](../tools/eras/result_capture/capture_table.py): the checked player/team selector lists. Its introductory TOP-unit caveat is older than the M1 verification.
- `src/recomp/gen/recomp_0005.c`: team leaf getters `000C9C40`, `000C9C80`, `000C9C90`.
- `src/recomp/gen/recomp_0006.c`: entity mapping `000CA4C0`, derived team arithmetic `000CA690`, base team dispatch `000CB080` / `000CAA00`, public team accessor `000CB2D0`, absolute game-clock conversion `000CD3F0`.
- `src/recomp/gen/recomp_0016.c`: live TOP getter `00250040`.
- [Actual Gamecast TOP image](../logs/eras/t11-gc7.png), inspected in this audit; existing JSON results [game 1](../logs/eras/eras-capture-1.json) and [game 4](../logs/eras/eras-capture-4.json).

This establishes the title's sources and earlier runtime measurements. It does not certify a new CBS collection pass or its live rendering.

## Accessor ABI and availability

The existing `nfl2k5_guest_stat` wrapper in `src/recomp_manual.c` preserves guest registers, lazy flags, x87 and XMM state. Accessor entry:

| Value | Player | Team |
|---|---|---|
| Function | `000CB240` | `000CB2D0` |
| `ecx` | Player record | Match team-data record |
| `edx` | Selector | Selector |
| First stack argument | Scope | Scope |
| Result | x87 ST0 | x87 ST0 |
| Return cleanup | `ret 4` | `ret 4` |

Scope **0** is this match; 1-4 select quarters, 6/7 select halves where supported, and 11 selects season data. Scope support depends on each descriptor. Do not make every descriptor answer an arbitrary scope or confuse scope 11 with current-game totals.

The away match team-data record is `0x00B30A58`, home `0x00B30864`. The corresponding team objects are away `0x00E5FC60` and home `0x00E5FC20`; object `+0x1C` owns the team-data record and `+0x18` owns the live team-stat block. Use the current object links as ownership evidence rather than trusting only fixed addresses.

`000CA4C0` maps the requested team entity through native globals:

| Entity matches | Live block returned |
|---|---|
| `[0x00B72A50]` | `[0x00B72A58]` |
| `[0x00B72A54]` | `[0x00B72A5C]` |
| Neither | Zero |

Therefore require the requested team record to match one active key and its mapped block to match the team's current `+0x18` block before accepting a team value. For players, validate the roster identity and current live-stat chain; the existing collector also checks the active player resolver. An absent source must remain unavailable, even though the title accessor may return zero for it.

## CBS player fields

All entries below use the **player** accessor and scope 0. They are native selectors, not invented offsets in a CBS-specific structure. The decoded JSON remains the full catalog for extensions.

| Family | Field | Selector | Treatment |
|---|---|---|---|
| QB | Completions / attempts | `04` / `23` | Valid zeroes remain zero; completions cannot exceed attempts |
| QB | Passing yards / TD / INT thrown | `4C` / `40` / `16` | Negative passing yards are valid |
| QB | Longest pass / passer rating | `1F` / `CF` | Native rating is a fraction: multiply by 100 for Gamecast's displayed value |
| RB/FB | Carries / rushing yards / TD | `03` / `50` / `3F` | Preserve negative yards |
| RB/FB | Longest run / yards after first hit / average | `1A` / `60` / `C2` | Prefer supplied native average over a differently scoped calculation |
| WR/TE | Receptions / receiving yards / TD | `2E` / `4F` / `3E` | TE uses the receiving family |
| WR/TE | Longest reception / YAC / average | `1B` / `44` / `C5` | Native average is derived from native inputs |
| Defense | Tackles / sacks / INT caught | `32` / `B8` / `17` | `16` is INT thrown, not defensive INT |
| Defense | INT return yards / forced fumbles / recoveries | `48` / `11` / `12` | Do not substitute team turnover count |
| Defense | Fumble return yards / tackles for loss / hurries / tipped passes | `47` / `34` / `15` / `22` | Keep the title's labels; no unproven solo/assist split |
| K | Total FG made / attempted / longest | `C8` / `C7` / `1C` | Totals are derived from range counters |
| K | FG made by 0-29 / 30-39 / 40-49 / 50+ range | `AD` / `AE` / `AF` / `B0` | Distinct native counters |
| K | FG attempted by the same ranges | `A9` / `AA` / `AB` / `AC` | Distinct native counters |
| K | XP made / attempted / blocked | `58` / `59` / `5A` | Unknown values are not missed kicks |
| K | Kickoffs / touchbacks | `19` / `39` | Native kickoff fields |
| P | Punts / punt yards / longest / inside 20 / touchbacks | `2B` / `4E` / `20` / `2D` / `3A` | Optional extension; no current-player binding implied |
| P | Net average | `C0` | Derived `61 / 2B` |
| Returns | KR count / yards / TD; PR count / yards / TD | `18` / `4B` / `3C`; `2A` / `4D` / `41` | Do not confuse return yards with scrimmage yards |

The separate base selector **`30`** has a defensive-screen label resembling sacks but remains unresolved in the earlier captures. It returned zero for every observed player. Do not use it instead of the proven `B8` conversion.

The generic API serializes numeric fields for all player families, including irrelevant zeroes. Choose the family from verified `kind`/position, not merely from the existence of `attempts` or another numeric property. New collection masks must keep unavailable fields distinct from real zero performances.

## Team defensive totals and aggregation

The team path `CB2D0 -> CB080 -> CAA00 -> CA4C0` resolves the native live block and invokes a **team getter**. These particular getters do not walk the player roster:

| Native team metric | Selector | Getter / computation |
|---|---|---|
| Tackles | `32` | `000C9C40`: unsigned byte at resolved team block `+0x184` |
| Half-sack units | `2F` | `000C9C80`: unsigned byte at block `+0x17C` |
| Sacks | `B8` | Derived op 6: team `2F * f32[0x004E4184]`; the constant is exactly `0.5` |
| Defensive interceptions | `17` | `000C9C90`: unsigned byte at block `+0x170` |

The labels and conversion are verified. This audit does **not** establish a separate solo/assisted tackle taxonomy; CBS should label `32` as **TACKLES**, matching the title. Never round sacks to an integer or interpret the raw half-unit counter as whole sacks.

Existing captures give useful cross-checks, not permission to replace team getters with sums:

| Capture / side | Native team tackles | Sum of captured player tackles | Team INT / player sum |
|---|---:|---:|---:|
| Game 1 TEN | 14 | 14 | 1 / 1 |
| Game 1 DAL | 11 | 11 | 0 / 0 |
| Game 4 TEN | 8 | 8 | 1 / 1 |
| Game 4 DAL | 9 | 9 | 0 / 0 |

All four observed sack totals were zero, so those games do not provide runtime evidence of a half-sack display. The native derived-table arithmetic itself proves the half-unit conversion. A later nonzero/shared sack capture remains useful validation.

Other verified team selectors useful for CBS are first downs `0E`, possessions `5D`, passing yards `4C`, rushing yards `50`, and total yards `D0 = 50 + 4C`. `5D` is the count of possessions, **not plays in the current drive**.

## Time of possession: seconds are established

The full Gamecast image `logs/eras/t11-gc7.png` visibly shows TEN **0:19:44**, DAL **0:14:48**. The captured team accessor values are 1184 and 888, which are exactly those durations in seconds. A second captured game shows **0:10:10** / **0:09:50**, with raw values 610 / 590. See M1's comparison table and `logs/eras/t11.stderr.log` lines 10620/10622 for the first pair.

The static chain agrees:

1. Team selector `5F` points at getter **`00250040`**.
2. That getter starts from the accumulated possession counter at the resolved team live block `+0x274`.
3. It reads the current native possession record through `000CD050`, identifies its possessing team, and adds the ongoing possession's elapsed game-clock duration only to that team.
4. The absolute clock conversion **`000CD3F0`** uses the configured quarter minutes at `0x00E6000C`, multiplier `f32[0x004E6D14] = 60.0`, period and current remaining clock. `f32[0x004E419C] = 1.0` supplies the period offset.

These constants were read from the owned XBE in this audit. No wall-clock timer is involved. Read the native accessor rather than just block `+0x274`, because the block alone omits the live possession contribution.

**Short-quarter test caveat:** M1 used an artificial one-minute running-clock override, but the configured quarter-minutes setting used by the title's possession conversion remained different. One captured game therefore reports 610+590 = 1200 seconds. This is what Gamecast itself shows. Do not reject an otherwise valid `5F` sample merely because it disagrees with host elapsed time or the harness's overridden `periodLength`; preserve the game's authoritative statistic.

## Drive data: do not substitute total TOP

No descriptor in the decoded catalog is labeled as current-drive plays/yards/time. The existing `DriveTrack` in `nfl2k5_presentation.cpp` is an observation-based approximation: it begins on the first observed scrimmage phase, counts changed down/spot records, derives yards from observed field positions and elapsed time from clocks. Missing the actual beginning, live state coalescing, repeated down/spot changes and special teams make it unsuitable as an authoritative replacement.

The native popup hook already extracts the title's **Drive Summary** and emits:

```js
{
  name: 'DRIVE_SUMMARY',
  team: 'away', // or home/null according to the existing bridge
  drive: { plays, yards, timeOfPossession, source: 'game' }
}
```

Use this actual popup's supplied plays, yards and time for the completed-drive graphic. A drive payload with `source: 'tracked'` is not equivalent evidence. Keep a current-drive panel absent until its source is established, or label and scope the completed-drive summary accurately.

**Parser issue found during this audit:** the pre-existing popup parser searched forward across several tokens after a label and serialized absent TIME as zero. A missing PLAYS value could accidentally consume a later YARDS value. Root was notified to make value extraction label-owned and strict, return null for absent fields, and validate minute/second syntax before CBS treats the result as game-supplied data. This note does not claim that parser repair has been implemented or verified.

## Completion and remaining verification

Available for implementation: masked current-match team totals, half-unit-preserving player/team sacks, native TOP seconds, supplied player identities/stat families and genuine completed-drive summary events. Unknown source data should remain absent; no zero-filled filler or generic stat popup should be used to imply a native measurement.

Remaining useful checks are a fresh live CBS capture matching Gamecast during an ongoing possession, a nonzero/shared-sack case, the specific native card's actual identity/scope/acknowledgement lifecycle, and a malformed/missing Drive Summary field regression. None requires guessing a new title structure or changing football logic.
