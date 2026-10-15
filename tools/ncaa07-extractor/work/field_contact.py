from pathlib import Path
import sys,io
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ncaa_formats import sections
from ncaa_textures import parse_mmap,decode_texture
from PIL import Image,ImageDraw
root=Path(r'H:\NCAA07_Extractor_Work\field_raw')
sheet=Image.new('RGB',(1200,800),(100,100,100));draw=ImageDraw.Draw(sheet)
for k,i in enumerate(range(0x4c,0x4c+24)):
 p=root/f'{i:08x}.tmd'
 b=p.read_bytes();r,t=sections(b);s=t['Text'];raw=b[s['offset']:s['offset']+s['size']]
 meta=parse_mmap(raw);img=Image.open(io.BytesIO(decode_texture(raw,meta,0)))
 img.thumbnail((180,150));x=(k%6)*200;y=(k//6)*200
 sheet.paste(img,(x,y+25),img if img.mode=='RGBA' else None);draw.text((x,y+5),f'{i:04x} '+r.string(t['Info']['offset']),fill='white')
out=Path(__file__).resolve().parents[1]/'work/field_contact.png';sheet.save(out)
