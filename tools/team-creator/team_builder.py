"""Build every team that ships as a recipe (what the installer runs).

    NFL2K5-TeamBuilder.exe <game folder> [--force] [--team <folder>]

A team folder under mods\\teams with a creator.json (the Team Creator's spec)
but no team.rost is built from the player's own disc in <game folder>\\original\\disc:
uniforms, front-end logos, photos and the roster block, exactly as the Team
Creator's Build does. Paths in the spec (logo, helmet_logo, photo_image) may be
relative to the team folder, so a recipe can be shared without any game files.
--force rebuilds teams that are already built.
"""
from __future__ import annotations

import json
import sys
import traceback
from pathlib import Path

HERE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))
sys.path.insert(0, str(HERE))
import build_team  # noqa: E402


def _resolve(team: Path, value):
    if not value:
        return value
    p = Path(value)
    return str(p if p.is_absolute() else team / p)


def build_folder(team: Path, root: Path, log) -> None:
    spec = json.loads((team / "creator.json").read_text(encoding="utf-8"))
    spec["folder"] = team.name
    for key in ("logo", "helmet_logo"):
        if spec.get(key):
            spec[key] = _resolve(team, spec[key])
    for p in spec.get("roster", []):
        if p.get("photo_image"):
            p["photo_image"] = _resolve(team, p["photo_image"])
    build_team.build(spec, root, progress=lambda m: log(f"  {m}"))


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith("--")]
    force = "--force" in argv
    only = argv[argv.index("--team") + 1] if "--team" in argv else None
    root = Path(args[0] if args else ".").resolve()
    teams = root / "mods" / "teams"
    logdir = root / "logs"
    logdir.mkdir(exist_ok=True)
    lines = []

    def log(m):
        print(m)
        lines.append(m)

    if not (root / "original" / "disc" / "vc_53450030").exists():
        log(f"No extracted disc at {root / 'original' / 'disc'}; nothing built.")
        (logdir / "team-builder.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
        return 2
    failed = 0
    for team in sorted(p for p in teams.iterdir() if p.is_dir()) if teams.exists() else []:
        if only and team.name != only:
            continue
        if not (team / "creator.json").exists() or ((team / "team.rost").exists() and not force):
            continue
        log(f"Building {team.name}")
        try:
            build_folder(team, root, log)
        except Exception:
            failed += 1
            log(traceback.format_exc())
    (logdir / "team-builder.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
