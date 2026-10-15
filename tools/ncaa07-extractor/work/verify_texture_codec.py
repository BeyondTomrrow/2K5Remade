"""Fixture verification plus independent Morton layout and corruption checks."""
from pathlib import Path
import hashlib
import json
import struct
from collections import Counter
from PIL import Image
import texture_codec as t

assert t.unswizzle(bytes(range(8)),4,2,1)==bytes([0,1,4,5,2,3,6,7])
assert t.unswizzle(bytes(range(16)),2,8,1)==bytes(range(16))
for w,h in [(16,8),(32,16),(64,128),(512,256),(256,512)]:
    offsets=t.morton_offsets(w,h)
    assert len(set(offsets))==w*h and min(offsets)==0 and max(offsets)==w*h-1

fixture=Path('outputs/plydata_extracted/00000001.3ds').read_bytes()
for corrupt in [fixture[:39],fixture[:-1],fixture[:40]+b'\0'*16+fixture[56:]]:
    try:t.parse_mmap(corrupt)
    except (t.DecodeError,UnicodeError):pass
    else:raise AssertionError('Corruption was accepted')

def verify(data,folder):
    meta=t.parse_mmap(data)
    disk=json.loads((folder/'manifest.json').read_text())
    assert disk['source_sha256']==hashlib.sha256(data).hexdigest()
    assert disk['palette_byte_order']=='BGRA'
    # The verified prefix/mip structure exactly tiles the pixel region and
    # palette region up to their following tables; no guessed slack bytes.
    spans=sorted((b['offset'],b['data_offset']+b['size']) for b in meta['bitmaps'])
    for left,right in zip(spans,spans[1:]):assert left[1]==right[0]
    if spans:assert spans[-1][1]==meta['tables']['palette']
    spans=sorted((p['offset'],p['data_offset']+p['size']) for p in meta['palettes'])
    for left,right in zip(spans,spans[1:]):assert left[1]==right[0]
    if spans:assert spans[-1][1]==meta['tables']['name']
    for item in disk['images']:
        if 'png' not in item:continue
        im=Image.open(folder/item['png']);im.load()
        assert im.mode=='RGBA' and im.size==(item['width'],item['height'])
        assert hashlib.sha256(im.tobytes()).hexdigest()==item['rgba_sha256']
    return meta

root=Path('outputs/textures');stats=Counter()
for p in sorted(Path('outputs/plydata_extracted').glob('*.3ds')):
    meta=verify(p.read_bytes(),root/'plydata'/p.stem);stats['ply_mmaps']+=1

uis=Path(r'H:\07\NCAA Football 07  (axekin.com)\data\UIS_MODL.DAT').read_bytes()
summary=json.loads((root/'uis_modl/archive_manifest.json').read_text())
for row in summary:
    if row['status']!='decoded':continue
    model=uis[row['offset']:row['offset']+row['size']]
    child=next(c for c in t.model_children(model) if c['tag']=='Text')
    verify(model[child['offset']:child['offset']+child['size']],root/'uis_modl'/f"{row['entry']:03d}")
    stats['uis_mmaps']+=1

model=Path(r'H:\NCAA07_Extractor_Work\stadiums_raw\00000000.tmd').read_bytes()
children=t.model_children(model);child=next(c for c in children if c['tag']=='Text')
meta=verify(model[child['offset']:child['offset']+child['size']],root/'stadium_airfor')
mat=t.material_references(model,next(c for c in children if c['tag']=='Matl'),meta)
assert len(mat)==59
assert sum(r['texture_count'] for r in mat)==58
assert all(r['status']=='exact_name_match' for m in mat for r in m['textures'])
assert mat[20]['name']=='Flatshaded1' and mat[20]['texture_count']==0
assert meta['bitmaps'][19]['owners']==[19,35,36]
stats['stadium_mmaps']=1;stats['stadium_exact_material_references']=58
stats['pngs_verified']=sum(1 for p in root.rglob('*.png') if p.name!='contact_sheet.png')
print(json.dumps(dict(stats),indent=2))
(root/'verification.json').write_text(json.dumps(dict(stats),indent=2),encoding='utf-8')
