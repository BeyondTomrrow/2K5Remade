#!/usr/bin/env python3
"""Export the Franchise office scenes (fr.iff coach_desk, front_office) to glTF
with 2K Football Mod Tools' exporter, and measure which part of the per-team
wallphoto / office_photos atlases each picture frame shows (its UV box).
Read-only on the disc; output is local reference only (never commit it).

    python -I build/eras/office/export_office_models.py <mod_tools_root> <archive index> <out_dir>
"""
import json
import struct
import sys
import zlib
from pathlib import Path


def main(argv):
    root, index, out = Path(argv[1]).resolve(), Path(argv[2]).resolve(), Path(argv[3]).resolve()
    sys.path[:0] = [str(root), str(root / "tools")]
    import nfl_outer as no
    import nfl_txtr as tx
    from mod_editor.core import nfl2k5_models as nm

    archive = no.parse_archive(index)
    crc = zlib.crc32("FR.IFF".encode("utf-16le")) & 0xFFFFFFFF
    entry = next(e for e in archive.entries if e.name_id == crc)
    data = no.read_entry_bytes(archive, entry)
    spans, names = {}, {}
    for ch in tx.parse_chunks(data, allow_trailing=True):
        if ch.kind == "SCNE" and ch.index in (4, 6):
            key = nm.model_key(entry.table_index, ch.index)
            spans[key] = data[ch.offset:ch.offset + 0x20 + ch.stored_size]
            names[key] = {4: "coach_desk", 6: "front_office"}[ch.index]
    source = nm.ModelSpanSource(spans)
    report = {}
    for key, name in names.items():
        dest = out / "models" / f"{name}.gltf"
        dest.parent.mkdir(parents=True, exist_ok=True)
        res = nm.export_model(source, key, dest, include_textures=True, include_skins=False)
        report[name] = {"gltf": str(dest), "result": str(res)[:300]}
        # UV boxes per mesh primitive on the atlases
        g = json.loads(dest.read_text(encoding="utf-8"))
        binp = dest.with_suffix(".bin").read_bytes()
        mats = [m.get("name") for m in g.get("materials", [])]
        boxes = []
        for mesh in g.get("meshes", []):
            for prim in mesh.get("primitives", []):
                mat = mats[prim["material"]] if "material" in prim else None
                if mat not in ("wallphoto", "office_photos", "teamlogo", "monscreen", "monitor_screen", "vcSwShader1"):
                    continue
                acc_i = prim["attributes"].get("TEXCOORD_0")
                if acc_i is None:
                    continue
                acc = g["accessors"][acc_i]
                bv = g["bufferViews"][acc["bufferView"]]
                off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
                stride = bv.get("byteStride", 8)
                us, vs = [], []
                for k in range(acc["count"]):
                    u, v = struct.unpack_from("<2f", binp, off + k * stride)
                    us.append(u); vs.append(v)
                boxes.append({"mesh": mesh.get("name"), "material": mat,
                              "u": [round(min(us), 4), round(max(us), 4)],
                              "v": [round(min(vs), 4), round(max(vs), 4)]})
        report[name]["atlas_uv_boxes"] = boxes
    (out / "office_uv_boxes.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    for name, r in report.items():
        print("==", name, r["gltf"])
        for b in r["atlas_uv_boxes"]:
            print(f"   {str(b['mesh']):28s} {b['material']:15s} u {b['u']} v {b['v']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
