"""Rosters for new teams: imported from Madden / NCAA Football, or generated.

A roster here is a list of player dicts:
  first, last, pos (NFL 2K5 code 0..16), number, height (in), weight (lb),
  years_pro, skin (0..21), face (0..15), hand (1 right), ratings (28, in the
  game's record order -- RATINGS below), ovr (for sorting only)

pick_53() chooses a game-day roster by position; to_csv() writes the
roster.csv the game reads when the team loads (src/nfl2k5_team_files.c).
"""
from __future__ import annotations

import csv
import io
import random

import eadb

POSITIONS = ("QB", "K", "P", "WR", "CB", "FS", "SS", "HB", "FB", "TE", "OLB", "ILB", "C", "G", "T", "DT", "DE")
RATINGS = ("speed", "agility", "pass_arm_strength", "stamina", "kick_power", "durability", "strength",
           "jumping", "coverage", "run_route", "tackle", "break_tackle", "pass_accuracy",
           "pass_read_coverage", "catch", "run_blocking", "pass_blocking", "hold_onto_ball", "pass_rush",
           "run_coverage", "kick_accuracy", "kicking_style", "leadership", "power_run_style", "composure",
           "scramble", "consistency", "aggressiveness")

# EA position (PPOS) -> NFL 2K5 code
EA_POS = {0: 0, 1: 7, 2: 8, 3: 3, 4: 9, 5: 14, 6: 13, 7: 12, 8: 13, 9: 14, 10: 16, 11: 16, 12: 15,
          13: 10, 14: 11, 15: 10, 16: 4, 17: 5, 18: 6, 19: 1, 20: 2}

# A 53-man roster by NFL 2K5 position (the 53rd is the best player left).
DEPTH = {0: 3, 7: 3, 8: 1, 3: 6, 9: 3, 14: 4, 13: 4, 12: 2, 16: 4, 15: 4, 10: 4, 11: 3,
         4: 5, 5: 2, 6: 2, 1: 1, 2: 1}

# EA rating fields, first one present wins (names moved between years).
EA_RATINGS = {
    "speed": ["PSPD"], "agility": ["PAGI"], "pass_arm_strength": ["PTHP"], "stamina": ["PSTA"],
    "kick_power": ["PKPR"], "durability": ["PINJ"], "strength": ["PSTR"], "jumping": ["PJMP"],
    "coverage": [("avg", "PMCV", "PZCV"), ("avg", "PLMC", "PLZC"), "PCOV"],
    "run_route": ["PRTR", ("avg", "PLSR", "PLMR", "PDRR")],
    "tackle": ["PTAK"], "break_tackle": ["PBTK", "PLBT"],
    "pass_accuracy": ["PTHA", ("avg", "PSAC", "PMAC", "PDAC")],
    "pass_read_coverage": ["PAWR"], "catch": ["PCTH"], "run_blocking": ["PRBK"],
    "pass_blocking": ["PPBK"], "hold_onto_ball": ["PCAR"],
    "pass_rush": [("max", "PPMV", "PFMV"), "PBSH"], "run_coverage": ["PPRC", "PLPR"],
    "kick_accuracy": ["PKAC"], "leadership": ["PAWR"], "composure": ["PAWR"],
    "consistency": ["POVR"], "aggressiveness": ["PHIT", "PLHT"],
}


def _ea(row: dict, spec, default=50) -> int:
    for k in spec:
        if isinstance(k, tuple):
            vals = [row[x] for x in k[1:] if isinstance(row.get(x), int) and row.get(x)]
            if vals:
                return round(sum(vals) / len(vals)) if k[0] == "avg" else max(vals)
        elif isinstance(row.get(k), int) and row.get(k):
            return row[k]
    return default


class EARoster:
    """A Madden or NCAA Football roster file (EA DB): its teams and players."""

    def __init__(self, path: str):
        self.path = path
        data = open(path, "rb").read()
        self.tables = eadb.read(data)
        if "TEAM" not in self.tables or "PLAY" not in self.tables:
            raise ValueError("not a roster: no TEAM / PLAY tables")
        self.teams = eadb.rows(self.tables["TEAM"])
        self.players = eadb.rows(self.tables["PLAY"])
        self.kind = "NCAA Football" if any("TMNA" == f.name for f in self.tables["TEAM"].fields) else "Madden"

    def team_list(self) -> list[tuple[int, str]]:
        out = []
        for t in self.teams:
            if self.kind == "NCAA Football":
                label = f"{t.get('TDNA', '')} {t.get('TMNA', '')}".strip()
            else:
                label = f"{t.get('TLNA', '')} {t.get('TDNA', '')}".strip()
            n = sum(1 for p in self.players if p.get("TGID") == t.get("TGID"))
            if n:
                out.append((t["TGID"], f"{label}  ({n} players)"))
        return sorted(out, key=lambda x: x[1])

    def team_info(self, tgid: int) -> dict:
        t = next(t for t in self.teams if t["TGID"] == tgid)
        college = self.kind == "NCAA Football"
        prim = (t.get("TBCR", 0), t.get("TBCG", 0), t.get("TBCB", 0))
        sec = (t.get("TB2R", 0), t.get("TB2G", 0), t.get("TB2B", 0))
        return {"city": t.get("TDNA" if college else "TLNA", ""), "nickname": t.get("TMNA" if college else "TDNA", ""),
                "abbreviation": (t.get("TSNA") or "")[:4], "primary": prim, "secondary": sec}

    def team_players(self, tgid: int) -> list[dict]:
        out = []
        for p in self.players:
            if p.get("TGID") != tgid:
                continue
            pos = EA_POS.get(p.get("PPOS", 0), 0)
            r = {name: max(1, min(99, _ea(p, EA_RATINGS[name]))) for name in EA_RATINGS}
            r["kicking_style"] = 50
            r["power_run_style"] = 99 if p.get("PTRK", 0) > p.get("PELU", 0) + 10 else 1 if p.get("PELU", 0) > p.get("PTRK", 0) + 10 else 50
            r["scramble"] = r["speed"] if pos == 0 else 50
            years = p.get("PYRP", p.get("PYEA", 0))
            out.append({
                "first": p.get("PFNA", ""), "last": p.get("PLNA", ""), "pos": pos,
                "number": p.get("PJEN", 0), "height": p.get("PHGT", 72) or 72,
                "weight": (p.get("PWGT", 60) or 60) + 160, "years_pro": min(31, years),
                "skin": min(21, p.get("PSKI", 3) * 3), "face": random.randrange(16), "hand": 1,
                "ratings": [r[k] for k in RATINGS], "ovr": p.get("POVR", 50),
            })
        return out


# ---- generation ------------------------------------------------------------

FIRST = ("James John Robert Michael William David Richard Joseph Thomas Charles Chris Daniel Matthew Anthony "
         "Mark Donald Steven Paul Andrew Joshua Kenneth Kevin Brian George Timothy Ronald Jason Ryan Jacob "
         "Gary Nicholas Eric Jonathan Stephen Larry Justin Scott Brandon Benjamin Samuel Gregory Frank "
         "Alexander Raymond Patrick Jack Dennis Jerry Tyler Aaron Jose Adam Nathan Henry Douglas Zachary "
         "Peter Kyle Noah Ethan Jeremy Walter Christian Keith Roger Terry Austin Sean Gerald Carl Harold "
         "Dylan Arthur Lawrence Jordan Jesse Bryan Billy Bruce Gabriel Joe Logan Albert Willie Alan Eugene "
         "Russell Vincent Philip Bobby Johnny Bradley Marcus Darius Jamal Tre DeShawn Malik Isaiah Elijah").split()
LAST = ("Smith Johnson Williams Brown Jones Garcia Miller Davis Rodriguez Martinez Hernandez Lopez Wilson "
        "Anderson Thomas Taylor Moore Jackson Martin Lee Thompson White Harris Clark Lewis Robinson Walker "
        "Young Allen King Wright Scott Hill Green Adams Baker Nelson Carter Mitchell Roberts Turner Phillips "
        "Campbell Parker Evans Edwards Collins Stewart Morris Murphy Cook Rogers Morgan Peterson Cooper Reed "
        "Bailey Bell Kelly Howard Ward Cox Richardson Wood Watson Brooks Bennett Gray James Hughes Price "
        "Sanders Myers Long Ross Foster Powell Jenkins Perry Russell Sullivan Butler Barnes Fisher Henderson "
        "Coleman Simmons Patterson Jordan Reynolds Hamilton Graham Wallace Hayes Bryant Gibson Ellis").split()
# typical ratings emphasis per position: (rating, weight) boosts
EMPHASIS = {
    0: ("pass_arm_strength", "pass_accuracy", "pass_read_coverage", "composure"),
    7: ("speed", "agility", "break_tackle", "hold_onto_ball"), 8: ("run_blocking", "strength", "break_tackle"),
    3: ("speed", "catch", "run_route", "agility"), 9: ("catch", "run_blocking", "strength"),
    12: ("run_blocking", "pass_blocking", "strength"), 13: ("run_blocking", "pass_blocking", "strength"),
    14: ("pass_blocking", "run_blocking", "strength"), 15: ("strength", "pass_rush", "tackle"),
    16: ("pass_rush", "speed", "tackle"), 10: ("tackle", "speed", "run_coverage"),
    11: ("tackle", "run_coverage", "strength"), 4: ("speed", "coverage", "agility"),
    5: ("coverage", "speed", "tackle"), 6: ("tackle", "coverage", "run_coverage"),
    1: ("kick_power", "kick_accuracy"), 2: ("kick_power", "kick_accuracy"),
}
SIZE = {0: (75, 220), 7: (71, 210), 8: (72, 245), 3: (73, 200), 9: (77, 255), 12: (76, 300),
        13: (76, 315), 14: (78, 315), 15: (75, 305), 16: (76, 270), 10: (75, 245), 11: (74, 240),
        4: (71, 190), 5: (72, 205), 6: (72, 210), 1: (72, 195), 2: (74, 210)}


def generate(strength: int = 75, seed: int | None = None) -> list[dict]:
    """A full roster (more than 53) around a team strength of 40..99."""
    rnd = random.Random(seed)
    out = []
    used = set()
    for pos, n in DEPTH.items():
        for k in range(n + 1):
            base = max(35, min(99, strength - k * 6 + rnd.randint(-6, 6)))
            r = {name: max(20, min(99, base - 25 + rnd.randint(-8, 8))) for name in RATINGS}
            for name in EMPHASIS.get(pos, ()):
                r[name] = max(30, min(99, base + rnd.randint(-5, 5)))
            r["stamina"] = max(60, r["stamina"])
            r["durability"] = max(55, r["durability"])
            r["kicking_style"], r["power_run_style"], r["scramble"] = 50, 50, 50
            h, w = SIZE[pos]
            while True:
                num = rnd.randint(1, 99)
                if num not in used:
                    break
            used.add(num)
            out.append({"first": rnd.choice(FIRST), "last": rnd.choice(LAST), "pos": pos, "number": num,
                        "height": h + rnd.randint(-2, 2), "weight": w + rnd.randint(-12, 12),
                        "years_pro": rnd.randint(0, 10), "skin": rnd.randrange(22), "face": rnd.randrange(16),
                        "hand": 1, "ratings": [r[k] for k in RATINGS], "ovr": base})
    return out


def pick_53(players: list[dict]) -> list[dict]:
    """Best players per position to the DEPTH counts, best first within a
    position (that order is the depth chart), then the best left to 53."""
    by_pos: dict[int, list[dict]] = {}
    for p in sorted(players, key=lambda p: -p["ovr"]):
        by_pos.setdefault(p["pos"], []).append(p)
    chosen = []
    for pos, n in DEPTH.items():
        chosen += by_pos.get(pos, [])[:n]
    rest = sorted([p for p in players if p not in chosen], key=lambda p: -p["ovr"])
    chosen += rest[:max(0, 53 - len(chosen))]
    chosen.sort(key=lambda p: (p["pos"], -p["ovr"]))
    return chosen[:53]


def to_csv(players: list[dict]) -> str:
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["first", "last", "pos", "number", "height", "weight", "years_pro", "skin", "face", "hand", *RATINGS])
    for p in players:
        w.writerow([p["first"][:15].replace(",", ""), p["last"][:15].replace(",", ""), p["pos"], p["number"] % 100,
                    p["height"], p["weight"], p["years_pro"], p["skin"], p["face"], p["hand"], *p["ratings"]])
    return buf.getvalue()
