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

# Targets observed through RECOMP_ICALL_FEEDBACK while booting SoftDrink. They
# are short retail helper entries that sit between normally detected functions,
# so the immediate scan cannot discover them until they have already run.
# Keeping them here makes a clean pack analysis reproduce the working build.
RUNTIME_SEEDS = {
    # sub_000B1470 dispatches through a 25-entry switch table at 0x000B1574.
    # Most case labels are tiny shared tails inside the containing routine and
    # are therefore not discovered as independent functions by the analyzer.
    # Seed the complete table together so a new menu/game state cannot expose
    # one missing case at a time.
    0x000B149E: "SoftDrink menu switch case entry.",
    0x000B14A8: "SoftDrink menu switch case entry.",
    0x000B14B2: "SoftDrink menu switch case entry.",
    0x000B14BC: "SoftDrink menu switch case entry.",
    0x000B14C4: "SoftDrink menu switch case entry.",
    0x000B14CE: "SoftDrink menu switch case entry.",
    0x000B14D6: "SoftDrink menu switch case entry.",
    0x000B14DE: "SoftDrink menu switch case entry.",
    0x000B14E6: "SoftDrink menu switch case entry.",
    0x000B14EE: "SoftDrink menu switch case entry.",
    0x000B1470: "Observed shared loop entry during native SoftDrink boot.",
    0x000B14F6: "Observed SoftDrink menu callback during native MyCareer boot.",
    0x000B14FE: "SoftDrink menu switch case that exited the Play Now repro.",
    0x000B1505: "SoftDrink menu switch case entry.",
    0x000B150D: "SoftDrink menu switch case entry.",
    0x000B1515: "SoftDrink menu switch case entry.",
    0x000B151C: "SoftDrink menu switch case entry.",
    0x000B1520: "SoftDrink menu switch case entry.",
    0x000B1528: "SoftDrink menu switch case entry.",
    0x000B152C: "SoftDrink menu switch case entry.",
    0x000B1530: "SoftDrink menu switch case entry.",
    0x000B1534: "SoftDrink menu switch case entry.",
    0x000B154E: "SoftDrink menu switch shared tail.",
    0x000B4A60: "Observed patched resource helper thunk during native boot.",
    0x000FC075: "Observed SoftDrink indirect entry inside the team/game setup helper.",
    0x0014E079: "Observed SoftDrink push-ret target during native boot.",
    0x00156877: "Observed patched menu/resource callback during native boot.",
    0x002B9950: "Observed MyCareer menu callback during native boot.",
    0x014BADE0: "Observed SoftDrink resource-load hook before ESPN 25th Anniversary content.",
    # Direct detours whose entries follow another helper's return. They are
    # valid code in v0.4 but were omitted from the original boundary scan,
    # leaving real tail jumps linked to empty unresolved stubs.
    0x014DD695: "SoftDrink expanded stat-count return helper.",
    0x014DD69B: "SoftDrink expanded stat-table detour to 0x003636AA.",
    0x014DD6E6: "SoftDrink animation helper epilogue; restores edi/esi before 0x0024FFD0. Empty stub caused the 2026-10-07 crash.",
    0x014DD78D: "SoftDrink expanded player-table lookup detour.",
    0x014DEE44: "SoftDrink injected state-reset helper.",
    0x014E4E21: "SoftDrink conditional injected helper entry.",
    0x014E82D6: "SoftDrink MyCareer state transition detour to the retail FSM helpers.",
    0x014E837B: "SoftDrink MyCareer state lookup detour to 0x0006E2E0.",
    0x014E9850: "SoftDrink game setup hook preserving CPU/FPU state.",
}


def looks_utf16(b):
    """Embedded UTF-16 text decoded as code (SOFTDRINK 0x014D9BE0 swallowed the
    real function at 0x014D9C10, 2026-10-08)."""
    b = b[:16]
    return len(b) >= 12 and all(b[i] == 0 for i in range(1, 12, 2)) and         all(0x20 <= b[i] < 0x7F for i in range(0, 12, 2))


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
    hs = struct.unpack_from("<I", d, 0x108)[0]
    if 0xCC4 < hs <= 0x1000:        # code in the grown header (xbe_parser: .HDRCODE)
        out.append((".HDRCODE", 6, base + 0xA00, hs - 0xA00, 0xA00))
    return out


def main():
    xbe, an = sys.argv[1], sys.argv[2]
    d = open(xbe, "rb").read()
    retail = open(os.path.join(ROOT, "original", "disc", "default.xbe"), "rb").read()
    secs = sections(d)
    rsecs = {s[0] for s in sections(retail)}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    funcs = json.load(open(os.path.join(an, "disasm", "functions.json")))

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
        in_retail = f.get("section") in rsecs
        s = int(f["start"], 16)
        n = int(f.get("size") or 0)
        sec = exec_sec(s)
        if not sec or n <= 0:
            continue
        name, sva, raw, rsz = sec
        o = raw + s - sva
        if looks_utf16(d[o:o + n]) or looks_utf16(d[o + 2:o + n]):
            continue
        insns = list(md.disasm(d[o:o + n], s))
        for k, ins in enumerate(insns):
            # A detour that overwrote a function's first bytes with `jmp cave`
            # comes back with `push X; ret` or `jmp X` past those bytes. The
            # analysis then sees a 5-byte function there, so X is no known
            # instruction boundary: seed it anyway when it decodes as code
            # (SOFTDRINK v0.4 0x14E4DCD -> 0x156648, Play Now 2026-10-07).
            landing = None
            if ins.mnemonic == "push" and k + 1 < len(insns) and insns[k + 1].mnemonic == "ret" \
                    and not insns[k + 1].op_str and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                landing = ins.operands[0].imm & 0xFFFFFFFF
            elif ins.mnemonic.startswith("j") and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                landing = ins.operands[0].imm & 0xFFFFFFFF
            if in_retail:
                # Retail code the mod patched with jmp/call into its own
                # sections (0x00062C96 -> 0x014D9C10): only those targets.
                if ins.mnemonic in ("jmp", "call") and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                    t = ins.operands[0].imm & 0xFFFFFFFF
                    es = exec_sec(t)
                    if es and es[0] not in rsecs:
                        found.setdefault(t, "0x%08X %s (retail patch)" % (ins.address, ins.mnemonic))
                continue
            # "push <continuation>; jmp F": F returns into the continuation, which
            # the converter then tail-calls (SOFTDRINK 0x014E36DA -> 0x014E3719).
            if ins.mnemonic == "push" and k + 1 < len(insns) and insns[k + 1].mnemonic == "jmp"                     and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                cont = ins.operands[0].imm & 0xFFFFFFFF
                if exec_sec(cont) and abs(cont - ins.address) < 0x2000:
                    found.setdefault(cont, "0x%08X push continuation" % ins.address)
            if landing is not None and landing not in boundaries and exec_sec(landing):
                lname, lsva, lraw, lrsz = exec_sec(landing)
                lo = lraw + landing - lsva
                if next(md.disasm(d[lo:lo + 16], landing), None) is not None:
                    found.setdefault(landing, "0x%08X %s (detour landing)" % (ins.address, ins.mnemonic))
            for op in ins.operands:
                if op.type != capstone.x86.X86_OP_IMM:
                    continue
                t = op.imm & 0xFFFFFFFF
                # Keep an already-detected target too.  The current analysis
                # may only know that start because an earlier generated seed
                # exposed it.  Dropping it here makes the next clean analysis
                # forget the address and is therefore not reproducible.
                if t not in boundaries or not exec_sec(t):
                    continue
                found.setdefault(t, "0x%08X %s" % (ins.address, ins.mnemonic))
    seeds = json.load(open(os.path.join(ROOT, "analysis", "seed_functions.json")))
    have = {int(x["start"], 16) for x in seeds}
    added = 0
    # Retail function starts the mod hooked. A start the mod overwrote
    # with `jmp cave` is still entered through retail pointer tables, but the
    # pack's own analysis no longer finds it (0x0023CE70, called through a
    # vtable at [edi+0x310], 2026-10-07).
    rpath = os.path.join(ROOT, "analysis", "disasm", "functions.json")
    if os.path.exists(rpath):
        rsec = sections(retail)
        def rread(va, n):
            for name, fl, sva, rsz, raw in rsec:
                if sva <= va < sva + rsz:
                    return retail[raw + va - sva:raw + va - sva + n]
            return b""
        for f in json.load(open(rpath)):
            t = int(f["start"], 16)
            sec = exec_sec(t)
            if t in have or not sec:
                continue
            name, sva, raw, rsz = sec
            mine = d[raw + t - sva:raw + t - sva + 8]
            # Only starts the mod overwrote with a jmp: seeding every retail
            # start re-split the pack's functions and left code such as
            # 0x000EFA23 in no function at all (boot crash, 2026-10-07).
            if mine[:1] == b"\xE9" and rread(t, 1) != b"\xE9":
                seeds.append({"start": "0x%08X" % t, "note": "Retail function start kept by the pack."})
                have.add(t)
                added += 1
    def target_is_text(t):
        sec = exec_sec(t)
        if not sec:
            return False
        name, sva, raw, rsz = sec
        o = raw + t - sva
        return looks_utf16(d[o:o + 16]) or looks_utf16(d[o + 2:o + 18])

    for t, where in sorted(found.items()):
        if t in have or target_is_text(t):
            continue
        seeds.append({"start": "0x%08X" % t, "note": "Mod pack code references it (%s)." % where})
        have.add(t)
        added += 1
    # Handler pointers that only exist inside compressed tables: SOFTDRINK
    # unpacks its screen descriptors at runtime (sub_014E5185: a byte count
    # <0x80 = that many literals, >=0x80 = copy (n&0x7F)+3 bytes from a 16-bit
    # back offset), and MyCareer's handler 0x014E8311 lives only there
    # (black screen, 2026-10-08). Unpack every "mov eax, src; call <unpacker>"
    # source and seed the code addresses found in it.
    sig = bytes.fromhex("5789d7560fb60889ca85c9")
    def va_of(raw_off):
        for name, fl, sva, rsz, raw in secs:
            if raw <= raw_off < raw + rsz:
                return sva + raw_off - raw
        return None
    def raw_of(va):
        for name, fl, sva, rsz, raw in secs:
            if sva <= va < sva + rsz:
                return raw + va - sva
        return None
    k = d.find(sig)
    while k >= 0:
        unpack = va_of(k)
        k = d.find(sig, k + 1)
        if unpack is None:
            continue
        for name, fl, sva, rsz, raw in secs:
            if not fl & 4:
                continue
            body = d[raw:raw + rsz]
            for pos in range(5, len(body) - 5):
                if body[pos] != 0xE8 or body[pos - 5] != 0xB8:
                    continue
                if (sva + pos + 5 + struct.unpack_from("<i", body, pos + 1)[0]) & 0xFFFFFFFF != unpack:
                    continue
                prev = body[pos - 5:pos]
                src = struct.unpack_from("<I", prev, 1)[0]
                o = raw_of(src)
                if o is None:
                    continue
                out = bytearray()
                while o < len(d) and len(out) < 0x10000:
                    n = d[o]
                    if not n:
                        break
                    if n & 0x80:
                        cnt = (n & 0x7F) + 3
                        back_off = struct.unpack_from("<H", d, o + 1)[0]
                        o += 3
                        for _ in range(cnt):
                            out.append(out[len(out) - back_off] if 0 < back_off <= len(out) else 0)
                    else:
                        out += d[o + 1:o + 1 + n]
                        o += 1 + n
                for j in range(0, len(out) - 3):
                    t = struct.unpack_from("<I", out, j)[0]
                    es = exec_sec(t)
                    if es and es[0] not in rsecs and t not in have and not target_is_text(t):
                        seeds.append({"start": "0x%08X" % t, "note": "Mod pack code pointer in unpacked table 0x%08X." % src}); have.add(t); added += 1

    for t, note in sorted(RUNTIME_SEEDS.items()):
        if t in have or not exec_sec(t):
            continue
        seeds.append({"start": "0x%08X" % t, "note": note})
        have.add(t)
        added += 1
    out = os.path.join(an, "seed_functions.json")
    json.dump(seeds, open(out, "w"), indent=2)
    print("[pack-seeds] %d new seeds from mod code (%d retail) -> %s" % (added, len(have), out))
    for t, where in sorted(found.items())[:40]:
        print("   0x%08X  from %s" % (t, where))


if __name__ == "__main__":
    main()
