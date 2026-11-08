"""My NFL Eras M2 test T2.2 (offline half): the players the result capture read at the
final whistle (box JSON, schema nfl2k5_eras_box/v1) against the runtime package's roster
CSV rows, row for row: first and last name, jersey, position; and the name pointers
against the arena layout logged at staging ("arena XXXXXXXX" in the [TEAMS] staged line).

usage: python -I tools/eras/m2_identity_check.py <box.json> <stderr.log> <home_id> <away_id>
"""
import csv
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PKG = ROOT / "data" / "eras" / "runtime" / "1980"
ARENA_CHARS = 32


def rows(team_id):
    with open(PKG / "rosters" / f"{team_id}.csv", newline="") as f:
        return list(csv.DictReader(f))


def main():
    box_path, log_path, home, away = sys.argv[1:5]
    box = json.loads(Path(box_path).read_text(encoding="utf-8"))
    log = Path(log_path).read_text(encoding="utf-8", errors="replace")
    arenas = {int(m.group(1)): int(m.group(2), 16)
              for m in re.finditer(r"\[TEAMS\] staged side (\d).*arena ([0-9A-F]{8})", log)}
    bad_total = 0
    for side, key, team_id in ((0, "home", home), (1, "away", away)):
        want = rows(team_id)
        got = box[key]["players"]
        arena = arenas.get(side)
        bad = []
        if len(got) != len(want):
            bad.append(f"count {len(got)} != {len(want)}")
        for i, (p, r) in enumerate(zip(got, want)):
            exp_first = arena + (i * 2) * ARENA_CHARS * 2 if arena is not None else None
            exp_last = arena + (i * 2 + 1) * ARENA_CHARS * 2 if arena is not None else None
            checks = [
                (p.get("first"), r["first"]), (p.get("last"), r["last"]),
                (int(p.get("jersey", -1)), int(r["number"])), (int(p.get("position", -1)), int(r["pos"])),
                (int(p["first_ptr"], 16), exp_first), (int(p["last_ptr"], 16), exp_last),
            ]
            if any(a != b for a, b in checks):
                bad.append(f"row {i}: {checks}")
        bad_total += len(bad)
        print(f"{key} ({team_id}): {len(got)} players at the whistle, {len(want)} CSV rows, "
              f"arena {arena:08X}, {len(bad)} mismatches" if arena is not None else
              f"{key}: no arena in the log")
        for b in bad[:10]:
            print("   ", b)
    print("T2.2 offline:", "OK" if bad_total == 0 else f"FAIL ({bad_total})")
    return 0 if bad_total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
