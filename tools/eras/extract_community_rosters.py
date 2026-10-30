"""My NFL Eras: community-made PS2 NFL 2K5 roster saves (1970+), decoded to JSON.

    python -I tools/eras/extract_community_rosters.py [--no-retail] [--tables]

Input: the unpacked PS2 memory-card saves under data/eras/ps2_saves/<stem>/<dir>/<dir>
(tools/eras/ps2max.py made them from the user's .max files; unpack.log lists them).
Only ROSTER saves are read here (TYPE "ROS", 593,984-byte payload). Franchise saves
(TYPE "FXG", 720,044 bytes) are listed in index.json but not extracted.

A roster payload is a version-0 ROST container: a 0x20-byte "ROST" resource wrapper,
then the ROST preamble at +0x20 (see .tmp-2k-football-mod-tools/tools/nfl2k5_ps2_save.py).
It is decoded with 2K Football Mod Tools' RosterDocument
(.tmp-2k-football-mod-tools/mod_editor/core/nfl2k5_roster_records.py): positions via
position_names('retail'), ratings in RATING_BYTE_ORDER, birth years via
decode_birth_year(raw, reference_year=<season>).

Output:
    data/eras/community_rosters/<season>.json    one file per season; the one season with
                                                  two saves (2019) gets 2019-<stem>.json too
    data/eras/community_rosters/index.json       every save, its season, the quality stats

Every file is labelled "community PS2 roster (unverified)": nothing in it has been
checked against history. The quality stats are mechanical (duplicates, cloned bodies,
leftover retail 2004 records, renamed slots, impossible heights), not judgements of
accuracy.

The optional retail baseline is the Xbox disc's own roster (original/vc_53450030,
read with rr.load_image); the PS2 retail roster is assumed, not proved, to match it.
Data only: nothing in a save is executed. Run with python -I.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import hmac
import json
import re
import statistics
import struct
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS_REPO = ROOT / ".tmp-2k-football-mod-tools"
sys.path.insert(0, str(TOOLS_REPO))
from mod_editor.core import nfl2k5_roster_records as rr  # noqa: E402

SAVES_DIR = ROOT / "data" / "eras" / "ps2_saves"
OUT_DIR = ROOT / "data" / "eras" / "community_rosters"
RETAIL_SOURCE = ROOT / "original"
SCHEMA = "nfl2k5_community_roster/v1"
SOURCE_LABEL = "community PS2 roster (unverified)"
ROSTER_PAYLOAD_SIZE = 593_984
GAME_CALENDAR_YEAR = 2004          # the retail game's season (rr.franchise_reference_year's base year)
CLUB_SLOTS = 32
POS = rr.position_names("retail")
RATING_ORDER = list(rr.RATING_BYTE_ORDER)
HEIGHT_RANGE = (64, 83)            # inches; outside this a record is flagged, not changed
WEIGHT_MAX = 399                   # 400-405 lb (raw 250-255) is flagged

# stem -> (season, role). The stem is the user's .max file name; the season comes from it
# (ros1970 = 1970, Ros2k1 = 2001, ros2k10 = 2010). One season has two roster saves:
# Ros2k19 (save directory "Ros2k19") is primary, ros2019 (generic directory "2K7") is
# written beside it as 2019-ros2019.json.
SAVES: dict[str, tuple[int, str]] = {
    "ros1970": (1970, "primary"), "ros1971": (1971, "primary"), "ros1972": (1972, "primary"),
    "ros1973": (1973, "primary"), "ros1974": (1974, "primary"), "ros1976": (1976, "primary"),
    "ros1977": (1977, "primary"), "Ros1978": (1978, "primary"), "Ros1979": (1979, "primary"),
    "ros1980": (1980, "primary"), "ros1981": (1981, "primary"), "ros1982": (1982, "primary"),
    "ros1983": (1983, "primary"), "ros1984": (1984, "primary"), "ros1985": (1985, "primary"),
    "ros1986": (1986, "primary"), "ros1987": (1987, "primary"), "ros1988": (1988, "primary"),
    "ros1989": (1989, "primary"), "ros1990": (1990, "primary"), "ros1991": (1991, "primary"),
    "ros1992": (1992, "primary"), "ros1993": (1993, "primary"), "ros1994": (1994, "primary"),
    "ros1995": (1995, "primary"), "ros1997": (1997, "primary"), "ros1998": (1998, "primary"),
    "Ros2k1": (2001, "primary"), "Ros2k2": (2002, "primary"), "Ros2k3": (2003, "primary"),
    "Ros2k4": (2004, "primary"), "Ros2k6": (2006, "primary"), "Ros2k7": (2007, "primary"),
    "ros2k8": (2008, "primary"), "ros2k9": (2009, "primary"), "ros2k10": (2010, "primary"),
    "ros2k11": (2011, "primary"), "ros2k12": (2012, "primary"), "ros2k13": (2013, "primary"),
    "ros2k15": (2015, "primary"), "Ros2k16": (2016, "primary"), "Ros2k17": (2017, "primary"),
    "ros2k18": (2018, "primary"), "Ros2k19": (2019, "primary"), "ros2019": (2019, "alternate"),
    "Ros2k20": (2020, "primary"), "Ros2k21": (2021, "primary"), "Ros2k22": (2022, "primary"),
}
# Stems deliberately not extracted, and why (other pre-1970 stems are skipped by rule).
SKIPPED = {
    "rosfictional": "not a season (fictional league)",
    "Ros1967": "named like a roster but holds a franchise payload (byte-identical to Fra1967); pre-1970",
}

NAME_OK = re.compile(r"^[A-Za-z][A-Za-z .'\-]*$")
PLACEHOLDER_WORDS = {p.lower() for p in POS} | {rr.position_long_name(c).lower() for c in range(len(POS))} | {
    "player", "unknown", "none", "noname", "created", "fake", "test", "tbd", "tba", "blank"}


def stem_season(stem: str) -> int | None:
    """The season a stem names (only used to spot stems the SAVES table does not list)."""
    for pattern, base in ((r"(?i)(?:ros|fra|fran|frani)(\d{4})", 0), (r"(?i)(?:ros|fra|fran|frani)2k(\d{1,2})", 2000),
                          (r"(?i)(?:ros|fra)(\d{2})", 1900)):
        m = re.fullmatch(pattern, stem)
        if m:
            return base + int(m.group(1))
    return None


def find_save(stem: str) -> Path:
    stem_dir = SAVES_DIR / stem
    found = [d for d in stem_dir.iterdir() if d.is_dir() and (d / d.name).is_file()]
    if len(found) != 1:
        raise SystemExit(f"{stem}: expected one save directory with a payload, found {len(found)}")
    return found[0]


def read_save(save_dir: Path) -> dict:
    """Payload, TYPE code and the EXTRA integrity check.

    EXTRA is usually the 4-byte CRC-32 of the payload; some saves instead carry a 20-byte
    HMAC-SHA1 under the same key the Xbox release uses (rr.SIG_KEY) -- both are accepted.
    """
    payload = (save_dir / save_dir.name).read_bytes()
    type_raw = (save_dir / "TYPE").read_bytes() if (save_dir / "TYPE").is_file() else b""
    extra = (save_dir / "EXTRA").read_bytes() if (save_dir / "EXTRA").is_file() else b""
    if len(extra) == 20:
        ok = hmac.compare_digest(hmac.new(rr.SIG_KEY, payload, hashlib.sha1).digest(), extra)
        integrity = "hmac-sha1 ok" if ok else "hmac-sha1 MISMATCH"
    elif len(extra) >= 4:
        ok = struct.unpack_from("<I", extra, 0)[0] == (zlib.crc32(payload) & 0xFFFFFFFF)
        integrity = "crc32 ok" if ok else "crc32 MISMATCH"
    else:
        integrity = "no EXTRA"
    return dict(payload=payload, directory=save_dir.name, integrity=integrity,
                type_code=type_raw.decode("utf-16le", "replace").split("\x00")[0] or None)


def raw_birth_year(values: dict) -> int:
    return values["birth_year_low"] | (values["birth_year_high"] << 3)


def iso_date(year: int, month: int, day: int) -> str | None:
    try:
        return dt.date(year, month, day).isoformat()
    except ValueError:
        return None


def person_key(p: rr.Player) -> tuple:
    v = p.record.values
    return (p.first, p.last, raw_birth_year(v), v["birth_month"], v["birth_day"], v["height"], v["weight_raw"])


def body_key(p: rr.Player) -> tuple:
    """Bio fields a cloned record keeps when only its name is changed."""
    v = p.record.values
    return (raw_birth_year(v), v["birth_month"], v["birth_day"], v["height"], v["weight_raw"],
            p.college_index, v["position"], v["jersey"])


def name_flags(first: str, last: str, team_words: set[str]) -> list[str]:
    flags = []
    for part, label in ((first, "first"), (last, "last")):
        text = part.strip()
        if not text:
            flags.append(f"empty {label} name")
        elif any(ch.isdigit() for ch in text):
            flags.append(f"digits in {label} name")
        elif "(" in text or ")" in text:
            flags.append(f"bracketed note in {label} name")
        elif not NAME_OK.match(text):
            flags.append(f"unusual characters in {label} name")
    # a placeholder is a position word plus a team word or another position word ("DE texans");
    # one ordinary-looking part ("Scott Player", "Cleveland Green") is a real name, not a placeholder
    words = {first.strip().lower(), last.strip().lower()}
    if words & PLACEHOLDER_WORDS and (words - PLACEHOLDER_WORDS) <= team_words | {""}:
        flags.append("placeholder name (position/team words)")
    return flags


def body_flags(values: dict) -> list[str]:
    flags = []
    if not HEIGHT_RANGE[0] <= values["height"] <= HEIGHT_RANGE[1]:
        flags.append(f"implausible height ({values['height']} in)")
    if values["weight_raw"] + 150 > WEIGHT_MAX:
        flags.append(f"implausible weight ({values['weight_raw'] + 150} lb)")
    return flags


def load_retail(enabled: bool) -> dict | None:
    if not enabled or not RETAIL_SOURCE.exists():
        return None
    try:
        doc = rr.load_image(RETAIL_SOURCE)
    except Exception as exc:  # the baseline is optional
        print(f"retail baseline unavailable: {exc}", file=sys.stderr)
        return None
    keys = {person_key(p) for p in doc.players if p.pool == "primary"}
    names = {(p.first, p.last) for p in doc.players if p.pool == "primary"}
    teams = [(t.city, t.nickname, t.abbreviation) for t in doc.teams]
    return dict(keys=keys, names=names, teams=teams, source=str(RETAIL_SOURCE.relative_to(ROOT)) + "/vc_53450030")


def birth_convention(doc: rr.RosterDocument, season: int) -> dict:
    """How this save's two-digit births line up with years pro (mechanical, not history).

    shift = stored year (read against the 2004 game calendar) + years_pro + 22 - season.
    ~0 means the stored years are real birth years; ~(2004 - season) means the author
    kept the players' AGES right for the 2004 game calendar instead. 22 is a nominal
    rookie age, so only the median is meaningful.
    """
    shifts = []
    for t in doc.teams[:CLUB_SLOTS]:
        for p in doc.team_players(t.index):
            v = p.record.values
            shifts.append(rr.decode_birth_year(raw_birth_year(v), GAME_CALENDAR_YEAR) + v["years_pro"] + 22 - season)
    if not shifts:
        return dict(estimate="no club players")
    shifts.sort()
    med = statistics.median(shifts)
    age_shift = GAME_CALENDAR_YEAR - season
    if abs(med) <= 3 and abs(med - age_shift) > 3:
        estimate = "real birth years"
    elif abs(med - age_shift) <= 3 and abs(med) > 3:
        estimate = "age-shifted to 2004"
    elif abs(med) <= 3 and abs(med - age_shift) <= 3:
        estimate = "indistinguishable (season near 2004)"
    else:
        estimate = "mixed or unclear"
    return dict(estimate=estimate, median_shift=med, p10_shift=shifts[len(shifts) // 10],
                p90_shift=shifts[len(shifts) * 9 // 10], age_shift_if_2004_calendar=age_shift)


def extract(stem: str, season: int, role: str, retail: dict | None) -> tuple[dict, dict, set]:
    save_dir = find_save(stem)
    save = read_save(save_dir)
    payload = save["payload"]
    if save["type_code"] != "ROS" or len(payload) != ROSTER_PAYLOAD_SIZE:
        raise SystemExit(f"{stem}: TYPE {save['type_code']!r}, {len(payload)} bytes: not a roster save")
    base = rr.find_block_base(payload)
    doc = rr.RosterDocument(payload, base=base, source=str(save_dir.relative_to(ROOT)), reference_year=season)
    convention = birth_convention(doc, season)

    club_of: dict[int, int] = {}
    club_hits: Counter = Counter()
    for t in doc.teams[:CLUB_SLOTS]:
        for off in t.slots:
            club_hits[off] += 1
            club_of.setdefault(off, t.index)
    slot_of = {(t.index, off): s for t in doc.teams for s, off in enumerate(t.slots)}
    team_words = {w.lower() for t in doc.teams[:CLUB_SLOTS] for w in (t.city, t.nickname, t.abbreviation) if w}

    unique_club = list({off: doc.by_offset[off] for t in doc.teams[:CLUB_SLOTS] for off in t.slots}.values())
    name_count = Counter((p.first, p.last) for p in unique_club)
    person_count = Counter(person_key(p) for p in unique_club)
    body_groups: dict[tuple, list[rr.Player]] = defaultdict(list)
    for p in unique_club:
        body_groups[body_key(p)].append(p)
    clone_offsets = {p.offset for g in body_groups.values() if len({(q.first, q.last) for q in g}) > 1 for p in g}

    players = []
    for p in doc.players:
        if p.pool != "primary" or p.group not in ("team", "free_agent", "reserve"):
            continue
        r, v = p.record, p.record.values
        raw = raw_birth_year(v)
        month, day = v["birth_month"], v["birth_day"]
        club = club_of.get(p.offset)
        flags = name_flags(p.first, p.last, team_words) + body_flags(v)
        if club_hits[p.offset] > 1:
            flags.append("same record on more than one club roster")
        if club is not None and person_count[person_key(p)] > 1:
            flags.append("duplicate player: another club record has the same name and bio")
        elif club is not None and name_count[(p.first, p.last)] > 1:
            flags.append("name shared by another club record (different bio)")
        if p.offset in clone_offsets:
            flags.append("cloned body: same bio as a differently named club record")
        retail_match = bool(retail and person_key(p) in retail["keys"])
        if retail_match:
            flags.append("bio identical to a retail 2004 record")
        birth = iso_date(rr.decode_birth_year(raw, season), month, day)
        if birth is None:
            flags.append("invalid birth date")
        players.append(dict(
            record_index=p.index,
            first=p.first, last=p.last,
            group=p.group,
            team=club, team_abbreviation=doc.teams[club].abbreviation if club is not None else None,
            depth_slot=slot_of.get((club, p.offset)) if club is not None else None,
            teams=sorted(set(p.teams)),
            position=POS[v["position"]] if v["position"] < len(POS) else f"#{v['position']}",
            position_code=v["position"],
            jersey=v["jersey"],
            height_in=v["height"],
            weight_lb=r.weight,
            birth_date=birth,
            birth_date_if_age_shifted=iso_date(
                rr.decode_birth_year(raw, GAME_CALENDAR_YEAR) - (GAME_CALENDAR_YEAR - season), month, day),
            birth_year_raw=raw, birth_month=month, birth_day=day,
            years_pro=v["years_pro"],
            college=p.college or None,
            ratings=[v[name] for name in RATING_ORDER],
            overall_estimate=r.overall(),
            pbp_id=v["pbp_id"], photo_id=v["photo_id"], player_type=v["player_type"],
            retail_2004_match=retail_match,
            flags=flags,
        ))

    teams = []
    for t in doc.teams:
        members = [doc.by_offset[off] for off in t.slots]
        entry = dict(index=t.index, city=t.city, nickname=t.nickname, abbreviation=t.abbreviation,
                     player_count=t.player_count, players_resolved=len(t.slots), is_club=t.index < CLUB_SLOTS,
                     kind=t.kind)
        if retail and t.index < len(retail["teams"]):
            rc, rn, ra = retail["teams"][t.index]
            entry["renamed_from_retail"] = (t.city, t.nickname, t.abbreviation) != (rc, rn, ra)
            if entry["renamed_from_retail"]:
                entry["retail_name"] = dict(city=rc, nickname=rn, abbreviation=ra)
        if t.index < CLUB_SLOTS and members:
            entry["mean_overall_estimate"] = round(statistics.mean(m.record.overall() for m in members), 1)
            entry["cloned_body_records"] = sum(m.offset in clone_offsets for m in members)
            entry["duplicate_player_records"] = sum(person_count[person_key(m)] > 1 for m in members)
            entry["shared_name_records"] = sum(name_count[(m.first, m.last)] > 1 for m in members)
            entry["implausible_body_records"] = sum(bool(body_flags(m.record.values)) for m in members)
            entry["retail_2004_matches"] = sum(bool(retail and person_key(m) in retail["keys"]) for m in members)
            entry["position_counts"] = dict(Counter(POS[m.record.values["position"]] for m in members))
        teams.append(entry)

    clubs = [t for t in teams if t["is_club"]]
    sizes = [t["players_resolved"] for t in clubs]
    means = [t["mean_overall_estimate"] for t in clubs if "mean_overall_estimate" in t]
    league_median = round(statistics.median(means), 1) if means else None
    club_rows = [pl for pl in players if pl["team"] is not None]
    low_rated = [t["abbreviation"] for t in clubs if league_median is not None and
                 t.get("mean_overall_estimate", 99) <= league_median - 12]
    name_issue = [(pl, name_flags(pl["first"], pl["last"], team_words)) for pl in club_rows]
    name_issue = [(pl, flags) for pl, flags in name_issue if flags]
    quality = dict(
        club_slots_with_players=sum(1 for s in sizes if s),
        club_roster_sizes=dict(min=min(sizes), max=max(sizes), total=sum(sizes)),
        club_records_unique=len(unique_club),
        records_on_two_or_more_club_rosters=sum(1 for c in club_hits.values() if c > 1),
        duplicate_player_records=sum(c for c in person_count.values() if c > 1),
        shared_name_records=sum(c for c in name_count.values() if c > 1),
        cloned_body_records=len(clone_offsets),
        implausible_body_records=sum(1 for pl in club_rows if any(f.startswith("implausible") for f in pl["flags"])),
        invalid_birth_dates=sum(1 for pl in club_rows if pl["birth_date"] is None),
        name_issues=[f"{pl['first']}|{pl['last']} ({pl['team_abbreviation']}): " + "; ".join(flags)
                     for pl, flags in name_issue],
        missing_college=sum(1 for pl in club_rows if not pl["college"]),
        retail_2004_matches=sum(1 for pl in club_rows if pl["retail_2004_match"]) if retail else None,
        retail_2004_name_matches=sum(1 for pl in club_rows if (pl["first"], pl["last"]) in retail["names"])
        if retail else None,
        renamed_club_slots=[f"{t['index']}: {t['retail_name']['city']} {t['retail_name']['nickname']} -> "
                            f"{t['city']} {t['nickname']} ({t['abbreviation']})"
                            for t in clubs if t.get("renamed_from_retail")] if retail else None,
        league_median_overall_estimate=league_median,
        low_rated_club_slots=low_rated,
        recombined_names=recombined_names(club_rows, set(low_rated)),
        free_agents=len(doc.free_agents),
        free_agent_records_extracted=sum(1 for pl in players if pl["group"] == "free_agent"),
        free_agent_retail_2004_matches=sum(1 for pl in players if pl["group"] == "free_agent"
                                           and pl["retail_2004_match"]) if retail else None,
        draft_class=len(doc.group_players("draft_class")),
        unlisted_primary_records=sum(1 for q in doc.players if q.pool == "primary" and q.group == "pool"),
        birth_convention=convention,
    )
    source = dict(label=SOURCE_LABEL, stem=stem, role=role, save_directory=save["directory"],
                  payload=str((save_dir / save_dir.name).relative_to(ROOT)).replace("\\", "/"),
                  payload_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest(),
                  type_code=save["type_code"], integrity=save["integrity"])
    out = dict(
        schema=SCHEMA, season=season, source=source,
        parser=dict(module=".tmp-2k-football-mod-tools/mod_editor/core/nfl2k5_roster_records.py",
                    loader="RosterDocument(payload, base=find_block_base(payload), reference_year=season)",
                    block_base=f"0x{base:x}", rost_version=doc.version, position_scheme="retail",
                    retail_baseline=retail["source"] if retail else None),
        rating_order=RATING_ORDER,
        notes=[
            "Source: " + SOURCE_LABEL + ". Names, ratings and bios are the save author's; none are verified.",
            "Team names are the save's own strings: slots 0-31 are the 32 clubs; 32-51 are the user, "
            "fantasy, alumni and Pro Bowl teams.",
            "players: primary-pool records on any team list or on the free-agent list. Draft-class and "
            "template records are left out (counted in quality).",
            "team = the first club slot (0-31) whose list holds the record, else null; depth_slot = its "
            "place in that list; teams = every team list holding it.",
            "ratings: one value per name in rating_order (RATING_BYTE_ORDER, record bytes +0x36..+0x51).",
            "birth_date decodes the two-digit year against the season (decode_birth_year(raw, season)). "
            "birth_date_if_age_shifted assumes the author kept ages right for the 2004 game calendar. "
            "quality.birth_convention says which one this save's years-pro values support.",
            "overall_estimate is the parser's own position-weighted estimate, not the game's overall.",
        ],
        quality=quality,
        teams=teams,
        players=players,
    )
    summary = dict(season=season, stem=stem, role=role, save_directory=save["directory"],
                   sha256=source["sha256"], integrity=save["integrity"], file=None,
                   club_names=[f"{t['city']} {t['nickname']} ({t['abbreviation']})" for t in clubs],
                   club_stats={t["abbreviation"]: dict(
                       size=t["players_resolved"], mean=t.get("mean_overall_estimate"),
                       clones=t.get("cloned_body_records", 0), dups=t.get("duplicate_player_records", 0),
                       shared_names=t.get("shared_name_records", 0), bodies=t.get("implausible_body_records", 0),
                       retail=t.get("retail_2004_matches", 0)) for t in clubs},
                   **quality)
    bio_team = {person_key(p) + (club_of[p.offset],) for p in unique_club}
    return out, summary, bio_team


def recombined_names(club_rows: list[dict], low: set[str]) -> dict | None:
    """Share of players whose first AND last name each occur elsewhere, but never together.

    Measured for the low-rated slots against all other slots, and as a baseline for every
    other slot against the rest. A much higher share in the low-rated slots is the sign of
    names assembled from other players' name parts (common names give a high baseline).
    """
    def share(group: list[dict], others: list[dict]) -> float:
        full = {(o["first"], o["last"]) for o in others}
        firsts, lasts = {o["first"] for o in others}, {o["last"] for o in others}
        hits = sum((g["first"], g["last"]) not in full and g["first"] in firsts and g["last"] in lasts for g in group)
        return hits / len(group) if group else 0.0
    if not low:
        return None
    filler = [r for r in club_rows if r["team_abbreviation"] in low]
    rest = [r for r in club_rows if r["team_abbreviation"] not in low]
    base = [share([r for r in rest if r["team_abbreviation"] == ab], [r for r in rest if r["team_abbreviation"] != ab])
            for ab in sorted({r["team_abbreviation"] for r in rest})]
    return dict(low_rated_slots=round(share(filler, rest), 2),
                other_slots_baseline=round(sum(base) / len(base), 2) if base else None)


def write_json(path: Path, data: dict) -> None:
    """Pretty header, one player per line: valid JSON that diffs well."""
    players = data.pop("players", None)
    head = json.dumps(data, indent=1, ensure_ascii=False)
    if players is None:
        path.write_text(head + "\n", encoding="utf-8")
        return
    rows = ",\n".join("  " + json.dumps(p, ensure_ascii=False, separators=(",", ":")) for p in players)
    path.write_text(head[:-2] + ',\n "players": [\n' + rows + "\n ]\n}\n", encoding="utf-8")
    data["players"] = players


def tables(index: dict) -> str:
    lines = ["| Season | Save | Club slots | Sizes | Renamed | Dup players | Shared names | Cloned bodies | "
             "Bad h/w | Retail-2004 bios | Low-rated slots | Birth years | Closest other save |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for s in index["roster_saves"]:
        sz = s["club_roster_sizes"]
        near = s.get("closest_other_save") or {}
        lines.append(
            f"| {s['season']} | {s['stem']}{' (alt)' if s['role'] != 'primary' else ''} | "
            f"{s['club_slots_with_players']}/32 | {sz['min']}-{sz['max']} ({sz['total']}) | "
            f"{len(s['renamed_club_slots'] or [])} | {s['duplicate_player_records']} | {s['shared_name_records']} | "
            f"{s['cloned_body_records']} | {s['implausible_body_records']} | {s['retail_2004_matches']} | "
            f"{', '.join(s['low_rated_club_slots']) or '-'} | {s['birth_convention'].get('estimate', '')} "
            f"({s['birth_convention'].get('median_shift')}/{s['birth_convention'].get('age_shift_if_2004_calendar')}) | "
            + (f"{near['stem']} ({near['shared_bio_team_records']}) |" if near else "none |"))
    slots = ("TB", "SEA", "CAR", "JAX", "BAL", "HOU", "CLE", "TEN")
    lines += ["", "Mean overall_estimate per slot, bold = low-rated (cloned-body / duplicate-player records):", "",
              "| Season | Save | League median | " + " | ".join(slots) + " | Recombined names (low-rated / baseline) |",
              "|---|---|---|" + "---|" * len(slots) + "---|"]
    for s in index["roster_saves"]:
        cs = s["club_stats"]
        cells = []
        for ab in slots:
            st = cs.get(ab)
            if st is None:
                cells.append("-")
                continue
            extra = f" ({st['clones']}/{st['dups']})" if st["clones"] or st["dups"] else ""
            mark = "**" if ab in s["low_rated_club_slots"] else ""
            cells.append(f"{mark}{st['mean']}{mark}{extra}")
        rn = s.get("recombined_names") or {}
        lines.append(f"| {s['season']} | {s['stem']} | {s['league_median_overall_estimate']} | " + " | ".join(cells)
                     + f" | {rn.get('low_rated_slots', '-')} / {rn.get('other_slots_baseline', '-')} |")
    return "\n".join(lines)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--no-retail", action="store_true", help="skip the retail 2004 baseline comparisons")
    ap.add_argument("--tables", action="store_true", help="print the markdown summary table")
    args = ap.parse_args()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    retail = load_retail(not args.no_retail)

    unknown, franchise = [], []
    for stem_dir in sorted(SAVES_DIR.iterdir(), key=lambda p: p.name.lower()):
        if not stem_dir.is_dir() or stem_dir.name in SAVES or stem_dir.name in SKIPPED:
            continue
        season = stem_season(stem_dir.name)
        save = read_save(find_save(stem_dir.name))
        if save["type_code"] == "FXG":
            franchise.append(dict(stem=stem_dir.name, season=season, save_directory=save["directory"],
                                  payload_bytes=len(save["payload"]), integrity=save["integrity"]))
        elif season is not None and season >= 1970:
            unknown.append(stem_dir.name)
    if unknown:
        print(f"warning: 1970+ roster stems not in SAVES: {unknown}", file=sys.stderr)

    summaries, bio_sets = [], {}
    for stem, (season, role) in sorted(SAVES.items(), key=lambda kv: (kv[1][0], kv[1][1] != "primary", kv[0])):
        data, summary, bio_team = extract(stem, season, role, retail)
        name = f"{season}.json" if role == "primary" else f"{season}-{stem}.json"
        write_json(OUT_DIR / name, data)
        summary["file"] = f"data/eras/community_rosters/{name}"
        summaries.append(summary)
        bio_sets[stem] = bio_team
        print(f"{season} {stem:12s} -> {name}: {summary['club_slots_with_players']}/32 clubs, "
              f"{summary['club_roster_sizes']['total']} club players", file=sys.stderr)
    # the other save whose club records (name, bio, team) overlap this one the most
    for s in summaries:
        best = max((o for o in summaries if o["stem"] != s["stem"]),
                   key=lambda o: len(bio_sets[s["stem"]] & bio_sets[o["stem"]]))
        shared = len(bio_sets[s["stem"]] & bio_sets[best["stem"]])
        s["closest_other_save"] = dict(stem=best["stem"], season=best["season"],
                                       shared_bio_team_records=shared) if shared else None
    index = dict(schema=SCHEMA + "/index", source_label=SOURCE_LABEL,
                 retail_baseline=retail["source"] if retail else None,
                 roster_saves=summaries,
                 skipped=[dict(stem=k, reason=v) for k, v in SKIPPED.items()],
                 franchise_saves_not_extracted=sorted(franchise, key=lambda f: (f["season"] or 0, f["stem"])))
    write_json(OUT_DIR / "index.json", index)
    if args.tables:
        print(tables(index))


if __name__ == "__main__":
    main()
