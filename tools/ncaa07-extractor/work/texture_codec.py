"""Independent decoder for the supplied NCAA07 MMAP v2 textures.

Verified fixture dialect: 128-byte zero prefixes on the first bitmap of each
file and on every palette; rectangular Morton pixel order; format 11 indexed
BGRA palettes, format 6 BGRA pixels. No PS2 CLUT permutation or alpha scaling.
Palette-free indexed files remain unresolved; exported index PNGs preserve them.
Requires Pillow. Does not import or modify ncaa07_tool.py.
"""
from __future__ import annotations

import argparse
from collections import Counter
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import re
import struct

from PIL import Image, ImageDraw


class DecodeError(ValueError):
    pass


def checked(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise DecodeError(f'Out-of-range slice {offset:#x}+{size:#x}/{len(data):#x}')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, checked(data, offset, struct.calcsize(fmt)))


def cstring(data, offset, lower=0, upper=None):
    upper = len(data) if upper is None else upper
    if not lower <= offset < upper:
        raise DecodeError(f'String pointer outside block: {offset:#x}')
    end = data.find(b'\0', offset, upper)
    if end < 0:
        raise DecodeError('Unterminated string')
    return data[offset:end].decode('ascii', errors='strict')


@lru_cache(maxsize=32)
def morton_offsets(width, height):
    """Xbox-style rectangular Morton: interleave x/y until an axis is exhausted."""
    if min(width, height) < 1 or width & (width-1) or height & (height-1):
        raise DecodeError('Only power-of-two dimensions are verified')
    if width * height > 4194304:
        raise DecodeError('Dimensions exceed the supported safety limit')
    def spread(value, axis):
        result = shift = 0
        bit = 1
        while bit < width or bit < height:
            for dimension, current_axis in [(width, 0), (height, 1)]:
                if bit < dimension:
                    if axis == current_axis and value & bit:
                        result |= 1 << shift
                    shift += 1
            bit <<= 1
        return result
    xs = [spread(x, 0) for x in range(width)]
    ys = [spread(y, 1) for y in range(height)]
    return tuple(x | y for y in ys for x in xs)


def unswizzle(raw, width, height, stride):
    if len(raw) != width*height*stride:
        raise DecodeError('Pixel size does not match dimensions')
    offsets = morton_offsets(width, height)
    if stride == 1:
        return bytes(raw[p] for p in offsets)
    return b''.join(raw[p*stride:(p+1)*stride] for p in offsets)


def parse_mmap(data):
    if checked(data, 0, 12) != b'MMAP\x02\x00\x00\x00\x00\x01\x02\x03':
        raise DecodeError('Unsupported MMAP header/version')
    nf, nb, np, unknown = unpack('<4H', data, 12)
    ft, bt, pt, nt, extra = unpack('<5I', data, 20)
    if bt != 40:
        raise DecodeError('Unexpected bitmap table position')
    checked(data, bt, nb*16); checked(data, pt, np*12)
    checked(data, ft, nf*12); checked(data, nt, nf*16)
    bitmaps = []
    for i in range(nb):
        w,h,fmt,size,offset = unpack('<HHIII', data, bt+i*16)
        if fmt not in (6,11) or size != w*h*(4 if fmt==6 else 1):
            raise DecodeError(f'Unverified pixel encoding at bitmap {i}: {fmt}')
        morton_offsets(w,h)
        bitmaps.append(dict(index=i,width=w,height=h,format=fmt,size=size,offset=offset))
    palettes = []
    for i in range(np):
        a,b,size,offset = unpack('<HHII', data, pt+i*12)
        if size != 1024 or checked(data, offset, 128) != bytes(128):
            raise DecodeError(f'Unverified palette layout {i}')
        checked(data,offset+128,size)
        palettes.append(dict(index=i,flags=[a,b],size=size,offset=offset,data_offset=offset+128))
    files=[]
    owners={}
    for i in range(nf):
        pc,bc,bi,pi = unpack('<HHII',data,ft+i*12)
        if bi+bc>nb or pi+pc>np:
            raise DecodeError(f'File table range invalid: {i}')
        name=checked(data,nt+i*16,16).split(b'\0')[0].decode('ascii')
        files.append(dict(index=i,name=name,palette_count=pc,bitmap_count=bc,
                          bitmap_index=bi,palette_index=pi))
        for mip in range(bc):
            index=bi+mip
            bmp=bitmaps[index]
            prefix=128 if mip==0 else 0
            if index in owners:
                if bmp['mip'] != mip:
                    raise DecodeError('Shared bitmap has conflicting mip levels')
                bmp['owners'].append(i)
                continue
            owners[index]=i
            if prefix and checked(data,bmp['offset'],128)!=bytes(128):
                raise DecodeError('First bitmap prefix is not zero')
            bmp.update(data_offset=bmp['offset']+prefix,mip=mip,owners=[i])
            checked(data,bmp['data_offset'],bmp['size'])
    if len(owners)!=nb: raise DecodeError('Unowned bitmap')
    return dict(file_count=nf,bitmap_count=nb,palette_count=np,unknown=unknown,
                tables=dict(file=ft,bitmap=bt,palette=pt,name=nt,extra=extra),
                files=files,bitmaps=bitmaps,palettes=palettes)


def model_children(data):
    if checked(data,0,4)!=b'TMdl': raise DecodeError('Not a TMdl')
    total,count,alignment,unknown=unpack('<IHHI',data,4)
    checked(data,0,total)
    children=[]
    for i in range(count):
        tag,offset,size,flags=unpack('<4sIII',data,16+i*16)
        checked(data,offset,size)
        if offset+size>total: raise DecodeError('Child extends past model')
        children.append(dict(tag=tag.decode('ascii'),offset=offset,size=size,flags=flags))
    return children


def material_references(model, child, textures):
    """Model-relative pointers; retain material index for geometry association."""
    start=child['offset']; end=start+child['size']
    count,=unpack('<I',model,start)
    checked(model,start+4,count*4)
    if start+4+count*4>end: raise DecodeError('Material pointer table too large')
    names={}
    for f in textures['files']:
        names.setdefault(f['name'],[]).append(f['index'])
    records=[]
    for i in range(count):
        ptr,=unpack('<I',model,start+4+i*4)
        if not start<=ptr or ptr+48>end: raise DecodeError('Material record outside Matl')
        words=unpack('<12I',model,ptr)
        name=cstring(model,words[0],start,end)
        shader=cstring(model,words[1],start,end)
        refs=[]
        if words[2]>9: raise DecodeError('Unverified material texture count')
        for slot in range(words[2]):
            texture=cstring(model,words[3+slot],start,end)
            matches=names.get(texture,[])
            refs.append(dict(slot=slot,name=texture,mmap_file_indices=matches,
                             status='exact_name_match' if len(matches)==1 else 'unresolved'))
        records.append(dict(material_index=i,record_offset=ptr,name=name,shader=shader,
                            texture_count=words[2],textures=refs,raw_words=list(words)))
    return records


def safe_name(name):
    return re.sub(r'[^A-Za-z0-9_.-]+','_',name)[:80] or 'unnamed'


def export_mmap(data, destination):
    meta=parse_mmap(data)
    destination.mkdir(parents=True,exist_ok=True)
    images=[]; previews=[]
    for f in meta['files']:
        for mip in range(f['bitmap_count']):
            bmp=meta['bitmaps'][f['bitmap_index']+mip]
            w,h=bmp['width'],bmp['height']; fmt=bmp['format']
            raw=checked(data,bmp['data_offset'],bmp['size'])
            linear=unswizzle(raw,w,h,4 if fmt==6 else 1)
            stem=f"{f['index']:03d}_{safe_name(f['name'])}_m{mip}"
            base=dict(file_index=f['index'],name=f['name'],mip=mip,width=w,height=h,
                      format=fmt,bitmap_index=bmp['index'],pixel_data_offset=bmp['data_offset'])
            if fmt==6:
                variants=[(None,Image.frombytes('RGBA',(w,h),linear,'raw','BGRA'),'decoded')]
            else:
                index_name=stem+'_indices.png'
                Image.frombytes('L',(w,h),linear).save(destination/index_name)
                base['indices_png']=index_name
                palette_ids=range(f['palette_index'],f['palette_index']+f['palette_count'])
                status='decoded'
                if not f['palette_count']:
                    # No implicit borrowing: all archive palettes are visibly marked candidates.
                    palette_ids=range(meta['palette_count']); status='candidate_palette'
                variants=[]
                for pi in palette_ids:
                    p=meta['palettes'][pi]; pal=checked(data,p['data_offset'],1024)
                    rgba=b''.join(pal[j*4:j*4+4] for j in linear)
                    variants.append((pi,Image.frombytes('RGBA',(w,h),rgba,'raw','BGRA'),status))
                if not variants:
                    images.append(dict(**base,status='indices_only_no_palette'))
            for pi,im,status in variants:
                suffix='' if pi is None else f'_p{pi:03d}'
                if status!='decoded': suffix+='_candidate'
                filename=stem+suffix+'.png';im.save(destination/filename)
                images.append(dict(**base,palette_index=pi,status=status,png=filename,
                                   rgba_sha256=hashlib.sha256(im.tobytes()).hexdigest()))
                if mip==0 and len(previews)<60 and (status=='decoded' or pi==0):
                    previews.append((filename,im.copy()))
    if previews:
        sheet=Image.new('RGB',(1000,((len(previews)+4)//5)*180),'#aaaaaa')
        draw=ImageDraw.Draw(sheet)
        for i,(name,im) in enumerate(previews):
            x=(i%5)*200;y=(i//5)*180
            im.thumbnail((190,150));sheet.paste(im,(x,y+24),im)
            draw.text((x+3,y+3),name[:29],fill='black')
        sheet.save(destination/'contact_sheet.png')
    meta.update(images=images,source_sha256=hashlib.sha256(data).hexdigest(),
                layout='rectangular_morton',palette_byte_order='BGRA',
                direct_byte_order='BGRA',alpha='unscaled_0_255',
                pending=['Runtime palette selection for zero-palette indexed files',
                         'Shader flags and blend semantics; non-observed MMAP dialects'])
    (destination/'manifest.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
    return meta


def export_model(data,destination):
    children=model_children(data)
    text=next((x for x in children if x['tag']=='Text' and x['size']),None)
    if text is None: raise DecodeError('Model has no nonempty Text')
    meta=export_mmap(checked(data,text['offset'],text['size']),destination)
    matl=next((x for x in children if x['tag']=='Matl'),None)
    materials=[]
    if matl:
        materials=material_references(data,matl,meta)
        for m in materials:
            for ref in m['textures']:
                ref['image_options']=[im for im in meta['images']
                                      if im['file_index'] in ref['mmap_file_indices'] and im['mip']==0]
    result=dict(children=children,materials=materials,
                verified='Material name/shader pointers, texture count and exact MMAP name references',
                pending='Geometry primitive material-index linkage, shader flags, palette/variant selection')
    (destination/'materials.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return meta,result


def export_terf(data,destination):
    """Walk DIR1 entries; do not scan for arbitrary Text/MMAP byte signatures."""
    if data[:4]!=b'TERF': raise DecodeError('Not a TERF archive')
    directory=data.find(b'DIR1'); table_size,=unpack('<I',data,directory+4)
    base=data.find(b'DATA',directory)
    if directory<0 or base<0: raise DecodeError('Missing archive directory/data')
    summary=[]
    for i in range(table_size//8):
        relative,size=unpack('<II',data,directory+8+i*8)
        offset=base+relative
        row=dict(entry=i,offset=offset,size=size)
        # DIR1 length in this fixture includes trailing non-entry words; retain them as skipped.
        if offset+size>len(data) or data[offset:offset+4]!=b'TMdl':
            row['status']='skipped_nonmodel_or_invalid_directory_slot'
        else:
            try:
                meta,mat=export_model(checked(data,offset,size),destination/f'{i:03d}')
                row.update(status='decoded',bitmaps=meta['bitmap_count'],files=meta['file_count'],
                           palettes=meta['palette_count'],images=len(meta['images']),
                           materials=len(mat['materials']))
            except (DecodeError,UnicodeError) as exc:
                row.update(status='unsupported',reason=str(exc))
        summary.append(row)
    destination.mkdir(parents=True,exist_ok=True)
    (destination/'archive_manifest.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    return summary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('destination',type=Path)
    args=parser.parse_args();source=args.source;dest=args.destination
    if source.is_dir():
        results=[]
        for path in sorted(source.glob('*.3ds')):
            meta=export_mmap(path.read_bytes(),dest/path.stem)
            results.append(dict(source=str(path),bitmaps=meta['bitmap_count'],
                                palettes=meta['palette_count'],images=len(meta['images'])))
        dest.mkdir(parents=True,exist_ok=True)
        (dest/'archive_manifest.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        print(json.dumps(results,indent=2));return
    data=source.read_bytes()
    if data[:4]==b'TERF':
        summary=export_terf(data,dest);print(dict(Counter(x['status'] for x in summary)))
    elif data[:4]==b'TMdl':
        meta,mat=export_model(data,dest)
        print(dict(files=meta['file_count'],bitmaps=meta['bitmap_count'],materials=len(mat['materials'])))
    else:
        meta=export_mmap(data,dest);print(dict(files=meta['file_count'],bitmaps=meta['bitmap_count']))


if __name__=='__main__':
    main()
