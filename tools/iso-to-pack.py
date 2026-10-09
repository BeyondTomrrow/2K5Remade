"""Turn a finished, patched Xbox disc image into a mod pack overlay.

    python tools/iso-to-pack.py <patched.iso> <pack folder> --name "SOFTDRINK 2K28" --version v0.4

Compares every file of the image with the retail file in original\\disc and
writes, in <pack folder>:
  overlay.bin   the index the game reads (external/xboxrecomp kernel_overlay.c)
  lit/<n>.bin   the changed bytes of each changed file
  default.xbe   the image's executable (the code build is made from it)
  pack.json     name, version, executable hash

This is the same pack shape tools/pack-install.py makes from a .2k5patch,
but the image is what the patch tool actually produced, so the executable and
the data are one consistent build. Nothing on the retail disc is touched.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from xiso_read import XIso  # noqa: E402

ROOT = os.path.dirname(HERE)
DISC = os.path.join(ROOT, "original", "disc")
MAGIC = b"2K5OVL1\0"
BLOCK = 4096
CHUNK = 1 << 22


def retail_path(rel):
    return os.path.join(DISC, rel.replace("/", os.sep))


def diff_extents(iso, rel, new_size, base_size, fo):
    """Runs of the image file that differ from the retail file (or lie past its
    end, where the overlay otherwise reads zeros). Returns [(offset, length)]."""
    runs = []
    cur = None
    base = open(retail_path(rel), "rb") if base_size else None
    pos = 0
    while pos < new_size:
        n = min(CHUNK, new_size - pos)
        a = iso.read(rel, pos, n)
        b = base.read(n) if base and pos < base_size else b""
        if len(b) < n:
            b = b + bytes(n - len(b))        # past the retail end: zeros
        if a != b:
            for o in range(0, n, BLOCK):
                if a[o:o + BLOCK] != b[o:o + BLOCK]:
                    if cur and cur[0] + cur[1] == pos + o:
                        cur[1] += min(BLOCK, n - o)
                    else:
                        if cur:
                            runs.append(tuple(cur))
                        cur = [pos + o, min(BLOCK, n - o)]
        pos += n
    if cur:
        runs.append(tuple(cur))
    if base:
        base.close()
    return runs


def convert(iso_path, pack, name="Patched disc", version="", author="", disc=None, log=print):
    """Write the overlay pack for one patched image; returns the XBE SHA-256."""
    global DISC
    if disc:
        DISC = disc
    iso = XIso(iso_path)
    os.makedirs(os.path.join(pack, "lit"), exist_ok=True)
    files = []          # (rel, base_size, new_size)
    for rel, (start, size) in sorted(iso.files.items()):
        p = retail_path(rel)
        if not os.path.exists(p):
            log("[iso] skipping %s (not on the retail disc folder)" % rel)
            continue
        files.append((rel, os.path.getsize(p), size))
    entries = []
    for i, (rel, base_size, size) in enumerate(files):
        runs = diff_extents(iso, rel, size, base_size, None)
        if size == base_size and not runs:
            continue
        lit_path = os.path.join(pack, "lit", "%d.bin" % i)
        total = 0
        ext = []
        with open(lit_path, "wb") as lit:
            for off, ln in runs:
                lit.write(iso.read(rel, off, ln))
                ext.append((off, ln, 0, 0xFFFF, total))
                total += ln
        entries.append((i, ext))
        log("[iso] %-22s %11d -> %11d bytes, %5d runs, %7.1f MB literal" % (rel, base_size, size, len(ext), total / 1e6))
    with open(os.path.join(pack, "overlay.bin"), "wb") as o:
        o.write(MAGIC)
        o.write(struct.pack("<II", len(files), len(entries)))
        for rel, base_size, size in files:
            name_b = rel.encode()
            o.write(struct.pack("<H", len(name_b)) + name_b)
            o.write(struct.pack("<QQ", base_size, size))
        for i, ext in entries:
            o.write(struct.pack("<II", i, len(ext)))
            for dst, ln, kind, srcf, src in ext:
                o.write(struct.pack("<QIBHQ", dst, ln, kind, srcf, src))
    xbe = iso.read("default.xbe", 0, iso.files["default.xbe"][1])
    with open(os.path.join(pack, "default.xbe"), "wb") as f:
        f.write(xbe)
    sha = hashlib.sha256(xbe).hexdigest()
    with open(os.path.join(pack, "pack.json"), "w") as f:
        json.dump({"name": name, "display_name": (name + " " + version).strip(), "version": version,
                   "author": author,
                   "description": "Finished disc image; overlay made by tools/iso-to-pack.py",
                   "source": os.path.basename(iso_path), "xbe_sha256": sha}, f, indent=1)
    log("[iso] pack written to %s (default.xbe sha256 %s)" % (pack, sha))
    return sha


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("pack")
    ap.add_argument("--name", default="Patched disc")
    ap.add_argument("--version", default="")
    ap.add_argument("--author", default="")
    args = ap.parse_args()
    convert(args.iso, args.pack, args.name, args.version, args.author)


if __name__ == "__main__":
    main()
