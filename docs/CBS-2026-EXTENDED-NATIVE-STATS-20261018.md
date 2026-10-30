# CBS live player, team and possession inserts — October 18, 2026

## Data boundary

The C guest collector in `src/recomp_manual.c` runs only at existing guest
service points. It calls verified original accessors on the guest context,
preserves the register/x87/SSE execution state, and returns immutable fixed
records to the C++ presentation cache. No timer, browser or HUD callback
calls recompiled code. No gameplay state is written.

`Nfl2k5LivePlayerSample` and `Nfl2k5LiveTeamSample` in
`src/nfl2k5_broadcast.h` carry the actual clock object, both roster bindings,
original playbook team codes, scope 0, period, sample timestamp and a validity
mask. Fields are doubles so half sacks survive. Missing fields remain absent
in JSON; they never become invented zeroes. Numeric roster IDs are not team
abbreviations. TOP is serialized only when its unit is explicitly seconds.

One relevant non-QB player is sampled per 50 ms; one team per 500 ms. The
descriptor/getter fingerprints are checked before calls. The package flag
`live_stat_cards` avoids collecting for unrelated presentations. The older
QB API and paint-acknowledged native-card protocol remain compatible.

Current-match player mappings include rush 03/50/3F, receiving 2E/4F/3E,
tackles 32, sacks B8 (half credits), defensive interceptions 17. Team data
uses the independent original team accessor, not sums of player records.
TOP 5F is seconds, independently matched against Gamecast captures in the
earlier Eras research. See `CBS-2026-NATIVE-STAT-SOURCES-20261018.md`.

## Trigger policy

Carry, reception, tackle, sack and defensive-interception increases create
bounded pending candidates. First samples establish baselines, not fake play
events. An unavailable position-specific counter becoming a verified positive
value may produce a stat snapshot; its old value is not assumed to be zero.
A subsequent real update refreshes an expired candidate's deadline; ordinary
sample refreshes do not continually restart it. Candidates expire;
higher-impact plays have priority. They wait for
1.2 seconds of a permitted visible scorebug, no native popup, pause or play
selection, and are spaced from the existing drive-QB insert. Periodically
eligible team tables rotate yardage, defense and possession. A missing team
sample cannot starve valid player inserts.

Presentation state chooses when to show a statistic; football simulation
does not change. The package remains selectable and original ESPN assets are
preserved. Other packages default off for this new policy.

## Drive summaries

Only the title's actual `DRIVE_SUMMARY` fields provide completed-drive plays,
yards and elapsed time. The parser stops at the next recognized label,
validates M:SS, and emits null for missing time. It cannot steal a different
label's value or turn missing time into 0:00. CBS defers delivery until the
native popup clears, verifies match ownership, and discards stale summaries.

The live native summary does not supply verified rush/pass play counts.
It therefore shows `6 PLAYS` instead of inventing a `5 RUSH, 1 PASS` split.
Team total TOP is shown in the team table, never passed off as current-drive
time. The source's continuously updating THIS DRIVE timer still needs an
authoritative active-drive source; no wall-clock approximation is enabled.

## Verification and remaining scope

Initial Release build and final guarded rebuild passed. Tests cover all
32 field mappings, original XBE metadata, deliberate corruption rejection,
typed player fields, half sacks, scope, unknown values, suppression and
controller lifecycle. The final test count is recorded in PROJECT_STATUS.md.

Actual native D3D11 CPU-versus-CPU review `native-review-extended01` used no
test events or fabricated statistics. It emitted and visibly rendered RB,
WR, several defensive positions, independent team defense, team TOP and
completed-drive graphics. Capture evidence and a separate labelled video
are under `outputs/cbs-2026/native-proof-20261018/`. The private game used
separate saves/settings and was stopped by its recorded PID.

TE collection/rendering is implemented and tested, but a naturally triggered
TE insert was not observed in that review. A read-only, non-atomic diagnostic
snapshot in `native-receivers-20261018.json` found no receiving-block pointer
for the inspected Gates/Witten/Campbell/Peelle records, while active WR records
had actual counts. This is not proof of a gameplay defect or permission to
invent TE zeroes. The verified-positive-availability transition is covered by
the actual host-code test. Receiver targets have no verified
native selector yet and remain unavailable. Exact source TE/defense skins,
the wireframe team-comparison opener, source fonts, active-drive split/time,
and remaining ordinary quarter/halftime sequences are not declared finished.
No APU, generated code, original game data or gameplay edits were made.
