"""Build a new team into mods\\teams\\<folder> (what the Team Creator's Build does).

    python build_team.py spec.json            # from a saved spec
    build(spec, game_root, progress)          # from the creator

spec = {
  "city", "nickname", "abbreviation", "folder" (optional),
  "base": "25"                  asset code of the NFL team whose uniform cut,
                                helmet shape and stadium crowd it starts from,
  "base_colors": [[r,g,b], ...] that team's colours (team.json of mods\\teams\\<ABBR>),
  "colors": [[r,g,b], ...]      the new team's, same order (primary, secondary, ...),
  "roster": [player dicts]      (rosters.py) -- 53 players in depth order
}

Writes team.json (identity, code, base, colours for the presentation layer),
team.rost (a one-team roster block from the player's own disc), roster.csv,
and files\\<code><side><style>.iff -- every uniform and crowd file of the base
team, recoloured, under the new team's own asset code.
"""
from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import game_assets as G  # noqa: E402
import rosters  # noqa: E402


def next_code(teams_dir: Path) -> str:
    used = set()
    for tj in teams_dir.glob("*/team.json"):
        try:
            used.add(str(json.loads(tj.read_text(encoding="utf-8")).get("code", "")).upper())
        except Exception:
            pass
    for c in "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        for first in "ZQ":
            if first + c not in used:
                return first + c
    raise RuntimeError("no free team code")


def hexrgb(c):
    return "#%02X%02X%02X" % tuple(c)


def build(spec: dict, game_root: str | Path, progress=print) -> Path:
    root = Path(game_root)
    teams = root / "mods" / "teams"
    folder = spec.get("folder") or re.sub(r"[^A-Za-z0-9]+", "", f"{spec['city']}{spec['nickname']}")[:40] or "NewTeam"
    out = teams / folder
    (out / "files").mkdir(parents=True, exist_ok=True)
    old = out / "team.json"
    code = None
    if old.exists():
        code = json.loads(old.read_text(encoding="utf-8")).get("code")
    code = code or next_code(teams)
    progress(f"Team code {code}, folder {out}")

    arc = G.Archive(root / "original" / "disc")
    (out / "team.rost").write_bytes(arc.roster_template())
    (out / "roster.csv").write_text(rosters.to_csv(spec["roster"]), encoding="utf-8")
    progress(f"Roster: {len(spec['roster'])} players")

    mapper = G.make_mapper([tuple(c) for c in spec["base_colors"]], [tuple(c) for c in spec["colors"]])
    files = arc.team_files(spec["base"])
    for f in (out / "files").glob("*.iff"):
        f.unlink()
    for i, name in enumerate(files):
        progress(f"Uniforms and crowd {i + 1}/{len(files)}: {name}")
        data = arc.read(name)
        new = G.recolor_iff(data, mapper)
        (out / "files" / (code + name[len(spec["base"]):])).write_bytes(new)

    info = {
        "code": code, "base": spec["base"],
        "city": spec["city"], "nickname": spec["nickname"], "abbreviation": spec["abbreviation"],
        "name": spec["nickname"],
        "colors": {"primary": hexrgb(spec["colors"][0]),
                   "secondary": hexrgb(spec["colors"][1] if len(spec["colors"]) > 1 else spec["colors"][0]),
                   "text": "#FFFFFF"},
        "logos": {"scorebug": "logos/scorebug.png"} if (out / "logos" / "scorebug.png").exists() else {},
        "aliases": [],
        "created_with": "NFL2K5-PC Team Creator",
    }
    (out / "team.json").write_text(json.dumps(info, indent=2), encoding="utf-8")
    (out / "creator.json").write_text(json.dumps(spec, indent=1), encoding="utf-8")
    progress(f"Done: {spec['city']} {spec['nickname']} ({code})")
    return out


if __name__ == "__main__":
    s = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    build(s, sys.argv[2] if len(sys.argv) > 2 else HERE.parent.parent)
