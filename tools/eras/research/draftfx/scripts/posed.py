"""Offline posing: skin an exported glTF character with a retail SMCD clip (player/referee family maps).
Library used by the draft-show preview renders."""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json, io, math, struct, dataclasses
from pathlib import Path
sys.path.insert(0, __import__("os").path.join(ROOT, r"tools\eras\research\office\scripts"))
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools"))
import numpy as np
from PIL import Image
import render as R
from mod_editor.core import nfl2k5_animation as A
from mod_editor.core import nfl2k5_animation_math as qm

INV = Path(__import__("os").path.join(ROOT, r"build\eras\draftfx\inventory.json"))
_src = None
def source():
    global _src
    if _src is None:
        _src = A.AnimationSource(Path(__import__("os").path.join(ROOT, r"original/disc/vc_53450030/0")), INV)
    return _src

def load_skinned(path, overrides=None, skin_index=0):
    """Returns (mats, prims, joints) where prims carry joints/weights; joints = [(name, parent, bind_world_pos)]."""
    path = Path(path); g = json.loads(path.read_text()); buf = (path.parent / g['buffers'][0]['uri']).read_bytes()
    def acc(i):
        a = g['accessors'][i]; v = g['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        arr = np.frombuffer(buf, dtype=dt, count=a['count'] * n, offset=off)
        return arr.reshape(a['count'], n) if n > 1 else arr
    images = []
    for im in g.get('images', []):
        v = g['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
        images.append(np.asarray(Image.open(io.BytesIO(buf[o:o + v['byteLength']])).convert('RGBA')).astype(np.float32) / 255)
    mats = []
    for m in g['materials']:
        t = m.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        img = images[g['textures'][t['index']]['source']] if t else None
        if overrides and m['name'] in overrides: img = overrides[m['name']]
        mats.append((m['name'], img))
    skins = g.get('skins', [])
    parent_of = {}
    for i, n in enumerate(g['nodes']):
        for c in n.get('children', []): parent_of[c] = i
    prims = []
    joint_tables = []
    for si, s in enumerate(skins):
        js = s['joints']; idx = {j: k for k, j in enumerate(js)}
        table = []
        for k, j in enumerate(js):
            p = parent_of.get(j); pk = idx.get(p, -1)
            t = np.array(g['nodes'][j].get('translation', [0, 0, 0]), float)
            wp = t + (table[pk][2] if pk >= 0 else 0)
            table.append((g['nodes'][j]['name'].split(':')[-1], pk, wp))
        joint_tables.append(table)
    for node in g['nodes']:
        if 'mesh' not in node: continue
        sk = node.get('skin')
        for p in g['meshes'][node['mesh']]['primitives']:
            at = p['attributes']
            pos = acc(at['POSITION']).astype(np.float64)
            uv = acc(at['TEXCOORD_0']).astype(np.float64) if 'TEXCOORD_0' in at else np.zeros((len(pos), 2))
            col = acc(at['_NFL_COLOR']).astype(np.float64) if '_NFL_COLOR' in at else np.ones((len(pos), 4))
            J = acc(at['JOINTS_0']).astype(np.int64) if 'JOINTS_0' in at else None
            W = acc(at['WEIGHTS_0']).astype(np.float64) if 'WEIGHTS_0' in at else None
            if W is not None and W.max() > 1.5: W = W / 255.0
            ind = acc(p['indices']).astype(np.int64)
            if p['mode'] == 5:
                t = [(ind[i], ind[i + 1], ind[i + 2]) if i % 2 == 0 else (ind[i + 1], ind[i], ind[i + 2]) for i in range(len(ind) - 2)]
            else:
                t = [tuple(ind[i:i + 3]) for i in range(0, len(ind) - 2, 3)]
            t = [x for x in t if len(set(x)) == 3]
            if t:
                prims.append(dict(tris=np.array(t), pos=pos, uv=uv, col=col, mat=p.get('material', 0), name=node.get('name'), J=J, W=W, skin=sk))
    return mats, prims, joint_tables

def clip_pose(identity, family, seconds):
    clip = dataclasses.replace(source().load(identity), family=family)
    seconds = min(seconds, clip.roots[0].duration * 0.999)
    return A.sample_pose(clip, seconds, loop=False), clip

def pose_world(table, pose, ref_family=None):
    """table joint order must match the pose order (25 logical joints)."""
    Rw, Pw = [], []
    for k, (name, parent, bind) in enumerate(table):
        q = qm.unit(pose[k]) if k < len(pose) else (1., 0., 0., 0.)
        if parent < 0:
            Rw.append(q); Pw.append(np.zeros(3))
        else:
            local = bind - table[parent][2]
            Pw.append(Pw[parent] + np.array(qm.rotate(Rw[parent], tuple(local))))
            Rw.append(qm.unit(qm.multiply(Rw[parent], q)))
    return Rw, Pw

def qmat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
                     [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
                     [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]])

def posed_tris(mats, prims, tables, poses, xform=None):
    """poses: per skin index a pose (or None for bind). xform: (scale, yaw_deg, offset) applied after skinning."""
    out = []
    for p in prims:
        pos = p['pos']
        if p['J'] is not None and p['skin'] is not None and poses.get(p['skin']) is not None:
            table = tables[p['skin']]
            Rw, Pw = pose_world(table, poses[p['skin']])
            Ms = [qmat(r) for r in Rw]
            new = np.zeros_like(pos)
            for c in range(p['J'].shape[1]):
                j = p['J'][:, c]; w = p['W'][:, c][:, None]
                binds = np.array([table[k][2] for k in j])
                rot = np.stack([Ms[k] for k in j])
                local = pos - binds
                new += w * (np.einsum('nij,nj->ni', rot, local) + np.stack([Pw[k] for k in j]))
            pos = new
        if xform:
            s, yaw, off = xform; a = math.radians(yaw)
            Ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
            pos = (pos @ Ry.T) * s + np.array(off)
        out.append((p['tris'], pos, p['uv'], p['col'], p['mat'], p['name']))
    return out

def hand_positions(tables, pose, skin=0, xform=None):
    table = tables[skin]; Rw, Pw = pose_world(table, pose)
    names = [t[0] for t in table]; res = {}
    for n in ('lhand', 'rhand', 'head', 'thorax'):
        if n in names:
            p = Pw[names.index(n)]
            if xform:
                s, yaw, off = xform; a = math.radians(yaw)
                Ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
                p = (Ry @ p) * s + np.array(off)
            res[n] = p
    return res

def follow_pose_world(table, body_table, body_pose):
    """World transforms for a secondary skin (e.g. a head) whose joints are named like the body's."""
    bRw, bPw = pose_world(body_table, body_pose)
    bnames = [t[0] for t in body_table]
    Rw, Pw = [], []
    for k, (name, parent, bind) in enumerate(table):
        if name in bnames:
            i = bnames.index(name); Rw.append(bRw[i]); Pw.append(bPw[i] + np.array(qm.rotate(bRw[i], tuple(bind - body_table[i][2]))))
        elif parent >= 0:
            Rw.append(Rw[parent]); Pw.append(Pw[parent] + np.array(qm.rotate(Rw[parent], tuple(bind - table[parent][2]))))
        else:
            Rw.append((1., 0., 0., 0.)); Pw.append(bind.copy())
    return Rw, Pw

def posed_tris2(mats, prims, tables, body_pose, body_skin=0, hide=(), xform=None):
    """Body skin posed by body_pose; any other skin follows by joint name."""
    out = []
    worlds = {}
    for si, tb in enumerate(tables):
        worlds[si] = pose_world(tb, body_pose) if si == body_skin or len(tb) == len(tables[body_skin]) else follow_pose_world(tb, tables[body_skin], body_pose)
    for p in prims:
        if p['name'] in hide: continue
        pos = p['pos']
        if p['J'] is not None and p['skin'] is not None:
            table = tables[p['skin']]; Rw, Pw = worlds[p['skin']]
            Ms = [qmat(r) for r in Rw]; new = np.zeros_like(pos)
            for c in range(p['J'].shape[1]):
                j = p['J'][:, c]; w = p['W'][:, c][:, None]
                binds = np.array([table[k][2] for k in j]); rot = np.stack([Ms[k] for k in j])
                new += w * (np.einsum('nij,nj->ni', rot, pos - binds) + np.stack([Pw[k] for k in j]))
            pos = new
        if xform:
            s, yaw, off = xform; a = math.radians(yaw)
            Ry = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
            pos = (pos @ Ry.T) * s + np.array(off)
        out.append((p['tris'], pos, p['uv'], p['col'], p['mat'], p['name']))
    return out
