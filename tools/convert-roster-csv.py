#!/usr/bin/env python3
"""Convert the project's modern roster CSV into NFL2K5Tool input text.

The CSV uses stable player-slot indexes.  NFL2K5Tool writes team slots in
their stored order, so rows are grouped by the game's team name and sorted by
that index.  Historic/all-star rows share the game's FreeAgents pool.
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
from pathlib import Path

TEAM = {
    "SF": "49ers", "CHI": "Bears", "CIN": "Bengals", "BUF": "Bills",
    "DEN": "Broncos", "CLE": "Browns", "TB": "Buccaneers", "ARZ": "Cardinals",
    "SD": "Chargers", "KC": "Chiefs", "IND": "Colts", "DAL": "Cowboys",
    "MIA": "Dolphins", "PHI": "Eagles", "ATL": "Falcons", "NYG": "Giants",
    "JAX": "Jaguars", "NYJ": "Jets", "DET": "Lions", "GB": "Packers",
    "CAR": "Panthers", "NE": "Patriots", "OAK": "Raiders", "STL": "Rams",
    "BAL": "Ravens", "WAS": "Redskins", "NO": "Saints", "SEA": "Seahawks",
    "PIT": "Steelers", "HOU": "Texans", "TEN": "Titans", "MIN": "Vikings",
}

GAME_ORDER = [
    "49ers", "Bears", "Bengals", "Bills", "Broncos", "Browns", "Buccaneers",
    "Cardinals", "Chargers", "Chiefs", "Colts", "Cowboys", "Dolphins", "Eagles",
    "Falcons", "Giants", "Jaguars", "Jets", "Lions", "Packers", "Panthers",
    "Patriots", "Raiders", "Rams", "Ravens", "Redskins", "Saints", "Seahawks",
    "Steelers", "Texans", "Titans", "Vikings", "FreeAgents",
]

FIELDS = [
    ("first", "fname"), ("last", "lname"), ("position", "Position"),
    ("jersey", "JerseyNumber"), ("speed", "Speed"), ("agility", "Agility"),
    ("strength", "Strength"), ("jumping", "Jumping"), ("coverage", "Coverage"),
    ("pass_rush", "PassRush"), ("run_coverage", "RunCoverage"),
    ("pass_blocking", "PassBlocking"), ("run_blocking", "RunBlocking"),
    ("catch", "Catch"), ("run_route", "RunRoute"),
    ("break_tackle", "BreakTackle"), ("hold_onto_ball", "HoldOntoBall"),
    ("power_run_style", "PowerRunStyle"),
    ("pass_accuracy", "PassAccuracy"), ("pass_arm_strength", "PassArmStrength"),
    ("pass_read_coverage", "PassReadCoverage"), ("tackle", "Tackle"),
    ("kick_power", "KickPower"), ("kick_accuracy", "KickAccuracy"),
    ("stamina", "Stamina"), ("durability", "Durability"),
    ("leadership", "Leadership"), ("scramble", "Scramble"),
    ("composure", "Composure"), ("consistency", "Consistency"),
    ("aggressiveness", "Aggressiveness"), ("college", "College"),
    ("birth_date", "DOB"), ("hand", "Hand"), ("weight", "Weight"),
    ("height", "Height"), ("body", "BodyType"), ("skin", "Skin"),
    ("face", "Face"), ("dreads", "Dreads"), ("helmet", "Helmet"),
    ("face_mask", "FaceMask"), ("face_shield", "Visor"),
    ("eye_black", "EyeBlack"), ("mouthpiece", "MouthPiece"),
    ("left_glove", "LeftGlove"), ("right_glove", "RightGlove"),
    ("left_wrist", "LeftWrist"), ("right_wrist", "RightWrist"),
    ("left_elbow", "LeftElbow"), ("right_elbow", "RightElbow"),
    ("sleeves", "Sleeves"), ("left_shoe", "LeftShoe"),
    ("right_shoe", "RightShoe"), ("neck_roll", "NeckRoll"),
    ("turtleneck", "Turtleneck"), ("years_pro", "YearsPro"),
]


def clean(row: dict[str, str], key: str) -> str:
    value = (row.get(key) or "").strip()
    indexed = {
        "skin": [f"Skin{i}" for i in range(1, 23)],
        "face": [f"Face{i}" for i in range(1, 24)],
        "face_mask": [f"FaceMask{i}" for i in range(1, 28)],
        "left_glove": ["None", "Type1", "Type2", "Type3", "Type4",
                       "Team1", "Team2", "Team3", "Team4", "Taped"],
        "right_glove": ["None", "Type1", "Type2", "Type3", "Type4",
                        "Team1", "Team2", "Team3", "Team4", "Taped"],
        "left_wrist": ["None", "SingleWhite", "DoubleWhite", "SingleBlack", "DoubleBlack",
                       "NeopreneSmall", "NeopreneLarge", "ElasticSmall", "ElasticLarge",
                       "SingleTeam", "DoubleTeam", "TapedSmall", "TapedLarge", "Quarterback"],
        "right_wrist": ["None", "SingleWhite", "DoubleWhite", "SingleBlack", "DoubleBlack",
                        "NeopreneSmall", "NeopreneLarge", "ElasticSmall", "ElasticLarge",
                        "SingleTeam", "DoubleTeam", "TapedSmall", "TapedLarge", "Quarterback"],
        "left_elbow": ["None", "White", "Black", "WhiteBlackStripe", "BlackWhiteStripe",
                       "BlackTeamStripe", "Team", "WhiteTeamStripe", "Elastic", "Neoprene",
                       "WhiteTurf", "BlackTurf", "Taped", "HighWhite", "HighBlack", "HighTeam"],
        "right_elbow": ["None", "White", "Black", "WhiteBlackStripe", "BlackWhiteStripe",
                        "BlackTeamStripe", "Team", "WhiteTeamStripe", "Elastic", "Neoprene",
                        "WhiteTurf", "BlackTurf", "Taped", "HighWhite", "HighBlack", "HighTeam"],
        "left_shoe": ["Shoe1", "Shoe2", "Shoe3", "Shoe4", "Shoe5", "Shoe6", "Taped"],
        "right_shoe": ["Shoe1", "Shoe2", "Shoe3", "Shoe4", "Shoe5", "Shoe6", "Taped"],
    }
    if key in indexed and value.isdigit():
        n = int(value)
        if 0 <= n < len(indexed[key]):
            value = indexed[key][n]
    if key == "position" and value == "HB":
        value = "RB"
    if key == "power_run_style":
        value = {"1": "Finesse", "50": "Balanced", "99": "Power"}.get(value, value)
    if key == "body":
        value = value.replace(" ", "")
    if key == "birth_date":
        value = value.lstrip("'")
        try:
            value = datetime.strptime(value, "%Y-%m-%d").strftime("%-m/%-d/%Y")
        except (ValueError, OSError):
            # Windows' strftime lacks %-m; the fallback below is portable.
            try:
                d = datetime.strptime(value, "%Y-%m-%d")
                value = f"{d.month}/{d.day}/{d.year}"
            except ValueError:
                pass
    if key == "height" and value.isdigit():
        inches = int(value)
        value = f"{inches // 12}'{inches % 12}\""
    return value


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("output")
    args = ap.parse_args()
    with open(args.csv, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))

    grouped = {team: [] for team in GAME_ORDER}
    for row in rows:
        if row.get("pool", "primary").lower() != "primary":
            continue
        game_team = TEAM.get(row.get("team", ""), "FreeAgents")
        grouped[game_team].append(row)
    for team_rows in grouped.values():
        team_rows.sort(key=lambda r: int(r.get("index") or 0))

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        f.write("Key=" + ",".join(game for _, game in FIELDS) + "\n")
        for team in GAME_ORDER:
            f.write(f"\nTeam = {team}\n")
            for row in grouped[team]:
                writer.writerow([clean(row, source) for source, _ in FIELDS])

    print(f"Wrote {len(rows)} player rows to {out}")
    for team in GAME_ORDER:
        print(f"  {team}: {len(grouped[team])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
