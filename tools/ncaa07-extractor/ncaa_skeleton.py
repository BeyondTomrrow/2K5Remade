"""Independent, read-only TMdl Skel decoder and fixture audit (stdlib only).

Run from the workspace: python work/skel_probe.py
JSON is written to stdout; no files are changed. Rotation triplets remain raw.
All offsets are relative to the beginning of the enclosing TMdl.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def require(condition, message):
    if not condition:
        raise ValueError(message)


def decode(data):
    require(len(data) >= 16 and data[:4] == b'TMdl', 'missing TMdl header')
    size, count, alignment, version = struct.unpack_from('<IHHI', data, 4)
    require(size == len(data), 'TMdl size mismatch')
    require(16 + count * 16 <= size, 'truncated child table')
    children = {}
    for at in range(16, 16 + count * 16, 16):
        tag = data[at:at + 4].decode('ascii')
        offset, length, instances = struct.unpack_from('<III', data, at + 4)
        require(offset <= size and length <= size - offset, 'child outside TMdl')
        require(tag not in children, 'duplicate child tag')
        children[tag] = (offset, length, instances)
    start, length, instances = children['Skel']
    end = start + length
    require(length >= 24 and instances == 1, 'unexpected Skel descriptor')
    n, names, positions, rotations, scales, reserved = struct.unpack_from('<6I', data, start)
    require(0 < n <= 65535 and reserved == 0, 'unexpected Skel header')
    regions = [('names', names, 16 * n), ('positions', positions, 16 * n),
               ('rotations', rotations, 6 * n), ('scales', scales, 12 * n)]
    previous = start + 24
    padding = []
    for label, offset, extent in regions:
        require(previous <= offset <= end and extent <= end - offset,
                label + ' outside Skel or overlapping previous table')
        gap = data[previous:offset]
        require(not any(gap), 'unexpected nonzero inter-table padding')
        padding.append({'before': label, 'offset': previous, 'length': len(gap)})
        previous = offset + extent
    require(previous == end, 'unexplained Skel tail')
    require(names == start + 24, 'unexpected names offset')
    require(all(offset % 16 == 0 for offset in (positions, rotations, scales)),
            'unaligned numeric table')
    bones = []
    for i in range(n):
        raw = data[names + i * 16:names + (i + 1) * 16]
        text, separator, suffix = raw.partition(b'\0')
        require(separator and not any(suffix) and all(32 <= c < 127 for c in text)
                and text, 'invalid name record')
        x, y, z, parent, flag = struct.unpack_from('<3fHH', data, positions + i * 16)
        rotation = struct.unpack_from('<3h', data, rotations + i * 6)
        scale = struct.unpack_from('<3f', data, scales + i * 12)
        require(all(math.isfinite(v) for v in (x, y, z, *scale)), 'nonfinite transform')
        require(parent == 0 if i == 0 else parent < i, 'invalid parent topology')
        require(flag in (0, 1), 'unobserved flag value')
        bones.append({'index': i, 'name': text.decode('ascii'),
                      'parent_raw': parent, 'parent': None if i == 0 else parent,
                      'flag_raw': flag, 'local_translation': [x, y, z],
                      'rotation_i16_raw': list(rotation), 'scale_candidate': list(scale),
                      'record_offsets': {'name': names + i * 16,
                                         'position_parent_flag': positions + i * 16,
                                         'rotation': rotations + i * 6,
                                         'scale': scales + i * 12}})
    for bone in bones:
        parent = bone['parent']
        bone['parent_name'] = None if parent is None else bones[parent]['name']
    normalized = bytearray(data[start:end])
    for field, offset in enumerate((names, positions, rotations, scales), 1):
        struct.pack_into('<I', normalized, field * 4, offset - start)
    info, info_length, _ = children['Info']
    return {'model_name': data[info:info + info_length].split(b'\0')[0].decode('ascii'),
            'file_size': size, 'file_sha256': hashlib.sha256(data).hexdigest(),
            'skel_offset': start, 'skel_size': length, 'bone_count': n,
            'header_u32': [n, names, positions, rotations, scales, reserved],
            'skel_sha256': hashlib.sha256(data[start:end]).hexdigest(),
            'normalized_skel_sha256': hashlib.sha256(normalized).hexdigest(),
            'padding': padding, 'bones': bones}


def audit(paths):
    fixtures = []
    groups = {}
    for path in paths:
        decoded = decode(path.read_bytes())
        decoded['file'] = path.name
        fixtures.append(decoded)
        groups.setdefault(decoded['normalized_skel_sha256'], []).append(path.name)
    require(bool(fixtures), 'no fixtures')
    by_count = {}
    for f in fixtures:
        by_count.setdefault(f['bone_count'], []).append(f)
    # Compare the genuinely different rigs by name, not by numeric index.
    comparisons = []
    if 75 in by_count and 32 in by_count:
        a, b = by_count[75][0], by_count[32][0]
        common = sorted(set(x['name'] for x in a['bones']) & set(x['name'] for x in b['bones']))
        amap = {x['name']: x for x in a['bones']}
        bmap = {x['name']: x for x in b['bones']}
        for name in common:
            x, y = amap[name], bmap[name]
            require(x['parent_name'] == y['parent_name'], 'cross-rig parent disagreement')
            require(x['rotation_i16_raw'] == y['rotation_i16_raw'], 'cross-rig rotation disagreement')
            require(max(abs(v - w) for v, w in zip(x['local_translation'], y['local_translation'])) < 1e-6,
                    'cross-rig translation disagreement')
        comparisons.append({'files': [a['file'], b['file']], 'common_names': len(common),
                            'matching_parent_names_and_raw_rotations': True,
                            'max_translation_delta': max(abs(v - w) for name in common
                                for v, w in zip(amap[name]['local_translation'], bmap[name]['local_translation'])),
                            'flag_differences': [name for name in common if amap[name]['flag_raw'] != bmap[name]['flag_raw']]})
    # Negative controls against actual bytes, exercising boundaries and topology.
    source = paths[0].read_bytes()
    sample = fixtures[0]
    bad_parent = bytearray(source)
    struct.pack_into('<H', bad_parent, sample['bones'][1]['record_offsets']['position_parent_flag'] + 12,
                     sample['bone_count'])
    bad_pointer = bytearray(source)
    struct.pack_into('<I', bad_pointer, sample['skel_offset'] + 8, len(source) + 16)
    controls = {}
    for label, damaged in [('truncated_file', source[:-1]), ('invalid_parent', bad_parent),
                           ('out_of_bounds_position_pointer', bad_pointer)]:
        try:
            decode(damaged)
        except ValueError as error:
            controls[label] = str(error)
        else:
            raise AssertionError('negative control accepted: ' + label)
    return {'fixture_count': len(fixtures), 'normalized_groups': groups,
            'cross_rig_comparisons': comparisons, 'negative_controls': controls,
            'interpretation': {'verified': ['names', 'parent indices', 'local translations',
                                           'raw signed-16 triplets', 'near-unit float triplets'],
                               'unresolved': ['rotation encoding and composition convention',
                                              'flag semantics', 'root self-parent convention',
                                              'full bind and inverse-bind matrices']},
            'fixtures': fixtures}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', type=Path)
    args = parser.parse_args()
    paths = args.files or sorted((Path(__file__).resolve().parents[1] / 'outputs' / 'plydata_extracted').glob('*.tmd'))
    print(json.dumps(audit(paths), indent=2))
