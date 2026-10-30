# Vendored from 2K Football Mod Tools (MIT, see tools/team-creator/vendor/LICENSE-2k-football-mod-tools.txt):
# mod_editor/core/nfl2k5_scne_builder.py, reviewed 2026-10-16. Changes: p8_texture and the compressed-chunk
# helpers removed (tools/eras/stadiums/coliseum_build.py has its own P8 encoder and uses the vendored nfl_txtr codec).
"""Static SCNE scene builder: parse an NFL 2K5 static scene into records and write it again from scratch.

The retail loader (0x45BC0 -> 0x2F140) relocates a scene in place: every serialized pointer is one-based and
self-relative (``target = field - 1 + value``), and the descriptor relocator walks each table with a record
relocator. The fields below are every pointer those relocators touch (read from the executable, 2026-09-23), so a
scene whose records carry exactly these pointers can be laid out anew, grown, or given new records:

* object (0x45BC0): +0x10 name, +0x14 descriptor; the video base is block + 0x20 + system bytes;
* descriptor (0x2F140): +0x00 name and the table pointers +0x10 aux14, +0x18 textures, +0x20 materials,
  +0x28 nodes, +0x30 shapes, +0x38 markers, +0x40 aux60, +0x48 aux50 (counts at the preceding words);
* texture 0x34DF0 (0x20): +0x00 self-relative; +0x04 pixels and +0x08 palette are offsets into the video part;
* material 0x304B0 (0x80): +0x00 name, +0x1C next pass, +0x30 texture, +0x34, +0x38, +0x3C;
* shape 0x22F90 (0x100): +0x40 name, +0x60 (only when u16 +0x52 is nonzero), +0x64 transforms, +0x70 submeshes,
  +0x74 morph records, +0x78, +0x7C, the eight stream pointers +0xD4..+0xF0; each submesh (0x80) +0x78 push
  words; each transform (0x70) +0x60; each morph record (0x0C) +0x00;
* node 0x21630 (0x60): +0x00 name, +0x04 shape name (matched to a shape by name), +0x14, +0x18, +0x1C;
* marker 0x38530 (0x40): +0x00 name, +0x30 link name;
* aux50 (0x50, inline in 0x2F140): +0x40 name. The afternoon stadiums carry one, ``sunShape``: a 3x3 basis, a
  position, the name, and a light colour (INFERRED: the sun for the long afternoon shadows).

The static renderer 0x243D0 copies each submesh's push words verbatim into the GPU push buffer and reads the shape
UV constant with ``movaps [shape + 0x30]``, so every record is kept 16-byte aligned; both parts are padded to 128
bytes like every retail scene. This module handles static scenes only: the aux14 and aux60 tables (animation
channels and cameras) and morph records must be empty; aux50 light records are carried as they are.
"""
from __future__ import annotations

import hashlib
import math
import struct
from dataclasses import dataclass, field
from typing import Iterable, Sequence


class ScneBuildError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise ScneBuildError(message)


HEADER_AREA = 0x100          # 12 zero bytes, "SCNE", name, descriptor pointer, inline name up to 0x100
DESCRIPTOR_SIZE = 0x54
STRIDES = dict(texture=0x20, material=0x80, node=0x60, shape=0x100, marker=0x40, submesh=0x80, transform=0x70,
               morph=0x0C)
MATERIAL_POINTERS = (0x00, 0x1C, 0x30, 0x34, 0x38, 0x3C)
SHAPE_STREAMS = tuple(range(0xD4, 0xF4, 4))
P8_FORMAT = 0x0B


def align(value, to):
    return (value + to - 1) // to * to


def _rel(buf, at):
    value = struct.unpack_from("<i", buf, at)[0]
    return None if value == 0 else at + value - 1


def _utf16z(buf, at, limit=512):
    out = []
    while len(out) < limit:
        require(0 <= at <= len(buf) - 2, "string runs past the scene")
        unit = struct.unpack_from("<H", buf, at)[0]
        if unit == 0:
            return "".join(out)
        out.append(chr(unit))
        at += 2
    raise ScneBuildError("unterminated scene string")


def sha(data):
    return hashlib.sha256(bytes(data)).hexdigest()


# --- records ------------------------------------------------------------------------------------------------

@dataclass
class Texture:
    record: bytearray            # 0x20 bytes; +0x04/+0x08 are rewritten on serialize
    pixels: bytes                # every mip level, swizzled P8 indices
    palette: bytes               # 1024 bytes BGRA

    @property
    def width(self):
        return 1 << ((struct.unpack_from("<I", self.record, 0x0C)[0] >> 20) & 0xF)

    @property
    def height(self):
        return 1 << ((struct.unpack_from("<I", self.record, 0x0C)[0] >> 24) & 0xF)

    @property
    def mips(self):
        return (struct.unpack_from("<I", self.record, 0x0C)[0] >> 16) & 0xF


@dataclass
class Material:
    record: bytearray            # 0x80
    name: str
    texture: int | None          # index into Scene.textures
    next_pass: int | None = None  # index into Scene.materials


@dataclass
class Submesh:
    record: bytearray            # 0x80 (+0x00 material index, +0x7C word count, +0x7E secondary count)
    words: bytes                 # the push words the renderer copies

    @property
    def material(self):
        return struct.unpack_from("<H", self.record, 0)[0]


@dataclass
class Transform:
    record: bytearray            # 0x70
    bone: str | None


@dataclass
class Shape:
    record: bytearray            # 0x100
    name: str
    transforms: list[Transform]
    submeshes: list[Submesh]
    streams: list[bytes | None]  # eight vertex streams (stride * vertex_count bytes each)
    extra_78: int | None = None  # pointer fields +0x78/+0x7C must be null for static shapes
    extra_7c: int | None = None

    @property
    def vertex_count(self):
        return struct.unpack_from("<H", self.record, 0x4C)[0]

    def stride(self, stream):
        return struct.unpack_from("<H", self.record, 0xC4 + 2 * stream)[0]


@dataclass
class Node:
    record: bytearray            # 0x60
    name: str
    shape_name: str
    matrix14: bytes              # 64 bytes (current matrix)
    matrix1c: bytes              # 64 bytes


@dataclass
class Marker:
    record: bytearray            # 0x40 (position x, y, z, w at +0x10)
    name: str
    link: str | None


@dataclass
class Light:
    record: bytearray            # 0x50 (aux50; +0x40 name)
    name: str | None


@dataclass
class Scene:
    name: str
    textures: list[Texture]
    materials: list[Material]
    nodes: list[Node]
    shapes: list[Shape]
    markers: list[Marker]
    descriptor_tail: bytes = b"\0" * 8   # descriptor +0x04..+0x0B (unassigned, zero in retail stadiums)
    lights: list[Light] = field(default_factory=list)

    # -- lookups --
    def material_index(self, name):
        for i, m in enumerate(self.materials):
            if m.name == name:
                return i
        raise ScneBuildError(f"no material {name}")

    def texture_of(self, material_name):
        return self.materials[self.material_index(material_name)].texture

    def shape(self, name):
        for s in self.shapes:
            if s.name == name:
                return s
        raise ScneBuildError(f"no shape {name}")

    def marker(self, name):
        for m in self.markers:
            if m.name == name:
                return m
        raise ScneBuildError(f"no marker {name}")

    def remove_shape(self, name):
        """Delete a shape and every node that draws it."""
        before = len(self.shapes)
        self.shapes = [s for s in self.shapes if s.name != name]
        require(len(self.shapes) == before - 1, f"no single shape {name}")
        self.nodes = [n for n in self.nodes if n.shape_name != name]

    # -- output --
    def serialize(self):
        return serialize(self)


# --- parse --------------------------------------------------------------------------------------------------

def parse(decoded, system_bytes, *, secondary=False):
    """Scene records of one decoded static SCNE (system part then video part).

    ``secondary`` (job st3, 2026-09-27; off by default, so every scene built before it parses and serializes byte for
    byte as before) carries a submesh's secondary push words verbatim: the retail field scenes store, right after some
    submeshes' primary words, a second run of words (+0x7E counts them; PROVED OFFLINE on s15: line primitives over the
    same vertices) that no relocator touches, so they travel with the primary words and keep their count."""
    buf = bytes(decoded)
    require(len(buf) >= system_bytes >= HEADER_AREA, "scene is smaller than its header")
    require(buf[12:16] == b"SCNE", "not a SCNE object")
    name = _utf16z(buf, _rel(buf, 0x10))
    desc = _rel(buf, 0x14)
    require(desc is not None and desc + DESCRIPTOR_SIZE <= system_bytes, "descriptor out of range")
    video = system_bytes

    def table(count_at, ptr_at, stride):
        count = struct.unpack_from("<I", buf, desc + count_at)[0]
        at = _rel(buf, desc + ptr_at)
        if count == 0:
            return []
        require(at is not None and at + count * stride <= system_bytes, "table out of range")
        return [at + i * stride for i in range(count)]

    for count_at, ptr_at in ((0x0C, 0x10), (0x3C, 0x40)):
        require(struct.unpack_from("<I", buf, desc + count_at)[0] == 0, "animated or camera scenes are not built")
    tex_at = table(0x14, 0x18, 0x20)
    textures = []
    tex_index = {}
    for i, at in enumerate(tex_at):
        rec = bytearray(buf[at:at + 0x20])
        require(_rel(buf, at) is None, "texture +0x00 is expected null")
        fmt = struct.unpack_from("<I", rec, 0x0C)[0]
        require((fmt >> 8) & 0xFF == P8_FORMAT and (fmt >> 4) & 0xF == 2, "only 2D P8 textures are built")
        w, h, mips = 1 << ((fmt >> 20) & 0xF), 1 << ((fmt >> 24) & 0xF), (fmt >> 16) & 0xF
        pix, pal = struct.unpack_from("<II", rec, 4)
        size = sum(max(1, w >> k) * max(1, h >> k) for k in range(mips))
        require(video + pix + size <= len(buf) and video + pal + 1024 <= len(buf), "texture data out of range")
        textures.append(Texture(rec, buf[video + pix:video + pix + size], buf[video + pal:video + pal + 1024]))
        tex_index[at] = i
    mat_at = table(0x1C, 0x20, 0x80)
    mat_index = {at: i for i, at in enumerate(mat_at)}
    materials = []
    for at in mat_at:
        rec = bytearray(buf[at:at + 0x80])
        for f in (0x34, 0x38, 0x3C):
            require(_rel(buf, at + f) is None, "material +0x34/+0x38/+0x3C are expected null")
        tex = _rel(buf, at + 0x30)
        nxt = _rel(buf, at + 0x1C)
        require(tex is None or tex in tex_index, "material texture is not a texture record")
        require(nxt is None or nxt in mat_index, "material next pass is not a material record")
        materials.append(Material(rec, _utf16z(buf, _rel(buf, at)), tex_index.get(tex),
                                  mat_index.get(nxt) if nxt is not None else None))
    shapes = []
    for at in table(0x2C, 0x30, 0x100):
        rec = bytearray(buf[at:at + 0x100])
        count = struct.unpack_from("<H", rec, 0x4C)[0]
        morphs, xforms, extra, subs = struct.unpack_from("<4H", rec, 0x4E)
        require(morphs == 0 and extra == 0, "skinned or morphing shapes are not built")
        require(struct.unpack_from("<I", rec, 0x44)[0] == 2, "shape version is not 2")
        require(_rel(buf, at + 0x74) is None and _rel(buf, at + 0x78) is None and _rel(buf, at + 0x7C) is None,
                "shape +0x74/+0x78/+0x7C are expected null")
        xat = _rel(buf, at + 0x64)
        transforms = []
        for k in range(xforms):
            t = xat + k * 0x70
            trec = bytearray(buf[t:t + 0x70])
            bone = _rel(buf, t + 0x60)
            transforms.append(Transform(trec, _utf16z(buf, bone) if bone is not None else None))
        sat = _rel(buf, at + 0x70)
        submeshes = []
        for k in range(subs):
            s = sat + k * 0x80
            srec = bytearray(buf[s:s + 0x80])
            words = struct.unpack_from("<H", srec, 0x7C)[0]
            second = struct.unpack_from("<H", srec, 0x7E)[0]
            require(secondary or second == 0, "secondary push words are not built")
            push = _rel(buf, s + 0x78)
            require(push is not None and push + 4 * (words + second) <= system_bytes, "push words out of range")
            submeshes.append(Submesh(srec, buf[push:push + 4 * (words + second)]))
        streams = []
        for k, f in enumerate(SHAPE_STREAMS):
            p = _rel(buf, at + f)
            stride = struct.unpack_from("<H", rec, 0xC4 + 2 * k)[0]
            if p is None:
                streams.append(None)
                continue
            require(p + stride * count <= system_bytes, "vertex stream out of range")
            streams.append(buf[p:p + stride * count])
        shapes.append(Shape(rec, _utf16z(buf, _rel(buf, at + 0x40)), transforms, submeshes, streams))
    nodes = []
    for at in table(0x24, 0x28, 0x60):
        rec = bytearray(buf[at:at + 0x60])
        m14, m1c = _rel(buf, at + 0x14), _rel(buf, at + 0x1C)
        require(_rel(buf, at + 0x18) is None, "node +0x18 is expected null")
        require(m14 is not None and m1c is not None, "node matrices missing")
        nodes.append(Node(rec, _utf16z(buf, _rel(buf, at)), _utf16z(buf, _rel(buf, at + 4)),
                          buf[m14:m14 + 64], buf[m1c:m1c + 64]))
    markers = []
    for at in table(0x34, 0x38, 0x40):
        rec = bytearray(buf[at:at + 0x40])
        link = _rel(buf, at + 0x30)
        markers.append(Marker(rec, _utf16z(buf, _rel(buf, at)), _utf16z(buf, link) if link is not None else None))
    lights = []
    for at in table(0x44, 0x48, 0x50):
        lname = _rel(buf, at + 0x40)
        lights.append(Light(bytearray(buf[at:at + 0x50]), _utf16z(buf, lname) if lname is not None else None))
    return Scene(name, textures, materials, nodes, shapes, markers, bytes(buf[desc + 4:desc + 12]), lights)


# --- serialize ----------------------------------------------------------------------------------------------

class _Writer:
    def __init__(self):
        self.buf = bytearray()
        self.fixups = []      # (field offset, key) resolved after layout
        self.targets = {}     # key -> offset

    def at(self):
        return len(self.buf)

    def pad(self, to):
        self.buf += bytes(align(len(self.buf), to) - len(self.buf))

    def put(self, data, key=None, alignment=16):
        self.pad(alignment)
        where = len(self.buf)
        if key is not None:
            require(key not in self.targets, f"duplicate layout key {key}")
            self.targets[key] = where
        self.buf += data
        return where

    def pointer(self, field_offset, key):
        if key is None:
            struct.pack_into("<i", self.buf, field_offset, 0)
        else:
            self.fixups.append((field_offset, key))

    def resolve(self):
        for field_offset, key in self.fixups:
            require(key in self.targets, f"unresolved pointer to {key}")
            struct.pack_into("<i", self.buf, field_offset, self.targets[key] - field_offset + 1)


def _texture_size(tex):
    w, h, m = tex.width, tex.height, tex.mips
    return sum(max(1, w >> k) * max(1, h >> k) for k in range(m))


def serialize(scene):
    """(decoded bytes, system bytes, video bytes) of a scene laid out from scratch.

    Layout (as retail stadium scenes): header area, descriptor, the node, marker, texture, material and shape
    tables, the node matrices, per shape its transforms, submesh records, push words and streams, then the
    string pool; the video part holds every texture's mips (128-byte aligned) and palette (64-byte aligned).
    """
    require(len(scene.shapes) < 65536 and len(scene.nodes) < 65536, "too many records")
    names = {s.name for s in scene.shapes}
    for n in scene.nodes:
        require(n.shape_name in names, f"node {n.name} draws a missing shape {n.shape_name}")
    for m in scene.materials:
        require(m.texture is None or 0 <= m.texture < len(scene.textures), f"material {m.name} texture index")
        require(m.next_pass is None or 0 <= m.next_pass < len(scene.materials), f"material {m.name} next pass")
    for s in scene.shapes:
        count = s.vertex_count
        require(struct.unpack_from("<H", s.record, 0x54)[0] == len(s.submeshes), f"{s.name}: submesh count")
        require(struct.unpack_from("<H", s.record, 0x50)[0] == len(s.transforms), f"{s.name}: transform count")
        for k, data in enumerate(s.streams):
            if data is not None:
                require(len(data) == s.stride(k) * count, f"{s.name}: stream {k} length")
        for sub in s.submeshes:
            require(sub.material < len(scene.materials), f"{s.name}: submesh material index")
            require(len(sub.words) % 4 == 0 and len(sub.words) // 4 == sum(struct.unpack_from("<2H", sub.record, 0x7C)),
                    f"{s.name}: push word count")
            require(max_index(sub.words) < count, f"{s.name}: push words index past the vertex count")

    w = _Writer()
    w.buf += bytes(HEADER_AREA)
    w.buf[12:16] = b"SCNE"
    name_units = scene.name.encode("utf-16le") + b"\0\0"
    require(0x20 + len(name_units) <= HEADER_AREA, "scene name too long")
    w.buf[0x20:0x20 + len(name_units)] = name_units
    struct.pack_into("<i", w.buf, 0x10, 0x20 - 0x10 + 1)
    desc = w.put(bytes(DESCRIPTOR_SIZE), key="descriptor")
    struct.pack_into("<i", w.buf, 0x14, desc - 0x14 + 1)
    w.buf[desc + 4:desc + 12] = scene.descriptor_tail
    strings = {}

    def string_key(text):
        strings.setdefault(text, None)
        return ("str", text)

    w.pointer(desc + 0x00, string_key(scene.name))

    def table(records, count_at, ptr_at, key):
        struct.pack_into("<I", w.buf, desc + count_at, len(records))
        if not records:
            w.pointer(desc + ptr_at, None)
            return None
        start = w.put(b"".join(bytes(r) for r in records), key=key)
        w.pointer(desc + ptr_at, key)
        return start

    node_at = table([n.record for n in scene.nodes], 0x24, 0x28, "nodes")
    marker_at = table([m.record for m in scene.markers], 0x34, 0x38, "markers")
    tex_at = table([t.record for t in scene.textures], 0x14, 0x18, "textures")
    mat_at = table([m.record for m in scene.materials], 0x1C, 0x20, "materials")
    shape_at = table([s.record for s in scene.shapes], 0x2C, 0x30, "shapes")
    light_at = table([l.record for l in scene.lights], 0x44, 0x48, "lights")
    for count_at, ptr_at in ((0x0C, 0x10), (0x3C, 0x40)):
        struct.pack_into("<I", w.buf, desc + count_at, 0)
        w.pointer(desc + ptr_at, None)
    for i, light in enumerate(scene.lights):
        w.pointer(light_at + i * 0x50 + 0x40, string_key(light.name) if light.name is not None else None)
    # textures: +0x00 null; +0x04/+0x08 set with the video layout below
    for i, n in enumerate(scene.nodes):
        at = node_at + i * 0x60
        w.pointer(at + 0x00, string_key(n.name))
        w.pointer(at + 0x04, string_key(n.shape_name))
        struct.pack_into("<I", w.buf, at + 0x08, 0)
        w.pointer(at + 0x18, None)
        w.put(bytes(n.matrix14), key=("m14", i))
        w.pointer(at + 0x14, ("m14", i))
        w.put(bytes(n.matrix1c), key=("m1c", i))
        w.pointer(at + 0x1C, ("m1c", i))
    for i, m in enumerate(scene.markers):
        at = marker_at + i * 0x40
        w.pointer(at + 0x00, string_key(m.name))
        w.pointer(at + 0x30, string_key(m.link) if m.link is not None else None)
    for i in range(len(scene.textures)):
        w.pointer(tex_at + i * 0x20, None)
    for i, m in enumerate(scene.materials):
        at = mat_at + i * 0x80
        w.pointer(at + 0x00, string_key(m.name))
        w.pointer(at + 0x1C, ("materials", m.next_pass) if m.next_pass is not None else None)
        w.pointer(at + 0x30, ("texture", m.texture) if m.texture is not None else None)
        for f in (0x34, 0x38, 0x3C):
            w.pointer(at + f, None)
    for i in range(len(scene.textures)):
        w.targets[("texture", i)] = tex_at + i * 0x20
    for i in range(len(scene.materials)):
        w.targets[("materials", i)] = mat_at + i * 0x80
    for i, s in enumerate(scene.shapes):
        at = shape_at + i * 0x100
        w.pointer(at + 0x40, string_key(s.name))
        xform = w.put(b"".join(bytes(t.record) for t in s.transforms), key=("xf", i)) if s.transforms else None
        for k, t in enumerate(s.transforms):
            w.pointer(xform + k * 0x70 + 0x60, string_key(t.bone) if t.bone is not None else None)
        w.pointer(at + 0x64, ("xf", i) if s.transforms else None)
        subs = w.put(b"".join(bytes(sm.record) for sm in s.submeshes), key=("sub", i)) if s.submeshes else None
        w.pointer(at + 0x70, ("sub", i) if s.submeshes else None)
        # +0x60 mirrors +0x70 as in retail (only relocated when u16 +0x52 is nonzero, which is refused above)
        w.pointer(at + 0x60, ("sub", i) if s.submeshes else None)
        for f in (0x74, 0x78, 0x7C):
            w.pointer(at + f, None)
        for k, sm in enumerate(s.submeshes):
            w.put(bytes(sm.words), key=("push", i, k))
            w.pointer(subs + k * 0x80 + 0x78, ("push", i, k))
        for k, data in enumerate(s.streams):
            if data is None:
                w.pointer(at + SHAPE_STREAMS[k], None)
                continue
            w.put(bytes(data), key=("stream", i, k))
            w.pointer(at + SHAPE_STREAMS[k], ("stream", i, k))
        struct.pack_into("<I", w.buf, at + 0x58, 0)
    w.pad(16)
    for text in strings:
        w.put(text.encode("utf-16le") + b"\0\0", key=("str", text), alignment=4)
    w.pad(128)
    system_bytes = len(w.buf)
    w.resolve()
    # video part
    video = bytearray()
    for i, t in enumerate(scene.textures):
        require(len(t.pixels) == _texture_size(t) and len(t.palette) == 1024, f"texture {i} data size")
        video += bytes(align(len(video), 128) - len(video))
        pix = len(video)
        video += t.pixels
        video += bytes(align(len(video), 64) - len(video))
        pal = len(video)
        video += t.palette
        struct.pack_into("<II", w.buf, tex_at + i * 0x20 + 4, pix, pal)
    video += bytes(align(len(video), 128) - len(video))
    return bytes(w.buf) + bytes(video), system_bytes, len(video)


# --- push words ---------------------------------------------------------------------------------------------

BEGIN_END = 0x17FC
ARRAY_ELEMENT16 = 0x1800
ARRAY_ELEMENT32 = 0x1808
DRAW_ARRAYS = 0x1810
TRIANGLE_STRIP = 6
QUADS = 8


def decode_words(words):
    """[(mode, [indices])] of one submesh's push words (BEGIN_END / ARRAY_ELEMENT16/32 / DRAW_ARRAYS)."""
    vals = struct.unpack(f"<{len(words) // 4}I", words)
    i, prims, mode, cur = 0, [], None, []
    while i < len(vals):
        h = vals[i]
        method, count = h & 0x1FFC, (h >> 18) & 0x7FF
        params = vals[i + 1:i + 1 + count]
        i += 1 + count
        if method == BEGIN_END:
            require(count == 1, "BEGIN_END takes one parameter")
            if params[0] == 0:
                require(mode is not None, "END without BEGIN")
                prims.append((mode, cur))
                mode, cur = None, []
            else:
                mode = params[0]
        elif method == ARRAY_ELEMENT16:
            for p in params:
                cur += [p & 0xFFFF, p >> 16]
        elif method == ARRAY_ELEMENT32:
            cur += list(params)
        elif method == DRAW_ARRAYS:
            for p in params:
                cur += list(range(p & 0xFFFFFF, (p & 0xFFFFFF) + (p >> 24) + 1))
        else:
            raise ScneBuildError(f"unknown push method 0x{method:04x}")
    require(mode is None, "push words end inside a primitive")
    return prims


def max_index(words):
    return max((max(ix) for _m, ix in decode_words(words) if ix), default=-1)


def encode_words(mode, indices):
    """BEGIN(mode), the indices as ARRAY_ELEMENT16 pairs (a trailing odd index as ARRAY_ELEMENT32), END."""
    require(mode in (TRIANGLE_STRIP, QUADS), "only triangle strips and quads are written")
    require(all(0 <= i < 0xFFFF for i in indices), "index out of the 16-bit range")
    out = [(1 << 18) | BEGIN_END, mode]
    pairs = [indices[k] | (indices[k + 1] << 16) for k in range(0, len(indices) - 1, 2)]
    for start in range(0, len(pairs), 2047):
        chunk = pairs[start:start + 2047]
        out += [0x40000000 | (len(chunk) << 18) | ARRAY_ELEMENT16] + chunk
    if len(indices) % 2:
        out += [(1 << 18) | ARRAY_ELEMENT32, indices[-1]]
    out += [(1 << 18) | BEGIN_END, 0]
    return struct.pack(f"<{len(out)}I", *out)


def strips_to_indices(strips):
    """Join triangle strips into one strip with degenerate bridges, keeping each strip's winding."""
    out = []
    for strip in strips:
        strip = list(strip)
        if len(strip) < 3:
            continue
        if out:
            out += [out[-1], strip[0]]
            if len(out) % 2:          # keep the next strip's first triangle at an even position
                out.append(strip[0])
        out += strip
    return out


# --- geometry helpers ----------------------------------------------------------------------------------------

def _f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def bounding_sphere(points):
    """(centre xyz, radius) enclosing every point as the game stores it (binary32), with a small margin.

    The frustum test (0x215A0 -> 0x2ADC0) culls a node by its shape's sphere, so the sphere must contain every
    stored vertex: the points are rounded to binary32 first, the centre is the bounding-box centre, and the
    radius is the largest distance times (1 + 1e-5) plus four binary32 steps, checked again in double precision.
    """
    pts = [tuple(_f32(v) for v in p) for p in points]
    require(pts, "sphere of no points")
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    centre = [_f32((lo[k] + hi[k]) / 2.0) for k in range(3)]
    r = max(math.dist(centre, p) for p in pts)
    radius = _f32(r * (1.0 + 1e-5))
    for _ in range(4):
        radius = _f32(math.nextafter(radius, math.inf))
    while max(math.dist(centre, p) for p in pts) > radius:
        radius = _f32(math.nextafter(radius * 1.00001, math.inf))
    return tuple(centre), radius


def set_sphere(shape, points):
    centre, radius = bounding_sphere(points)
    struct.pack_into("<4f", shape.record, 0x00, centre[0], centre[1], centre[2], 1.0)
    struct.pack_into("<f", shape.record, 0x48, radius)
    return centre, radius


def shape_positions(shape):
    """FLOAT3 register-0 positions of a shape (stream 0, offset 0, stride 12 in every static stadium shape)."""
    require(struct.unpack_from("<I", shape.record, 0x84)[0] == 0x32, f"{shape.name}: register 0 is not FLOAT3 in stream 0")
    data = shape.streams[0]
    stride = shape.stride(0)
    return [struct.unpack_from("<3f", data, i * stride) for i in range(shape.vertex_count)]


def set_positions(shape, points, *, sphere=True):
    require(len(points) == shape.vertex_count, f"{shape.name}: position count")
    stride = shape.stride(0)
    data = bytearray(shape.streams[0])
    for i, p in enumerate(points):
        struct.pack_into("<3f", data, i * stride, *map(float, p))
    shape.streams[0] = bytes(data)
    if sphere:
        set_sphere(shape, points)


# --- new records from templates ------------------------------------------------------------------------------

def clone_material(scene, template_name, name, texture):
    src = scene.materials[scene.material_index(template_name)]
    require(src.next_pass is None, "templates with extra passes are not cloned")
    return Material(bytearray(src.record), name, texture, None)


def static_shape(template, name, positions, colours, uvs, submeshes, *, uv_constant=None):
    """A new static shape with the template's vertex declaration (FLOAT3 | D3DCOLOR, NORMSHORT2 UV, SHORT1 0).

    ``colours`` are (r, g, b, a) bytes, ``uvs`` texture coordinates in texture repeats; the UV constant (+0x30)
    is fitted to the UV range (uv = normshort2 * S + O) unless given. ``submeshes`` is [(material index,
    push words)], the words from :func:`encode_words`.
    """
    require(struct.unpack_from("<4I", template.record, 0x84)[:1] == (0x32,), "template register 0 must be FLOAT3")
    regs = struct.unpack_from("<16I", template.record, 0x84)
    require(regs[1] == 0x00080115 and regs[3] == 0x140 and regs[6] == 0x00040121, "template declaration differs")
    require(template.stride(0) == 12 and template.stride(1) == 10, "template strides differ")
    n = len(positions)
    require(0 < n < 0xFFFF and len(colours) == n and len(uvs) == n, "vertex arrays differ in length")
    if uv_constant is None:
        us = [float(u) for u, _v in uvs]
        vs = [float(v) for _u, v in uvs]
        def fit(values):
            lo, hi = min(values), max(values)
            half = max((hi - lo) / 2.0, 1e-3) * 1.0005
            return half, (hi + lo) / 2.0
        (su, ou), (sv, ov) = fit(us), fit(vs)
    else:
        su, sv, ou, ov = uv_constant
    su, sv, ou, ov = (struct.unpack("<f", struct.pack("<f", x))[0] for x in (su, sv, ou, ov))
    s0 = bytearray(12 * n)
    s1 = bytearray(10 * n)
    for i, (p, c, t) in enumerate(zip(positions, colours, uvs)):
        struct.pack_into("<3f", s0, 12 * i, *map(float, p))
        r, g, b, a = (int(x) for x in c)
        qu = max(-32767, min(32767, int(round((float(t[0]) - ou) / su * 32767.0))))
        qv = max(-32767, min(32767, int(round((float(t[1]) - ov) / sv * 32767.0))))
        struct.pack_into("<4B2hh", s1, 10 * i, b, g, r, a, qu, qv, 0)
    rec = bytearray(template.record)
    struct.pack_into("<4f", rec, 0x30, su, sv, ou, ov)
    struct.pack_into("<H", rec, 0x4C, n)
    struct.pack_into("<H", rec, 0x54, len(submeshes))
    struct.pack_into("<H", rec, 0x50, len(template.transforms))
    subs = []
    tmpl_sub = template.submeshes[0].record
    for material, words in submeshes:
        srec = bytearray(tmpl_sub)
        struct.pack_into("<H", srec, 0x00, material)
        struct.pack_into("<HH", srec, 0x7C, len(words) // 4, 0)
        subs.append(Submesh(srec, bytes(words)))
    shape = Shape(rec, name, [Transform(bytearray(t.record), t.bone) for t in template.transforms], subs,
                  [bytes(s0), bytes(s1)] + [None] * 6)
    set_sphere(shape, positions)
    return shape


def node_for(template, name, shape_name):
    return Node(bytearray(template.record), name, shape_name, bytes(template.matrix14), bytes(template.matrix1c))


