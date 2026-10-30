"""Fixture audit of Geom scalar -> signed palette slot -> Clst -> Skel.

Run: python work/skel_geom_bridge.py. Reads all extracted TMDs; stdout only.
Clst weight is preserved as Q12 candidate; no blending formula is asserted.
"""
import json
from pathlib import Path
import struct
from skel_probe import decode, require


def bridge(data):
    skeleton = decode(data)
    bones = skeleton['bones']
    children = {data[a:a+4]: struct.unpack_from('<III', data, a+4)
                for a in range(16, 16 + struct.unpack_from('<H', data, 8)[0]*16, 16)}
    co, length, _ = children[b'Clst']
    pointer, count = struct.unpack_from('<II', data, co)
    require(pointer == co+8 and length == 8+count*4, 'Clst extent mismatch')
    clusters = [struct.unpack_from('<BBH', data, pointer+i*4) for i in range(count)]
    last = {bone: i for i, (bone, parent, weight) in enumerate(clusters)}
    for i, (bone, parent, weight) in enumerate(clusters):
        require(bone < len(bones) and weight <= 4096, 'invalid cluster')
        expected = 255 if bone == 0 else last.get(bones[bone]['parent_raw'])
        require(parent == expected, 'cluster predecessor differs from parent final cluster')
    go, _, _ = children[b'Geom']
    np, nc, sp, sc = struct.unpack_from('<4I', data, go)
    nodes = []
    for i in range(nc):
        a = np+i*20
        draw, palette_header = struct.unpack_from('<II', data, a)
        if not draw and not palette_header:
            continue
        require(draw and palette_header, 'partially active node')
        palette_count = struct.unpack_from('<I', data, palette_header)[0]
        require(0 < palette_count < 1000, 'invalid palette count')
        palettes = []
        for batch in range(palette_count):
            palette = struct.unpack_from('<I', data, palette_header+4+4*batch)[0]
            pc = struct.unpack_from('<I', data, palette)[0]
            require(palette+4+pc*4 <= len(data), 'palette out of bounds')
            entries = dict(struct.unpack_from('<hH', data, palette+4+j*4) for j in range(pc))
            require(len(entries) == pc and all(v < count for v in entries.values()), 'invalid palette')
            palettes.append(entries)
        base = struct.unpack_from('<H', data, a+14)[0]
        require(base+2 < sc, 'base stream out of bounds')
        vp, vc, vs, semantic = struct.unpack_from('<IHHI', data, sp+base*12)
        require(vs == 24 and semantic == 18, 'unexpected position format')
        p, c, stride, semantic = struct.unpack_from('<IHHI', data, sp+(base+2)*12)
        require(c == vc and stride == 4 and semantic == 0, 'unexpected selector stream')
        uses = {}
        ambiguous = 0
        for k in range(c):
            value = struct.unpack_from('<f', data, p+12+k*4)[0]
            slot = round(value/3)
            candidates = {entries[slot] for entries in palettes if slot in entries}
            require(value == slot*3 and candidates, 'selector misses signed palette slot')
            if len(candidates) == 1:
                cluster = next(iter(candidates))
                uses[cluster] = uses.get(cluster, 0)+1
            else:
                ambiguous += 1
        nodes.append({'node': i, 'base_stream': base, 'vertex_count': c,
                      'palette_count': palette_count,
                      'palettes': [[{'slot': slot, 'cluster': cluster,
                                   'bone': clusters[cluster][0],
                                   'bone_name': bones[clusters[cluster][0]]['name'],
                                   'weight_q12_candidate': clusters[cluster][2]}
                                  for slot, cluster in entries.items()] for entries in palettes],
                      'vertices_needing_batch_assignment': ambiguous,
                      'cluster_use_counts': uses})
    return {'bone_count': len(bones), 'cluster_count': count,
            'active_nodes': len(nodes), 'selector_count': sum(x['vertex_count'] for x in nodes),
            'nodes': nodes}


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1] / 'outputs/plydata_extracted'
    report = {}
    for path in sorted(root.glob('*.tmd')):
        result = bridge(path.read_bytes())
        if path.name != '00000003.tmd':
            del result['nodes']
        report[path.name] = result
    print(json.dumps(report, indent=2))
