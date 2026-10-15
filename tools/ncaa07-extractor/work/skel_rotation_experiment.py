"""Compare rotation hypotheses; results are NOT validated bind matrices.

Run: python work/skel_rotation_experiment.py
Column vectors, world = parent_world @ T @ R @ S; positive rotation is
right-handed. Each Euler order spells out matrix multiplication order.
"""
import itertools
import json
import math
from pathlib import Path
from skel_probe import decode


def mm(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mv(a, b):
    return [sum(a[i][k] * b[k] for k in range(3)) for i in range(3)]


def identity():
    return [[1, 0, 0], [0, 1, 0], [0, 0, 1]]


def euler(raw, order, factor):
    x, y, z = [v * factor for v in raw]
    cx, cy, cz = map(math.cos, (x, y, z))
    sx, sy, sz = map(math.sin, (x, y, z))
    axes = {'X': [[1, 0, 0], [0, cx, -sx], [0, sx, cx]],
            'Y': [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]],
            'Z': [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]}
    result = identity()
    for axis in order:
        result = mm(result, axes[axis])
    return result


def quaternion(raw):
    x, y, z = [v / 32767 for v in raw]
    squared = x*x + y*y + z*z
    if squared > 1:
        raise ValueError('quaternion vector exceeds unit length')
    w = math.sqrt(1 - squared)
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]


def world_positions(bones, rotation):
    worlds = []
    positions = []
    for bone in bones:
        local = rotation(bone['rotation_i16_raw'])
        scale = bone['scale_candidate']
        local = [[local[i][j] * scale[j] for j in range(3)] for i in range(3)]
        parent = bone['parent']
        translation = bone['local_translation']
        if parent is None:
            worlds.append(local)
            positions.append(translation)
        else:
            offset = mv(worlds[parent], translation)
            positions.append([positions[parent][i] + offset[i] for i in range(3)])
            worlds.append(mm(worlds[parent], local))
    return {bone['name']: positions[i] for i, bone in enumerate(bones)}


if __name__ == '__main__':
    path = Path(__file__).resolve().parents[1] / 'outputs/plydata_extracted/00000003.tmd'
    bones = decode(path.read_bytes())['bones']
    candidates = {}
    for order in itertools.permutations('XYZ'):
        order = ''.join(order)
        candidates['euler_turn65536_' + order] = world_positions(
            bones, lambda raw, order=order: euler(raw, order, math.tau / 65536))
    candidates['quat_xyz32767_positive_w'] = world_positions(bones, quaternion)
    candidates['euler_radians16384_XYZ'] = world_positions(
        bones, lambda raw: euler(raw, 'XYZ', 1 / 16384))
    selected = ['rankle', 'rball', 'rtoe', 'lankle', 'lball', 'ltoe', 'headend', 'rwrist', 'lwrist']
    print(json.dumps({name: {joint: [round(v, 8) for v in points[joint]] for joint in selected}
                      for name, points in candidates.items()}, indent=2))
