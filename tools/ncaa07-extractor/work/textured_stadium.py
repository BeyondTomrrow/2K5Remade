"""Attach verified MMAP/Matl images to the existing ncaa_formats GLB writer.

Usage: python work/textured_stadium.py input.tmd output.glb
Texture/material extraction is implemented independently in texture_codec.py.
No changes to ncaa_formats.py or ncaa07_tool.py are needed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import tempfile

import texture_codec


def export_textured_glb(source,output):
    sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
    import ncaa_formats
    source=Path(source);output=Path(output)
    data=source.read_bytes()
    asset_folder=output.parent/(output.stem+'_assets')
    textures,materials=texture_codec.export_model(data,asset_folder)
    model=ncaa_formats.decode_model(data,str(source))
    with tempfile.TemporaryDirectory(prefix='texture_glb_',dir=Path(__file__).parent) as tmp:
        base=Path(tmp)/'base.glb'
        geometry=ncaa_formats.export_glb(model,base)
        raw=base.read_bytes()
    json_size,kind=struct.unpack_from('<I4s',raw,12)
    if kind!=b'JSON':raise ValueError('Invalid base GLB')
    doc=json.loads(raw[20:20+json_size])
    bin_size,kind=struct.unpack_from('<I4s',raw,20+json_size)
    if kind!=b'BIN\0':raise ValueError('Invalid base GLB binary')
    binary=bytearray(raw[28+json_size:28+json_size+bin_size])
    doc.update(images=[],textures=[],samplers=[dict(magFilter=9729,minFilter=9987,wrapS=10497,wrapT=10497)])
    png_cache={};material_map={};pending=[]
    for material in doc['materials']:
        index=int(material['name'].rsplit('_',1)[1])
        if not 0<=index<len(materials['materials']):
            raise ValueError(f'Geometry material index {index} outside Matl table')
        source_material=materials['materials'][index]
        material_map[index]=source_material['name']
        material['name']=source_material['name']
        material['extras']=dict(sourceMaterial=index,sourceShader=source_material['shader'])
        refs=source_material['textures']
        if not refs:continue
        if len(refs)>1:pending.append(f'{index}: multiple texture slots; base color uses slot 0')
        options=[im for im in refs[0]['image_options'] if im['status']=='decoded']
        if not options:
            pending.append(f'{index}: no verified local palette/image');continue
        chosen=options[0]
        if len(options)>1:pending.append(f'{index}: first local palette variant selected')
        png_path=asset_folder/chosen['png'];png=png_path.read_bytes()
        digest=hashlib.sha256(png).hexdigest()
        if digest not in png_cache:
            binary.extend(b'\0'*(-len(binary)%4));offset=len(binary);binary.extend(png)
            view=len(doc['bufferViews'])
            doc['bufferViews'].append(dict(buffer=0,byteOffset=offset,byteLength=len(png)))
            image=len(doc['images'])
            doc['images'].append(dict(name=refs[0]['name'],bufferView=view,mimeType='image/png'))
            texture=len(doc['textures'])
            doc['textures'].append(dict(source=image,sampler=0))
            png_cache[digest]=texture
        material['pbrMetallicRoughness']['baseColorTexture']=dict(index=png_cache[digest],texCoord=0)
        material['pbrMetallicRoughness']['baseColorFactor']=[1,1,1,1]
        # The source shader's blend/cutout policy is still unknown. Preserve
        # decoded alpha via conventional glTF modes and report the approximation.
        from PIL import Image
        alpha=Image.open(png_path).getchannel('A')
        histogram=alpha.histogram()
        if sum(histogram[1:255]):material['alphaMode']='BLEND'
        elif histogram[0]:material.update(alphaMode='MASK',alphaCutoff=0.5)
        material['extras'].update(sourceTexture=refs[0]['name'],decodedPNG=chosen['png'])
    attached=0
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            material=doc['materials'][primitive['material']]
            if 'baseColorTexture' in material['pbrMetallicRoughness']:
                if 'TEXCOORD_0' not in primitive['attributes']:
                    raise ValueError('Textured primitive has no UV stream')
                attached+=1
    doc['extras']['textureStatus']='MMAP images and exact Matl texture names decoded'
    doc['extras']['textureLimitations']=['Source shader blend flags not decoded; alpha modes approximated',
                                       'UVs retained as decoded; game rendering equivalence pending']+pending
    glb=ncaa_formats.GLB();glb.doc=doc;glb.binary=binary;glb.write(output)
    report=dict(file=str(output),source=str(source),model=model['name'],
                geometry=geometry,materials=len(doc['materials']),embedded_images=len(doc['images']),
                textured_primitives=attached,total_primitives=sum(len(m['primitives']) for m in doc['meshes']),
                material_index_to_name=material_map,pending=doc['extras']['textureLimitations'])
    report['geometry'].pop('file',None)
    output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();print(json.dumps(export_textured_glb(args.source,args.output),indent=2))
