"""Bounded uniform texture editing for Team Studio; never patches the disc."""
from __future__ import annotations
import base64
import io
import json
import struct
import zipfile
from dataclasses import asdict
from PIL import Image
import game_assets as G
import logos

T = G.T

def normalized_uniform(uniform):
    import copy
    result=copy.deepcopy(uniform)
    if result.get('pant_stripe',{}).get('on') is False:
        result['pant_stripe']['color']=result['pants_color']
    if result.get('helmet_stripe',{}).get('on') is False:
        result['helmet_stripe']['width']=0
    return result

def detailed_image(name, image, uniform, base_colors, away=False):
    fields={'jersey00':('jersey_color','jersey_trim'),
        'pants00':('pants_color','pant_stripe'),'sleeve00':('jersey_color','jersey_trim')}
    primary,secondary=fields[name];first=uniform[primary]
    second=uniform[secondary]['color'] if secondary=='pant_stripe' else uniform[secondary]
    sources=base_colors[::-1] if name=='pants00' else base_colors
    mapper=G.make_mapper([tuple(c) for c in sources],[tuple(first),tuple(second)])
    # White pants and white away jerseys must also respond to the editor;
    # the original generic palette mapper intentionally leaves neutrals alone.
    neutral=first if name=='pants00' or away else second
    pixels=[]
    for r,g,b,a in image.convert('RGBA').getdata():
        if min(r,g,b)>95 and max(r,g,b)-min(r,g,b)<28:
            light=(r+g+b)/(3*245);nr,ng,nb=[min(255,round(c*light)) for c in neutral]
        else:nr,ng,nb=mapper((r,g,b))
        pixels.append((nr,ng,nb,a))
    out=Image.new('RGBA',image.size);out.putdata(pixels);return out

def paint_detailed(data, original, uniform, base_colors, away=False):
    for chunk in T.parse_chunks(original,allow_trailing=True):
        if chunk.kind!='TSET':continue
        decoded,infos=texture_infos(original,chunk)
        for index,info in enumerate(infos):
            if info.name not in ('jersey00','pants00','sleeve00'):continue
            image=Image.frombytes('RGBA',(info.width,info.height),T.texture_to_rgba(decoded,chunk,info))
            data=replace_texture(data,f'{chunk.index}:{index}',detailed_image(info.name,image,uniform,base_colors,away))
    return data

def image_url(image):
    output = io.BytesIO(); image.save(output, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(output.getvalue()).decode()

def _pointer(data, at):
    value = struct.unpack_from('<i', data, at)[0]
    target = at + value - 1
    if value == 0 or not 0 <= target < len(data):
        raise ValueError('Texture descriptor pointer is outside the resource')
    return target

def texture_infos(data, chunk):
    decoded, _ = T.decode_chunk(data, chunk)
    if chunk.kind == 'TXTR':
        return decoded, [T.parse_texture(decoded, chunk)]
    if chunk.kind != 'TSET':
        return decoded, []
    version, count = struct.unpack_from('<II', decoded)
    if version != 13 or not 0 < count <= 64:
        raise ValueError('Unsupported texture-set directory')
    infos = []
    for index in range(count):
        at = 0x18 + index * 0x24
        if at + 0x18 > chunk.system_bytes or decoded[at:at+4] != b'TXTR':
            raise ValueError('Invalid embedded texture record')
        name_at = _pointer(decoded, at+4); descriptor = _pointer(decoded, at+8)
        end = name_at
        while end+2 <= chunk.system_bytes and decoded[end:end+2] != b'\0\0': end += 2
        if end+2 > chunk.system_bytes or descriptor+24 > chunk.system_bytes:
            raise ValueError('Truncated texture metadata')
        _, pixels, palette, fmt, size, flags = struct.unpack_from('<6I', decoded, descriptor)
        width = size & 65535 if size else 1 << ((fmt >> 20) & 15)
        height = size >> 16 if size else 1 << ((fmt >> 24) & 15)
        infos.append(T.TextureInfo(decoded[name_at:end].decode('utf-16le'), name_at, descriptor,
            pixels, palette, fmt, size, flags, (fmt >> 4) & 15, (fmt >> 8) & 255,
            T.XBOX_FORMAT_NAMES.get((fmt >> 8) & 255, 'Unknown'), (fmt >> 16) & 15,
            width, height, 1 << ((fmt >> 28) & 15)))
    return decoded, infos

def textures(data):
    rows = []
    for chunk in T.parse_chunks(data, allow_trailing=True):
        if chunk.kind not in ('TXTR', 'TSET'): continue
        decoded, infos = texture_infos(data, chunk)
        for index, info in enumerate(infos):
            row = dict(id=f'{chunk.index}:{index}', chunk=chunk.index, reference=index,
                name=info.name, kind=chunk.kind, width=info.width, height=info.height,
                format=info.format_name, editable=info.format_code in (2, 6, 11),
                shared=chunk.kind == 'TSET')
            try:
                image = Image.frombytes('RGBA', (info.width, info.height), T.texture_to_rgba(decoded, chunk, info))
                row['image'] = image_url(image)
            except (ValueError, NotImplementedError) as exc: row['error'] = str(exc)
            rows.append(row)
    return rows

def _rebuild(chunk, decoded):
    if chunk.compressed:
        stream, _ = T.compress_vc_lz(bytes(decoded))
        scratch = (T.minimum_vc_lz_overlap_scratch(stream, len(stream), len(decoded)) + 15) & ~15
        stream += bytes(-len(stream) % 16)
        header = struct.pack('<4s7I', chunk.kind.encode(), len(stream), chunk.system_bytes,
            chunk.video_bytes, chunk.compression_magic, scratch+16, 0, 0)
        return header+stream
    return struct.pack('<4s7I', chunk.kind.encode(), len(decoded), chunk.system_bytes,
        chunk.video_bytes, 0, 0, 0, 0)+bytes(decoded)

def replace_texture(data, texture_id, image):
    ci, ri = map(int, texture_id.split(':'))
    chunks = T.parse_chunks(data, allow_trailing=True)
    chunk = next((c for c in chunks if c.index == ci), None)
    if chunk is None: raise ValueError('Texture chunk no longer exists')
    decoded, infos = texture_infos(data, chunk)
    if not 0 <= ri < len(infos): raise ValueError('Texture reference no longer exists')
    info = infos[ri]; image = image.convert('RGBA')
    if image.size != (info.width, info.height):
        raise ValueError(f'Image must be {info.width} × {info.height}; the original UV layout is preserved')
    decoded = bytearray(decoded); original_length = len(decoded)
    if info.format_code == 11:
        siblings = [x for x in infos if x.pixel_offset == info.pixel_offset and x.palette_offset != info.palette_offset]
        # A mud reference shares the clean texture's indices. Refit only its
        # palette, preserving every clean index and all mip levels.
        if info.name.endswith('_mud') and siblings:
            raw = decoded[chunk.system_bytes+info.pixel_offset:chunk.system_bytes+info.pixel_offset+info.width*info.height]
            indices = T.unswizzle_2d(bytes(raw), info.width, info.height, 1)
            totals = [[0, 0, 0, 0, 0] for _ in range(256)]
            for index, rgba in zip(indices, image.getdata()):
                for k in range(4): totals[index][k] += rgba[k]
                totals[index][4] += 1
            at = chunk.system_bytes+info.palette_offset
            for i, values in enumerate(totals):
                if values[4]:
                    r,g,b,a = [round(v/values[4]) for v in values[:4]]
                    decoded[at+i*4:at+i*4+4] = bytes((b,g,r,a))
        else:
            logos._encode_p8(decoded, chunk, info, image)
            # Clean/mud use one index chain; rebuild corresponding mud palettes
            # from the new palette so they never point at unrelated old colors.
            for sibling in siblings:
                source = chunk.system_bytes+info.palette_offset
                target = chunk.system_bytes+sibling.palette_offset
                for i in range(256):
                    b,g,r,a = decoded[source+i*4:source+i*4+4]
                    decoded[target+i*4:target+i*4+4] = bytes((int(b*.65),int(g*.72),int(r*.8),a))
    elif info.format_code in (2,6):
        offset = chunk.system_bytes+info.pixel_offset; width,height = image.size
        for _ in range(max(1,info.mip_levels)):
            pixels = image.resize((width,height), Image.Resampling.LANCZOS)
            if info.format_code == 6:
                raw = pixels.tobytes('raw','BGRA'); bpp = 4
            else:
                raw = b''.join(struct.pack('<H', (r>>3)<<11 | (g>>2)<<5 | (b>>3)) for r,g,b,a in pixels.getdata()); bpp = 2
            encoded = T.swizzle_2d(raw,width,height,bpp)
            decoded[offset:offset+len(encoded)] = encoded; offset += len(encoded)
            width,height = max(1,width//2),max(1,height//2)
    else: raise ValueError(f'{info.format_name} can be extracted but is not writable by this editor')
    if len(decoded) != original_length: raise ValueError('Texture write would change resource allocation')
    replacement = _rebuild(chunk, decoded) if chunk.compressed else data[chunk.offset:chunk.body_offset]+bytes(decoded)
    result = data[:chunk.offset]+replacement+data[chunk.end_offset:]
    rebuilt = next(c for c in T.parse_chunks(result, allow_trailing=True) if c.index == ci)
    check, check_infos = texture_infos(result, rebuilt)
    if check != decoded: raise ValueError('Encoded texture failed decompression readback')
    T.texture_to_rgba(check,rebuilt,check_infos[ri])
    return result

def export_zip(archive, code):
    output = io.BytesIO(); manifest = []
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
        for name in archive.team_files(code):
            data = archive.read(name)
            for row in textures(data):
                if 'image' not in row: continue
                path = f'{name}/{row["id"].replace(":","-")}_{row["name"]}.png'
                z.writestr(path,base64.b64decode(row.pop('image').split(',')[1]))
                manifest.append(dict(file=name,path=path,**row))
        z.writestr('textures.json',json.dumps(manifest,indent=2))
    return output.getvalue()
