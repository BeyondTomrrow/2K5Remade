"""Check a mod pack's overlay against the finished disc image it came from.

    python tools/verify-overlay.py <pack dir> <retail disc dir> <patched.iso> [--bytes]

Prints each disc file's retail size, the size the overlay serves and the
size on the image; with --bytes it also reads every changed file through the
overlay (the game's view) and compares all of it with the image.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "custom-animations", "tool"))
from ovlread import Overlay  # noqa: E402
from xiso_read import XIso   # noqa: E402


def main():
    pack, disc, iso_path = sys.argv[1:4]
    deep = "--bytes" in sys.argv
    iso = XIso(iso_path)
    isomap = {k.lower(): v[1] for k, v in iso.files.items()}
    ov = Overlay(pack, disc)
    bad = 0
    for f in ov.files:
        rel = f["rel"].replace("\\", "/")
        isz = isomap.get(rel.lower())
        changed = f["ext"] is not None
        flag = "" if isz == f["size"] else "   <-- SIZE MISMATCH"
        if flag:
            bad += 1
        print("%-22s retail %11d  overlay %11d  image %s  changed=%s%s"
              % (rel, f["base"], f["size"], isz, changed, flag))
        if deep and changed and isz:
            pos = 0
            diffs = 0
            first = None
            step = 1 << 22
            while pos < isz:
                n = min(step, isz - pos)
                a = ov.read(rel, pos, n)
                b = iso.read(rel.replace("\\", "/") if rel in iso.files else next(k for k in iso.files if k.lower() == rel.lower()), pos, n)
                if a != b:
                    diffs += 1
                    if first is None:
                        first = pos + next(i for i in range(min(len(a), len(b))) if a[i] != b[i]) if min(len(a), len(b)) and a[:min(len(a), len(b))] != b[:min(len(a), len(b))] else pos + min(len(a), len(b))
                pos += n
            print("     bytes: %s%s" % ("identical" if not diffs else "%d differing 4MB blocks, first near %#x" % (diffs, first or 0), ""))
    print("size mismatches:", bad)


if __name__ == "__main__":
    main()
