"""Bounds-checked NCAA 07 TMDL geometry reader and Blender-compatible GLB writer.

Layouts were established from the user's NCAA 07 PLYDATA and STADIUMS fixtures.
Every pointer in a TMDL is relative to its own TMdl header. No byte searching is
used to infer geometry. Unsupported streams remain in the source TMDL.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path


class FormatError(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data

    def check(self, offset: int, size: int):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise FormatError(f"Range {offset:#x}+{size:#x} exceeds {len(self.data):#x}")

    def unpack(self, fmt: str, offset: int):
        self.check(offset, struct.calcsize(fmt))
        return struct.unpack_from(fmt, self.data, offset)

    def string(self, offset: int):
        self.check(offset, 1)
        end = self.data.find(b"\0", offset)
        return self.data[offset:end if end >= 0 else len(self.data)].decode("ascii", "replace")


def sections(data: bytes):
    r = Reader(data)
    magic, length, count, alignment, version = r.unpack("<4sIHHI", 0)
    if magic != b"TMdl" or length > len(data) or length < 16 + count * 16:
        raise FormatError("Invalid TMDL header")
    r = Reader(data[:length])
    result = {}
    for i in range(count):
        tag, offset, size, number = r.unpack("<4sIII", 16 + i * 16)
        r.check(offset, size)
        result[tag.decode("ascii")] = {"offset": offset, "size": size, "count": number}
    return r, result


def decode_model(data: bytes, source="") -> dict:
    r, table = sections(data)
    name = r.string(table["Info"]["offset"]) if "Info" in table else Path(source).stem
    result = {"name": name, "source": source, "meshes": [], "warnings": [],
              "sections": table, "skeleton": None, "materials": []}
    result["_data"] = r.data
    if "Skel" in table:
        from ncaa07_tool import parse_skeleton
        s = table["Skel"]
        result["skeleton"] = parse_skeleton(r.data, s["offset"], s["size"])
    if "Geom" not in table:
        return result
    np, nc, sp, sc = r.unpack("<4I", table["Geom"]["offset"])
    r.check(np, nc * 20)
    r.check(sp, sc * 12)
    streams = [r.unpack("<IHHI", sp + i * 12) for i in range(sc)]
    for node_index in range(nc):
        at = np + node_index * 20
        draw_ptr, other, material, parent, flags, first = r.unpack("<II4H", at)
        if not draw_ptr:
            continue
        if first >= sc:
            raise FormatError(f"Node {node_index}: invalid stream index {first}")
        ptr, count, stride, semantic = streams[first]
        if semantic != 18 or stride != 24:
            result["warnings"].append(f"Node {node_index}: unsupported position stream {semantic:#x}/{stride}")
            continue
        r.check(ptr + 12, count * stride)
        positions, normals = [], []
        for i in range(count):
            values = r.unpack("<6f", ptr + 12 + i * stride)
            if not all(math.isfinite(v) for v in values):
                raise FormatError(f"Node {node_index}: non-finite vertex")
            positions.append(values[:3])
            normals.append(values[3:])
        attrs = {}
        for si in range(first + 1, sc):
            ap, an, ast, asem = streams[si]
            if asem == 18 and ast == 24:
                break
            if an == count and asem == 256 and ast == 8:
                r.check(ap + 12, an * ast)
                attrs["uv"] = [r.unpack("<2f", ap + 12 + i * ast) for i in range(an)]
            elif an == count and asem == 64 and ast == 4:
                r.check(ap + 12, an * ast)
                # D3DFVF_DIFFUSE is a DWORD ARGB; little-endian bytes are BGRA.
                attrs["colors"] = [tuple(raw[j] / 255 for j in (2, 1, 0, 3))
                                   for i in range(an)
                                   for raw in [r.data[ap + 12 + i * 4:ap + 16 + i * 4]]]
        draw_count, = r.unpack("<I", draw_ptr)
        r.check(draw_ptr + 4, draw_count * 4)
        faces = []
        for di in range(draw_count):
            dp, = r.unpack("<I", draw_ptr + 4 + di * 4)
            triangles, strip_id, a, b, c = r.unpack("<HHIII", dp)
            # Each record is a triangle strip: N triangles, N+2 uint16 indices.
            indices = r.unpack("<" + str(triangles + 2) + "H", dp + 16)
            if indices and max(indices) >= count:
                raise FormatError(f"Node {node_index}: index {max(indices)} exceeds {count} vertices")
            for i in range(triangles):
                tri = (indices[i], indices[i + 1], indices[i + 2])
                if i & 1:
                    tri = (tri[1], tri[0], tri[2])
                if len(set(tri)) == 3:
                    faces.append(tri)
        result["meshes"].append({"name": f"{name}_part_{node_index:04d}",
                                 "node_index": node_index, "material": material,
                                 "stream_index": first, "positions": positions,
                                 "normals": normals, "faces": faces, **attrs})
    return result


class GLB:
    def __init__(self):
        self.binary = bytearray()
        self.doc = {"asset": {"version": "2.0", "generator": "NCAA07 verified TMDL decoder"},
                    "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
                    "buffers": [], "bufferViews": [], "accessors": [], "materials": []}

    def image(self, png, name):
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(png)
        view = len(self.doc["bufferViews"])
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(png)})
        index = len(self.doc.setdefault("images", []))
        self.doc["images"].append({"name": name, "bufferView": view, "mimeType": "image/png"})
        ti = len(self.doc.setdefault("textures", []))
        self.doc["textures"].append({"source": index})
        return ti

    def accessor(self, values, components, ctype=5126, target=34962, bounds=False):
        while len(self.binary) % 4:
            self.binary.append(0)
        start = len(self.binary)
        fmt = "f" if ctype == 5126 else "I"
        flat = [v for row in values for v in row] if components > 1 else list(values)
        self.binary.extend(struct.pack("<" + str(len(flat)) + fmt, *flat))
        vi = len(self.doc["bufferViews"])
        view = {"buffer": 0, "byteOffset": start, "byteLength": len(self.binary) - start}
        if target:
            view["target"] = target
        self.doc["bufferViews"].append(view)
        ai = len(self.doc["accessors"])
        a = {"bufferView": vi, "componentType": ctype, "count": len(values),
             "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}[components]}
        if bounds:
            a["min"] = [min(row[i] for row in values) for i in range(components)]
            a["max"] = [max(row[i] for row in values) for i in range(components)]
        self.doc["accessors"].append(a)
        return ai

    def write(self, path: Path):
        self.doc["buffers"] = [{"byteLength": len(self.binary)}]
        js = json.dumps(self.doc, separators=(",", ":"), allow_nan=False).encode()
        js += b" " * (-len(js) % 4)
        self.binary.extend(b"\0" * (-len(self.binary) % 4))
        total = 12 + 8 + len(js) + 8 + len(self.binary)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(struct.pack("<4sII", b"glTF", 2, total) +
                         struct.pack("<I4s", len(js), b"JSON") + js +
                         struct.pack("<I4s", len(self.binary), b"BIN\0") + self.binary)


def export_glb(model: dict, output: Path):
    if not model["meshes"]:
        raise FormatError("No supported mesh geometry in this TMDL")
    glb = GLB()
    matmap = {}
    texture_meta = None
    text_data = None
    texture_cache = {}
    material_records = {}
    from ncaa_textures import parse_mmap, material_references, decode_texture, DecodeError
    if "Text" in model["sections"] and model["sections"]["Text"]["size"]:
        ts = model["sections"]["Text"]
        text_data = model["_data"][ts["offset"]:ts["offset"] + ts["size"]]
        try:
            texture_meta = parse_mmap(text_data)
            if "Matl" in model["sections"]:
                records = material_references(model["_data"], model["sections"]["Matl"], texture_meta)
                material_records = {m["material_index"]: m for m in records}
        except (DecodeError, UnicodeError) as error:
            model["warnings"].append(f"Texture/material: {error}")
    for mesh in model["meshes"]:
        if not mesh["faces"]:
            continue
        mi = mesh["material"]
        if mi not in matmap:
            matmap[mi] = len(glb.doc["materials"])
            material = {"name": f"source_material_{mi}", "doubleSided": True,
                        "pbrMetallicRoughness": {"metallicFactor": 0, "roughnessFactor": 0.85}}
            if model.get('_preview_material_tint'):
                material['pbrMetallicRoughness']['baseColorFactor'] = model['_preview_material_tint']
                material['extras'] = {'shaderReconstruction': 'Editable preview tint on source grayscale detail'}
            rec = material_records.get(mi)
            if rec:
                material["name"] = rec["name"]
                if rec["textures"]:
                    ref = rec["textures"][0]
                    matches = ref["mmap_file_indices"]
                    if len(matches) == 1:
                        fi = matches[0]
                        try:
                            if fi not in texture_cache:
                                png = decode_texture(text_data, texture_meta, fi)
                                texture_cache[fi] = glb.image(png, ref["name"])
                            material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": texture_cache[fi]}
                            material["alphaMode"] = "MASK"
                            material["alphaCutoff"] = 0.5
                        except DecodeError as error:
                            model["warnings"].append(f"Texture {ref['name']}: {error}")
            glb.doc["materials"].append(material)
        attrs = {"POSITION": glb.accessor(mesh["positions"], 3, bounds=True),
                 "NORMAL": glb.accessor(mesh["normals"], 3)}
        if "uv" in mesh:
            attrs["TEXCOORD_0"] = glb.accessor(mesh["uv"], 2)
        if "colors" in mesh:
            attrs["COLOR_0"] = glb.accessor(mesh["colors"], 4)
        idx = glb.accessor([v for face in mesh["faces"] for v in face], 1, 5125, 34963)
        mesh_index = len(glb.doc["meshes"])
        glb.doc["meshes"].append({"name": mesh["name"], "primitives": [
            {"attributes": attrs, "indices": idx, "material": matmap[mi], "mode": 4}]})
        ni = len(glb.doc["nodes"])
        glb.doc["nodes"].append({"name": mesh["name"], "mesh": mesh_index,
                                 "extras": {"sourceNode": mesh["node_index"], "sourceMaterial": mi}})
        glb.doc["scenes"][0]["nodes"].append(ni)
    # NCAA source is Z-up; glTF is Y-up. A root transform preserves every
    # source vertex and imports back into Blender's Z-up space without loss.
    children = list(glb.doc["scenes"][0]["nodes"])
    root_index = len(glb.doc["nodes"])
    glb.doc["nodes"].append({"name": model["name"], "children": children,
                             "rotation": [-math.sqrt(0.5), 0, 0, math.sqrt(0.5)]})
    glb.doc["scenes"][0]["nodes"] = [root_index]
    glb.doc["extras"] = {"source": model["source"], "model": model["name"],
                           "warnings": model["warnings"],
                           "rigStatus": "skeleton metadata retained; rig binding pending" if model["skeleton"] else "static model"}
    if model["skeleton"]:
        glb.doc["extras"]["sourceSkeleton"] = model["skeleton"]
    glb.write(output)
    return {"file": str(output), "model": model["name"], "parts": len(glb.doc["meshes"]),
            "vertices": sum(len(x["positions"]) for x in model["meshes"]),
            "triangles": sum(len(x["faces"]) for x in model["meshes"]),
            "textures": len(glb.doc.get("images", [])),
            "warnings": model["warnings"]}
