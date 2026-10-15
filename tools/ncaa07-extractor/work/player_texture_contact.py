from pathlib import Path
import sys,io
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ncaa_assemble import TextureBank
from ncaa_textures import decode_texture
from PIL import Image,ImageDraw
sc=Path(r'H:\NCAA07_Extractor_Work')
bank=TextureBank(sc/'uniform_raw/00000008.3ds');gear=TextureBank('outputs/plydata_extracted/00000001.3ds')
sheet=Image.new('RGB',(1200,800),'#555555');draw=ImageDraw.Draw(sheet)
images=[]
for i in range(6,12):
 b=TextureBank(sc/'uniform_raw'/f'{i:08x}.3ds');images.append((str(i)+' helmet',b.png('HELMET')))
for name in ['TORSOSLEEVE','HIPTHIGH','PALETTE','SOCKS']:images.append((name,bank.png(name)))
f=bank.meta['files'][bank.names['PALETTE']]
for pi in range(12):
 p=bank.meta['palettes'][f['palette_index']+pi];palette=bank.data[p['data_offset']:p['data_offset']+1024]
 images.append(('ARM pal '+str(pi),gear.png('ARM',palette)))
for k,(name,png)in enumerate(images):
 im=Image.open(io.BytesIO(png));im.thumbnail((190,160));x=k%6*200;y=k//6*200;sheet.paste(im,(x,y+25),im);draw.text((x,y+5),name,fill='white')
sheet.save('work/player_texture_contact.png')
