"""Summarize real presentation deliveries and locate composed native captures.

Read-only: never opens the game process or synthesizes a presentation event.
"""
import collections
import json
import pathlib
import re
import sys

folder = pathlib.Path(sys.argv[1]).resolve()
events, shots = [], []
for line in (folder / "stderr.log").read_text(errors="replace").splitlines():
    if "[HTMLPRES] event " in line:
        try:
            events.append(json.loads(line.split("[HTMLPRES] event ", 1)[1]))
        except ValueError:
            pass
    match = re.search(r"\[PSHOT\] (.+) t=(\d+)", line)
    if match:
        shots.append((int(match[2]) / 1000, match[1]))
print("Player positions:", dict(collections.Counter(
    e.get("player", {}).get("position") for e in events if e["name"] == "PLAYER_STAT")))
print("Team categories:", dict(collections.Counter(
    e.get("category") for e in events if e["name"] == "TEAM_STAT")))
result = []
for key, kind in [("WR", "PLAYER_STAT"), ("HB", "PLAYER_STAT"),
                  ("DT", "PLAYER_STAT"), ("TE", "PLAYER_STAT"),
                  ("possession", "TEAM_STAT"), ("defense", "TEAM_STAT"),
                  ("drive", "DRIVE_SUMMARY")]:
    event = next((e for e in events if e["name"] == kind and (
        key == "drive" or e.get("player", {}).get("position") == key or
        e.get("category") == key)), None)
    if event:
        shot = next((s for s in shots if s[0] >= event["time"] + 2), None)
        result.append({"family": key, "event": event, "capture": shot})
print(json.dumps(result, indent=2))
