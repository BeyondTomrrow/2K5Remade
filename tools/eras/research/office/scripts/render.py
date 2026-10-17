"""Tiny numpy rasteriser for the exported office glTFs (preview only)."""
import json, struct, sys, math, io
from pathlib import Path
import numpy as np
from PIL import Image

def load_gltf(path, overrides):
    path = Path(path)
    g = json.loads(path.read_text())
    buf = (path.parent / g['buffers'][0]['uri']).read_bytes()
    def acc(i):
        a = g['accessors'][i]; v = g['bufferViews'][a['bufferView']]
        n = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        arr = np.frombuffer(buf, dtype=dt, count=a['count']*n, offset=off)
        return arr.reshape(a['count'], n) if n > 1 else arr
    images = []
    for im in g.get('images', []):
        v = g['bufferViews'][im['bufferView']]
        o = v.get('byteOffset', 0)
        images.append(np.asarray(Image.open(io.BytesIO(buf[o:o+v['byteLength']])).convert('RGBA')).astype(np.float32)/255)
    mats = []
    for m in g['materials']:
        t = m.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        img = images[g['textures'][t['index']]['source']] if t else None
        if m['name'] in overrides: img = overrides[m['name']]
        mats.append((m['name'], img))
    tris = []
    for node in g['nodes']:
        if 'mesh' not in node: continue
        for p in g['meshes'][node['mesh']]['primitives']:
            pos = acc(p['attributes']['POSITION']).astype(np.float64)
            uv = acc(p['attributes']['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in p['attributes'] else np.zeros((len(pos),2))
            col = acc(p['attributes']['_NFL_COLOR']).astype(np.float64) if '_NFL_COLOR' in p['attributes'] else np.ones((len(pos),4))
            idx = acc(p['indices']).astype(np.int64)
            if p['mode'] == 5:
                t = [(idx[i], idx[i+1], idx[i+2]) if i % 2 == 0 else (idx[i+1], idx[i], idx[i+2]) for i in range(len(idx)-2)]
            else:
                t = [tuple(idx[i:i+3]) for i in range(0, len(idx)-2, 3)]
            t = [x for x in t if len(set(x)) == 3]
            if t:
                tris.append((np.array(t), pos, uv, col, p.get('material', 0), node.get('name')))
    return mats, tris

def look_at(eye, target, up=(0,1,0)):
    eye = np.array(eye, float); f = np.array(target, float) - eye; f /= np.linalg.norm(f)
    r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
    return eye, r, u, f

def render(mats, tris, eye, target, W=640, H=480, fov_deg=46.4, light=1.7, hide=()):
    eye, r, u, f = look_at(eye, target)
    tanh = math.tan(math.radians(fov_deg)/2)
    color = np.zeros((H, W, 3)); color[:] = (0.08, 0.08, 0.1)
    depth = np.full((H, W), np.inf)
    near = 5.0
    def proj(v):
        x, y, z = v[0], v[1], v[2]
        return (x / z) / (tanh * W / H) * (W/2) + W/2, -(y / z) / tanh * (H/2) + H/2
    for t, pos, uv, col, mi, nname in tris:
        if nname in hide: continue
        name, img = mats[mi]
        rel = pos - eye
        cam = np.stack([rel @ r, rel @ u, rel @ f], axis=1)
        attr = np.concatenate([cam, uv, col[:, :4]], axis=1)   # x y z u v r g b a
        for a, b, c in t:
            poly = [attr[a], attr[b], attr[c]]
            zs = [p[2] for p in poly]
            if min(zs) < near:
                if max(zs) < near: continue
                out = []
                for i in range(3):
                    p, q = poly[i], poly[(i+1) % 3]
                    pin, qin = p[2] >= near, q[2] >= near
                    if pin: out.append(p)
                    if pin != qin:
                        s = (near - p[2]) / (q[2] - p[2]); out.append(p + (q - p) * s)
                poly = out
                if len(poly) < 3: continue
            for k in range(1, len(poly) - 1):
                _raster(color, depth, img, light, W, H, proj, poly[0], poly[k], poly[k+1])
    return Image.fromarray((color*255).astype(np.uint8))

def _raster(color, depth, img, light, W, H, proj, A, B, C):
    (xa, ya), (xb, yb), (xc, yc) = proj(A), proj(B), proj(C)
    za, zb, zc = A[2], B[2], C[2]
    minx = max(int(math.floor(min(xa, xb, xc))), 0); maxx = min(int(math.ceil(max(xa, xb, xc))), W-1)
    miny = max(int(math.floor(min(ya, yb, yc))), 0); maxy = min(int(math.ceil(max(ya, yb, yc))), H-1)
    if minx > maxx or miny > maxy: return
    area = (xb-xa)*(yc-ya) - (xc-xa)*(yb-ya)
    if abs(area) < 1e-9: return
    px, py = np.meshgrid(np.arange(minx, maxx+1) + 0.5, np.arange(miny, maxy+1) + 0.5)
    w0 = ((xb-px)*(yc-py) - (xc-px)*(yb-py)) / area
    w1 = ((xc-px)*(ya-py) - (xa-px)*(yc-py)) / area
    w2 = 1 - w0 - w1
    m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
    if not m.any(): return
    w0, w1, w2 = w0[m], w1[m], w2[m]
    iz = w0/za + w1/zb + w2/zc
    zz = 1/iz
    ys, xs = np.nonzero(m)
    gy = ys + miny; gx = xs + minx
    closer = zz < depth[gy, gx]
    if not closer.any(): return
    gy, gx, zz, w0, w1, w2 = gy[closer], gx[closer], zz[closer], w0[closer], w1[closer], w2[closer]
    p0 = (w0/za)*zz; p1 = (w1/zb)*zz; p2 = (w2/zc)*zz
    at = p0[:, None]*A[None, 3:] + p1[:, None]*B[None, 3:] + p2[:, None]*C[None, 3:]
    vc = at[:, 2:6]
    if img is not None:
        h, w = img.shape[:2]
        ix = (np.floor(at[:, 0] * w).astype(int)) % w; iy = (np.floor(at[:, 1] * h).astype(int)) % h
        tex = img[iy, ix]
        keep = tex[:, 3] > 0.5
        if not keep.any(): return
        gy, gx, zz, tex, vc = gy[keep], gx[keep], zz[keep], tex[keep], vc[keep]
        rgb = tex[:, :3] * np.clip(vc[:, :3]*light, 0, 1.6)
    else:
        rgb = np.clip(vc[:, :3]*light, 0, 1) * 0.7
    depth[gy, gx] = zz
    color[gy, gx] = np.clip(rgb, 0, 1)

if __name__ == '__main__':
    pass
