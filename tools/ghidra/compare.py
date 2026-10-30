"""Compare Ghidra's functions with our analysis (analysis/fdb.sqlite).

    python tools/ghidra/compare.py <ghidra-functions.csv> [fdb.sqlite]

Reports, by function start address:
  both      found by Ghidra and by our analysis
  ghidra    only Ghidra found it (candidates for missed code)
  ours      only our analysis found it (check: Ghidra may have merged or dropped it)
and, for the functions in both, the size difference (large differences mark
disagreements about where a function ends).

Output: tools/ghidra/compare-report.txt and a summary on stdout.
"""
import csv
import os
import sqlite3
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def load_ghidra(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            out[int(row["address"], 16)] = row
    return out


def load_ours(path):
    db = sqlite3.connect(path)
    out = {}
    for start, size, name, recomp in db.execute("SELECT start, size, name, recomp FROM functions"):
        out[start] = {"size": size, "name": name, "recomp": recomp}
    return out


def main():
    gcsv = sys.argv[1]
    dbpath = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "analysis", "fdb.sqlite")
    g = load_ghidra(gcsv)
    o = load_ours(dbpath)
    both = sorted(set(g) & set(o))
    only_g = sorted(set(g) - set(o))
    only_o = sorted(set(o) - set(g))
    big = []
    for a in both:
        gs = int(g[a]["size"])
        os_ = o[a]["size"]
        if abs(gs - os_) > max(16, os_ // 10):
            big.append((a, gs, os_))
    rep = os.path.join(ROOT, "tools", "ghidra", "compare-report.txt")
    with open(rep, "w", encoding="utf-8") as f:
        f.write("both %d | ghidra only %d | ours only %d | size disagreements %d\n" % (
            len(both), len(only_g), len(only_o), len(big)))
        f.write("\n# ghidra only (first 500)\n")
        for a in only_g[:500]:
            f.write("0x%08X %s size %s\n" % (a, g[a]["name"], g[a]["size"]))
        f.write("\n# ours only (first 500)\n")
        for a in only_o[:500]:
            f.write("0x%08X %s size %s recomp=%s\n" % (a, o[a]["name"], o[a]["size"], o[a]["recomp"]))
        f.write("\n# size disagreements (first 500): address ghidra_size ours_size\n")
        for a, gs, os_ in big[:500]:
            f.write("0x%08X %d %d\n" % (a, gs, os_))
    print("both %d | ghidra only %d | ours only %d | size disagreements %d" % (
        len(both), len(only_g), len(only_o), len(big)))
    print("report:", rep)


if __name__ == "__main__":
    main()
