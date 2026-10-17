"""Software preview of an NFL 2K5 stadium bundle (no GPU, no game): the stadium, field and cityscape scenes
rasterised with their own textures, vertex colours and UV constants.

    python tools/eras/stadiums/render_preview.py BUNDLE.iff OUT_PREFIX [--size 1280x720]

Writes OUT_PREFIX_<view>.png for a few fixed cameras (sideline, end zone, blimp, peristyle). Positions are the
game's centimetres (x across, y up, z along the field). The crowd material (the game's runtime atlas) is drawn as
a speckled crowd; jumbo_tron as a dark screen.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
for p in (HERE, HERE.parents[1] / "team-creator" / "vendor"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))
import nfl_txtr as T  # noqa: E402
import scne as S  # noqa: E402
import p8  # noqa: E402

VIEWS = {
    "sideline": dict(eye=(-3200, 1500, -900), target=(3000, 800, 600), fov=62),
    "endzone": dict(eye=(0, 1400, -6500), target=(0, 900, 9000), fov=62),
    "blimp": dict(eye=(-24000, 17000, -21000), target=(0, 0, 1500), fov=48),
    "peristyle": dict(eye=(2500, 600, 3000), target=(-1500, 3200, 14000), fov=66),
    "high_sideline": dict(eye=(9000, 5200, 0), target=(-2000, 1000, 0), fov=70),
    "flyover": dict(eye=(0, 9000, -27000), target=(0, 2500, 20000), fov=60),       # the backdrop over the rim
    "midfield": dict(eye=(-5200, 3600, -2600), target=(0, 0, 600), fov=60),         # 50-yard line, end zone
}


# field overlays not seen in regular-season games in game (the playoff banner and conference shields)
PLAYOFF_ONLY = {"playoff_logo", "AFC_shield", "NFC_shield"}


def crowd_texture():
    rng = np.random.default_rng(7)
    t = np.zeros((64, 64, 4), np.uint8)
    base = rng.integers(40, 230, (64, 64, 3))
    tint = np.array([200, 160, 60])                     # gold-ish
    mix = rng.random((64, 64, 1)) < 0.35
    t[..., :3] = np.where(mix, tint, base)
    t[..., 3] = 255
    return t


def scene_triangles(sc, skip_shapes=()):
    """Yields (positions Nx3, uvs Nx2, colours Nx4, tri index array Mx3, texture RGBA or None, material name)."""
    tex_cache = {}
    for s in sc.shapes:
        if s.name in skip_shapes:
            continue
        P = np.array(S.shape_positions(s), np.float64)
        su, sv, ou, ov = struct.unpack_from("<4f", s.record, 0x30)
        n = s.vertex_count
        if s.streams[1] is None or s.stride(1) < 10:
            continue
        st = s.stride(1)
        raw = np.frombuffer(s.streams[1], np.uint8).reshape(n, st)
        col = raw[:, [2, 1, 0, 3]].astype(np.float32) / 255.0
        q = raw[:, 4:8].copy().view(np.int16).reshape(n, 2).astype(np.float64)
        uv = np.stack([q[:, 0] / 32767.0 * su + ou, q[:, 1] / 32767.0 * sv + ov], axis=1)
        for sm in s.submeshes:
            mat = sc.materials[sm.material]
            if mat.name in PLAYOFF_ONLY:
                continue
            tris = []
            for mode, ix in S.decode_words(sm.words):
                if mode == S.TRIANGLE_STRIP:
                    for k in range(len(ix) - 2):
                        a, b, c = ix[k], ix[k + 1], ix[k + 2]
                        if k & 1:
                            a, b = b, a
                        if a != b and b != c and a != c:
                            tris.append((a, b, c))
                elif mode == S.QUADS:
                    for k in range(0, len(ix) - 3, 4):
                        tris += [(ix[k], ix[k + 1], ix[k + 2]), (ix[k], ix[k + 2], ix[k + 3])]
            if not tris:
                continue
            if mat.name == "crowd":
                tex = crowd_texture()
            elif mat.name == "jumbo_tron":
                tex = np.full((8, 8, 4), (20, 30, 60, 255), np.uint8)
            elif mat.texture is not None:
                if mat.texture not in tex_cache:
                    t = sc.textures[mat.texture]
                    tex_cache[mat.texture] = p8.decode(t.record, t.pixels, t.palette)
                tex = tex_cache[mat.texture]
            else:
                tex = None
            yield P, uv, col, np.array(tris), tex, mat.name


def look_at(eye, target, fov, w, h):
    eye, target = np.array(eye, float), np.array(target, float)
    f = target - eye
    f /= np.linalg.norm(f)
    up = np.array([0.0, 1.0, 0.0])
    r = np.cross(up, f)            # the game's frame is right-handed: looking +z with y up, +x is on the left
    r /= np.linalg.norm(r)
    u = np.cross(f, r)
    foc = (w / 2) / np.tan(np.radians(fov) / 2)
    return eye, r, u, f, foc


def render(groups, view, w, h, sky=(150, 185, 225)):
    eye, r, u, f, foc = look_at(view["eye"], view["target"], view["fov"], w, h)
    img = np.zeros((h, w, 3), np.float32)
    img[:] = np.array(sky, np.float32) / 255.0
    # a simple sky gradient
    grad = np.linspace(0.75, 1.1, h)[:, None, None]
    img *= grad
    zbuf = np.full((h, w), np.inf, np.float32)
    for P, uv, col, tris, tex, name in groups:
        d = P - eye
        cx, cy, cz = d @ r, d @ u, d @ f
        near = 20.0
        for a, b, c in tris:
            zs = (cz[a], cz[b], cz[c])
            if min(zs) < near:
                continue
            xs = [w / 2 - foc * cx[i] / cz[i] for i in (a, b, c)]
            ys = [h / 2 - foc * cy[i] / cz[i] for i in (a, b, c)]
            x0, x1 = int(max(0, np.floor(min(xs)))), int(min(w - 1, np.ceil(max(xs))))
            y0, y1 = int(max(0, np.floor(min(ys)))), int(min(h - 1, np.ceil(max(ys))))
            if x0 > x1 or y0 > y1:
                continue
            area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if abs(area) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
            w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
            w2 = 1.0 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            iz = w0 / zs[0] + w1 / zs[1] + w2 / zs[2]
            depth = 1.0 / np.maximum(iz, 1e-12)
            sub = zbuf[y0:y1 + 1, x0:x1 + 1]
            vis = inside & (depth < sub)
            if not vis.any():
                continue
            # perspective-correct attributes
            p0, p1, p2 = w0 / zs[0] * depth, w1 / zs[1] * depth, w2 / zs[2] * depth
            if tex is not None:
                tu = p0 * uv[a, 0] + p1 * uv[b, 0] + p2 * uv[c, 0]
                tv = p0 * uv[a, 1] + p1 * uv[b, 1] + p2 * uv[c, 1]
                th, tw = tex.shape[:2]
                px = np.floor(np.mod(tu, 1.0) * tw).astype(int) % tw
                py = np.floor(np.mod(tv, 1.0) * th).astype(int) % th
                texel = tex[py, px].astype(np.float32) / 255.0
                vis &= texel[..., 3] > 0.5
                rgb = texel[..., :3]
            else:
                rgb = np.full(gx.shape + (3,), 0.6, np.float32)
            vc = p0[..., None] * col[a, :3] + p1[..., None] * col[b, :3] + p2[..., None] * col[c, :3]
            out = np.clip(rgb * vc * 1.15, 0, 1)
            sub[vis] = depth[vis]
            region = img[y0:y1 + 1, x0:x1 + 1]
            region[vis] = out[vis]
    return (np.clip(img, 0, 1) * 255).astype(np.uint8)


def load_groups(bundle, skip_field=False, skip_city=False):
    d = Path(bundle).read_bytes()
    groups = []
    for c in T.parse_chunks(d, allow_trailing=True):
        if c.kind != "SCNE":
            continue
        body, _ = T.decode_chunk(d, c)
        name = S._utf16z(body, S._rel(body, 0x10))
        if name not in ("stadium", "field", "cityscape") or (skip_field and name == "field") or \
                (skip_city and name == "cityscape"):
            continue
        sc = S.parse(body, c.system_bytes, secondary=True)
        groups += list(scene_triangles(sc))
    # the field scene's grass (its texture lives in a loose TXTR chunk): draw it plain green under everything
    return groups


def main(argv=None):
    from PIL import Image
    ap = argparse.ArgumentParser()
    ap.add_argument("bundle")
    ap.add_argument("out_prefix")
    ap.add_argument("--size", default="1280x720")
    ap.add_argument("--views", nargs="*", default=list(VIEWS))
    a = ap.parse_args(argv)
    w, h = (int(x) for x in a.size.split("x"))
    groups = load_groups(a.bundle)
    for v in a.views:
        img = render(groups, VIEWS[v], w, h)
        out = f"{a.out_prefix}_{v}.png"
        Image.fromarray(img, "RGB").save(out)
        print(out, flush=True)


if __name__ == "__main__":
    main()
