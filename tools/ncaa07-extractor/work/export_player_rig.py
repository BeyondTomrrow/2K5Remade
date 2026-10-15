"""Export an explicitly experimental player rig, never modifying main modules.

Default: file04, ZYX, joint-pivot quaternion interpolation for source placement.
Q12 child/parent weights become conventional glTF LBS weights. This is an
editable skin, not a claim to reproduce the game's still-unknown deformation.
No stadium Z-up conversion is applied to these Y-up player coordinates.
"""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
from skel_seam_fit import ROOT, load, bone_matrices, partial_matrices, cluster_points


class GLB:
    def __init__(self):
        self.binary = bytearray()
        self.doc = {'asset': {'version': '2.0', 'generator': 'Independent experimental NCAA07 rig'},
                    'scene': 0, 'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [],
                    'bufferViews': [], 'accessors': [], 'skins': [],
                    'materials': [{'name': 'rig_debug', 'doubleSided': True,
                                   'pbrMetallicRoughness': {'baseColorFactor': [.68,.72,.8,1],
                                                          'metallicFactor': 0, 'roughnessFactor': .8}}]}

    def accessor(self, values, components, dtype='<f4', target=None, bounds=False):
        array = np.asarray(values, dtype=dtype).reshape(-1, components)
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(array.tobytes())
        view = {'buffer': 0, 'byteOffset': offset, 'byteLength': array.nbytes}
        if target:
            view['target'] = target
        vi = len(self.doc['bufferViews'])
        self.doc['bufferViews'].append(view)
        accessor = {'bufferView': vi, 'componentType': {'<f4':5126, '<u4':5125, '<u2':5123}[dtype],
                    'count': len(array), 'type': {1:'SCALAR', 2:'VEC2', 3:'VEC3', 4:'VEC4', 16:'MAT4'}[components]}
        if bounds:
            accessor['min'], accessor['max'] = array.min(0).tolist(), array.max(0).tolist()
        ai = len(self.doc['accessors'])
        self.doc['accessors'].append(accessor)
        return ai

    def write(self, path):
        self.doc['buffers'] = [{'byteLength': len(self.binary)}]
        encoded = json.dumps(self.doc, separators=(',', ':'), allow_nan=False).encode()
        encoded += b' ' * (-len(encoded) % 4)
        self.binary.extend(b'\0' * (-len(self.binary) % 4))
        total = 28 + len(encoded) + len(self.binary)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(struct.pack('<4sII', b'glTF', 2, total) +
                         struct.pack('<I4s', len(encoded), b'JSON') + encoded +
                         struct.pack('<I4s', len(self.binary), b'BIN\0') + self.binary)


def export(model, output, order='ZYX', mode='slerp_pivot'):
    worlds = bone_matrices(model, order)
    frames = partial_matrices(model, order, mode)
    positions = cluster_points(model, frames)
    rows = model['rows']
    bones = model['skeleton']['bones']
    joints = np.zeros((len(rows), 4), dtype=np.uint16)
    weights = np.zeros((len(rows), 4))
    for i, row in enumerate(rows):
        bone = int(row[3])
        parent = bones[bone]['parent']
        weight = row[4]/4096
        joints[i, 0], weights[i, 0] = bone, weight
        if parent is None:
            weights[i, 0] = 1
        else:
            joints[i, 1], weights[i, 1] = parent, 1-weight
    normal_frames = np.linalg.inv(frames[:, :3, :3]).transpose(0,2,1)
    normals = np.einsum('nij,nj->ni', normal_frames[rows[:,2].astype(int)], rows[:,8:11])
    normals /= np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-30)
    glb = GLB()
    for bone in bones:
        parent = bone['parent']
        local = worlds[bone['index']] if parent is None else np.linalg.inv(worlds[parent]) @ worlds[bone['index']]
        node = {'name': bone['name'], 'matrix': local.T.reshape(-1).tolist(),
                'extras': {'sourceBoneIndex': bone['index'], 'sourceFlag': bone['flag_raw'],
                           'rawRotationI16': bone['rotation_i16_raw']}}
        children = [b['index'] for b in bones if b['parent'] == bone['index']]
        if children:
            node['children'] = children
        glb.doc['nodes'].append(node)
    inverse = np.linalg.inv(worlds)
    ibm = glb.accessor(inverse.transpose(0,2,1).reshape(-1,16), 16)
    glb.doc['skins'].append({'name': 'Experimental_ZYX_Q12_parent_child',
                             'inverseBindMatrices': ibm, 'skeleton': 0,
                             'joints': list(range(len(bones)))})
    root_children = [0]
    for source_node in np.unique(rows[:,0]).astype(int):
        source_node = int(source_node)
        indices = np.flatnonzero(rows[:,0] == source_node)
        lookup = np.full(len(rows), -1)
        lookup[indices] = np.arange(len(indices))
        faces = model['faces'][np.isin(model['faces'][:,0], indices)]
        if not len(faces):
            continue
        attributes = {'POSITION': glb.accessor(positions[indices], 3, target=34962, bounds=True),
                      'NORMAL': glb.accessor(normals[indices], 3, target=34962),
                      'JOINTS_0': glb.accessor(joints[indices], 4, '<u2', 34962),
                      'WEIGHTS_0': glb.accessor(weights[indices], 4, target=34962)}
        faces_accessor = glb.accessor(lookup[faces].reshape(-1), 1, '<u4', 34963)
        mesh = len(glb.doc['meshes'])
        glb.doc['meshes'].append({'name': f'part_{source_node:04d}',
                                 'primitives': [{'attributes': attributes, 'indices': faces_accessor,
                                                 'material': 0, 'mode': 4}],
                                 'extras': {'sourceNode': source_node,
                                            'sourceClusterPerVertex': rows[indices,2].astype(int).tolist(),
                                            'sourceVertexPerVertex': rows[indices,1].astype(int).tolist()}})
        node = len(glb.doc['nodes'])
        glb.doc['nodes'].append({'name': f'part_{source_node:04d}', 'mesh': mesh, 'skin': 0})
        root_children.append(node)
    root = len(glb.doc['nodes'])
    glb.doc['nodes'].append({'name': model['skeleton']['model_name'], 'children': root_children})
    glb.doc['scenes'][0]['nodes'] = [root]
    glb.doc['extras'] = {'experimental': True, 'source': model['path'].name,
                         'sourceSHA256': model['skeleton']['file_sha256'],
                         'sourceCoordinates': 'player Y-up; no stadium Z-up rotation applied',
                         'rotationConvention': 'column vectors; Rz @ Ry @ Rx; radians=raw*2pi/65536',
                         'partialPlacement': mode,
                         'sourceSkelHeaderU32': model['skeleton']['header_u32'],
                         'sourceClstRows_u8Bone_u8ParentCluster_u16Weight': model['clusters'].tolist(),
                         'sourceDrawBatchAudit': model['draw_audit'],
                         'weights': 'candidate conventional LBS: child=rawQ12/4096, parent=1-child',
                         'limitations': ['partial cluster runtime deformation unverified',
                                         'all source active parts retained, including overlapping variants',
                                         'no source textures or animations']}
    # Independent bind identity check before float32 serialization.
    rest = np.einsum('nij,njk->nik', worlds, inverse)
    recovered = np.zeros_like(positions)
    homogeneous = np.c_[positions, np.ones(len(positions))]
    for component in range(4):
        recovered += weights[:,component,None] * np.einsum('nij,nj->ni', rest[joints[:,component],:3,:], homogeneous)
    error = float(np.max(np.linalg.norm(recovered-positions, axis=1)))
    assert error < 1e-10 and np.allclose(weights.sum(1),1)
    glb.write(output)
    return {'output': str(output), 'source': model['path'].name, 'order': order, 'partial_mode': mode,
            'joint_count': len(bones), 'mesh_count': len(glb.doc['meshes']),
            'vertices': len(rows), 'triangles': len(model['faces']),
            'partially_weighted_vertices': int(np.sum((rows[:,4]>0)&(rows[:,4]<4096))),
            'bind_identity_max_error_float64': error,
            'bounds': [positions.min(0).tolist(), positions.max(0).tolist()],
            'experimental': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', nargs='?', default='00000004.tmd')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--order', default='ZYX', choices=['XYZ','XZY','YXZ','YZX','ZXY','ZYX'])
    parser.add_argument('--partial', default='slerp_pivot',
                        choices=['linear_world','linear_pivot','euler_trs','euler_pivot','slerp_trs','slerp_pivot'])
    args = parser.parse_args()
    source = Path(args.file)
    if not source.exists():
        source = ROOT/'outputs/plydata_extracted'/args.file
    output = args.output or ROOT/'outputs'/f'player_{source.stem}_experimental_{args.order.lower()}_{args.partial}.glb'
    print(json.dumps(export(load(source), output, args.order, args.partial), indent=2))
