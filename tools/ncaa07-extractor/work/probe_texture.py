from pathlib import Path
import struct
from PIL import Image, ImageDraw

def probe(b, label):
    nf, nb, np = struct.unpack_from('<HHH', b, 12)
    ft, bt, pt, nt, extra = struct.unpack_from('<5I', b, 20)
    print(label, nf, nb, np, 'tables', ft, bt, pt, nt, extra)
    sheet = Image.new('RGB', (1024, min(nf, 12)*180), '#888888')
    draw = ImageDraw.Draw(sheet)
    for i in range(nf):
        name = b[nt+i*16:nt+(i+1)*16].split(b'\0')[0].decode('ascii', 'replace')
        pc, bc, bi, pi = struct.unpack_from('<HHII', b, ft+i*12)
        print(i, name, (pc, bc, bi, pi))
        if not bc or i >= 12: continue
        w,h,fmt,size,off = struct.unpack_from('<HHIII',b,bt+bi*16)
        raw=b[off+128:off+128+size]
        for variant in range(4):
            if fmt == 11:
                a,c,ps,po=struct.unpack_from('<HHII',b,pt+pi*12)
                pal=b[po+128:po+128+ps]
                if variant&1:
                    pal=b''.join(pal[((j&~24)|((j&8)<<1)|((j&16)>>1))*4:((j&~24)|((j&8)<<1)|((j&16)>>1))*4+4] for j in range(256))
                indices=raw
                if variant&2:
                    out=bytearray(w*h)
                    for y in range(h):
                        for x in range(w):
                            block=(y&~15)*w+(x&~15)*2
                            swap=(((y+2)>>2)&1)*4
                            py=(((y&~3)>>1)+(y&1))&7
                            pos=block+py*w*2+((x+swap)&7)*4+((y>>1)&1)+((x>>2)&2)
                            if pos<len(raw): out[y*w+x]=raw[pos]
                    indices=out
                if variant>=2:
                    out=bytearray(w*h)
                    for y in range(h):
                        for x in range(w):
                            pos=0; bit=1; shift=0
                            while bit<w or bit<h:
                                if bit<w: pos|=bool(x&bit)<<shift; shift+=1
                                if bit<h: pos|=bool(y&bit)<<shift; shift+=1
                                bit<<=1
                            out[y*w+x]=raw[pos]
                    indices=out
                rgba=b''.join(pal[j*4:j*4+4] for j in indices)
                im=Image.frombytes('RGBA',(w,h),rgba)
            elif fmt==6: im=Image.frombytes('RGBA',(w,h),raw,'raw','BGRA')
            else: continue
            im.putalpha(255)
            im.thumbnail((240,150))
            sheet.paste(im,(variant*256,i*180+25))
            draw.text((variant*256,i*180),f'{name} {w}x{h} variant {variant}',fill='black')
    dest=Path('outputs/texture_investigation');dest.mkdir(exist_ok=True)
    sheet.save(dest/(label+'.png'))

if __name__=='__main__':
    probe(Path('outputs/plydata_extracted/00000001.3ds').read_bytes(),'ply_candidates')
    b=Path(r'H:\07\NCAA Football 07  (axekin.com)\data\UIS_MODL.DAT').read_bytes()
    probe(b[0xe00:0xe00+132636],'uis_candidates')
