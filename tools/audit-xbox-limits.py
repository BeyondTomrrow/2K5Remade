"""Check the Xbox-era capacity limits this project has catalogued against the
actual executable, and report each one with its evidence.

    python tools/audit-xbox-limits.py [path\\to\\default.xbe]

Default: original/disc/default.xbe (the retail disc; the built-in widescreen
patch does not touch any of these bytes). Every entry is a byte-exact check
of the instruction or table that imposes the limit, so the audit says
VERIFIED only when the bytes in this executable are the ones described.
Mismatches print MISMATCH with what was found instead. Used by
docs/XBOX-LIMITATION-AUDIT.md.
"""
import struct, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(path):
    x = open(path, "rb").read()
    base = struct.unpack_from("<I", x, 0x104)[0]
    n = struct.unpack_from("<I", x, 0x11C)[0]
    t = struct.unpack_from("<I", x, 0x120)[0] - base
    secs = []
    for i in range(n):
        s = t + i * 56
        sva, vs, raw, rs = struct.unpack_from("<IIII", x, s + 4)
        secs.append((sva, vs, raw, rs))
    def rd(va, k):
        for sva, vs, raw, rs in secs:
            if sva <= va < sva + rs:
                off = raw + va - sva
                return x[off:off + k]
        return None
    return x, rd


# (name, VA, expected retail bytes (hex), meaning)
CHECKS = [
    # Franchise length. mod_editor/core/nfl2k5_season_cap.py (2K Football Mod Tools):
    # cmp eax, 1Eh ; jle -- the season index gate that ends a franchise after 30 seasons.
    ("franchise season gate", 0x2480CB, "83f81e", "cmp eax,30: franchise ends after season index 30"),
    # Create-a-Team stadium picker. mod_editor/core/nfl2k5_roster_storage.py: a 67-entry ID
    # list at 0x531B70 read by three instructions and bounded by five compares/loads.
    ("stadium picker list reader 1", 0x3191C2, "8a90701b5300", "mov dl,[eax+531B70h]: 67-entry stadium ID list"),
    ("stadium picker list reader 2", 0x3192C9, "3a90701b5300", "cmp dl,[eax+531B70h]"),
    ("stadium picker list reader 3", 0x319340, "0fb688701b5300", "movzx ecx,byte [eax+531B70h]"),
    ("stadium picker search bound", 0x31926D, "b943000000", "mov ecx,67"),
    ("stadium picker preview modulo", 0x319289, "83c042", "add eax,66"),
    ("stadium picker search bound 2", 0x31928D, "b943000000", "mov ecx,67"),
    ("stadium picker wrap bound", 0x3192D7, "83f843", "cmp eax,67"),
    ("stadium picker wrap bound 2", 0x31934C, "83f843", "cmp eax,67"),
    ("stadium picker controller init", 0x31A9CE, "c705f0f7c80042000000", "mov [C8F7F0h],66 (last index)"),
    ("stadium picker controller wrap", 0x31A9FD, "83f843", "cmp eax,67"),
    # Created teams. ASTRA_STORAGE_GROWTH_REPORT.md: the created-team predicate only
    # recognises team IDs 90 and 91 -- the two Team Create slots seen in game.
    ("created-team predicate", 0x319370, "668b8018010000663d5a00740c663d5b00",
     "mov ax,[eax+118h]; cmp ax,90; je; cmp ax,91: two created-team IDs"),
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "original", "disc", "default.xbe")
    x, rd = load(path)
    ok = bad = 0
    print("Executable: %s (%d bytes)" % (path, len(x)))
    for name, va, hexb, meaning in CHECKS:
        want = bytes.fromhex(hexb)
        got = rd(va, len(want))
        if got == want:
            ok += 1
            print("VERIFIED  %-34s VA %08X  %s  -- %s" % (name, va, hexb, meaning))
        else:
            bad += 1
            print("MISMATCH  %-34s VA %08X  want %s got %s" % (name, va, hexb, got.hex() if got else "(unmapped)"))
    lst = rd(0x531B70, 82)
    if lst:
        ids = list(lst[:67])
        print("stadium picker list @531B70 (first 67 IDs): %s" % ",".join(str(i) for i in ids))
        print("  distinct IDs %d, max ID %d; bytes 67..81 after the list: %s" % (len(set(ids)), max(ids), lst[67:82].hex()))
    print("%d verified, %d mismatched" % (ok, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
