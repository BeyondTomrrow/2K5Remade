"""NCAA Football 07 stadium source for the 2K5 stadium converter.

Reads one STADIUMS.DAT model (``stadiums_raw/<entry>.tmd``, extracted by tools/ncaa07-extractor) with that
extractor's own decoder and returns plain parts: per draw node the vertices (NCAA yards, Z up, field along Y),
UVs, vertex colours, triangle faces, the material name, the NCAA shader name and the texture as RGBA.

The USC Trojans' stadium (the Los Angeles Memorial Coliseum) is STAD.SRES 128: entries 0x600 (FAIR, day) and
0x604 (NIGHT).
"""
from __future__ import annotations

import io
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXTRACTOR = ROOT / "tools" / "ncaa07-extractor"
if str(EXTRACTOR) not in sys.path:
    sys.path.insert(0, str(EXTRACTOR))

from ncaa_formats import decode_model  # noqa: E402
from ncaa_textures import parse_mmap, material_references, decode_texture, DecodeError  # noqa: E402

NCAA_WORK = Path(r"H:\NCAA07_Extractor_Work")
USC_DAY = NCAA_WORK / "stadiums_raw" / "00000600.tmd"
USC_NIGHT = NCAA_WORK / "stadiums_raw" / "00000604.tmd"


@dataclass
class Part:
    name: str
    material: str
    shader: str
    texture: str | None
    positions: list
    uvs: list
    colours: list           # (r, g, b, a) 0..1
    faces: list             # (i, j, k)
    normals: list = field(default_factory=list)


@dataclass
class Source:
    name: str
    parts: list
    textures: dict          # texture name -> PIL RGBA image


def load(path: Path) -> Source:
    from PIL import Image
    path = Path(path)
    model = decode_model(path.read_bytes(), str(path))
    data = model["_data"]
    records, meta, text = {}, None, None
    if "Text" in model["sections"] and model["sections"]["Text"]["size"]:
        ts = model["sections"]["Text"]
        text = data[ts["offset"]:ts["offset"] + ts["size"]]
        meta = parse_mmap(text)
        if "Matl" in model["sections"]:
            records = {m["material_index"]: m for m in material_references(data, model["sections"]["Matl"], meta)}
    textures, parts = {}, []
    for mesh in model["meshes"]:
        if not mesh["faces"]:
            continue
        rec = records.get(mesh["material"])
        mname = rec["name"] if rec else f"material_{mesh['material']}"
        shader = rec["shader"] if rec else ""
        tname = None
        if rec and rec["textures"]:
            ref = rec["textures"][0]
            if len(ref["mmap_file_indices"]) == 1:
                tname = ref["name"]
                if tname not in textures:
                    try:
                        png = decode_texture(text, meta, ref["mmap_file_indices"][0])
                        textures[tname] = Image.open(io.BytesIO(png)).convert("RGBA")
                    except DecodeError:
                        tname = None
        n = len(mesh["positions"])
        parts.append(Part(mesh["name"], mname, shader, tname, mesh["positions"],
                          mesh.get("uv", [(0.0, 0.0)] * n), mesh.get("colors", [(1.0, 1.0, 1.0, 1.0)] * n),
                          mesh["faces"], mesh["normals"]))
    return Source(model["name"], parts, textures)
