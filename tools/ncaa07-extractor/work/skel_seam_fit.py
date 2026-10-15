"""Resolve draw palettes and compare rigid cross-bone seams (NumPy only).

No main application imports; no file writes. JSON results go to stdout.
Matrix order names are explicit column-vector products, e.g. XYZ=Rx@Ry@Rz.
"""
import argparse
from collections import Counter
import itertools
import json
import math
from pathlib import Path
import struct
import numpy as np
from skel_probe import decode, require
from skel_rotation_experiment import euler

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    data = path.read_bytes()
    skeleton = decode(data)
    sections = {data[a:a+4]: struct.unpack_from('<III', data, a+4)
                for a in range(16, 16+struct.unpack_from('<H', data, 8)[0]*16, 16)}
    cp, cn = struct.unpack_from('<II', data, sections[b'Clst'][0])
    clusters = np.array([struct.unpack_from('<BBH', data, cp+4*i) for i in range(cn)])
    nptr, nc, sptr, sc = struct.unpack_from('<4I', data, sections[b'Geom'][0])
    rows, faces, nodes, audit = [], [], [], []
    for node in range(nc):
        draw, palette_header, material, parent, flags, base, *tail = struct.unpack_from('<II4H4B', data, nptr+node*20)
        if not draw:
            continue
        pc = struct.unpack_from('<I', data, palette_header)[0]
        palettes = []
        resident = {}
        for batch in range(pc):
            pp = struct.unpack_from('<I', data, palette_header+4+4*batch)[0]
            count = struct.unpack_from('<I', data, pp)[0]
            resident.update(dict(struct.unpack_from('<hH', data, pp+4+4*i) for i in range(count)))
            palettes.append(dict(resident))
        vp, vc, stride, semantic = struct.unpack_from('<IHHI', data, sptr+12*base)
        require(stride == 24 and semantic == 18, 'unexpected vertex format')
        vertices = np.array([struct.unpack_from('<6f', data, vp+12+24*i) for i in range(vc)])
        ip, ic, stride, semantic = struct.unpack_from('<IHHI', data, sptr+12*(base+2))
        require(ic == vc and stride == 4 and semantic == 0, 'unexpected selector format')
        selectors = np.array(struct.unpack_from('<'+str(ic)+'f', data, ip+12))
        require(np.all(selectors == np.rint(selectors/3)*3), 'fractional selector')
        slots = np.rint(selectors/3).astype(int)
        draws = struct.unpack_from('<I', data, draw)[0]
        mapping = {}
        vertex_clusters = {}
        batches_used = Counter()
        for di in range(draws):
            dp = struct.unpack_from('<I', data, draw+4+di*4)[0]
            triangles, batch, z0, z1, z2 = struct.unpack_from('<HHIII', data, dp)
            require(not (z0 or z1 or z2) and batch < pc, 'invalid draw header/batch')
            indices = struct.unpack_from('<'+str(triangles+2)+'H', data, dp+16)
            out = []
            for vi in indices:
                require(vi < vc and slots[vi] in palettes[batch],
                        f'unmapped draw vertex node={node} draw={di} batch={batch} vi={vi} vc={vc} slot={slots[vi] if vi < vc else None} palette={list(palettes[batch])}')
                cluster = palettes[batch][slots[vi]]
                require(cluster < cn, 'invalid cluster')
                vertex_clusters.setdefault(vi, set()).add(cluster)
                key = (vi, cluster)
                if key not in mapping:
                    mapping[key] = len(rows)
                    rows.append((node, vi, cluster, int(clusters[cluster, 0]),
                                 int(clusters[cluster, 2]), *vertices[vi]))
                out.append(mapping[key])
            for k in range(triangles):
                face = (out[k+1], out[k], out[k+2]) if k & 1 else (out[k], out[k+1], out[k+2])
                if len(set(face)) == 3:
                    faces.append(face)
            batches_used[batch] += 1
        nodes.append(node)
        audit.append({'node': node, 'palette_count': pc, 'draws_per_batch': dict(batches_used),
                      'referenced_vertices': len(vertex_clusters),
                      'vertices_with_multiple_clusters': sum(len(v)>1 for v in vertex_clusters.values())})
    return {'path': path, 'skeleton': skeleton, 'clusters': clusters,
            'rows': np.array(rows), 'faces': np.array(faces), 'draw_audit': audit}


def bone_matrices(model, order, factor=math.tau/65536):
    matrices = []
    for bone in model['skeleton']['bones']:
        local = np.eye(4)
        local[:3, :3] = np.array(euler(bone['rotation_i16_raw'], order, factor)) @ np.diag(bone['scale_candidate'])
        local[:3, 3] = bone['local_translation']
        parent = bone['parent']
        matrices.append(local if parent is None else matrices[parent] @ local)
    return np.array(matrices)


def transformed(model, matrices):
    rows = model['rows']
    return np.einsum('nij,nj->ni', matrices[rows[:, 3].astype(int), :3, :3], rows[:, 5:8]) + matrices[rows[:, 3].astype(int), :3, 3]


def partial_matrices(model, order, mode):
    worlds = bone_matrices(model, order)
    bones = model['skeleton']['bones']
    result = []
    for bone_index, predecessor, raw_weight in model['clusters']:
        bone = bones[bone_index]
        weight = raw_weight/4096
        target = worlds[bone_index]
        parent = np.eye(4) if bone['parent'] is None else worlds[bone['parent']]
        if weight == 1 or bone['parent'] is None:
            result.append(target)
            continue
        if mode in ('linear_world', 'linear_pivot'):
            mixed = parent*(1-weight)+target*weight
            if mode == 'linear_pivot':
                mixed[:3, 3] = target[:3, 3]
            result.append(mixed)
            continue
        local = np.eye(4)
        local[:3, 3] = np.array(bone['local_translation']) * (weight if mode.endswith('_trs') else 1)
        if mode.startswith('euler'):
            rotation = np.array(euler(bone['rotation_i16_raw'], order, math.tau/65536*weight))
        else:
            full = np.array(euler(bone['rotation_i16_raw'], order, math.tau/65536))
            angle = math.acos(np.clip((np.trace(full)-1)/2, -1, 1))
            if angle < 1e-10:
                rotation = np.eye(3)
            else:
                axis = np.array([full[2, 1]-full[1, 2], full[0, 2]-full[2, 0], full[1, 0]-full[0, 1]])/(2*math.sin(angle))
                x, y, z = axis
                skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
                rotation = np.eye(3)+math.sin(angle*weight)*skew+(1-math.cos(angle*weight))*(skew@skew)
        local[:3, :3] = rotation @ np.diag(bone['scale_candidate'])
        result.append(parent@local)
    return np.array(result)


def cluster_points(model, matrices):
    rows = model['rows']
    return np.einsum('nij,nj->ni', matrices[rows[:, 2].astype(int), :3, :3], rows[:, 5:8]) + matrices[rows[:, 2].astype(int), :3, 3]


def boundary_indices(model):
    counts = Counter()
    for a, b, c in model['faces']:
        for edge in ((a,b),(b,c),(c,a)):
            counts[tuple(sorted(edge))] += 1
    return np.array(sorted({i for edge, count in counts.items() if count == 1 for i in edge}), dtype=int)


def cross_seams(model, points, boundary=True, rigid=False):
    rows = model['rows']
    indices = boundary_indices(model) if boundary else np.arange(len(rows))
    if rigid:
        indices = indices[rows[indices, 4] == 4096]
    # Deduplicate each cluster's local coordinates; keep provenance node for exclusion.
    _, keep = np.unique(rows[indices][:, [0,2,5,6,7]], axis=0, return_index=True)
    indices = indices[keep]
    p = points[indices]
    distances, matches = [], []
    for start in range(0, len(p), 96):
        ii = indices[start:start+96]
        delta = p[start:start+96,None,:]-p[None,:,:]
        squared = np.einsum('ijk,ijk->ij', delta, delta)
        invalid = (rows[ii,0,None] == rows[indices,0][None,:]) | (rows[ii,2,None] == rows[indices,2][None,:])
        squared[invalid] = np.inf
        match = np.argmin(squared, axis=1)
        distances.extend(np.sqrt(squared[np.arange(len(match)),match]))
        matches.extend(indices[match])
    return indices, np.array(distances), np.array(matches)


def nearest(a, b):
    if not len(a) or not len(b):
        return np.array([]), np.array([], dtype=int)
    distances, indices = [], []
    # Exact nearest neighbor, bounded working memory. No SciPy dependency.
    for start in range(0, len(a), 128):
        delta = a[start:start+128, None, :] - b[None, :, :]
        squared = np.einsum('ijk,ijk->ij', delta, delta)
        ix = np.argmin(squared, axis=1)
        distances.extend(np.sqrt(squared[np.arange(len(ix)), ix]))
        indices.extend(ix)
    return np.array(distances), np.array(indices)


def summary(values):
    values = np.asarray(values)
    return {'count': len(values), 'min': float(np.min(values)),
            'median': float(np.median(values)), 'p90': float(np.quantile(values, .9)),
            'rms': float(np.sqrt(np.mean(values**2))),
            'below_1e-4': int(np.sum(values < 1e-4)),
            'below_1e-3': int(np.sum(values < 1e-3)),
            'below_1e-2': int(np.sum(values < 1e-2))} if len(values) else {'count': 0}


def rigid_pairs(model):
    rows = model['rows']
    eligible = rows[:, 4] == 4096
    groups = {int(b): np.flatnonzero(eligible & (rows[:, 3] == b)) for b in np.unique(rows[eligible, 3])}
    # Ancestor/descendant joints are physically adjacent even when an intermediate
    # helper bone has no vertices. Include up to three links, selected before fitting.
    pairs = []
    bones = model['skeleton']['bones']
    for child in groups:
        parent = child
        for depth in range(3):
            parent = bones[parent]['parent']
            if parent is None:
                break
            if parent in groups:
                pairs.append((parent, child, groups[parent], groups[child]))
    return pairs


def rigid_experiment(model):
    pairs = rigid_pairs(model)
    results = []
    for permutation in itertools.permutations('XYZ'):
        order = ''.join(permutation)
        points = transformed(model, bone_matrices(model, order))
        combined, detail, correspondences = [], [], []
        for parent, child, ia, ib in pairs:
            # Unique geometric vertices avoid UV/morph copies dominating the metric.
            _, ua = np.unique(model['rows'][ia, 5:8], axis=0, return_index=True)
            _, ub = np.unique(model['rows'][ib, 5:8], axis=0, return_index=True)
            ia, ib = ia[ua], ib[ub]
            dist, match = nearest(points[ia], points[ib])
            reverse, _ = nearest(points[ib], points[ia])
            # Fixed lowest-k cross-part distances: not the median of whole surfaces.
            k = min(12, len(dist), len(reverse))
            low = np.r_[np.sort(dist)[:k], np.sort(reverse)[:k]]
            combined.extend(low)
            detail.append({'parent': model['skeleton']['bones'][parent]['name'],
                           'child': model['skeleton']['bones'][child]['name'],
                           'lowest_k_each_direction': k, **summary(low)})
            for i in np.argsort(dist)[:k]:
                correspondences.append((int(ia[i]), int(ib[match[i]])))
        results.append({'order': order, 'seams': summary(combined), 'pairs': detail,
                        'correspondences': correspondences})
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', default=['00000004.tmd', '00000005.tmd', '00000011.tmd'])
    args = parser.parse_args()
    result = {}
    for name in args.files:
        path = Path(name)
        if not path.exists():
            path = ROOT / 'outputs/plydata_extracted' / name
        model = load(path)
        experiments = rigid_experiment(model)
        for exp in experiments:
            del exp['correspondences']
        result[path.name] = {'draws': model['draw_audit'], 'vertices': len(model['rows']),
                             'rigid_vertices': int(np.sum(model['rows'][:, 4] == 4096)),
                             'rigid_experiments': experiments}
    print(json.dumps(result, indent=2))
