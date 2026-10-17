#!/usr/bin/env python3
"""Which shapes (props) of the Franchise office scenes use which materials.
Read-only; local research for docs/eras/research/OFFICE-RETEXTURE.md.

    python -I build/eras/office/office_shapes.py <tools_dir> <archive index> <out_dir>
"""
import json
import sys
import zlib
from pathlib import Path


def main(argv):
    tools, index, out = Path(argv[1]).resolve(), Path(argv[2]).resolve(), Path(argv[3]).resolve()
    sys.path.insert(0, str(tools))
    import nfl_outer as no
    import nfl_txtr as tx
    import nfl_scne_inventory as si
    from nfl_scene_probe import ResourceRecord

    archive = no.parse_archive(index)
    crc = zlib.crc32("FR.IFF".encode("utf-16le")) & 0xFFFFFFFF
    entry = next(e for e in archive.entries if e.name_id == crc)
    data = no.read_entry_bytes(archive, entry)
    result = {}
    for ch in tx.parse_chunks(data, allow_trailing=True):
        if ch.kind != "SCNE":
            continue
        dec, _ = tx.decode_chunk(data, ch)
        dec = bytes(dec)
        rec = ResourceRecord(outer_index=entry.table_index, outer_id=f"{entry.name_id:08x}", outer_size=entry.size,
                             chunk_index=ch.index, chunk_offset=ch.offset, kind="SCNE",
                             stored_size=ch.stored_size, word_08=ch.system_bytes, word_0c=ch.video_bytes,
                             word_10=ch.compression_magic, word_14=ch.overlap_scratch_bytes)
        try:
            scene, _names, _maps, _rgba = si.parse_scene(ch.index, rec, dec, {})
        except Exception as exc:  # noqa: BLE001
            result[f"chunk{ch.index}"] = {"error": f"{type(exc).__name__}: {exc}"}
            continue
        mats = {m["index"]: m["name"] for m in scene["materials"]}
        shapes = []
        for s in scene["shapes"]:
            subs = [x for x in scene.get("submeshes", []) if x.get("shape_index") == s["index"]]
            shapes.append({
                "index": s["index"],
                "name": s.get("name"),
                "vertices": s.get("vertex_count"),
                "materials": sorted({mats.get(x.get("material_index"), "?") for x in subs}),
            })
        result[scene["name"]] = {"shapes": shapes, "shape_keys": sorted(scene["shapes"][0].keys()) if scene["shapes"] else [],
                                 "submesh_keys": sorted(scene["submeshes"][0].keys()) if scene.get("submeshes") else []}
    out.mkdir(parents=True, exist_ok=True)
    (out / "office_shapes.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    for name, sc in result.items():
        if "error" in sc:
            print(name, sc["error"])
            continue
        print("==", name)
        for s in sc["shapes"]:
            print(f"  {s['index']:3d} {str(s['name']):40s} v={s['vertices']} {', '.join(s['materials'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
