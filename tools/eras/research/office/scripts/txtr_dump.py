"""Decode standalone TXTR chunks of outer entries to PNG.
Usage: python txtr_dump.py <folder with inventory.json> <out folder> 23 24 ..."""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json, struct
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools\tools"))
from pathlib import Path
import nfl_outer, nfl_txtr
SP = Path(sys.argv[1]); out = Path(sys.argv[2]); outers = [int(x) for x in sys.argv[3:]]
ar = nfl_outer.parse_archive(Path(__import__("os").path.join(ROOT, r"original\disc\vc_53450030\0")))
inv = json.load(open(SP/"inventory.json"))
for o in outers:
    data = nfl_outer.read_entry_bytes(ar, ar.entries[o])
    d = out / f"o{o}"; d.mkdir(parents=True, exist_ok=True)
    for c in inv["chunks"]:
        if c["outer_index"] != o or c["kind"] != "TXTR": continue
        off = c["chunk_offset"]
        kind, stored, sysb, vidb, magic, scratch, r0, r1 = nfl_txtr.HEADER.unpack_from(data, off)
        ch = nfl_txtr.Chunk(c["chunk_index"], off, "TXTR", stored, sysb, vidb, magic, scratch, r0, r1)
        try:
            dec, _ = nfl_txtr.decode_chunk(data, ch)
            info = nfl_txtr.parse_texture(dec, ch)
            rgba = nfl_txtr.texture_to_rgba(dec, ch, info)
            nm = nfl_txtr.safe_texture_name(info.name or '', f"tex{c['chunk_index']}")
            fn = d / f"{c['chunk_index']:03d}_{nm}.png"
            nfl_txtr.write_png(fn, info.width, info.height, rgba)
            print(o, c["chunk_index"], repr(info.name), info.width, info.height, info.format_name, info.mip_levels)
        except Exception as ex:
            print(o, c["chunk_index"], "ERR", ex)
