"""Independent readback of serialized experimental skin, including bind identity."""
import json
from pathlib import Path
import struct
import sys
import numpy as np


def check(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from('<4sII',data)
    assert (magic, version, size) == (b'glTF', 2, len(data))
    length, tag = struct.unpack_from('<I4s',data,12)
    assert tag == b'JSON'
    doc = json.loads(data[20:20+length])
    start = 20+length
    blen, btag = struct.unpack_from('<I4s',data,start)
    assert btag == b'BIN\0' and start+8+blen == len(data)
    binary = memoryview(data)[start+8:]
    def accessor(index):
        a = doc['accessors'][index]
        view = doc['bufferViews'][a['bufferView']]
        dim = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        dtype = {5126:'<f4',5125:'<u4',5123:'<u2'}[a['componentType']]
        return np.frombuffer(binary, dtype=dtype, count=a['count']*dim,
                             offset=view.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,dim)
    local = [np.array(n.get('matrix',np.eye(4).T.reshape(-1))).reshape(4,4).T for n in doc['nodes']]
    parents = {child:i for i,n in enumerate(doc['nodes']) for child in n.get('children',[])}
    def world(i):
        return world(parents[i])@local[i] if i in parents else local[i]
    skin = doc['skins'][0]
    bind = accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1).astype(float)
    matrices = np.array([world(i)@b for i,b in zip(skin['joints'],bind)])
    error, vertices, triangles, weighted = 0., 0, 0, 0
    for node in doc['nodes']:
        if 'mesh' not in node:
            continue
        assert node.get('skin') == 0
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            attrs = primitive['attributes']
            p, j, w = (accessor(attrs[k]) for k in ['POSITION','JOINTS_0','WEIGHTS_0'])
            assert np.isfinite(p).all() and np.isfinite(w).all() and np.all(w>=0)
            assert j.max()<len(bind) and np.max(np.abs(w.sum(1)-1))<1e-6
            idx = accessor(primitive['indices'])
            assert idx.max()<len(p)
            recovered = np.zeros_like(p,dtype=float)
            for k in range(4):
                recovered += w[:,k,None]*np.einsum('nij,nj->ni',matrices[j[:,k],:3,:],np.c_[p,np.ones(len(p))])
            error = max(error,float(np.max(np.linalg.norm(recovered-p,axis=1))))
            vertices += len(p)
            triangles += len(idx)//3
            weighted += int(np.sum(np.sum(w>0,axis=1)>1))
    assert error<1e-6
    return {'file':path.name,'joints':len(bind),'vertices':vertices,'triangles':triangles,
            'vertices_with_two_nonzero_weights':weighted,'serialized_skin_max_rest_error':error}


if __name__ == '__main__':
    print(json.dumps(check(Path(sys.argv[1])),indent=2))
