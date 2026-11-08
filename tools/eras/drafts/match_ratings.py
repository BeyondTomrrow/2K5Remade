"""Match a PFR draft class against the community PS2 rosters (data/eras/community_rosters).

usage: python -I match_ratings.py <repo_root> <draft_year> <out_json>
"""
import sys, re, json, unicodedata
from collections import Counter, defaultdict
from pathlib import Path

root, year, out = Path(sys.argv[1]), int(sys.argv[2]), Path(sys.argv[3])
draft = json.loads((root / f"data/eras/league/drafts/{year}.json").read_text(encoding="utf-8"))
CR = root / "data/eras/community_rosters"
LAST_SEASON = year + 14          # 15 seasons: rookie year .. year+14
seasons = [y for y in range(year, LAST_SEASON + 1) if (CR / f"{y}.json").is_file()]
missing_seasons = [y for y in range(year, LAST_SEASON + 1) if y not in seasons]

SUFFIX = re.compile(r"\b(jr|sr|ii|iii|iv)\b\.?")
def norm(s):
    s = unicodedata.normalize("NFKD", s or "").encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^a-z]", "", SUFFIX.sub("", s))
ALIAS = {"lsu": "louisiana state", "smu": "southern methodist", "byu": "brigham young", "usc": "southern california",
         "ucla": "california los angeles", "tcu": "texas christian", "utep": "texas el paso", "unlv": "nevada las vegas",
         "ole miss": "mississippi", "pitt": "pittsburgh", "miami (fl)": "miami", "boston col.": "boston college",
         "southern cal": "southern california", "texas a&m": "texas am"}
def norm_college(s):
    s = (s or "").strip()
    s = ALIAS.get(s.lower(), s)
    s = unicodedata.normalize("NFKD", s or "").encode("ascii", "ignore").decode().lower()
    s = re.sub(r"\bst\.?(?=\s|$)", "state", s)   # "Iowa St." -> "iowa state" (leading "St." handled below)
    s = re.sub(r"^state\.? ", "saint ", s)
    s = re.sub(r"\bcol\.?(?=\s|$)", "college", s)
    s = s.replace("university of ", "").replace(" university", "")
    return re.sub(r"[^a-z]", "", s)

# PFR / Wikipedia position -> compatible 2K5 positions
GROUP = {
    "QB": {"QB"}, "RB": {"HB", "FB"}, "HB": {"HB", "FB"}, "FB": {"FB", "HB"},
    "WR": {"WR"}, "TE": {"TE"},
    "T": {"T", "G"}, "OT": {"T", "G"}, "G": {"G", "T", "C"}, "OG": {"G", "T", "C"}, "C": {"C", "G"}, "OL": {"T", "G", "C"},
    "DE": {"DE", "DT", "OLB"}, "DT": {"DT", "DE"}, "NT": {"DT", "DE"}, "DL": {"DE", "DT"},
    "LB": {"OLB", "ILB", "DE"}, "OLB": {"OLB", "ILB", "DE"}, "ILB": {"ILB", "OLB"},
    "DB": {"CB", "SS", "FS"}, "CB": {"CB", "SS", "FS"}, "S": {"SS", "FS", "CB"}, "SS": {"SS", "FS", "CB"}, "FS": {"FS", "SS", "CB"},
    "K": {"K", "P"}, "P": {"P", "K"}, "LS": {"C", "G", "T", "TE"},
}
WIKI_POS = {"Running back": "RB", "Fullback": "FB", "Linebacker": "LB", "Wide receiver": "WR", "Defensive tackle": "DT",
            "Cornerback": "CB", "Tight end": "TE", "Defensive end": "DE", "Defensive back": "DB", "Quarterback": "QB",
            "Offensive tackle": "T", "Offensive Tackle": "T", "Tackle": "T", "Guard": "G", "Safety": "S", "Strong Safety": "SS",
            "Kicker": "K", "Center": "C", "Punter": "P", "Long snapper": "LS"}
def compatible(pick):
    s = set(GROUP.get(pick["pos"], set()))
    s |= GROUP.get(WIKI_POS.get(pick.get("wiki_position") or "", ""), set())
    return s

rosters = {}
for y in seasons:
    d = json.loads((CR / f"{y}.json").read_text(encoding="utf-8"))
    idx = defaultdict(list); by_last = defaultdict(list)
    for p in d["players"]:
        idx[norm(p["first"]) + "|" + norm(p["last"])].append(p)
        by_last[norm(p["last"])].append(p)
    rosters[y] = (d["rating_order"], idx, by_last)
rating_order = rosters[seasons[0]][0]

def candidates(pick, y):
    _, idx, by_last = rosters[y]
    names = {pick["player"]} | ({pick["wiki_name"]} if pick.get("wiki_name") else set())
    found = []
    for n in names:
        parts = n.replace(".", ". ").split()
        first, last = " ".join(parts[:-1]), parts[-1]
        if last.lower().rstrip(".") in ("jr", "sr", "ii", "iii", "iv") and len(parts) > 2:
            first, last = " ".join(parts[:-2]), parts[-2]
        found += idx.get(norm(first) + "|" + norm(last), [])
    uniq = {id(p): p for p in found}
    return list(uniq.values())

def nickname_candidates(pick, y):
    """Same last name, first names where one is a prefix of the other (Ken/Kenneth, Mike/Michael is NOT caught),
    accepted only with position AND college match (checked by the caller)."""
    _, idx, by_last = rosters[y]
    parts = pick["player"].replace(".", ". ").split()
    if parts[-1].lower().rstrip(".") in ("jr", "sr", "ii", "iii", "iv") and len(parts) > 2:
        parts = parts[:-1]
    f, l = norm(" ".join(parts[:-1])), norm(parts[-1])
    out = []
    for p in by_last.get(l, []):
        g = norm(p["first"])
        if g != f and len(min(f, g, key=len)) >= 2 and (g.startswith(f) or f.startswith(g)):
            out.append(p)
    return out

def score(pick, p, y):
    """(accept, confidence, reasons)"""
    reasons = []
    pos_ok = p["position"] in compatible(pick)
    reasons.append("pos ok" if pos_ok else f"pos {p['position']} vs {pick['pos']}")
    pc, rc = norm_college(pick["college"]), norm_college(p.get("college"))
    if pc and rc:
        col = "match" if (pc == rc or pc in rc or rc in pc) else "differs"
    else:
        col = "unknown"
    reasons.append(f"college {col}")
    exp_yp = y - year + 1          # LT 1981 carries years_pro 1 in the 1981 save
    yp_ok = p.get("years_pro") is not None and abs(p["years_pro"] - exp_yp) <= 2
    reasons.append("years_pro ok" if yp_ok else f"years_pro {p.get('years_pro')} (expected ~{exp_yp})")
    if col == "differs" and not pos_ok:
        return False, 0, reasons
    if col == "match" and pos_ok:
        conf = 3
    elif col == "match" or (pos_ok and col == "unknown"):
        conf = 2
    elif pos_ok:   # college differs, position ok
        conf = 1
    else:
        conf = 1
    if not yp_ok:
        conf -= 1
    return conf >= 1, conf, reasons

results = []
for pick in draft["picks"]:
    seasons_found = {}
    amb = []
    dropped = []
    for y in seasons:
        if pick["last_season"] is not None and y > pick["last_season"]:
            # outside PFR's career span: a later same-name player, or a retired player left in the save
            if candidates(pick, y) or nickname_candidates(pick, y):
                dropped.append(y)
            continue
        if pick["last_season"] is None and y > year + 1:
            continue
        cands = candidates(pick, y)
        scored = [(score(pick, p, y), p) for p in cands]
        ok = [(s, p) for s, p in scored if s[0]]
        nick = False
        if not ok:
            for p in nickname_candidates(pick, y):
                s = score(pick, p, y)
                if s[0] and "pos ok" in s[2] and "college match" in s[2]:
                    ok.append(((True, s[1] - 1, s[2] + ["first-name variant: " + p["first"]]), p)); nick = True
        if not ok:
            continue
        ok.sort(key=lambda t: -t[0][1])
        best = ok[0]
        ties = [t for t in ok if t[0][1] == best[0][1]
                and (t[1]["position"], t[1].get("college"), t[1]["ratings"]) != (best[1]["position"], best[1].get("college"), best[1]["ratings"])]
        if len(ties) > 1:
            amb.append({"season": y, "records": [{"team": t[1]["team_abbreviation"], "position": t[1]["position"], "college": t[1].get("college"),
                        "overall_estimate": t[1]["overall_estimate"]} for t in ties]})
        s, p = best
        seasons_found[y] = {"team": p["team_abbreviation"], "group": p["group"], "position": p["position"], "college": p.get("college"),
                            "years_pro": p.get("years_pro"), "overall_estimate": p["overall_estimate"],
                            "ratings": dict(zip(rating_order, p["ratings"])), "confidence": s[1], "checks": s[2],
                            "record_index": p["record_index"]}
    entry = {k: pick[k] for k in ("round", "pick", "team", "player", "pos", "college", "pfr_id", "games", "last_season", "pro_bowls", "ap1", "hof")}
    entry["wiki_position"] = pick.get("wiki_position")
    if year in seasons_found:
        entry["status"] = "rookie_rated"
    elif year + 1 in seasons_found:
        entry["status"] = "first_rated_second_season"
    elif seasons_found:
        entry["status"] = "rated_later"
    else:
        entry["status"] = "unmatched"
    entry["seasons_rated"] = sorted(seasons_found)
    if seasons_found:
        peak_y = max(seasons_found, key=lambda y: (seasons_found[y]["overall_estimate"], -y))
        entry["rookie"] = seasons_found.get(year)
        entry["peak"] = {"season": peak_y, "overall_estimate": seasons_found[peak_y]["overall_estimate"],
                         "years_after_draft": peak_y - year}
        entry["overall_by_season"] = {y: v["overall_estimate"] for y, v in sorted(seasons_found.items())}
        entry["min_confidence"] = min(v["confidence"] for v in seasons_found.values())
        entry["seasons"] = seasons_found
    if amb:
        entry["ambiguous"] = amb
    if dropped:
        entry["name_found_outside_pfr_career"] = dropped
    results.append(entry)

by_round = defaultdict(Counter)
for e in results:
    by_round[e["round"]][e["status"]] += 1
    by_round[e["round"]]["total"] += 1
summary = {r: dict(c) for r, c in sorted(by_round.items())}
tot = Counter(e["status"] for e in results)
played = [e for e in results if e["games"]]
doc = {
    "schema": "nfl_draft_ratings_match/v1", "draft_year": year,
    "inputs": {"draft": f"data/eras/league/drafts/{year}.json", "rosters": [f"data/eras/community_rosters/{y}.json" for y in seasons],
               "missing_seasons": missing_seasons},
    "source_label": "community PS2 roster (unverified) ratings matched to Pro-Football-Reference picks",
    "method": ["name = normalized first|last (accents, punctuation, Jr/Sr/II/III dropped); Wikipedia spelling also tried",
               "accept if position is in the pick's compatible 2K5 group or the college matches; reject if both differ",
               "confidence 3 = position+college, 2 = one of them (other unknown), 1 = college differs but position ok; -1 if years_pro is off by >2 from (season - draft_year + 1)",
               "seasons after PFR's last_season are not used (same-name later players, retired players left in saves); picks with no PFR games are still searched in their draft season and the next",
               "overall_estimate is the extractor's position-weighted estimate, not the game's overall"],
    "totals": dict(tot), "picks_who_played_nfl_games": len(played),
    "played_and_rated": sum(1 for e in played if e["status"] != "unmatched"),
    "by_round": summary, "picks": results}
out.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")
print("seasons", seasons, "missing", missing_seasons)
print("totals", dict(tot), "played", len(played), "played&rated", doc["played_and_rated"])
for r, c in summary.items():
    print(r, c)
print("ambiguous picks", sum(1 for e in results if "ambiguous" in e))
print("low confidence (min_conf<=1)", [(e["pick"], e["player"], e["min_confidence"]) for e in results if e.get("min_confidence", 9) <= 1][:40])
