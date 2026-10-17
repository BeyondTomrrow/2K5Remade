"""List a front-end menu's rows from the retail XBE (read-only).

    python build/eras/match-launch/menu_rows.py 0x515660 0x585474 0x501E48 0x526C10

A menu header is 0x34 bytes (title +0, rows +0x10); rows are 0x34 bytes ending
in kind 3. Kind 0 links to the header at +8; kind 9 runs the action at +0x28
(ECX = screen stack 0xA84B18, EDX = its state entry); kinds 5 (Off/On) and 7
(list) are value rows with seven functions at +0x0C: max, min, get, next,
previous, value text, widest text (src/nfl2k5_video_menu.c). For value rows
the setting's RAM address is read from the getter's generated code
(build/gen-locals/*.c). Writes nothing.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from xbe_peek import Xbe  # noqa: E402

x = Xbe()
_bodies = None


def wstr(va, n=48):
    s = ""
    for i in range(n):
        try:
            c = x.u32(va + 2 * i) & 0xFFFF
        except ValueError:
            return "?"
        if c == 0:
            break
        s += chr(c) if c < 128 else "?"
    return s


def bodies():
    global _bodies
    if _bodies is None:
        _bodies = {}
        for f in glob.glob(os.path.join(ROOT, "build", "gen-locals", "recomp_*.c")):
            txt = open(f, encoding="utf-8", errors="replace").read()
            for m in re.finditer(r"^void sub_([0-9A-F]{8})\(void\)\n\{(.*?)\n\}", txt, re.S | re.M):
                _bodies[int(m.group(1), 16)] = m.group(2)
    return _bodies


def getter_addr(fn):
    b = bodies().get(fn)
    m = re.search(r"eax = MEM32\(0x([0-9A-F]+)\);", b or "")
    return "0x" + m.group(1) if m else "?"


def const(fn):
    b = bodies().get(fn) or ""
    m = re.search(r"loc_[0-9A-F]+: ;\n\s+eax = (0x[0-9A-F]+|\d+);", b)
    if m:
        return m.group(1)
    return "0" if re.search(r"loc_[0-9A-F]+: ;\n\s+eax = 0; /\* xor self", b) else "?"


def rows(header):
    print("== header %08X %r rows %08X" % (header, wstr(x.u32(header)), x.u32(header + 0x10)))
    base = x.u32(header + 0x10)
    for i in range(32):
        r = base + 0x34 * i
        kind = x.u32(r)
        if kind == 3:
            break
        label = wstr(x.u32(r + 4))[:28]
        f = [x.u32(r + 0x0C + 4 * j) for j in range(7)]
        if kind in (5, 7):
            print("  %-28s kind %d max %s min %s get %08X -> [%s] next %08X prev %08X" %
                  (label, kind, const(f[0]), const(f[1]), f[2], getter_addr(f[2]), f[3], f[4]))
        else:
            print("  %-28s kind %d target %08X action %08X per-frame %08X" %
                  (label, kind, x.u32(r + 8), x.u32(r + 0x28), x.u32(r + 0x2C)))


if __name__ == "__main__":
    for a in sys.argv[1:]:
        rows(int(a, 0))
