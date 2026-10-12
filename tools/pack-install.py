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
import argparse
import atexit
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import zipfile

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = PROJECT_ROOT
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


def parse_args():
    parser = argparse.ArgumentParser(
        description="Install a 2K5 Mod Studio .2k5patch as a native-PC disc overlay."
    )
    parser.add_argument("patch", help="Path to the .2k5patch file, or a patched ISO/XISO disc image")
    parser.add_argument("--root", default=PROJECT_ROOT,
                        help="NFL2K5-PC installation root (default: project root)")
    parser.add_argument("--disc", default=None,
                        help="Clean extracted retail disc folder (default: <root>/original/disc)")
    parser.add_argument("--verify", action="store_true",
                        help="Rebuild and SHA-256 verify every changed file")
    parser.add_argument("--replace", action="store_true",
                        help="Replace an existing installation of the same pack")
    parser.add_argument("--native-exe", default=None,
                        help="Legacy option; ignored (packs run through the installed game)")
    parser.add_argument("--native-catalog", default=None,
                        help="Legacy option; ignored (packs run through the installed game)")
    parser.add_argument("--require-native", action="store_true",
                        help="Legacy option; accepted for compatibility and ignored")
    parser.add_argument("--log", default=None,
                        help="Write the install transcript to this file")
    parser.add_argument("--activate", action="store_true",
                        help="Make this pack the version launched by the normal shortcut")
    return parser.parse_args()


def release_label(path):
    match = re.search(r"(?:^|[-_])v(\d+(?:\.\d+)+)(?:[-_.]|$)", os.path.basename(path), re.I)
    return "v" + match.group(1) if match else ""


def main():
    global ROOT, DISC
    args = parse_args()
    log_handle = None
    if args.log:
        log_path = os.path.abspath(args.log)
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        log_handle = open(log_path, "w", encoding="utf-8", buffering=1)

        class Tee:
            def __init__(self, *streams):
                self.streams = streams

            def write(self, value):
                for stream in self.streams:
                    stream.write(value)
                return len(value)

            def flush(self):
                for stream in self.streams:
                    stream.flush()

        sys.stdout = Tee(sys.stdout, log_handle)
        sys.stderr = Tee(sys.stderr, log_handle)
    ROOT = os.path.abspath(args.root)
    DISC = os.path.abspath(args.disc or os.path.join(ROOT, "original", "disc"))
    patch_path = os.path.abspath(args.patch)
    verify = args.verify
    if not os.path.isfile(patch_path):
        sys.exit("patch file does not exist: %s" % patch_path)
    if not os.path.isdir(DISC):
        sys.exit("clean extracted disc folder does not exist: %s" % DISC)
    if patch_path.lower().endswith(".xdiff"):
        return install_xdiff(args, patch_path)
    if not zipfile.is_zipfile(patch_path):
        return install_image(args, patch_path)
    z = zipfile.ZipFile(patch_path)
    m = json.loads(z.read("manifest.json"))
    if m.get("format") != 3 or m.get("file_contract") != "source-copy-v1":
        sys.exit("unsupported pack format %r / %r" % (m.get("format"), m.get("file_contract")))
    label = release_label(patch_path)
    display_version = label or str(m.get("version", ""))
    folder_name = slug(m["name"] + "-" + display_version)
    packs_dir = os.path.join(ROOT, "mods", "packs")
    out = os.path.join(packs_dir, folder_name)
    if os.path.exists(out) and not args.replace:
        sys.exit("pack is already installed: %s (use --replace to reinstall)" % out)
    os.makedirs(packs_dir, exist_ok=True)
    stage = tempfile.mkdtemp(prefix=folder_name + ".install-", dir=packs_dir)
    cleanup_stage = [stage]

    def remove_incomplete_stage():
        if cleanup_stage and os.path.isdir(cleanup_stage[0]):
            shutil.rmtree(cleanup_stage[0], ignore_errors=True)

    atexit.register(remove_incomplete_stage)
    out = stage
    os.makedirs(os.path.join(out, "lit"), exist_ok=True)
    files = m["files"]
    index = {f["path"]: i for i, f in enumerate(files)}

    # The retail files must be the exact clean files the pack was made against.
    # Size-only validation can accept a different regional/repacked disc and
    # produce a subtly corrupt overlay, so validate every declared source hash.
    for f in files:
        p = disc_path(f["path"])
        if not os.path.exists(p) or os.path.getsize(p) != f["before"]["size"]:
            sys.exit("retail file %s is missing or a different size" % f["path"])
        actual = sha_file(p)
        if actual.lower() != f["before"]["sha256"].lower():
            sys.exit("retail file %s does not match the clean USA retail disc" % f["path"])
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

    xbe_hash = hashlib.sha256(xbe).hexdigest() if xi is not None else ""
    display_name = (m["name"] + " " + display_version).strip() if label else m["name"]
    info = {"name": m["name"], "display_name": display_name,
            "version": display_version, "manifest_version": m.get("version"),
            "author": m.get("author"), "description": m.get("description"),
            "source": patch_path, "patch_sha256": sha_file(patch_path),
            "xbe_sha256": xbe_hash,
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

    if args.native_exe or args.native_catalog or args.require_native:
        print("[pack] native code-pack options are no longer needed; using the installed game executable")

    final_out = os.path.join(packs_dir, folder_name)
    backup = final_out + ".previous"
    try:
        if os.path.exists(backup):
            shutil.rmtree(backup)
        cleanup_stage.clear()
        if os.path.exists(final_out):
            os.replace(final_out, backup)
        os.replace(out, final_out)
        if os.path.exists(backup):
            shutil.rmtree(backup)
    except Exception:
        if not os.path.exists(final_out) and os.path.exists(backup):
            os.replace(backup, final_out)
        raise
    if args.activate:
        active_path = os.path.join(ROOT, "mods", "active-pack.ini")
        active_tmp = active_path + ".tmp"
        with open(active_tmp, "w", encoding="ascii", newline="\n") as active:
            active.write(folder_name + "\n")
        os.replace(active_tmp, active_path)
    print("[pack] installed to %s" % final_out)
    print("[pack] XBE SHA-256 %s" % (xbe_hash or "none"))


# Friendly labels for known finished disc images, keyed by patched XBE hash.
KNOWN_IMAGES = {
    "2b0fbbbb89b5c72aaed7c454bf6e78dd2c97f1e417e2d761e515907ed81471ab": ("SOFTDRINK 2K28", "v0.4"),
}


XDIFF_PACKS = {
    # patched default.xbe SHA-256 -> (folder, name, description)
    "81d6e16c25ca7ea1014d0ced915f910f5d354eaeabeb9adfcbb6956e2a0b54f8":
        ("Widescreen", "Widescreen",
         "The original game with 2K5 Mod Studio's Hor+ widescreen patch (cruuz and contributors): "
         "the picture fills the window at any aspect ratio."),
}


def install_xdiff(args, diff_path):
    """A byte patch for the retail default.xbe only (tools/patches/*.xdiff):
    header 2K5XDIF1, retail SHA-256, result SHA-256, run count, then
    (offset, length, bytes) runs. No game data ships with it."""
    blob = open(diff_path, "rb").read()
    if blob[:8] != b"2K5XDIF1":
        sys.exit("not an executable patch: %s" % diff_path)
    want_in, want_out = blob[8:40].hex(), blob[40:72].hex()
    xbe = bytearray(open(os.path.join(DISC, "default.xbe"), "rb").read())
    if hashlib.sha256(xbe).hexdigest() != want_in:
        sys.exit("default.xbe is not the clean USA retail executable this patch is for")
    n = struct.unpack_from("<I", blob, 72)[0]
    k = 76
    for _ in range(n):
        off, ln = struct.unpack_from("<II", blob, k)
        k += 8
        xbe[off:off + ln] = blob[k:k + ln]
        k += ln
    xbe_hash = hashlib.sha256(xbe).hexdigest()
    if xbe_hash != want_out or xbe_hash not in XDIFF_PACKS:
        sys.exit("patched executable did not verify")
    folder, name, desc = XDIFF_PACKS[xbe_hash]
    out = os.path.join(ROOT, "mods", "packs", folder)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "default.xbe"), "wb") as fo:
        fo.write(xbe)
    with open(os.path.join(out, "pack.json"), "w", encoding="utf-8") as fo:
        json.dump({"name": name, "display_name": name, "version": "1", "description": desc,
                   "source": os.path.basename(diff_path), "xbe_sha256": xbe_hash}, fo, indent=1)
    if args.native_exe or args.native_catalog or args.require_native:
        print("[pack] native code-pack options are no longer needed; using the installed game executable")
    if args.activate:
        active_path = os.path.join(ROOT, "mods", "active-pack.ini")
        with open(active_path, "w", encoding="ascii") as active:
            active.write(folder)
    print("[pack] installed to %s" % out)
    print("[pack] XBE SHA-256 %s" % xbe_hash)


def install_image(args, image_path):
    """A patched ISO/XISO instead of a .2k5patch: the image is the finished
    build, so its executable and data always match (tools/iso-to-pack.py)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("iso_to_pack", os.path.join(os.path.dirname(os.path.abspath(__file__)), "iso-to-pack.py"))
    itp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(itp)
    packs_dir = os.path.join(ROOT, "mods", "packs")
    os.makedirs(packs_dir, exist_ok=True)
    stage = tempfile.mkdtemp(prefix="image.install-", dir=packs_dir)
    try:
        print("[pack] converting disc image %s" % image_path)
        xbe_hash = itp.convert(image_path, stage, disc=DISC)
        name, version = KNOWN_IMAGES.get(xbe_hash, (os.path.splitext(os.path.basename(image_path))[0], ""))
        with open(os.path.join(stage, "pack.json"), encoding="utf-8") as fo:
            info = json.load(fo)
        info.update({"name": name, "display_name": (name + " " + version).strip(), "version": version})
        with open(os.path.join(stage, "pack.json"), "w", encoding="utf-8") as fo:
            json.dump(info, fo, indent=1)
        folder_name = slug(name + ("-" + version if version else ""))
        if args.native_exe or args.native_catalog or args.require_native:
            print("[pack] native code-pack options are no longer needed; using the installed game executable")
        final_out = os.path.join(packs_dir, folder_name)
        if os.path.exists(final_out):
            if not args.replace:
                sys.exit("pack is already installed: %s (use --replace to reinstall)" % final_out)
            shutil.rmtree(final_out)
        os.replace(stage, final_out)
        stage = None
    finally:
        if stage and os.path.isdir(stage):
            shutil.rmtree(stage, ignore_errors=True)
    if args.activate:
        active_path = os.path.join(ROOT, "mods", "active-pack.ini")
        with open(active_path + ".tmp", "w", encoding="ascii", newline="\n") as active:
            active.write(folder_name + "\n")
        os.replace(active_path + ".tmp", active_path)
    print("[pack] installed to %s" % final_out)
    print("[pack] XBE SHA-256 %s" % xbe_hash)


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
