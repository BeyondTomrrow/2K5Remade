"""Reproducible six-order seam comparison and partial-frame candidate report.

Writes research artifacts under work/ only. Exact NumPy nearest-neighbor search.
Near-pair selection is the UNION across all six orders, not just the winner.
"""
import json
from pathlib import Path
import numpy as np
from skel_seam_fit import (ROOT, load, bone_matrices, transformed, cross_seams,
                           summary, cluster_points, partial_matrices)

ORDERS = ['XYZ','XZY','YXZ','YZX','ZXY','ZYX']
MODES = ['linear_world','linear_pivot','euler_trs','euler_pivot','slerp_trs','slerp_pivot']


def rigid(model):
    coordinates, union, populations = {}, set(), {}
    for order in ORDERS:
        points = transformed(model, bone_matrices(model, order))
        coordinates[order] = points
        ix, distances, match = cross_seams(model, points, rigid=True)
        populations[order] = summary(distances)
        union.update(tuple(sorted((int(a), int(b)))) for a,b in zip(ix[distances<.00015], match[distances<.00015]))
    pairs = sorted(union)
    rows = model['rows']
    return {'selection': 'union of undirected cross-node, cross-cluster rigid boundary nearest pairs within 0.00015 in ANY order',
            'boundary_populations': populations,
            'pairs': [{'a_node_vertex_cluster_bone': rows[a,:4].astype(int).tolist(),
                       'b_node_vertex_cluster_bone': rows[b,:4].astype(int).tolist(),
                       'a_local_xyz': rows[a,5:8].tolist(), 'b_local_xyz': rows[b,5:8].tolist(),
                       'distance_by_order': {order: float(np.linalg.norm(p[a]-p[b])) for order,p in coordinates.items()}}
                      for a,b in pairs],
            'fixed_union_pair_statistics': {order: summary([np.linalg.norm(p[a]-p[b]) for a,b in pairs])
                                            for order,p in coordinates.items()}}


def main():
    result = {'definitions': {
        'coordinates': 'player source Y-up, unconverted; distances are source units',
        'rotations': 'column vectors, order letters are explicit matrix product; raw*2pi/65536',
        'rigid': 'Clst raw weight ==4096; helpers and partial vertices excluded',
        'boundary': 'vertices on edges incident to exactly one nondegenerate triangle within each source part',
        'broad_metric': 'nearest point on a different node AND different cluster; all boundary points, NOT known seam correspondences',
        'warning': 'near-pair selection is exploratory; duplicate LOD rigs are not independent skeleton observations',
        'coordinate_invariance': 'common orthogonal root conversion preserves all distances, so Y-up/Z-up cannot choose an Euler order'},
        'files': {}}
    for name in ['00000004.tmd','00000005.tmd','00000006.tmd','00000007.tmd','00000011.tmd','00000012.tmd','00000021.tmd']:
        model = load(ROOT/'outputs/plydata_extracted'/name)
        entry = {'rigid_six_orders': rigid(model), 'partial_ZYX': {}}
        if name in ('00000004.tmd','00000005.tmd','00000011.tmd'):
            for mode in MODES:
                points = cluster_points(model, partial_matrices(model, 'ZYX', mode))
                _, distances, _ = cross_seams(model, points)
                entry['partial_ZYX'][mode] = summary(distances)
        result['files'][name] = entry
        print(name, 'rigid pairs', len(entry['rigid_six_orders']['pairs']), flush=True)
    # The layout/draw attachment audit includes every fixture, independently of
    # selecting the files above for geometric nearest-neighbor experiments.
    result['all_fixture_attachment_audit'] = []
    for path in sorted((ROOT/'outputs/plydata_extracted').glob('*.tmd')):
        model = load(path)
        result['all_fixture_attachment_audit'].append({
            'file': path.name, 'render_vertices': len(model['rows']),
            'multi_cluster_vertices': sum(n['vertices_with_multiple_clusters'] for n in model['draw_audit']),
            'multi_palette_nodes': [n for n in model['draw_audit'] if n['palette_count']>1]})
    path = ROOT/'work/player_seam_statistics.json'
    path.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(path)


if __name__ == '__main__':
    main()
