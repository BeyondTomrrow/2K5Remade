"""Install a 2K5 Mod Studio .2k5patch (format 3, "source-copy-v1") as an overlay.

    python tools/pack-install.py mods/packs/SOFTDRINK-2K28-v0.2.2k5patch [--verify]

The pack describes each changed disc file as runs against the retail file:
literal runs (operations/NNNN.bin: u64 offset, u32 length, 32-byte sha256
before, 32-byte sha256 after, then the bytes) and copy records
(operations/NNNN-copies-K.bin: 84 bytes each -- u64 destination, u64 source,
u32 length, two sha256) that copy from the same retail file, or from another
one ("cross_copies", source_path). Everything else is the retail file at the
same offset, truncated or zero-extended to the new size.

Output in mods/packs/<pack name>/ (nothing on the retail disc is touched):
  overlay.bin        the index the game reads (src/nfl2k5_packs.c)
  lit/<n>.bin        each file's literal bytes, concatenated
  default.xbe        the pack's patched executable (for the code build)
  pack.json          name, version, recipe, file list

--verify rebuilds every changed file through the overlay and checks the
pack's sha256 for it (reads the retail files once; a few minutes).
"""
import hashlib
import json
import os
import re
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISC = os.path.join(ROOT, "original", "disc")
MAGIC = b"2K5OVL1\0"
LIT, COPY = 0, 1


def slug(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("_") or "pack"


def disc_path(rel):
    return os.path.join(DISC, rel.replace("/", os.sep))


def sha_file(path, size=None):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        left = size
        while True:
            chunk = f.read(1 << 24 if left is None else min(1 << 24, left))
            if not chunk:
                break
            h.update(chunk)
            if left is not None:
                left -= len(chunk)
                if not left:
                    break
    return h.hexdigest()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    verify = "--verify" in sys.argv
    if not args:
        sys.exit(__doc__)
    z = zipfile.ZipFile(args[0])
    m = json.loads(z.read("manifest.json"))
    if m.get("format") != 3 or m.get("file_contract") != "source-copy-v1":
        sys.exit("unsupported pack format %r / %r" % (m.get("format"), m.get("file_contract")))
    out = os.path.join(ROOT, "mods", "packs", slug(m["name"] + "-" + str(m.get("version", ""))))
    os.makedirs(os.path.join(out, "lit"), exist_ok=True)
    files = m["files"]
    index = {f["path"]: i for i, f in enumerate(files)}

    # The retail files must be the ones the pack was made against.
    for f in files:
        p = disc_path(f["path"])
        if not os.path.exists(p) or os.path.getsize(p) != f["before"]["size"]:
            sys.exit("retail file %s is missing or a different size" % f["path"])
    print("[pack] %s %s by %s: %d files" % (m["name"], m.get("version", ""), m.get("author", ""), len(files)))

    entries = []
    for i, f in enumerate(files):
        before, after = f["before"], f["after"]
        if f["mode"] == "copy":
            if before["sha256"] != after["sha256"]:
                sys.exit("%s: a changed file stored whole is not supported yet" % f["path"])
            continue                                      # unchanged: the retail file serves it
        extents = []
        lit_path = os.path.join(out, "lit", "%d.bin" % i)
        lit_off = 0
        with open(lit_path, "wb") as lit:
            p = z.read(f["payload"]["member"])
            k = 0
            while k < len(p):
                off, ln = struct.unpack_from("<QI", p, k)
                k += 76
                lit.write(p[k:k + ln])
                extents.append((off, ln, LIT, 0xFFFF, lit_off))
                lit_off += ln
                k += ln
        del p
        copy_members = [(f["copies"]["member"], i)] if "copies" in f else []
        for cc in f.get("cross_copies", []):
            copy_members.append((cc["member"], index[cc["source_path"]]))
        for member, src_file in copy_members:
            c = z.read(member)
            for k in range(0, len(c), 84):
                dst, src, ln = struct.unpack_from("<QQI", c, k)
                extents.append((dst, ln, COPY, src_file, src))
        extents.sort()
        for a, b in zip(extents, extents[1:]):
            if a[0] + a[1] > b[0]:
                sys.exit("%s: overlapping runs at %X" % (f["path"], b[0]))
        entries.append((i, f, extents))
        print("[pack]   %-16s %8.1f MB -> %8.1f MB, %6d extents, %7.1f MB literal"
              % (f["path"], before["size"] / 1e6, after["size"] / 1e6, len(extents), lit_off / 1e6))

    # overlay.bin: header, the file table (all files, so copy sources resolve
    # by index), then each changed file's extents.
    with open(os.path.join(out, "overlay.bin"), "wb") as o:
        o.write(MAGIC)
        o.write(struct.pack("<II", len(files), len(entries)))
        for f in files:
            name = f["path"].encode()
            o.write(struct.pack("<H", len(name)) + name)
            o.write(struct.pack("<QQ", f["before"]["size"], f["after"]["size"]))
        for i, f, ext in entries:
            o.write(struct.pack("<II", i, len(ext)))
            for dst, ln, kind, srcf, src in ext:
                o.write(struct.pack("<QIBHQ", dst, ln, kind, srcf if srcf != 0xFFFF else 0xFFFF, src))

    # The patched default.xbe, for building the pack's code.
    xi = index.get("default.xbe")
    if xi is not None:
        xbe = rebuild(files, [e for e in entries if e[0] == xi][0], out)
        with open(os.path.join(out, "default.xbe"), "wb") as fo:
            fo.write(xbe)
        print("[pack] default.xbe %s" % ("OK" if hashlib.sha256(xbe).hexdigest() == files[xi]["after"]["sha256"] else "MISMATCH"))

    info = {"name": m["name"], "version": m.get("version"), "author": m.get("author"),
            "description": m.get("description"), "source": os.path.abspath(args[0]),
            "files": [{"path": f["path"], "before": f["before"], "after": f["after"]} for f in files],
            "recipe": m.get("recipe", {}).get("overrides", {})}
    with open(os.path.join(out, "pack.json"), "w", encoding="utf-8") as fo:
        json.dump(info, fo, indent=1)

    if verify:
        bad = 0
        for i, f, ext in entries:
            h = hashlib.sha256()
            for chunk in stream(files, (i, f, ext), out):
                h.update(chunk)
            ok = h.hexdigest() == f["after"]["sha256"]
            bad += not ok
            print("[pack] verify %-16s %s" % (f["path"], "OK" if ok else "MISMATCH"))
        if bad:
            sys.exit("%d files did not verify" % bad)
    print("[pack] installed to %s" % out)


def stream(files, entry, out, block=1 << 22):
    """The patched file, in order, built the way the game reads it."""
    i, f, ext = entry
    size = f["after"]["size"]
    base_size = f["before"]["size"]
    handles = {}

    def src_read(fi, off, ln):
        if fi not in handles:
            handles[fi] = open(disc_path(files[fi]["path"]), "rb")
        h = handles[fi]
        h.seek(off)
        return h.read(ln)

    lit = open(os.path.join(out, "lit", "%d.bin" % i), "rb")
    pos = 0
    e = 0
    while pos < size:
        end = min(size, pos + block)
        buf = bytearray()
        # retail bytes (or zeros past the retail end) under everything
        if pos < base_size:
            buf += src_read(i, pos, min(end, base_size) - pos)
        buf += bytes(end - pos - len(buf))
        while e < len(ext) and ext[e][0] + ext[e][1] <= pos:
            e += 1
        k = e
        while k < len(ext) and ext[k][0] < end:
            dst, ln, kind, srcf, src = ext[k]
            a, b = max(dst, pos), min(dst + ln, end)
            if kind == LIT:
                lit.seek(src + (a - dst))
                data = lit.read(b - a)
            else:
                data = src_read(srcf, src + (a - dst), b - a)
            buf[a - pos:b - pos] = data
            k += 1
        yield bytes(buf)
        pos = end
    lit.close()
    for h in handles.values():
        h.close()


def rebuild(files, entry, out):
    return b"".join(stream(files, entry, out))


if __name__ == "__main__":
    main()
