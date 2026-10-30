"""A throw-away test team that plays its home games in the converted Coliseum, with no code change.

    python tools/eras/stadiums/coliseum_test_team.py build      # data/eras/stadiums/coliseum/test-team/
    python tools/eras/stadiums/coliseum_test_team.py install    # copy it to mods/teams/zzColiseumTest
    python tools/eras/stadiums/coliseum_test_team.py remove     # take it out of mods/teams again
    python tools/eras/stadiums/coliseum_test_team.py pack       # data/eras/stadiums/coliseum/pack/LA_Coliseum/
    python tools/eras/stadiums/coliseum_test_team.py install-pack   # mods/eras/stadiums/LA_Coliseum (needs the
                                                                    # code patch in docs/eras/research/COLISEUM-IMPORT.md)

How it works (all existing runtime paths, src/nfl2k5_team_files.c and src/nfl2k5_teams.c):

* an added team (mods\\teams\\<folder>\\team.json with a Z code + team.rost) is reachable in Team Select's outside
  list; the folder name sorts last, so one step back from the first resident team lands on it;
* a .rost carries one stadium record (root pair 3) and the team record's +0x114 points at it; Team Select's
  set_home makes it the game's stadium ([0xE5FE64]) when the home team is chosen (mode >= 4);
* the record's asset code (+0x0C) is the file-name key of the nine stadium bundles: here "s60";
* every file in mods\\teams\\<folder>\\files\\ goes into the extra archive pack at boot (nfl2k5_archive_extend), so
  s60dd.iff ... s60ns.iff are found by name like the disc's own.

The roster is the Army Black Knights test team's (the 1994 49ers template); team.json makes it the Rams (base 23:
uniforms, crowd and colours fall back to the Rams' files) under the name "Coliseum Test".
"""
from __future__ import annotations

import json
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / "data" / "eras" / "stadiums" / "coliseum" / "build"
KIT = ROOT / "data" / "eras" / "stadiums" / "coliseum" / "test-team" / "zzColiseumTest"
TEMPLATE = ROOT / "mods" / "teams" / "ArmyBlackKnights" / "team.rost"
INSTALLED = ROOT / "mods" / "teams" / "zzColiseumTest"
CODE = "s60"
VARIANTS = [t + w for t in "dan" for w in "drs"]


def _rel(d, at):
    v = struct.unpack_from("<i", d, at)[0]
    return None if v == 0 else at + v - 1


def _wstr(d, at):
    out = []
    while True:
        c = struct.unpack_from("<H", d, at)[0]
        if not c:
            return "".join(out)
        out.append(chr(c))
        at += 2


def _put(d, at, old, new):
    """Write ``new`` over the string ``old`` in place (same slot, zero padded)."""
    if len(new) > len(old):
        raise SystemExit(f"{new!r} does not fit the slot of {old!r}")
    d[at:at + 2 * len(old) + 2] = new.encode("utf-16le") + bytes(2 * (len(old) - len(new)) + 2)


def build():
    d = bytearray(TEMPLATE.read_bytes())
    stadium, team = _rel(d, 0x14), _rel(d, 0x1C)
    if _rel(d, team + 0x114) != stadium:
        raise SystemExit("template: the team record does not point at its own stadium record")
    fields = {"name": 0x00, "location": 0x08, "code": 0x0C, "display": 0x10}
    wanted = {"name": "LA Coliseum", "location": "Los Angeles, CA", "code": CODE, "display": "L.A. Coliseum"}
    for key, off in fields.items():
        at = _rel(d, stadium + off)
        _put(d, at, _wstr(d, at), wanted[key])
    struct.pack_into("<I", d, stadium + 0x04, 93607)      # capacity (1946-1979 era, approx.)
    struct.pack_into("<I", d, stadium + 0x18, 0)          # outdoor
    struct.pack_into("<I", d, stadium + 0x1C, 1)          # natural grass
    KIT.mkdir(parents=True, exist_ok=True)
    (KIT / "files").mkdir(exist_ok=True)
    (KIT / "team.rost").write_bytes(bytes(d))
    (KIT / "team.json").write_text(json.dumps({
        "code": "Z9", "base": "23", "city": "Coliseum", "nickname": "Test", "abbreviation": "LAC",
        "name": "Test", "colors": {"primary": "#003594", "secondary": "#FFD100", "text": "#FFFFFF"},
        "logos": {}, "aliases": [], "created_with": "tools/eras/stadiums/coliseum_test_team.py"}, indent=2),
        encoding="utf-8")
    for v in VARIANTS:
        shutil.copy2(BUILD / f"{CODE}{v}.iff", KIT / "files" / f"{CODE}{v}.iff")
    print("kit:", KIT)


PACK = ROOT / "data" / "eras" / "stadiums" / "coliseum" / "pack" / "LA_Coliseum"
PACK_INSTALLED = ROOT / "mods" / "eras" / "stadiums" / "LA_Coliseum"
PACK_JSON = {"name": "LA MEMORIAL COLISEUM", "display": "L.A. Memorial Coliseum", "location": "Los Angeles, CA",
             "code": CODE, "clone": "QUALCOMM STADIUM", "host": "LOCO ARENA", "capacity": 93607, "indoor": 0, "grass": 1,
             "source": "NCAA Football 07 USC stadium (STADIUMS.DAT 0x600/0x604), tools/eras/stadiums/coliseum_build.py"}


def pack():
    """The Eras stadium pack: stadium.json + the nine bundles (loaded only through the Eras launcher)."""
    (PACK / "files").mkdir(parents=True, exist_ok=True)
    (PACK / "stadium.json").write_text(json.dumps(PACK_JSON, indent=2), encoding="utf-8")
    for v in VARIANTS:
        shutil.copy2(BUILD / f"{CODE}{v}.iff", PACK / "files" / f"{CODE}{v}.iff")
    print("pack:", PACK)


def install_pack():
    if PACK_INSTALLED.exists():
        shutil.rmtree(PACK_INSTALLED)
    shutil.copytree(PACK, PACK_INSTALLED)
    print("installed:", PACK_INSTALLED)


def install():
    if INSTALLED.exists():
        shutil.rmtree(INSTALLED)
    shutil.copytree(KIT, INSTALLED)
    print("installed:", INSTALLED)


def remove():
    if INSTALLED.exists():
        shutil.rmtree(INSTALLED)
    print("removed:", INSTALLED)


if __name__ == "__main__":
    {"build": build, "install": install, "remove": remove, "pack": pack, "install-pack": install_pack}[sys.argv[1]]()
