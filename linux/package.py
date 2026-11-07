#!/usr/bin/env python3
"""Pack the Linux x86-64 release tarball.

    python linux/package.py --binary build/nfl2k5/NFL2K5 --sdl3 sdl3/lib/libSDL3.so.0 \
        --version 0.9.0 --out dist/NFL2K5-linux-x86_64-0.9.0.tar.gz

Written in Python so file modes are explicit wherever it runs. Lists are
allow-lists on purpose, as in installer/NFL2K5-PC.iss: nothing from the player's
disc, no packs, saves or user media can end up in the archive, and neither can
My NFL Eras or the old-school mod.
"""
import argparse, gzip, hashlib, io, os, sys, tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TEXT = [  # source (repository relative) -> path in the archive
    ("linux/dist/README.txt", "README.txt"),
    ("installer/nfl2k5_video.default.ini", "nfl2k5_video.ini"),
    ("mods/README.md", "mods/README.md"),
    ("mods/textures/README.txt", "mods/textures/README.txt"),
    ("Music/README.txt", "Music/README.txt"),
    ("original/README.md", "original/README.md"),
    ("docs/third-party/kenney-input-prompts-LICENSE.txt", "docs/third-party/kenney-input-prompts-LICENSE.txt"),
    ("docs/third-party/softdrink-2k28-NOTICE.txt", "docs/third-party/softdrink-2k28-NOTICE.txt"),
    ("docs/third-party/nfl2k5tool-NOTICE.txt", "docs/third-party/nfl2k5tool-NOTICE.txt"),
    ("docs/LINUX.md", "docs/LINUX.md"),
]
EMPTY_DIRS = ["original", "original/CACHE", "mods/packs", "mods/patches"]
TREES = [("assets/controls", (".png", ".json"))]
FORBIDDEN = ("eras", "oldschool", "old-school", ".xbe", ".iso", "vc_53450030", ".2k5patch", ".sav")


def info(name, size, mode):
    t = tarfile.TarInfo(name)
    t.size, t.mode, t.mtime, t.uname, t.gname = size, mode, 0, "root", "root"
    return t


def rd_all(p):
    with open(p, "rb") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--sdl3", required=True)
    ap.add_argument("--license", action="append", default=[], help="SRC=NAME under docs/third-party")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    top = "NFL2K5-linux-x86_64-" + a.version

    # An empty interpreter function list still links; packs then crash at run time.
    if a.binary and b"converted functions differ" not in rd_all(a.binary):
        sys.exit("binary has no interpreter")
    ranges = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(a.binary))), "generated", "nfl2k5_func_ranges.c")
    if not os.path.exists(ranges) or "g_nfl2k5_func_range_count = 0;" in open(ranges).read():
        sys.exit("refusing to pack: %s is missing or empty, mod packs would crash" % ranges)

    files = []  # (archive path, bytes, mode)

    def rd(p):
        with open(p, "rb") as f:
            return f.read()

    files.append(("NFL2K5", rd(a.binary), 0o755))
    files.append(("lib/libSDL3.so.0", rd(a.sdl3), 0o755))
    # the working tree may hold CRLF on Windows; /bin/sh needs LF
    files.append(("nfl2k5.sh", rd(os.path.join(ROOT, "linux/dist/nfl2k5.sh")).replace(b"\r\n", b"\n"), 0o755))
    for src, dst in TEXT:
        files.append((dst, rd(os.path.join(ROOT, src)).replace(b"\r\n", b"\n"), 0o644))
    for tree, exts in TREES:
        for d, _, names in os.walk(os.path.join(ROOT, tree)):
            for n in sorted(names):
                if n.lower().endswith(exts):
                    p = os.path.join(d, n)
                    files.append((os.path.relpath(p, ROOT).replace("\\", "/"), rd(p), 0o644))
    miniz = os.path.join(ROOT, "external/xemu-src/util/miniz/miniz.c")
    if os.path.exists(miniz):
        text = rd(miniz)
        files.append(("docs/third-party/miniz-LICENSE.txt", text[:text.index(b"*/") + 2], 0o644))
    for spec in a.license:
        src, name = spec.split("=", 1)
        files.append(("docs/third-party/" + name, rd(src), 0o644))

    bad = [f[0] for f in files if any(w in f[0].lower() for w in FORBIDDEN) and not f[0].startswith("docs/")]
    if bad:
        sys.exit("refusing to pack: %s" % bad)

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz, \
            tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as t:
        for d in EMPTY_DIRS:
            di = info(top + "/" + d, 0, 0o755)
            di.type = tarfile.DIRTYPE
            t.addfile(di)
        for name, data, mode in sorted(files):
            t.addfile(info(top + "/" + name, len(data), mode), io.BytesIO(data))
    h = hashlib.sha256(rd(a.out)).hexdigest()
    with open(a.out + ".sha256", "w", newline="\n") as f:
        f.write("%s  %s\n" % (h, os.path.basename(a.out)))
    print(a.out, os.path.getsize(a.out), h)


if __name__ == "__main__":
    main()
