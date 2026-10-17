#!/usr/bin/env python3
"""Read-only inventory of the Franchise office packages (research for
docs/eras/research/OFFICE-RETEXTURE.md).

    python -I tools/eras/research/office/office_inventory.py <tools_dir> <archive index> <out_dir> [--png]

tools_dir     .tmp-2k-football-mod-tools/tools (nfl_outer, nfl_txtr, nfl_scne_inventory)
archive index original/disc/vc_53450030/0
out_dir       build/eras/office

Lists every resource of fr.iff (outer 23, FRANCHISE1), the first per-team
package fr00.iff (outer 24, FRANCHISE2) and mini.iff when found: SCNE scenes
with their materials (name, flags, texture), embedded textures (format, size)
and node names; TXTR resources by name. With --png the embedded textures are
written as PNGs (local reference only; never commit them). Nothing on the disc
is modified.
"""
from __future__ import annotations

import json
import struct
import sys
import zlib
from pathlib import Path


def content_hash(video, pixel_offset, palette_offset, w, h, fmt):
    """tex_content_hash (nv2a_gpu_d3d11.inc.c) computed from the disc bytes:
    FNV over the base level as 32-bit words, then the 1,024-byte palette (P8).
    Assumes the game uploads the resource bytes unchanged (verify once with
    texture_dump = 1)."""
    if fmt == 0x0B:
        n = w * h
    elif fmt == 0x0C:
        n = (w // 4) * (h // 4) * 8
    elif fmt in (0x0E, 0x0F):
        n = (w // 4) * (h // 4) * 16
    else:
        n = w * h * 4
    data = video[pixel_offset:pixel_offset + n]
    if len(data) < n:
        return None
    hv = 2166136261
    for i in range(0, n - n % 4, 4):
        hv = ((hv ^ int.from_bytes(data[i:i + 4], "little")) * 16777619) & 0xFFFFFFFF
    if fmt == 0x0B:
        pal = video[palette_offset:palette_offset + 1024]
        if len(pal) < 1024:
            return None
        for i in range(0, 1024, 4):
            hv = ((hv ^ int.from_bytes(pal[i:i + 4], "little")) * 16777619) & 0xFFFFFFFF
    return f"{hv:08X}"


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    tools, index, out = Path(argv[1]).resolve(), Path(argv[2]).resolve(), Path(argv[3]).resolve()
    want_png = "--png" in argv
    sys.path.insert(0, str(tools))
    import nfl_outer as no                      # noqa: E402
    import nfl_txtr as tx                       # noqa: E402
    import nfl_scne_inventory as si             # noqa: E402

    archive = no.parse_archive(index)
    by_id = {e.name_id: e for e in archive.entries}

    def crc(name):
        return zlib.crc32(name.upper().encode("utf-16le")) & 0xFFFFFFFF

    targets = [("fr.iff", "FRANCHISE1 shared office")]
    codes = ([f"{v:02d}" for v in range(0, 32)] + [f"{v:02d}" for v in range(33, 38)]
             + [f"{v:02d}" for v in range(40, 47)] + [f"{v:02d}" for v in range(50, 86)]
             + [f"{v:02d}" for v in range(95, 100)])
    targets += [(f"fr{c}.iff", "FRANCHISE2 per-team") for c in codes]
    targets += [("mini.iff", "FRMINI")]
    report = {"archive": str(index), "packages": []}
    for fname, role in targets:
        entry = by_id.get(crc(fname))
        if entry is None:
            report["packages"].append({"file": fname, "role": role, "found": False})
            continue
        data = no.read_entry_bytes(archive, entry)
        chunks = tx.parse_chunks(data, allow_trailing=True)
        pkg = {"file": fname, "role": role, "found": True, "outer_index": entry.table_index,
               "name_id": f"0x{entry.name_id:08X}", "size": entry.size, "resources": []}
        for ch in chunks:
            row = {"chunk": ch.index, "kind": ch.kind, "system_bytes": ch.system_bytes,
                   "video_bytes": ch.video_bytes, "compressed": ch.compressed}
            try:
                dec, _ = tx.decode_chunk(data, ch)
                dec = bytes(dec)
            except Exception as exc:                # noqa: BLE001
                row["error"] = str(exc)
                pkg["resources"].append(row)
                continue
            if ch.kind == "TXTR":
                try:
                    info = tx.parse_texture(dec, ch)
                    row.update(name=info.name, format=info.format_name, width=info.width,
                               height=info.height, mips=info.mip_levels,
                               content_hash=content_hash(dec[ch.system_bytes:], info.pixel_offset,
                                                         info.palette_offset, info.width, info.height,
                                                         info.format_code))
                    if want_png and fname in ("fr.iff", "fr00.iff", "fr01.iff") and info.format_code in (0x02, 0x06, 0x0B, 0x0C, 0x7F):
                        rgba = tx.texture_to_rgba(dec, ch, info)
                        tx.write_png(out / "textures" / fname / f"txtr_{ch.index:02d}_{info.name}.png",
                                     info.width, info.height, rgba)
                except Exception as exc:            # noqa: BLE001
                    row["error"] = str(exc)
            elif ch.kind == "SCNE" and dec[0x0C:0x10] == b"SCNE":
                sysz = ch.system_bytes
                try:
                    _, name = si.pointer_name(dec, 0x10, sysz, "scene name")
                    desc = si.resolve_relative(dec, 0x14, sysz, "descriptor")
                    lay = si.table_layout(dec, desc, sysz)
                    row["name"] = name
                    row["tables"] = {k: v["count"] for k, v in lay.items()}
                    texs = []
                    t0 = lay["textures"]["offset"]
                    for i in range(lay["textures"]["count"]):
                        ti = si.texture_info(dec, t0 + i * 0x20, name, i)
                        texs.append({"index": i, "format": ti.format_name, "w": ti.width, "h": ti.height,
                                     "mips": ti.mip_levels,
                                     "content_hash": content_hash(dec[sysz:], ti.pixel_offset, ti.palette_offset,
                                                                  ti.width, ti.height, ti.format_code)})
                        if want_png and ti.format_code in (0x02, 0x06, 0x0B, 0x0C, 0x7F) and ti.dimensions == 2:
                            try:
                                rgba = tx.texture_to_rgba(dec, ch, ti)
                                tx.write_png(out / "textures" / fname / f"{name}_tex{i:03d}_{ti.width}x{ti.height}.png",
                                             ti.width, ti.height, rgba)
                            except Exception as exc:  # noqa: BLE001
                                texs[-1]["png_error"] = str(exc)
                    mats = []
                    m0 = lay["materials"]["offset"]
                    for i in range(lay["materials"]["count"]):
                        off = m0 + i * 0x80
                        _, mname = si.pointer_name(dec, off, sysz, "material")
                        tgt = si.resolve_relative(dec, off + 0x30, sysz, "material texture")
                        tix = (tgt - t0) // 0x20 if (tgt is not None and t0 is not None) else None
                        mats.append({"index": i, "name": mname, "flags8": dec[off + 8], "refl9": dec[off + 9],
                                     "texture_index": tix,
                                     "texture": texs[tix] if tix is not None and 0 <= tix < len(texs) else None})
                    nodes = []
                    n0 = lay["nodes"]["offset"]
                    for i in range(lay["nodes"]["count"]):
                        _, nname = si.pointer_name(dec, n0 + i * 0x60, sysz, "node")
                        nodes.append(nname)
                    row["materials"] = mats
                    row["embedded_textures"] = texs
                    row["nodes"] = nodes
                except Exception as exc:            # noqa: BLE001
                    row["error"] = f"{type(exc).__name__}: {exc}"
            pkg["resources"].append(row)
        report["packages"].append(pkg)
    out.mkdir(parents=True, exist_ok=True)
    (out / "office_inventory.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    for pkg in report["packages"]:
        print(f"{pkg['file']:10s} {pkg['role']:26s} found={pkg['found']} outer={pkg.get('outer_index')}")
        for r in pkg.get("resources", []):
            extra = r.get("name", "")
            if r["kind"] == "SCNE":
                extra += f" materials={len(r.get('materials', []))} textures={len(r.get('embedded_textures', []))}"
            elif r["kind"] == "TXTR":
                extra += f" {r.get('format')} {r.get('width')}x{r.get('height')}"
            print(f"   chunk {r['chunk']:3d} {r['kind']} {extra} {r.get('error', '')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
