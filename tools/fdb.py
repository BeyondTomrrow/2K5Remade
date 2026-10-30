"""Searchable function database for the NFL 2K5 retail XBE (and optionally a pack).

    python tools/fdb.py build            # writes analysis/fdb.sqlite
    python tools/fdb.py search <text>    # functions whose name/strings/notes match
    python tools/fdb.py show <address>   # one function in full
    python tools/fdb.py xrefs <address>  # callers and callees

Built only from files the pipeline already produces (analysis/, src/recomp/gen,
src/recomp_manual.c, mods/packs/*). Nothing here changes the recompiler.

Columns worth knowing:
  purpose      heuristic label from referenced strings and known names
  purpose_src  "string:<keyword>", "name", or "" (none)
  confidence   how sure we are of the name/purpose: high (named by func_id or
               a manual override), medium (detector-confident), low (guessed
               from strings only), none
  recomp       "generated", "manual" (hand override), "stub" (unresolved in the
               converter) or "missing"
  pack_changed bytes that differ in SOFTDRINK 2K28 v0.2 (the pack's code)
"""
import glob
import json
import os
import re
import sqlite3
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AN = os.path.join(ROOT, "analysis")
DB = os.path.join(AN, "fdb.sqlite")
GEN = os.path.join(ROOT, "src", "recomp", "gen")
MANUAL = os.path.join(ROOT, "src", "recomp_manual.c")
RETAIL = os.path.join(ROOT, "original", "disc", "default.xbe")
PACK = os.path.join(ROOT, "mods", "packs", "softdrink_default.xbe")
# Pack variants: name -> (analysis dir, generated dir, patched XBE)
PACKS = {
    "v02": (os.path.join(ROOT, "analysis-softdrink"), os.path.join(ROOT, "build", "gen-softdrink"), PACK),
    "v04": (os.path.join(ROOT, "analysis-softdrink-v04"), os.path.join(ROOT, "build", "gen-softdrink-v04"),
            os.path.join(ROOT, "mods", "packs", "SOFTDRINK_2K28-v0.4", "default.xbe")),
}

# Keyword buckets for the areas the user asked for. First match wins for the
# purpose label; each string gives evidence, so the label is only a guess.
BUCKETS = [
    ("ESPN broadcast", r"espn|scorebug|broadcast|lineup|announcer|commentary"),
    ("camera", r"camera|cam_|replay"),
    ("animation", r"anim|motion|celebrat|dance|pose|\.ani"),
    ("player model", r"player|roster|helmet|jersey|face|body|model"),
    ("gameplay", r"play |down|snap|tackle|pass|rush|kick|coverage|blocking|pursuit|fatigue"),
    ("franchise", r"franchise|draft|season|schedule|coach|career|week"),
    ("UI/menu", r"menu|button|screen|option|dialog|press start|select"),
    ("audio", r"\.wav|\.wma|\.xwb|\.xsb|sound|music|xact|dsound|voice"),
    ("memory", r"heap|alloc|pool|memory|resource|archive|\.iff|\.big"),
    ("video", r"\.bik|\.xmv|\.mpg|movie|intro"),
]


def sections(d):
    base = struct.unpack_from("<I", d, 0x104)[0]
    nsec, sa = struct.unpack_from("<II", d, 0x11C)
    so = sa - base
    out = []
    for i in range(nsec):
        fl, va, vsz, raw, rsz, na = struct.unpack_from("<IIIIII", d, so + i * 56)
        out.append((fl, va, rsz, raw))
    return out


UTF16_RUN = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")


def utf16_strings(d, secs):
    """UTF-16LE strings in the non-executable sections: [(va, text)].

    The game's own text (menus, team and player names, screen titles) is stored
    this way; the ASCII string extractor misses all of it.
    """
    out = []
    # Every section: the XBE flag bits do not separate code from data here
    # (.rdata carries the executable bit), so scan them all.
    for fl, va, rsz, raw in secs:
        blob = d[raw:raw + rsz]
        for m in UTF16_RUN.finditer(blob):
            if m.start() % 2:
                continue
            text = m.group().decode("utf-16le")
            out.append((va + m.start(), text))
    return out


def string_refs(d, secs, addrs, ranges):
    """For each string address, the functions whose code holds that address as a
    32-bit value (an immediate or displacement). Data words that hold the
    address (pointer tables) have no owning function and are not listed.

    ranges: sorted [(start, end, start)] for every analysed function.
    Returns {addr: [function start, ...]}.
    """
    import bisect
    lows = [r[0] for r in ranges]
    refs = {}
    for addr in addrs:
        pat = struct.pack("<I", addr)
        owners = set()
        for fl, va, rsz, raw in secs:
            blob = d[raw:raw + rsz]
            i = blob.find(pat)
            while i != -1:
                h = va + i
                k = bisect.bisect_right(lows, h) - 1
                if k >= 0 and ranges[k][0] <= h < ranges[k][1]:
                    owners.add(ranges[k][2])
                i = blob.find(pat, i + 1)
        refs[addr] = sorted(owners)
    return refs


def changed_bytes(retail, pack, start, size):
    """Bytes that differ between retail and pack for [start, start+size)."""
    if pack is None:
        return 0
    rs, ps = sections(retail), sections(pack)

    def off(secs, data, va):
        for fl, sva, rsz, raw in secs:
            if fl & 4 and sva <= va < sva + rsz:
                return raw + va - sva
        return None

    a, b = off(rs, retail, start), off(ps, pack, start)
    if a is None or b is None:
        return 0
    n = min(size, len(retail) - a, len(pack) - b)
    return sum(1 for i in range(n) if retail[a + i] != pack[b + i])


def generated_status(gen_dir=None):
    """sub name -> 'generated' | 'stub' for every function the converter emitted."""
    status = {}
    gen_dir = gen_dir or GEN
    for path in glob.glob(os.path.join(gen_dir, "recomp_*.c")):
        name = os.path.basename(path)
        if name == "recomp_stubs_unresolved.c":
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.match(r"^void (sub_[0-9A-Fa-f]{8})\(void\)", line)
                if m:
                    status[m.group(1)] = "generated"
    stub_path = os.path.join(gen_dir, "recomp_stubs_unresolved.c")
    if os.path.exists(stub_path):
        with open(stub_path, encoding="utf-8", errors="replace") as f:
            for m in re.finditer(r"(sub_[0-9A-Fa-f]{8})", f.read()):
                status.setdefault(m.group(1), "stub")
    return status


def manual_overrides():
    """Addresses the hand overrides replace: the case labels of recomp_lookup_manual."""
    if not os.path.exists(MANUAL):
        return set()
    text = open(MANUAL, encoding="utf-8", errors="replace").read()
    i = text.find("recomp_func_t recomp_lookup_manual(")
    if i < 0:
        return set()
    body = text[i:text.find("\n}\n", i)]
    return {int(a, 16) for a in re.findall(r"case 0x([0-9A-Fa-f]{6,8})u", body)}


def build(pack=None):
    """Build the retail database, or with pack="v02"/"v04" that SOFTDRINK build's."""
    if pack:
        an, gen_dir, target_xbe = PACKS[pack]
        out_db = os.path.join(an, "fdb.sqlite")
    else:
        an, out_db, target_xbe, gen_dir = AN, DB, RETAIL, GEN
    funcs = json.load(open(os.path.join(an, "disasm", "functions.json")))
    xrefs = json.load(open(os.path.join(an, "disasm", "xrefs.json")))
    strings = json.load(open(os.path.join(an, "disasm", "strings.json")))
    identified = json.load(open(os.path.join(an, "func_id", "identified_functions.json")))
    try:
        abi = json.load(open(os.path.join(an, "abi", "abi_functions.json")))
    except (OSError, ValueError):
        abi = []
    gen = generated_status(gen_dir)
    manual = manual_overrides()
    retail = open(RETAIL, "rb").read()              # the comparison base
    target = open(target_xbe, "rb").read()
    pack_bytes = target if pack else None           # changed-bytes column: packs only

    ident = {}
    for f in identified:
        if f.get("method", "none") != "none":
            ident[int(f["start"], 16)] = f
    abi_by = {}
    for a in abi if isinstance(abi, list) else []:
        try:
            abi_by[int(str(a.get("start") or a.get("address")), 16)] = a
        except (TypeError, ValueError):
            pass

    # string -> functions that reference it: the ASCII table from the analysis,
    # plus the UTF-16 text the game actually displays (found by scanning).
    str_for = {}
    for s in strings:
        for ref in s.get("referenced_from", []):
            str_for.setdefault(int(ref, 16), []).append(s["string"])
    secs = sections(target)
    ranges = sorted((int(f["start"], 16), int(f["start"], 16) + int(f.get("size") or 0),
                     int(f["start"], 16)) for f in funcs)
    u16 = utf16_strings(target, secs)
    refs = string_refs(target, secs, [va for va, _ in u16], ranges)
    u16_rows = []
    for va, text in u16:
        owners = refs.get(va, [])
        u16_rows.append((va, text, owners))
        for o in owners:
            str_for.setdefault(o, []).append(text)

    if os.path.exists(out_db):
        os.remove(out_db)
    db = sqlite3.connect(out_db)
    db.executescript("""
    CREATE TABLE functions (
        start INTEGER PRIMARY KEY, end INTEGER, size INTEGER, address TEXT,
        name TEXT, section TEXT, detect_method TEXT, detect_conf REAL,
        ident_name TEXT, ident_category TEXT, ident_method TEXT,
        instructions INTEGER, calls_to TEXT, called_by TEXT,
        strings TEXT, purpose TEXT, purpose_src TEXT, confidence TEXT,
        recomp TEXT, manual INTEGER, abi TEXT, pack_changed INTEGER, notes TEXT);
    CREATE TABLE xrefs (src INTEGER, dst INTEGER, kind TEXT);
    CREATE TABLE strings (addr INTEGER PRIMARY KEY, enc TEXT, text TEXT, refs TEXT);
    CREATE INDEX strings_text ON strings(text);
    CREATE INDEX xrefs_src ON xrefs(src);
    CREATE INDEX xrefs_dst ON xrefs(dst);
    CREATE VIRTUAL TABLE fts USING fts5(name, purpose, strings, notes, content='functions', content_rowid='start');
    """)

    rows = []
    for f in funcs:
        start = int(f["start"], 16)
        name = f["name"]
        ident_f = ident.get(start)
        ident_name = ident_f.get("name") if ident_f else None
        # purpose from strings, else from a name the identifier gave
        strs = str_for.get(start, [])
        purpose, src = "", ""
        blob = " ".join(strs).lower()
        for label, pat in BUCKETS:
            m = re.search(pat, blob)
            if m:
                purpose, src = label, "string:%s" % m.group(0)
                break
        if ident_name and not ident_name.startswith("sub_") and not purpose:
            purpose, src = ident_name, "name"
        # confidence: named by identifier or manual = high; detector-confident = medium;
        # string-only purpose = low; nothing = none
        is_manual = start in manual
        if (ident_name and not ident_name.startswith("sub_")) or is_manual:
            conf = "high"
        elif (f.get("confidence") or 0) >= 0.8:
            conf = "medium"
        elif src.startswith("string"):
            conf = "low"
        else:
            conf = "none"
        recomp = gen.get(name, "missing")
        if is_manual:
            recomp = "manual"
        a = abi_by.get(start)
        abi_s = json.dumps({k: a[k] for k in list(a)[:4]}) if a else None
        rows.append((
            start, start + int(f.get("size") or 0), int(f.get("size") or 0), f["start"],
            name, f.get("section"), f.get("detection_method"), f.get("confidence"),
            ident_name, ident_f.get("category") if ident_f else None,
            ident_f.get("method") if ident_f else None,
            f.get("num_instructions"),
            " ".join(f.get("calls_to", [])), " ".join(f.get("called_by", [])),
            " | ".join(strs), purpose, src, conf, recomp, int(is_manual),
            abi_s, changed_bytes(retail, pack_bytes, start, int(f.get("size") or 0)) if pack_bytes is not None else 0,
            None,
        ))
    db.executemany("INSERT INTO functions VALUES (%s)" % ",".join("?" * 23), rows)
    db.executemany("INSERT INTO xrefs VALUES (?,?,?)",
                   ((int(x["from"], 16), int(x["to"], 16), x.get("type")) for x in xrefs))
    db.executemany("INSERT INTO strings VALUES (?,?,?,?)", (
        (va, "utf16", text, " ".join("0x%08X" % o for o in owners)) for va, text, owners in u16_rows))
    for s in strings:
        db.execute("INSERT OR IGNORE INTO strings VALUES (?,?,?,?)", (
            int(s["address"], 16), "ascii", s["string"],
            " ".join(s.get("referenced_from", []))))
    db.execute("INSERT INTO fts(fts) VALUES('rebuild')")
    db.commit()
    counts = dict(db.execute("SELECT recomp, COUNT(*) FROM functions GROUP BY recomp").fetchall())
    print("[fdb] %d functions, %d xrefs -> %s" % (len(rows), len(xrefs), out_db))
    print("[fdb] recomp status:", counts)
    print("[fdb] purpose labelled:", db.execute("SELECT COUNT(*) FROM functions WHERE purpose<>''").fetchone()[0])
    db.close()


def connect():
    if not os.path.exists(DB):
        sys.exit("No database yet: run  python tools/fdb.py build")
    return sqlite3.connect(DB)


def show(addr):
    db = connect()
    v = int(addr, 16)
    cur = db.execute("SELECT * FROM functions WHERE start=?", (v,))
    cols = [c[0] for c in cur.description]
    row = cur.fetchone()
    if not row:
        sys.exit("no function at %s" % addr)
    for k, val in zip(cols, row):
        if val not in (None, "", 0) or k in ("recomp", "confidence"):
            print("%-14s %s" % (k, val))
    print("xrefs in:  %d    xrefs out: %d" % (
        db.execute("SELECT COUNT(*) FROM xrefs WHERE dst=?", (v,)).fetchone()[0],
        db.execute("SELECT COUNT(*) FROM xrefs WHERE src=?", (v,)).fetchone()[0]))


def search(text, limit=40):
    db = connect()
    q = " ".join(re.findall(r"[A-Za-z0-9_]+", text)) or text
    try:
        rows = db.execute(
            "SELECT f.address, f.name, f.purpose, f.confidence, f.recomp, f.size "
            "FROM fts JOIN functions f ON f.start = fts.rowid WHERE fts MATCH ? "
            "ORDER BY rank LIMIT ?", (q, limit)).fetchall()
    except sqlite3.OperationalError:
        rows = db.execute(
            "SELECT address, name, purpose, confidence, recomp, size FROM functions "
            "WHERE name LIKE ? OR purpose LIKE ? OR strings LIKE ? LIMIT ?",
            ("%" + text + "%",) * 3 + (limit,)).fetchall()
    for r in rows:
        print("%-12s %-22s %-12s %-6s %-9s %5s  %s" % (r[0], r[1], r[2][:12], r[3], r[4], r[5], ""))
    print("%d result(s)" % len(rows))


def xrefs(addr):
    db = connect()
    v = int(addr, 16)
    print("callers / references in:")
    for src, kind in db.execute("SELECT src, kind FROM xrefs WHERE dst=? LIMIT 60", (v,)):
        print("  0x%08X  %s" % (src, kind))
    print("references out:")
    for dst, kind in db.execute("SELECT dst, kind FROM xrefs WHERE src=? LIMIT 60", (v,)):
        print("  0x%08X  %s" % (dst, kind))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "build":
        arg = sys.argv[sys.argv.index("--pack") + 1] if "--pack" in sys.argv else None
        build(pack=arg)
    elif cmd == "search":
        search(" ".join(sys.argv[2:]))
    elif cmd == "show":
        show(sys.argv[2])
    elif cmd == "xrefs":
        xrefs(sys.argv[2])
    else:
        sys.exit(__doc__)
