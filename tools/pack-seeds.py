"""Seed functions for a mod pack's patched XBE.

    python tools/pack-seeds.py <pack default.xbe> <analysis-dir>

Mod code (sections the retail XBE does not have, and code that differs from
retail) enters the original code at places the retail game never did: a
detour cave returns into the middle of a function (push-ret or jmp), and
patched pointer tables point mid-function. The converter only creates entry
points for known function starts, so those landings dispatched to nothing
("[ICALL] Unresolved guest target 0014E079", SOFTDRINK 2K28, 2026-10-03).

Every 32-bit immediate in the mod's code that is an instruction boundary in
an executable section becomes a seed, on top of the retail seeds. Writes
<analysis-dir>/seed_functions.json for tools/analyze.ps1.
"""
import json
import os
import struct
import sys

import capstone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sections(d):
    base = struct.unpack_from("<I", d, 0x104)[0]
    nsec, sa = struct.unpack_from("<II", d, 0x11C)
    so = sa - base
    out = []
    for i in range(nsec):
        fl, va, vsz, raw, rsz, na = struct.unpack_from("<IIIIII", d, so + i * 56)
        no = na - base
        name = d[no:d.index(b"\0", no)].decode(errors="replace")
        out.append((name, fl, va, rsz, raw))
    return out


def main():
    xbe, an = sys.argv[1], sys.argv[2]
    d = open(xbe, "rb").read()
    retail = open(os.path.join(ROOT, "original", "disc", "default.xbe"), "rb").read()
    secs = sections(d)
    rsecs = {s[0] for s in sections(retail)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    funcs = json.load(open(os.path.join(an, "disasm", "functions.json")))
    starts = {int(f["start"], 16) for f in funcs}

    def exec_sec(va):
        for name, fl, sva, rsz, raw in secs:
            if fl & 4 and sva <= va < sva + rsz:
                return name, sva, raw, rsz
        return None

    # Instruction boundaries of every analysed function (to validate targets).
    boundaries = set()
    for f in funcs:
        s = int(f["start"], 16)
        n = int(f.get("size") or 0)
        sec = exec_sec(s)
        if not sec or n <= 0 or n > 0x40000:
            continue
        name, sva, raw, rsz = sec
        o = raw + s - sva
        for ins in md.disasm(d[o:o + n], s):
            boundaries.add(ins.address)

    # Mod code: the functions in sections retail does not have.
    found = {}
    for f in funcs:
        if f.get("section") in rsecs:
            continue
        s = int(f["start"], 16)
        n = int(f.get("size") or 0)
        sec = exec_sec(s)
        if not sec or n <= 0:
            continue
        name, sva, raw, rsz = sec
        o = raw + s - sva
        for ins in md.disasm(d[o:o + n], s):
            for op in ins.operands:
                if op.type != capstone.x86.X86_OP_IMM:
                    continue
                t = op.imm & 0xFFFFFFFF
                if t in starts or t not in boundaries or not exec_sec(t):
                    continue
                found.setdefault(t, "0x%08X %s" % (ins.address, ins.mnemonic))
    seeds = json.load(open(os.path.join(ROOT, "analysis", "seed_functions.json")))
    have = {int(x["start"], 16) for x in seeds}
    added = 0
    for t, where in sorted(found.items()):
        if t in have:
            continue
        seeds.append({"start": "0x%08X" % t, "note": "Mod pack code references it (%s)." % where})
        added += 1
    out = os.path.join(an, "seed_functions.json")
    json.dump(seeds, open(out, "w"), indent=2)
    print("[pack-seeds] %d new seeds from mod code (%d retail) -> %s" % (added, len(have), out))
    for t, where in sorted(found.items())[:40]:
        print("   0x%08X  from %s" % (t, where))


if __name__ == "__main__":
    main()
