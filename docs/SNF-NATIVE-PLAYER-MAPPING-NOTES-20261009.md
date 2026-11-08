# Native SNF player mapping notes — 2026-10-09

This is a bounded read-only review of existing source, generated C and logs.
No build, game launch, native analysis executable, new memory hook or guest
callback was run. Only this document was added during this review.

## What already exists

The opcode-133 hook receives more information than the presentation callback
currently exposes. `tools/apply-gen-patches.py:346–349` inserts
`nfl2k5_playercard_probe(eax)` after `sub_000DCFA0` returns in
`sub_002604D0`. The existing generated implementation is in
`src/recomp/gen/recomp_0017.c:25284–25290`.

`src/main.c:1102–1137` receives this complete guest context. It reads native
kind from `ctx+0x48`, inline title from `ctx+0x4C`, and diagnostically scans
the context for roster records. However, its presentation call forwards
**only kind and title**, discarding the context/selected-player information.

There are reusable identity-reading pieces:

- `src/main.c:981–1006`: `pc_name` handles field-relative or absolute UTF-16
  name pointers; `pc_player` recognizes candidate 0x54-byte roster records
  using name fields at `record+0x10` and `record+0x14`.
- `src/main.c:1127–1128`: the existing probe reports position byte at
  `record+0x35`, photo ID at `record+6`, and jersey bits from
  `(record+0x20 >> 3) & 0x7F`.
- `src/recomp_manual.c:157–166`: an existing guest-context identity reader
  creates owned short/full names once a selected record is known.
- `src/recomp_manual.c:37–72`: an existing stat accessor accepts a player
  record and field ID. It preserves registers, x87/XMM state and verifies
  the call's stack/x87 behavior. Its current implementation explicitly
  selects current-match scope zero. It is only wired into the QB producer.

These are useful existing pieces, **not a verified complete non-QB card
extractor**. A candidate name-bearing record from a scan must not be chosen
arbitrarily as the player featured by a card.

## Concrete selected-player evidence in existing logs

Several recorded cards have a direct roster record at `ctx+0x14`:

| Existing evidence | Native kind | Direct record identity |
|---|---:|---|
| `logs/qd.stderr.log:2693–2695` | 1 | Drake London, position 3, photo 881, jersey 5 |
| `logs/qd.stderr.log:2752–2754` | 1 | Ja'Marr Chase, position 3, photo 670, jersey 1 |
| `logs/sd12.stderr.log:2730–2732` | 1 | Rashee Rice, position 3, photo 786, jersey 4 |
| `logs/sd11.stderr.log:2737` | 1 | Sam LaPorta, position 9, photo 3119, jersey 87 |
| `logs/drop2.stderr.log:4260–4262` | 0 | Bijan Robinson, position 7, photo 1747, jersey 7 |

The popup following the London/Chase/Rice examples has blank title/message
and timeout 4.35 seconds; the Robinson example has blank title/message and
timeout 6.15 seconds. The record identities are directly logged, but these
observations do not establish each card's stat scope or visible contents.

**`ctx+0x14` is not universally a player.** In
`logs/sd12.stderr.log:4605–4609`, native kind 3 has a roster-table pointer at
that offset; the scan logs multiple nested roster players. The generated
kind-specific branches also read `context+0x14` for different purposes:
`recomp_0017.c:15227`, `:15568` and `:15998`.

`docs/ESPN-GRAPHICS-TRIGGER-MAP.md` already shows that the same native kind
can accompany several titles and that many titles are blank. Its title
association is observational proximity, not a verified player/scope schema.
Native card kind is also a different namespace from `Nfl2k5StatKind`.

## Scope and stat limitations

The existing manual accessor's comment identifies scope zero as current
match and scope 11 as season data (`src/recomp_manual.c:12–15`); it pushes
zero at line 54. Generated `sub_000CB240` explicitly branches on its scope
argument (`src/recomp/gen/recomp_0006.c:5695–5729`). That supports keeping
scope as an explicit extraction input, but does not prove which scope a
particular ESPN card selected.

The currently wired producer verifies only QB fields:
completions `0x04`, attempts `0x23`, passing yards `0x4C`, passing touchdowns
`0x40`, interceptions `0x16` (`src/recomp_manual.c:203–208`). No ordinary
non-QB `nfl2k5_broadcast_player_stat` caller was found in current `src`.
Receiver/RB/defense/kicker field IDs, historical scope/year, team side and
selected-card duration still need verified extraction.

## Evidence required before enabling further replacements

1. Correlate a card's context and handler with a capture showing the named
   player, exact title, field labels, values and period/year. Use multiple
   examples and both team sides; do not treat a blank title as today's stats.
2. Verify which context field is the selected roster record for that family,
   including roster membership/position and its lifetime. Distinguish direct
   player pointers from roster/team/list pointers.
3. Identify the actual native field IDs and scope supplied by the card's stat
   calls. Verify each result against its visible native numbers, especially
   zero-stat players and historical/career cards.
4. Capture original popup duration/end-of-card handling and preserve it before
   changing `ctx+8` to close the native card. Snapshot all identity/data while
   the guest context is valid, then send owned event data to the HTML layer.

Until those facts are verified, keep the native fallback for generic,
receiving and historical families. The event bridge can carry correctly
supplied non-QB identity/stats now, but it must not fabricate them from the
possession-side QB cache. Newly edited native bridge code has been reviewed
statically only and remains uncompiled/unverified in the running game.
